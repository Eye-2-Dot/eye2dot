"""
Eye 2 Dot 백엔드 서버
ESP32-CAM → 이미지 수신 → Gemini 인식 → 점역 → 응답

기기(펌웨어)용으로는 여기에 더해
  - 점자를 거울 모양으로 바꿔 준다 (mirror.py, ?device=fw1·fw2 를 붙였을 때만)
  - 펌웨어 1 에는 음성을 미리 만들어 번호(speech_id)를 붙여 준다 (speech_store.py)
  - 앱 "출력하기" 작업을 기기별 대기열로 넘겨 준다 (폴링, jobs.py)
"""

import os
import io
import json
from typing import Any, Optional

import httpx
from fastapi import FastAPI, Request, UploadFile, File, Form, Query, HTTPException
from fastapi.concurrency import run_in_threadpool
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel
from dotenv import load_dotenv
from google import genai
from google.genai import types
from google.genai import errors as genai_errors
from PIL import Image

import mirror
import speech_store
from braille import text_to_braille, text_to_braille_lines, cells_to_unicode
from jobs import JobQueue
from speech import text_to_pcm, estimate_duration, SpeechError

# ─────────────────────────────────────────────
# 초기화
# ─────────────────────────────────────────────
load_dotenv()

API_KEY = os.getenv("GEMINI_API_KEY")
if not API_KEY:
    raise RuntimeError(".env 파일에 GEMINI_API_KEY가 없습니다")

client = genai.Client(api_key=API_KEY)
# gemini-3.8-flash 는 수요가 몰려 high demand 오류가 잦고 느림 (30초+)
# lite 는 5초 내외이며 사물 인식 정확도도 충분함
MODEL = "gemini-3.1-flash-lite"

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

# 기기 이름 → 모드. 펌웨어 1 = 라벨 출력기, 펌웨어 2 = 책 타각기
DEVICE_MODES = {"fw1": "label", "fw2": "book"}
MODE_DEVICES = {mode: device for device, mode in DEVICE_MODES.items()}
# 음성 안내가 있는 기기. 펌웨어 1 전용이라 fw2 에는 speech_id 를 붙이지 않는다
SPEECH_DEVICES = {"fw1"}


def _invalid_mode_payload(mode: str) -> dict:
    return {
        "ok": False,
        "error": "invalid_mode",
        "reason": f"mode는 {sorted(VALID_MODES)} 중 하나여야 합니다 (받은 값: '{mode}')",
    }


def _invalid_mode_response(mode: str) -> JSONResponse:
    """잘못된 mode에 대한 400 응답. 실제 엔드포인트와 목이 같은 응답을 쓰도록 여기 한 곳에 둔다."""
    return JSONResponse(_invalid_mode_payload(mode), status_code=400)


def _invalid_device_response(reason: str) -> JSONResponse:
    """잘못된 device에 대한 400 응답 (기기용 주소와 폴링이 같이 쓴다)."""
    return JSONResponse({"ok": False, "error": "invalid_device", "reason": reason}, status_code=400)


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

    # Gemini 호출(수 초)이 이벤트 루프를 막지 않도록 별도 스레드에서 처리한다 (응답은 같다)
    return await run_in_threadpool(handle, image_bytes, mode)


