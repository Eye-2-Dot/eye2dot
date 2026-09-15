"""
Eye 2 Dot 백엔드 서버
ESP32-CAM → 이미지 수신 → Gemini 인식 → 점역 → 응답
"""

import os
import io
import json
from typing import Optional

import httpx
from fastapi import FastAPI, Request, UploadFile, File, Form, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response
from dotenv import load_dotenv
from google import genai
from google.genai import types
from google.genai import errors as genai_errors
from PIL import Image

from braille import text_to_braille, text_to_braille_lines, cells_to_unicode
from speech import text_to_pcm, estimate_duration, SpeechError

# ─────────────────────────────────────────────
# 초기화
# ─────────────────────────────────────────────
load_dotenv()

API_KEY = os.getenv("GEMINI_API_KEY")
if not API_KEY:
    raise RuntimeError(".env 파일에 GEMINI_API_KEY가 없습니다")

client = genai.Client(api_key=API_KEY)
MODEL = "gemini-2.5-flash"

app = FastAPI(title="Eye 2 Dot API")

# ─────────────────────────────────────────────
# CORS
#
# Flutter 앱을 Chrome에서 띄우면 브라우저가 다른 출처(localhost:포트)로
# 요청하는 것으로 보기 때문에, 허용하지 않으면 요청이 차단된다.
#
# 개발용 설정. 외부 배포 시 특정 도메인만 허용할 것.
# ─────────────────────────────────────────────
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
    # 브라우저는 커스텀 응답 헤더를 기본적으로 JS에 보여주지 않는다.
    # 앱(Chrome)에서도 /speech의 재생 시간을 읽을 수 있도록 노출한다.
    expose_headers=["X-Audio-Duration"],
)

VALID_MODES = {"label", "book"}


def _invalid_mode_response(mode: str) -> JSONResponse:
    """잘못된 mode에 대한 400 응답. 실제 엔드포인트와 목이 같은 응답을 쓰도록 여기 한 곳에 둔다."""
    return JSONResponse(
        {
            "ok": False,
            "error": "invalid_mode",
            "reason": f"mode는 {sorted(VALID_MODES)} 중 하나여야 합니다 (받은 값: '{mode}')",
        },
        status_code=400,
    )


def _braille_fields(text: str, mode: str) -> dict:
    """점역 결과를 응답에 담을 형태로 만든다.

    책 모드는 본문이 길어서 여러 줄이 되므로, 펌웨어가 어디서 줄을 바꿔야 할지
    알 수 있도록 줄 단위 2차원 배열로 낸다.
    라벨 모드는 사물 이름 한 개라 항상 한 줄이므로 1차원 배열 그대로 낸다.

    실제 처리(handle)와 목(process_mock)이 같은 함수를 쓰도록 여기 한 곳에 둔다.
    """
    if mode == "book":
        lines = text_to_braille_lines(text)
        return {
            "braille": lines,
            # 줄 구분이 보이도록 줄바꿈으로 이어 붙인다
            "braille_preview": "\n".join(cells_to_unicode(line) for line in lines),
            "cell_count": sum(len(line) for line in lines),  # 전체 셀 수
        }

    cells = text_to_braille(text)
    return {
        "braille": cells,
        "braille_preview": cells_to_unicode(cells),
        "cell_count": len(cells),
    }


# ─────────────────────────────────────────────
# Gemini 호출 실패 — error_code로 원인을 구분해서 상위(handle)로 전달
# ─────────────────────────────────────────────
class GeminiCallError(Exception):
    def __init__(self, error_code: str, reason: str):
        self.error_code = error_code
        self.reason = reason
        super().__init__(reason)


def _call_gemini(contents) -> str:
    """Gemini 호출 + 응답 텍스트 추출. 실패 원인을 구분해 GeminiCallError로 변환한다."""
    try:
        response = client.models.generate_content(model=MODEL, contents=contents)
    except genai_errors.APIError as e:
        if e.code == 429:
            raise GeminiCallError("rate_limit", "Gemini 요청 한도를 초과했습니다. 잠시 후 다시 시도하세요.") from e
        raise GeminiCallError("network_error", f"Gemini API 오류: {e.message or e}") from e
    except httpx.TransportError as e:
        raise GeminiCallError("network_error", f"Gemini 서버에 연결하지 못했습니다: {e}") from e

    try:
        return response.text.strip()
    except (ValueError, AttributeError) as e:
        raise GeminiCallError("parse_failed", "Gemini 응답에서 텍스트를 읽지 못했습니다.") from e


