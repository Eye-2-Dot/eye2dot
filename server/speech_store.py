"""
음성 번호(speech_id) 보관소 — 펌웨어 1 전용

서버가 음성을 미리 만들어 번호를 붙여 두면, 기기(ESP32)는 GET /speech/{speech_id} 로
번호만 대고 받아 간다. 긴 문장을 주소에 실어 보내지 않아도 되고, 음성을 응답 전에
만들어 두면 기기의 음성 받기 제한 시간(10초)에 걸리지 않는다.

공개 함수 (main.py 가 씀):
    create_ready(text) : 지금 이 자리에서 음성을 만들고 번호를 돌려준다. 실패하면 None.
                         /process-file·/process-mock?device=fw1 처럼 응답 전에 만들어 둘 때.
    create(text)       : 번호를 바로 돌려주고 음성은 뒤에서(백그라운드) 만든다.
                         /print 처럼 요청한 쪽을 기다리게 하면 안 될 때.
    ensure(sid, text)  : 번호가 아직 살아 있으면 그대로, 만료됐으면 create(text) 로 새 번호.
    get(sid)           : 번호 → PCM 바이트. 만드는 중이면 끝날 때까지(최대 WAIT_SECONDS) 기다린다.
                         없는 번호면 UnknownSpeech, 음성 생성 실패면 SpeechError.

보관 규칙:
    - 메모리에만 둔다 (서버를 다시 켜면 비워진다).
    - 최대 MAX_ENTRIES 개, 만든 뒤 TTL_SECONDS 가 지나면 버린다. 넘치면 오래된 것부터.
    - speech.py 의 텍스트 캐시(최근 64개)와는 별개다. PCM 바이트는 같은 객체를 가리키므로
      두 곳에 있어도 메모리를 두 배로 쓰지는 않는다.
"""

import secrets
import threading
import time
from collections import OrderedDict
from concurrent.futures import ThreadPoolExecutor

from speech import text_to_pcm, SpeechError

MAX_ENTRIES = 32        # 200자 문장(약 300KB)으로 가득 차도 10MB 남짓
TTL_SECONDS = 10 * 60   # 만든 뒤 10분
# 만드는 중인 음성을 기다리는 최대 시간 = 음성 생성(15초) + 변환(10초). speech.py 의 제한 시간과 맞춘다
WAIT_SECONDS = 25

# 백그라운드 생성용. 동시에 여러 개를 만들어도 edge-tts 서버에 무리가 가지 않도록 2개까지만
_executor = ThreadPoolExecutor(max_workers=2, thread_name_prefix="speech-store")


class UnknownSpeech(Exception):
    """없는 번호이거나 만료된 번호."""


class _Entry:
    __slots__ = ("text", "created", "done", "pcm", "error")

    def __init__(self, text: str):
        self.text = text
        self.created = time.monotonic()
        self.done = threading.Event()   # 생성이 끝나면(성공·실패 모두) set
        self.pcm = None
        self.error = None               # 실패하면 SpeechError


# 넣은 순서 = 오래된 순서. FastAPI 가 요청마다 다른 스레드에서 부르므로 락으로 보호한다.
_entries: "OrderedDict[str, _Entry]" = OrderedDict()
_lock = threading.Lock()


def _drop_expired(now: float) -> None:
    """만료된 항목을 버린다. 락을 잡은 상태에서 부를 것."""
    expired = [sid for sid, e in _entries.items() if now - e.created > TTL_SECONDS]
    for sid in expired:
        del _entries[sid]


def _store(entry: _Entry) -> str:
    """새 번호를 붙여 보관한다. 자리가 없으면 가장 오래된 것부터 버린다."""
    with _lock:
        _drop_expired(time.monotonic())
        while len(_entries) >= MAX_ENTRIES:
            _entries.popitem(last=False)
        sid = secrets.token_hex(6)          # 영문(a~f)·숫자 12자 → 주소에 그대로 넣을 수 있다
        while sid in _entries:
            sid = secrets.token_hex(6)
        _entries[sid] = entry
        return sid


def _generate(entry: _Entry) -> None:
    """entry.text 로 음성을 만든다. 성공·실패 모두 entry 에 담고 done 을 켠다."""
    try:
        entry.pcm = text_to_pcm(entry.text)
    except SpeechError as e:
        entry.error = e
    except Exception as e:  # 예상하지 못한 오류도 기기에는 같은 형식으로 알린다
        entry.error = SpeechError("conversion_failed", f"음성을 만들지 못했습니다: {e}")
    finally:
        entry.done.set()


def create_ready(text: str):
    """음성을 지금 만들고 번호를 돌려준다. 만들지 못하면 None (번호를 만들지 않는다).

    오래 걸리는 작업(수 초)이므로 이벤트 루프가 아닌 스레드에서 부를 것.
    """
    text = text.strip()
    if not text:
        return None
    entry = _Entry(text)
    _generate(entry)
    if entry.error is not None:
        print(f"[음성 번호] 만들지 못함 — {entry.error.error_code}: {entry.error.reason}")
        return None
    sid = _store(entry)
    print(f"[음성 번호] {sid} ← '{text}' ({len(entry.pcm)} bytes)")
    return sid


def create(text: str):
    """번호를 바로 돌려주고 음성은 백그라운드에서 만든다. 텍스트가 비면 None."""
    text = text.strip()
    if not text:
        return None
    entry = _Entry(text)
    sid = _store(entry)
    _executor.submit(_generate, entry)
    print(f"[음성 번호] {sid} ← '{text}' (백그라운드로 만드는 중)")
    return sid


def ensure(sid, text: str):
    """sid 가 살아 있으면 그대로, 없거나 만료됐으면 text 로 새 번호를 만든다(백그라운드)."""
    if sid:
        with _lock:
            _drop_expired(time.monotonic())
            if sid in _entries:
                return sid
    return create(text)


def get(sid: str, wait_seconds: float = WAIT_SECONDS) -> bytes:
    """번호 → PCM 바이트.

    없는 번호·만료된 번호 → UnknownSpeech
    만들다 실패 → 그때의 SpeechError
    wait_seconds 안에 다 못 만듦 → SpeechError("tts_unavailable")
    """
    with _lock:
        _drop_expired(time.monotonic())
        entry = _entries.get(sid)
    if entry is None:
        raise UnknownSpeech(sid)
    if not entry.done.wait(wait_seconds):
        raise SpeechError("tts_unavailable", "음성을 아직 만드는 중입니다. 잠시 후 다시 요청하세요.")
    if entry.error is not None:
        raise entry.error
    return entry.pcm
