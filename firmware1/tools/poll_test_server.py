"""
Eye 2 Dot — 폴링 시험용 미니 서버

실제 서버(server/main.py)를 건드리지 않고 ESP32 폴링을 시험하기 위한 것.
docs/POLLING_API.md 의 규격을 그대로 구현했다. 서버 담당은 이 파일을 참고해 main.py 에 옮기면 된다.

음성도 시험할 수 있다 (speech.py·인터넷 없이):
    /print 로 넣은 작업에 speech_id 를 붙이고,
    GET /speech/{speech_id} 와 GET /speech?text= 는 둘 다 1초짜리 440Hz 사인파 PCM 을 돌려준다
    (8kHz, 8비트 unsigned, 모노, 무음 = 128 — 실제 서버의 /speech 와 같은 형식)

실행 (저장소 루트의 .venv 를 켠 상태, 이 폴더 안에서):
    uvicorn poll_test_server:app --host 0.0.0.0 --port 8000

※ 실제 서버와 같은 8000번을 쓰므로 둘 중 하나만 켤 것.
"""

import math
import time
import uuid
from collections import deque

from fastapi import FastAPI
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel, Field

app = FastAPI(title="Eye 2 Dot 폴링 시험 서버")

DEVICE_OF_MODE = {"label": "fw1", "book": "fw2"}

queues = {"fw1": deque(), "fw2": deque()}  # 기다리는 작업
in_progress = {}                           # 기기에 넘겨준 작업  id → {device, job, since}
history = []                               # 끝난 작업 (최근 20개)
speech_ids = set()                         # /print 가 붙여 준 speech_id

# 시험용 음성: 1초짜리 440Hz 사인파 (8kHz, 8비트 unsigned, 무음 = 128)
SAMPLE_RATE = 8000
TEST_TONE = bytes(
    int(round(128 + 100 * math.sin(2 * math.pi * 440 * n / SAMPLE_RATE)))
    for n in range(SAMPLE_RATE)
)


class PrintRequest(BaseModel):
    """앱의 "출력하기" — 결과 화면에서 받은 값을 그대로 보낸다."""
    mode: str = "label"
    text: str = "우유"
    reason: str = "흰색 종이팩에 우유 표기가 있습니다."
    braille: list = Field(default_factory=lambda: [13, 41])
    with_speech_id: bool = True   # false 로 보내면 speech_id 없이 → ESP32 가 /speech?text= 로 받는 경로 시험


class DoneReport(BaseModel):
    device: str
    ok: bool
    error: str = ""


def fail(status: int, error: str, reason: str):
    return JSONResponse(status_code=status,
                        content={"ok": False, "error": error, "reason": reason})


@app.get("/health")
def health():
    return {"status": "ok", "model": "poll-test"}


@app.post("/print")
def enqueue(req: PrintRequest):
    device = DEVICE_OF_MODE.get(req.mode)
    if device is None:
        return fail(400, "invalid_mode", "모드 값이 잘못되었습니다.")
    if not req.braille:
        return fail(400, "missing_braille", "출력할 점자 데이터가 없습니다.")

    job = {"id": uuid.uuid4().hex[:12], "mode": req.mode,
           "text": req.text, "reason": req.reason, "braille": req.braille}
    if req.with_speech_id:
        job["speech_id"] = "s" + job["id"]
        speech_ids.add(job["speech_id"])
    queues[device].append(job)
    print(f"[대기열] {device} ← {job['id']} ({req.text})")
    return {"ok": True, "job_id": job["id"], "device": device,
            "waiting": len(queues[device])}


@app.get("/jobs/next")
def next_job(device: str):
    if device not in queues:
        return fail(400, "invalid_device", "기기 이름은 fw1 또는 fw2 입니다.")
    if not queues[device]:
        return {"ok": True, "job": None}

    job = queues[device].popleft()
    in_progress[job["id"]] = {"device": device, "job": job, "since": time.time()}
    print(f"[전달] {device} → {job['id']}")
    return {"ok": True, "job": job}


@app.post("/jobs/{job_id}/done")
def job_done(job_id: str, rep: DoneReport):
    entry = in_progress.pop(job_id, None)
    if entry is None:
        return fail(404, "unknown_job", "진행 중인 작업이 아닙니다.")

    took = round(time.time() - entry["since"], 1)
    history.append({"id": job_id, "device": rep.device, "ok": rep.ok,
                    "error": rep.error, "seconds": took})
    del history[:-20]
    print(f"[완료] {job_id} ok={rep.ok} {rep.error} ({took}초)")
    return {"ok": True}


@app.get("/jobs")
def status():
    """지금 상태 한눈에 보기 (브라우저로 열어도 됨)"""
    return {
        "waiting": {d: [j["id"] for j in q] for d, q in queues.items()},
        "in_progress": {i: {"device": e["device"],
                            "seconds": round(time.time() - e["since"], 1)}
                        for i, e in in_progress.items()},
        "history": history,
    }


def pcm_response(pcm: bytes):
    """실제 서버의 /speech 와 같은 모양: 본문 = raw PCM, Content-Length 자동, 재생 시간 헤더"""
    return Response(content=pcm, media_type="application/octet-stream",
                    headers={"X-Audio-Duration": f"{len(pcm) / SAMPLE_RATE:.3f}"})


@app.get("/speech/{speech_id}")
def speech_by_id(speech_id: str):
    """speech_id 로 음성 받기 (실제 서버에는 아직 없음 — 서버 담당이 만들 자리)"""
    if speech_id not in speech_ids:
        return fail(404, "unknown_speech", "그런 speech_id 가 없습니다.")
    print(f"[음성] /speech/{speech_id} → 440Hz 1초 ({len(TEST_TONE)} bytes)")
    return pcm_response(TEST_TONE)


@app.get("/speech")
def speech_by_text(text: str = ""):
    """text 로 음성 받기 (실제 서버에 이미 있는 주소). 시험 서버는 내용과 상관없이 440Hz"""
    if not text.strip():
        return fail(400, "missing_text", "읽어줄 텍스트가 없습니다.")
    print(f"[음성] /speech?text={text} → 440Hz 1초 ({len(TEST_TONE)} bytes)")
    return pcm_response(TEST_TONE)