# ─────────────────────────────────────────────
# 이미지 전처리
# ─────────────────────────────────────────────
def preprocess(image_bytes: bytes, max_side: int = 1024) -> bytes:
    """이미지가 너무 크면 축소 (API 비용 절감 + 속도 향상)"""
    try:
        img = Image.open(io.BytesIO(image_bytes))
        if max(img.size) > max_side:
            ratio = max_side / max(img.size)
            new_size = (int(img.width * ratio), int(img.height * ratio))
            img = img.resize(new_size, Image.LANCZOS)
            buf = io.BytesIO()
            img.convert("RGB").save(buf, format="JPEG", quality=85)
            return buf.getvalue()
    except Exception as e:
        print(f"[전처리 건너뜀] {e}")
    return image_bytes


# ─────────────────────────────────────────────
# Gemini 호출 — 사물 인식 (이름 + 판단 근거)
# ─────────────────────────────────────────────
def recognize_object(image_bytes: bytes) -> dict:
    """사물 인식 → {"name": "컵", "reason": "손잡이가 달린 원통형 용기입니다."}"""

    prompt = """이 사진 속 사물을 식별하세요.

반드시 아래 JSON 형식으로만 답하세요. 다른 설명이나 마크다운 기호는 절대 쓰지 마세요.

{"name": "사물이름", "reason": "판단근거"}

규칙:
- name: 한국어 명사 1개. 최대 6글자. 점자 라벨에 쓸 짧은 이름.
- reason: 왜 그렇게 판단했는지 한국어 1문장. 30자 이내.
- 사물을 식별할 수 없으면 name을 빈 문자열로 두세요."""

    raw = _call_gemini([
        types.Part.from_bytes(data=image_bytes, mime_type="image/jpeg"),  # 이미지 파트
        prompt,  # 텍스트는 문자열 그대로 넘기면 SDK가 자동 변환
    ])

    # 모델이 ```json ... ``` 로 감싸는 경우 제거
    if raw.startswith("```"):
        raw = raw.split("```")[1]
        if raw.startswith("json"):
            raw = raw[4:]
        raw = raw.strip()

    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"[JSON 파싱 실패] 원본 응답: {raw}")
        raise GeminiCallError("parse_failed", "인식 결과를 해석하지 못했습니다.") from e

    return {
        "name": data.get("name", "").strip(),
        "reason": data.get("reason", "").strip(),
    }


# ─────────────────────────────────────────────
# Gemini 호출 — 책 페이지 OCR
# ─────────────────────────────────────────────
def recognize_text(image_bytes: bytes) -> dict:
    """책 페이지 OCR → {"name": "본문 텍스트", "reason": "..."}"""

    prompt = """이 이미지에 있는 한국어 텍스트를 모두 추출하세요.

규칙:
- 추출한 텍스트만 출력하세요. 설명이나 머리말은 절대 쓰지 마세요.
- 줄바꿈은 유지하세요.
- 텍스트가 없으면 아무것도 출력하지 마세요."""

    text = _call_gemini([
        types.Part.from_bytes(data=image_bytes, mime_type="image/jpeg"),  # 이미지 파트
        prompt,  # 텍스트는 문자열 그대로 넘기면 SDK가 자동 변환
    ])
    return {
        "name": text,
        "reason": f"{len(text)}자를 인식했습니다." if text else "텍스트를 찾지 못했습니다.",
    }


# ─────────────────────────────────────────────
# 엔드포인트 1: 헬스 체크
# ─────────────────────────────────────────────
@app.get("/health")
def health():
    """서버가 살아있는지 확인용. 브라우저로 접속해보세요."""
    return {"status": "ok", "model": MODEL}


