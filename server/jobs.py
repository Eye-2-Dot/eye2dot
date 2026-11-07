"""
출력 작업 대기열 (폴링) — docs/POLLING_API.md 1~4장

앱이 "출력하기"로 넣은 작업(POST /print)을 기기별(fw1, fw2)로 줄 세워 두고,
기기가 GET /jobs/next 로 하나씩 가져가 출력한 뒤 POST /jobs/{id}/done 으로 보고한다.

정해 둔 것 (POLLING_API.md "정해 둔 것"):
    - 한 번에 하나씩, 들어온 순서대로 준다. 준 작업은 "진행 중"으로 옮기고 다시 주지 않는다.
    - 완료 보고가 안 와도 자동으로 다시 주지 않는다 (책 한 쪽이 두 번 찍히지 않게).
    - 메모리에만 둔다. 서버를 다시 켜면 비워진다.

이 모듈은 줄 세우기만 한다. 점자를 거울로 바꾸거나 음성 번호를 붙이는 일은 main.py 가 한다.
FastAPI 가 요청마다 다른 스레드에서 부르므로 모든 변경은 락 안에서 한다.
"""

import threading
import time
import uuid
from collections import deque

HISTORY_SIZE = 20   # GET /jobs 에 보여 줄 최근 완료 작업 수


class JobQueue:
    def __init__(self, devices):
        self._lock = threading.Lock()
        self._waiting = {d: deque() for d in devices}   # 기기 → 기다리는 작업들
        self._in_progress = {}                          # 작업 id → {"device", "job", "since"}
        self._history = []                              # 끝난 작업 (최근 HISTORY_SIZE 개)

    def add(self, device: str, job: dict):
        """작업을 줄 끝에 넣는다. 돌려주는 값 = (작업 id, 그 기기에서 기다리는 작업 수)."""
        job = {"id": uuid.uuid4().hex[:12], **job}   # id 는 영문·숫자만 (주소에 그대로 들어감)
        with self._lock:
            self._waiting[device].append(job)
            return job["id"], len(self._waiting[device])

    def take_next(self, device: str):
        """줄 맨 앞 작업을 꺼내 "진행 중"으로 옮긴다. 없으면 None."""
        with self._lock:
            if not self._waiting[device]:
                return None
            job = self._waiting[device].popleft()
            self._in_progress[job["id"]] = {"device": device, "job": job, "since": time.time()}
            return job

    def finish(self, job_id: str, device: str, ok: bool, error: str):
        """완료 보고. 진행 중인 작업이 아니면 None, 맞으면 걸린 시간(초)."""
        with self._lock:
            entry = self._in_progress.pop(job_id, None)
            if entry is None:
                return None
            took = round(time.time() - entry["since"], 1)
            self._history.append({"id": job_id, "device": device, "ok": ok,
                                  "error": error, "seconds": took})
            del self._history[:-HISTORY_SIZE]
            return took

    def status(self) -> dict:
        """GET /jobs 용: 기다리는 작업, 진행 중인 작업(경과 초), 최근 완료 기록."""
        now = time.time()
        with self._lock:
            return {
                "waiting": {d: [j["id"] for j in q] for d, q in self._waiting.items()},
                "in_progress": {i: {"device": e["device"], "seconds": round(now - e["since"], 1)}
                                for i, e in self._in_progress.items()},
                "history": list(self._history),
            }