# ─────────────────────────────────────────────
# 엔드포인트 3: 브라우저 테스트용 (파일 업로드)
# ─────────────────────────────────────────────
@app.post("/process-file")
async def process_file(
    image: UploadFile = File(...),
    # 같은 이름 "mode"를 쿼리와 폼 양쪽에서 받는다. 위치가 달라 서로 겹치지 않는다.
    mode_query: Optional[str] = Query(None, alias="mode"),
    mode_form: Optional[str] = Form(None, alias="mode"),
    device: Optional[str] = Query(None),
):
    """/docs 화면에서 파일을 올려 테스트할 때, 그리고 앱이 실서버로 보낼 때 사용.

    mode 우선순위: 쿼리(?mode=book) → 폼 필드 mode → 기본값 "label"
    앱은 /process-mock과 똑같이 쿼리로 보내므로, 쿼리를 읽지 않으면
    책 모드 요청이 조용히 라벨 모드로 처리된다.
    빈 문자열(?mode=)은 값이 없는 것으로 보고 다음 순서로 넘어간다.

    device(쿼리, 선택): fw1 또는 fw2. 붙이면 기기용 응답(거울 점자 등, _device_response 참고).
    없거나 빈 값이면 지금까지와 똑같은 앱용 응답이다.
    """
    mode = mode_query or mode_form or "label"
    image_bytes = await image.read()
    # Gemini 호출·음성 생성(수 초)이 이벤트 루프를 막지 않도록 별도 스레드에서 처리한다.
    # 막히면 그동안 다른 요청(기기의 폴링 등)이 모두 멈춘다. 응답 내용은 같다.
    if not device:
        return await run_in_threadpool(handle, image_bytes, mode)
    return await run_in_threadpool(_handle_for_device, image_bytes, mode, device)


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
async def process_mock(
    mode: str = "label",
    image: Optional[UploadFile] = File(None),
    device: Optional[str] = None,
):
    """Gemini를 호출하지 않고 고정된 더미 결과를 즉시 돌려주는 목 엔드포인트.

    사용법: POST /process-mock?mode=label
            파일은 붙여도 되고 안 붙여도 된다.
            (붙이더라도 읽지 않는다. image 인자를 받기만 하는 이유는
             앱이 실제 엔드포인트와 똑같은 형태로 요청을 보내볼 수 있게 하기 위함.)

    응답 형식은 /process-file과 완전히 같으므로,
    앱은 나중에 주소만 바꾸면 그대로 동작한다.
    device(쿼리, 선택)도 /process-file과 같다 (?device=fw1 → 기기용 응답).
    """
    if mode not in VALID_MODES:
        return _invalid_mode_response(mode)

    if not device:
        print(f"[목 응답] mode={mode}")
        return JSONResponse(MOCK_RESULTS[mode])

    error = _check_device(device, mode)
    if error is not None:
        return error
    print(f"[목 응답] mode={mode}, device={device}")
    # 펌웨어 1이면 음성을 만드느라 수 초 걸릴 수 있으므로 별도 스레드에서
    return await run_in_threadpool(_device_response, MOCK_RESULTS[mode], 200, device)


def _is_valid_image(image_bytes: bytes) -> bool:
    """바이트가 실제로 열 수 있는 이미지인지 확인 (Gemini 호출 전 사전 검증, 비용 낭비 방지)."""
    try:
        Image.open(io.BytesIO(image_bytes)).verify()
        return True
    except Exception:
        return False


# ─────────────────────────────────────────────
# 공통 처리 로직
#
# _process() 가 결과(dict)와 HTTP 상태 코드를 만들고,
# 앱용은 handle() 이 그대로 JSON 으로, 기기용은 _handle_for_device() 가 거울 처리 등을 더해 낸다.
# Gemini 호출로 수 초 걸리므로 스레드(run_in_threadpool)에서 부를 것.
# ─────────────────────────────────────────────
def _process(image_bytes: bytes, mode: str) -> tuple[dict, int]:
    if mode not in VALID_MODES:
        return _invalid_mode_payload(mode), 400

    if not _is_valid_image(image_bytes):
        return {"ok": False, "error": "invalid_image", "reason": "올바른 이미지 파일이 아닙니다."}, 400

    try:
        image_bytes = preprocess(image_bytes)

        if mode == "book":
            result = recognize_text(image_bytes)
        else:
            result = recognize_object(image_bytes)

        text = result["name"]

        if not text:
            return {
                "ok": False,
                "error": "recognition_failed",
                "reason": result["reason"],
            }, 200

        payload = {
            "ok": True,
            "text": text,                          # 앱은 화면 표시, 펌웨어 1은 음성 안내에 사용
            "reason": result["reason"],            # 판단 근거 1문장
            # braille(솔레노이드 구동용) / braille_preview(사람이 확인용) / cell_count
            # 책 모드는 braille이 줄 단위 2차원 배열이 된다.
            **_braille_fields(text, mode),
        }

        print(f"[응답] {text} / {payload['cell_count']}셀 / {payload['braille_preview']}")
        return payload, 200

    except GeminiCallError as e:
        status_map = {"rate_limit": 429, "network_error": 502, "parse_failed": 500}
        print(f"[Gemini 에러] {e.error_code}: {e.reason}")
        return (
            {"ok": False, "error": e.error_code, "reason": e.reason},
            status_map.get(e.error_code, 500),
        )

    except Exception as e:
        print(f"[에러] {type(e).__name__}: {e}")
        return {"ok": False, "error": str(e)}, 500