# ─────────────────────────────────────────────
# 엔드포인트 2: ESP32용 (raw JPEG 바이트)
# ─────────────────────────────────────────────
@app.post("/process")
async def process_raw(request: Request, mode: str = "label"):
    """ESP32가 원본 JPEG 바이트를 그대로 POST하는 엔드포인트.

    사용법: POST /process?mode=label
            Content-Type: image/jpeg
            Body: <JPEG 바이너리>
    """
    image_bytes = await request.body()

    if not image_bytes:
        raise HTTPException(status_code=400, detail="이미지가 비어 있습니다")

    print(f"[수신] mode={mode}, size={len(image_bytes)} bytes")

    return handle(image_bytes, mode)


# ─────────────────────────────────────────────
# 엔드포인트 3: 브라우저 테스트용 (파일 업로드)
# ─────────────────────────────────────────────
@app.post("/process-file")
async def process_file(mode: str = Form("label"), image: UploadFile = File(...)):
    """/docs 화면에서 파일을 올려 테스트할 때 사용."""
    image_bytes = await image.read()
    return handle(image_bytes, mode)


# ─────────────────────────────────────────────
# 엔드포인트 4: 앱 개발용 목(mock)
#
# GEMINI_API_KEY 없이, 카메라 없이 앱 화면을 개발할 수 있게
# 고정된 더미 결과를 즉시 돌려준다.
# ─────────────────────────────────────────────

# 책 모드용 예시 본문. 실제 OCR 결과처럼 여러 줄로 되어 있다.
MOCK_BOOK_TEXT = (
    "점자는 손끝으로 읽는 문자이다. 여섯 개의 점을 일정한 규칙에 따라 조합하여 글자를 "
    "나타내며, 가로 두 칸 세로 세 칸으로 이루어진 직사각형 안에 점을 찍어 표현한다.\n"
    "한글 점자는 초성과 중성과 종성을 각각 다른 점형으로 적기 때문에 한 글자가 여러 칸을 "
    "차지하는 경우가 많다.\n"
    "시각장애인은 이 점자를 통해 책을 읽고 글을 쓴다. 점자를 익히면 스스로 정보를 얻을 수 "
    "있고, 학습과 일상생활에서 다른 사람의 도움에 의존하지 않아도 된다."
)


def _mock_payload(text: str, reason: str, mode: str) -> dict:
    """더미 텍스트를 실제 성공 응답과 똑같은 형식으로 감싼다.

    braille 값을 손으로 적어넣지 않고 실제 처리와 같은 _braille_fields()를 쓴다.
    그래야 실서버 응답과 형식이 어긋나지 않는다.
    """
    return {
        "ok": True,
        "text": text,
        "reason": reason,
        **_braille_fields(text, mode),
    }


# 서버가 켜질 때 한 번만 점역해 두고, 요청이 오면 그대로 돌려준다.
MOCK_RESULTS = {
    "label": _mock_payload("우유", "흰색 종이팩에 우유 표기가 있습니다.", "label"),
    "book": _mock_payload(MOCK_BOOK_TEXT, "OCR로 텍스트를 인식했습니다.", "book"),
}


@app.post("/process-mock")
async def process_mock(mode: str = "label", image: Optional[UploadFile] = File(None)):
    """Gemini를 호출하지 않고 고정된 더미 결과를 즉시 돌려주는 목 엔드포인트.

    사용법: POST /process-mock?mode=label
            파일은 붙여도 되고 안 붙여도 된다.
            (붙이더라도 읽지 않는다. image 인자를 받기만 하는 이유는
             앱이 실제 엔드포인트와 똑같은 형태로 요청을 보내볼 수 있게 하기 위함.)

    응답 형식은 /process-file과 완전히 같으므로,
    앱은 나중에 주소만 바꾸면 그대로 동작한다.
    """
    if mode not in VALID_MODES:
        return _invalid_mode_response(mode)

    print(f"[목 응답] mode={mode}")
    return JSONResponse(MOCK_RESULTS[mode])


def _is_valid_image(image_bytes: bytes) -> bool:
    """바이트가 실제로 열 수 있는 이미지인지 확인 (Gemini 호출 전 사전 검증, 비용 낭비 방지)."""
    try:
        Image.open(io.BytesIO(image_bytes)).verify()
        return True
    except Exception:
        return False


