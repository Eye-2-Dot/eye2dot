"""
Eye 2 Dot 백엔드 서버
ESP32-CAM → 이미지 수신 → Gemini 인식 → 점역 → 응답
"""

import os
import io
import json
import httpx
from fastapi import FastAPI, Request, UploadFile, File, Form, HTTPException
from fastapi.responses import JSONResponse
from dotenv import load_dotenv
from google import genai
from google.genai import types
from google.genai import errors as genai_errors
from PIL import Image

from braille import text_to_braille, cells_to_unicode

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

VALID_MODES = {"label", "book"}


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
        return JSONResponse(
            {
                "ok": False,
                "error": "invalid_mode",
                "reason": f"mode는 {sorted(VALID_MODES)} 중 하나여야 합니다 (받은 값: '{mode}')",
            },
            status_code=400,
        )

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

        cells = text_to_braille(text)

        payload = {
            "ok": True,
            "text": text,                          # TTS 음성 안내용
            "reason": result["reason"],            # 판단 근거 1문장
            "braille": cells,                      # 솔레노이드 구동용
            "braille_preview": cells_to_unicode(cells),  # 사람이 확인용
            "cell_count": len(cells),
        }

        print(f"[응답] {text} / {len(cells)}셀 / {payload['braille_preview']}")
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