def handle(image_bytes: bytes, mode: str) -> JSONResponse:
    """앱용 응답 (지금까지와 같은 형식)."""
    payload, status = _process(image_bytes, mode)
    return JSONResponse(payload, status_code=status)


# ─────────────────────────────────────────────
# 기기(펌웨어)용 응답 — ?device=fw1 또는 fw2 를 붙였을 때
#
# 앱용 결과에서 다음만 바꾼다 (자세한 약속은 mirror.py 맨 위).
#   - braille       : 거울 모양 (칸 안의 좌우 열만 바꿈. 칸·줄 순서는 그대로)
#   - braille_preview: 뺀다 (기기는 읽지 않고, 거울인지 헷갈리므로)
#   - speech_id     : 펌웨어 1 에만. 음성을 응답 전에 만들어 두고 번호를 붙인다.
#                     만들지 못하면 붙이지 않는다 (ESP32 가 /speech?text= 로 대신 받는다)
#   - mirrored      : true (성공 응답에만)
# 실패 응답은 앱용과 같은 내용에 speech_id(실패 안내 음성)만 더한다. HTTP 상태 코드도 같다.
# ─────────────────────────────────────────────

# 실패 음성: reason 뒤에 붙이는 안내 문장
RETAKE_GUIDE = "다시 찍어 주세요."          # 사진을 다시 찍으면 나아질 수 있는 실패
RETRY_GUIDE = "잠시 후 다시 눌러 주세요."   # 서버·AI 쪽 문제
# RETAKE_GUIDE 를 쓰는 error. 여기 없는 error(rate_limit, network_error, parse_failed,
# 예기치 못한 서버 오류 등)는 모두 RETRY_GUIDE
RETAKE_ERRORS = {"recognition_failed", "invalid_image"}
# 기기 설정이 잘못된 요청. 다시 찍거나 눌러도 결과가 같으므로 음성을 만들지 않는다
CONFIG_ERRORS = {"invalid_mode", "invalid_device"}
# reason 에 이 말이 이미 있으면 안내 문장을 붙이지 않는다 (겹침 방지).
# 예: rate_limit 의 reason "…잠시 후 다시 시도하세요." 뒤에 "잠시 후 다시 눌러 주세요." 가 또 붙는 것
GUIDE_ALREADY_WORD = "다시"


def _check_device(device: str, mode: str) -> Optional[JSONResponse]:
    """device 가 맞지 않으면 400 invalid_device 응답, 맞으면 None.

    mode 자체가 잘못된 경우는 여기서 보지 않는다 (invalid_mode 가 먼저 나간다).
    """
    if device not in DEVICE_MODES:
        return _invalid_device_response(
            f"device는 {sorted(DEVICE_MODES)} 중 하나여야 합니다 (받은 값: '{device}')"
        )
    if mode in VALID_MODES and DEVICE_MODES[device] != mode:
        return _invalid_device_response(
            f"device '{device}'는 mode '{DEVICE_MODES[device]}' 전용입니다 (받은 mode: '{mode}')"
        )
    return None