# ─────────────────────────────────────────────
# 공통 처리 로직
# ─────────────────────────────────────────────
def handle(image_bytes: bytes, mode: str):
    if mode not in VALID_MODES:
        return _invalid_mode_response(mode)

    if not _is_valid_image(image_bytes):
        return JSONResponse(
            {"ok": False, "error": "invalid_image", "reason": "올바른 이미지 파일이 아닙니다."},
            status_code=400,
        )

    try:
        image_bytes = preprocess(image_bytes)

        if mode == "book":
            result = recognize_text(image_bytes)
        else:
            result = recognize_object(image_bytes)

        text = result["name"]

        if not text:
            return JSONResponse({
                "ok": False,
                "error": "recognition_failed",
                "reason": result["reason"],
            })

        payload = {
            "ok": True,
            "text": text,                          # 앱은 화면 표시, 펌웨어 1은 음성 안내에 사용
            "reason": result["reason"],            # 판단 근거 1문장
            # braille(솔레노이드 구동용) / braille_preview(사람이 확인용) / cell_count
            # 책 모드는 braille이 줄 단위 2차원 배열이 된다.
            **_braille_fields(text, mode),
        }

        print(f"[응답] {text} / {payload['cell_count']}셀 / {payload['braille_preview']}")
        return JSONResponse(payload)

    except GeminiCallError as e:
        status_map = {"rate_limit": 429, "network_error": 502, "parse_failed": 500}
        print(f"[Gemini 에러] {e.error_code}: {e.reason}")
        return JSONResponse(
            {"ok": False, "error": e.error_code, "reason": e.reason},
            status_code=status_map.get(e.error_code, 500),
        )

    except Exception as e:
        print(f"[에러] {type(e).__name__}: {e}")
        return JSONResponse(
            {"ok": False, "error": str(e)},
            status_code=500,
        )


# ─────────────────────────────────────────────
# 엔드포인트 5: 펌웨어 1 음성 안내용 (텍스트 → raw PCM)
#
# 음성 생성은 서버가 전부 하고, 기기(ATmega2560)는 받은 바이트를 재생만 한다.
# 성공 응답은 JSON이 아니라 오디오 바이트 그대로다.
# ─────────────────────────────────────────────

# 이보다 길면 재생 시간이 너무 길어져 기기의 버퍼 관리가 어렵다
MAX_SPEECH_TEXT_LENGTH = 200


@app.get("/speech")
def speech(text: str = ""):
    """텍스트를 8kHz / 8비트 unsigned / 모노 raw PCM으로 바꿔 돌려준다.

    사용법: GET /speech?text=우유   (text는 UTF-8로 퍼센트 인코딩)

    async def가 아니라 def인 이유: 음성 생성은 네트워크 통신과 ffmpeg 실행으로 수 초 걸리는
    블로킹 작업이다. def로 두면 FastAPI가 별도 스레드에서 실행하므로 그동안 다른 요청이 막히지 않는다.
    """
    text = text.strip()

    # 공백·기호뿐인 텍스트는 읽어줄 내용이 없으므로 빈 텍스트와 똑같이 취급한다
    if not any(ch.isalnum() for ch in text):
        return JSONResponse(
            {"ok": False, "error": "missing_text", "reason": "읽어줄 텍스트가 없습니다."},
            status_code=400,
        )

    if len(text) > MAX_SPEECH_TEXT_LENGTH:
        return JSONResponse(
            {
                "ok": False,
                "error": "text_too_long",
                "reason": f"text는 {MAX_SPEECH_TEXT_LENGTH}자 이하여야 합니다 (받은 길이: {len(text)}자)",
            },
            status_code=400,
        )

    try:
        pcm = text_to_pcm(text)
    except SpeechError as e:
        status_map = {"tts_unavailable": 503, "conversion_failed": 500}
        print(f"[음성 에러] {e.error_code}: {e.reason}")
        return JSONResponse(
            {"ok": False, "error": e.error_code, "reason": e.reason},
            status_code=status_map.get(e.error_code, 500),
        )

    duration = estimate_duration(pcm)
    print(f"[음성] {text} / {len(pcm)} bytes / {duration:.3f}초")
    return Response(
        content=pcm,
        media_type="application/octet-stream",
        # 기기가 본문을 받기 전에 재생 시간을 알 수 있게 한다.
        # 정확한 바이트 수는 자동으로 붙는 Content-Length를 쓰면 된다.
        headers={"X-Audio-Duration": f"{duration:.3f}"},
    )