def _success_speech_text(text: str, reason: str) -> str:
    """성공 음성 문장: "{text}입니다. {reason}". text 가 비면 reason 만. 200자까지."""
    text = (text or "").strip()
    reason = (reason or "").strip()
    sentence = f"{text}입니다." if text else ""
    if reason:
        sentence = f"{sentence} {reason}".strip()
    return sentence[:MAX_SPEECH_TEXT_LENGTH].strip()


def _failure_speech_text(error: str, reason: str) -> str:
    """실패 음성 문장: reason + 안내 문장 (어느 안내인지는 RETAKE_ERRORS 로 정한다).

    reason 에 이미 "다시"(GUIDE_ALREADY_WORD)가 들어 있으면 안내 문장을 붙이지 않는다.
    200자를 넘으면 reason 쪽을 잘라 안내 문장은 항상 남긴다.
    """
    retake = error in RETAKE_ERRORS
    guide = RETAKE_GUIDE if retake else RETRY_GUIDE
    reason = (reason or "").strip()
    if not reason:
        reason = "인식하지 못했습니다." if retake else "서버에서 문제가 생겼습니다."
    if reason[-1] not in ".!?":
        reason += "."
    if GUIDE_ALREADY_WORD in reason:
        if len(reason) > MAX_SPEECH_TEXT_LENGTH:
            reason = reason[:MAX_SPEECH_TEXT_LENGTH - 1].rstrip() + "."
        return reason
    room = MAX_SPEECH_TEXT_LENGTH - len(guide) - 1   # 1 = 사이 공백
    if len(reason) > room:
        reason = reason[:room - 1].rstrip() + "."
    return f"{reason} {guide}"


def _device_response(payload: dict, status: int, device: str) -> JSONResponse:
    """앱용 결과(payload, status) → 기기용 응답.

    펌웨어 1이면 음성을 만드느라 수 초 걸리므로 스레드(run_in_threadpool)에서 부를 것.
    """
    speaks = device in SPEECH_DEVICES

    if not payload.get("ok"):
        out = dict(payload)
        if speaks and out.get("error") not in CONFIG_ERRORS:
            speech_id = speech_store.create_ready(
                _failure_speech_text(out.get("error", ""), out.get("reason", ""))
            )
            if speech_id:
                out["speech_id"] = speech_id
        return JSONResponse(out, status_code=status)

    out = {
        "ok": True,
        "text": payload["text"],
        "reason": payload["reason"],
        "braille": mirror.mirror_braille(payload["braille"]),
        "cell_count": payload["cell_count"],
    }
    if speaks:
        speech_id = speech_store.create_ready(_success_speech_text(payload["text"], payload["reason"]))
        if speech_id:
            out["speech_id"] = speech_id
    out["mirrored"] = True
    print(f"[기기 응답] {device} / {out['cell_count']}셀 거울 / speech_id={out.get('speech_id')}")
    return JSONResponse(out, status_code=status)


def _handle_for_device(image_bytes: bytes, mode: str, device: str) -> JSONResponse:
    """/process-file?device= 의 처리. 스레드에서 부를 것."""
    if mode not in VALID_MODES:
        return _invalid_mode_response(mode)
    error = _check_device(device, mode)
    if error is not None:
        return error
    payload, status = _process(image_bytes, mode)
    return _device_response(payload, status, device)


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
        return _speech_error_response(e)

    return _pcm_response(pcm, text)


def _speech_error_response(e: SpeechError) -> JSONResponse:
    """음성 생성 실패 → JSON 오류 (/speech 와 /speech/{speech_id} 가 같이 쓴다)."""
    status_map = {"tts_unavailable": 503, "conversion_failed": 500}
    print(f"[음성 에러] {e.error_code}: {e.reason}")
    return JSONResponse(
        {"ok": False, "error": e.error_code, "reason": e.reason},
        status_code=status_map.get(e.error_code, 500),
    )


def _pcm_response(pcm: bytes, label: str) -> Response:
    """PCM 바이트 → 오디오 응답 (/speech 와 /speech/{speech_id} 가 같이 쓴다)."""
    duration = estimate_duration(pcm)
    print(f"[음성] {label} / {len(pcm)} bytes / {duration:.3f}초")
    return Response(
        content=pcm,
        media_type="application/octet-stream",
        # 기기가 본문을 받기 전에 재생 시간을 알 수 있게 한다.
        # 정확한 바이트 수는 자동으로 붙는 Content-Length를 쓰면 된다.
        headers={"X-Audio-Duration": f"{duration:.3f}"},
    )


@app.get("/speech/{speech_id}")
def speech_by_id(speech_id: str):
    """음성 번호로 음성 받기 (펌웨어 1 전용).

    번호는 /process-file·/process-mock?device=fw1 응답과 /jobs/next?device=fw1 작업에 붙는다.
    형식·헤더·오류는 GET /speech 와 같다. 없는 번호(만료 포함)는 404 unknown_speech.
    서버가 아직 음성을 만드는 중이면 끝날 때까지 기다린다 (최대 speech_store.WAIT_SECONDS).
    """
    try:
        pcm = speech_store.get(speech_id)
    except speech_store.UnknownSpeech:
        return JSONResponse(
            {
                "ok": False,
                "error": "unknown_speech",
                "reason": "그런 speech_id가 없습니다 (잘못된 번호이거나 10분이 지나 지워졌습니다).",
            },
            status_code=404,
        )
    except SpeechError as e:
        return _speech_error_response(e)

    return _pcm_response(pcm, f"/speech/{speech_id}")


# ─────────────────────────────────────────────
# 엔드포인트 6: 텍스트 점역 (앱의 "라벨 문구 직접 입력하기"용)
#
# 사진 없이 텍스트만 받아 braille.py로 점역한다. Gemini는 호출하지 않는다.
# 응답 형식은 /process-file 성공 응답과 완전히 같으므로
# 앱과 펌웨어는 사진으로 만든 결과와 똑같이 다루면 된다.
# ─────────────────────────────────────────────

# 모드별 입력 글자 수 상한. 두 모드는 다루는 글의 길이가 전혀 달라 따로 둔다.
# 라벨: 사물 이름은 보통 6자 내외다. 점자 라벨 한 장에 들어갈 현실적인 상한으로 50자를 둔다.
MAX_TRANSLATE_TEXT_LABEL = 50
# 책: A4 한 장 분량을 고려한 값. 서버 예시 본문(MOCK_BOOK_TEXT, 244자)도 넉넉히 들어간다.
MAX_TRANSLATE_TEXT_BOOK = 2000


class TranslateRequest(BaseModel):
    """/translate 요청 본문. 예: {"text": "우유", "mode": "label"}

    필드가 빠지거나 null이어도 FastAPI 기본 오류(422)가 아니라
    아래에서 직접 검사해 {ok, error, reason} 형식으로 답하도록 둘 다 생략 가능하게 둔다.
    """
    text: Optional[str] = None
    mode: Optional[str] = None


@app.post("/translate")
def translate(body: TranslateRequest):
    """텍스트 → 점자. 실패 시 400 + {ok: false, error, reason}.

    사용법: POST /translate
            Content-Type: application/json
            Body: {"text": "우유", "mode": "label"}   (mode 생략 시 "label")

    error 종류:
        invalid_mode  : mode가 label/book이 아님
        missing_text  : text가 비었거나 공백뿐이거나, 점자로 바꿀 글자가 없음(문장부호만 등)
        text_too_long : 모드별 상한(라벨 50자 / 책 2000자) 초과
    """
    mode = body.mode or "label"
    if mode not in VALID_MODES:
        return _invalid_mode_response(mode)

    # 앞뒤 공백만 뗀다. 책 본문 중간의 줄바꿈(\n)은 줄 나누기에 쓰이므로 그대로 둔다.
    text = (body.text or "").strip()
    if not text:
        return JSONResponse(
            {"ok": False, "error": "missing_text", "reason": "점역할 텍스트가 없습니다."},
            status_code=400,
        )

    if mode == "book":
        limit, subject = MAX_TRANSLATE_TEXT_BOOK, "책 본문은"
    else:
        limit, subject = MAX_TRANSLATE_TEXT_LABEL, "라벨 문구는"

    if len(text) > limit:
        return JSONResponse(
            {
                "ok": False,
                "error": "text_too_long",
                "reason": f"{subject} {limit}자 이하여야 합니다 (입력: {len(text)}자)",
            },
            status_code=400,
        )

    # 문장부호는 braille.py가 건너뛰므로 "!!! ..." 같은 입력은 빈 칸(0)만 남는다.
    # 점이 하나도 찍히지 않는 결과를 ok로 내보내면 펌웨어가 빈 라벨을 찍으려다 멈추므로 여기서 막는다.
    # (셀 수가 0인지가 아니라 0이 아닌 셀이 있는지를 봐야 빈 칸만 남은 경우도 걸러진다)
    if not any(text_to_braille(text)):
        return JSONResponse(
            {"ok": False, "error": "missing_text", "reason": "점자로 바꿀 수 있는 글자가 없습니다."},
            status_code=400,
        )

    payload = {
        "ok": True,
        "text": text,
        "reason": "직접 입력한 문구입니다.",
        **_braille_fields(text, mode),
    }
    print(f"[점역] mode={mode} / {text} / {payload['cell_count']}셀")
    return JSONResponse(payload)


# ─────────────────────────────────────────────
# 엔드포인트 7~10: 폴링 — 앱 "출력하기" → 기기 (docs/POLLING_API.md 1~4장)
#
#   앱    POST /print              → 서버 대기열 (기기별: label → fw1, book → fw2)
#   기기  GET  /jobs/next?device=  → 작업 하나 (거울 점자, fw1 은 speech_id)
#   기기  POST /jobs/{id}/done     → 결과 보고
#   확인  GET  /jobs               → 대기·진행 중·최근 기록
#
# 대기열에는 앱이 보낸 원래 점자를 그대로 두고, 기기에 줄 때 거울로 바꾼다.
# ─────────────────────────────────────────────
job_queue = JobQueue(DEVICE_MODES)


class PrintRequest(BaseModel):
    """/print 요청 본문. 예: {"mode":"label", "text":"우유", "reason":"…", "braille":[13,41]}

    앱은 결과 화면에서 받은 값을 그대로 보낸다 (점역을 다시 하지 않는다).
    빠진 값도 FastAPI 기본 오류(422)가 아니라 {ok, error, reason} 로 답하도록 모두 생략 가능하게 둔다.
    """
    mode: Optional[str] = None
    text: Optional[str] = None
    reason: Optional[str] = None
    braille: Optional[Any] = None


class DoneReport(BaseModel):
    """/jobs/{id}/done 요청 본문. 예: {"device":"fw1", "ok":true, "error":""}"""
    device: str
    ok: bool
    error: str = ""


def _is_cell(value) -> bool:
    """점자 칸 하나로 쓸 수 있는 값인지 (0~63 정수. bool 은 아님)."""
    return isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 63


def _braille_problem(braille, mode: str) -> Optional[str]:
    """braille 의 모양·값이 틀리면 그 이유, 맞으면 None. (비었는지는 부르기 전에 따로 본다)

    라벨: 칸 배열 (1차원). 책: 줄 배열 (2차원, 줄마다 칸이 하나 이상).
    """
    if not isinstance(braille, list):
        return "braille은 배열이어야 합니다."
    if mode == "book":
        if not all(isinstance(line, list) and line for line in braille):
            return "책 모드의 braille은 줄마다 칸이 하나 이상 있는 2차원 배열이어야 합니다."
        cells = [value for line in braille for value in line]
    else:
        if any(isinstance(value, list) for value in braille):
            return "라벨 모드의 braille은 1차원 배열이어야 합니다."
        cells = braille
    for value in cells:
        if not _is_cell(value):
            return f"칸 값은 0~63 정수여야 합니다 (받은 값: {value!r})."
    return None


def _cell_count(braille) -> int:
    """전체 칸 수 (책은 모든 줄의 합)."""
    if braille and isinstance(braille[0], list):
        return sum(len(line) for line in braille)
    return len(braille)


@app.post("/print")
def print_job(body: PrintRequest):
    """앱 "출력하기" — 작업을 기기 대기열에 넣는다.

    error 종류 (모두 HTTP 400):
        invalid_mode    : mode 가 label/book 이 아님
        missing_braille : braille 이 없거나 빈 배열
        invalid_braille : 모양이 틀림(라벨 1차원 / 책 2차원) 또는 칸 값이 0~63 정수가 아님
    """
    mode = body.mode or "label"
    if mode not in VALID_MODES:
        return _invalid_mode_response(mode)

    if not body.braille:
        return JSONResponse(
            {"ok": False, "error": "missing_braille", "reason": "출력할 점자 데이터가 없습니다."},
            status_code=400,
        )
    problem = _braille_problem(body.braille, mode)
    if problem:
        return JSONResponse({"ok": False, "error": "invalid_braille", "reason": problem}, status_code=400)

    device = MODE_DEVICES[mode]
    job = {"mode": mode, "text": body.text or "", "reason": body.reason or "", "braille": body.braille}
    if device in SPEECH_DEVICES:
        # 앱을 기다리게 하지 않도록 음성은 뒤에서 만든다. 기기가 가져갈 무렵이면 대개 다 만들어져 있다
        job["speech_id"] = speech_store.create(_success_speech_text(job["text"], job["reason"]))

    job_id, waiting = job_queue.add(device, job)
    print(f"[대기열] {device} ← {job_id} ({job['text']})")
    return {"ok": True, "job_id": job_id, "device": device, "waiting": waiting}


@app.get("/jobs/next")
def jobs_next(device: str = ""):
    """기기가 1초마다 "출력할 것 있나?" 묻는다. 한 번에 하나, 들어온 순서대로.

    준 작업은 "진행 중"으로 옮기고 다시 주지 않는다 (완료 보고가 안 와도).
    점자는 거울로 바꿔서 주고, 펌웨어 1 에는 speech_id 를 붙인다
    (대기열에서 10분 넘게 기다려 번호가 만료됐으면 새로 붙인다).
    기기는 2초만 기다리므로 여기서는 음성을 기다리지 않는다.
    """
    if device not in DEVICE_MODES:
        return _invalid_device_response(
            f"기기 이름은 {sorted(DEVICE_MODES)} 중 하나여야 합니다 (받은 값: '{device}')"
        )

    job = job_queue.take_next(device)
    if job is None:
        return {"ok": True, "job": None}

    out = {
        "id": job["id"],
        "mode": job["mode"],
        "text": job["text"],
        "reason": job["reason"],
        "braille": mirror.mirror_braille(job["braille"]),
        "cell_count": _cell_count(job["braille"]),
    }
    if device in SPEECH_DEVICES:
        speech_id = speech_store.ensure(job.get("speech_id"), _success_speech_text(job["text"], job["reason"]))
        if speech_id:
            out["speech_id"] = speech_id
    out["mirrored"] = True

    print(f"[전달] {device} → {job['id']}")
    return {"ok": True, "job": out}


@app.post("/jobs/{job_id}/done")
def job_done(job_id: str, report: DoneReport):
    """기기의 결과 보고. error: print_failed / bad_braille / cancelled (POLLING_API.md 3장)."""
    took = job_queue.finish(job_id, report.device, report.ok, report.error)
    if took is None:
        return JSONResponse(
            {"ok": False, "error": "unknown_job", "reason": "진행 중인 작업이 아닙니다."},
            status_code=404,
        )
    print(f"[완료] {job_id} ok={report.ok} {report.error} ({took}초)")
    return {"ok": True}


@app.get("/jobs")
def jobs_status():
    """지금 상태 한눈에 보기 (브라우저로 열어도 된다)."""
    return job_queue.status()