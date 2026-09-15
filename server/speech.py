"""
텍스트 → 음성(raw PCM) 변환 모듈

펌웨어 1(라벨 출력기)이 인식 결과를 소리 내어 읽어줄 때 쓴다.
음성 생성은 서버가 전부 맡고, 기기(ATmega2560)는 받은 바이트를 PWM으로 재생만 한다.

공개 함수 (main.py가 이 이름으로 import함, 변경 금지):
    text_to_pcm(text)      : 한국어 텍스트 → raw PCM 바이트
    estimate_duration(pcm) : PCM 바이트 → 재생 시간(초)
    SpeechError            : 음성 생성 실패 예외. error_code로 원인을 구분한다

오디오 형식 (기기 재생 코드와 반드시 일치해야 한다):
    8000 Hz / 8비트 unsigned (0~255, 무음 = 128) / 모노 / 헤더 없는 raw 바이트
    → 1초 = 정확히 8000바이트

변환 경로:
    텍스트 ──edge-tts──▶ MP3 (24kHz, 48kbps, 모노) ──ffmpeg──▶ raw PCM

pydub을 쓰지 않는 이유:
    pydub은 표준 라이브러리 audioop에 의존하는데, audioop은 Python 3.13에서 삭제됐다.
    MP3 디코딩은 어차피 ffmpeg가 하므로 중간 래퍼 없이 ffmpeg를 직접 호출한다.

필요한 패키지: edge-tts, imageio-ffmpeg (requirements.txt에 포함)
"""

import asyncio
import concurrent.futures
import shutil
import subprocess
import threading
from collections import OrderedDict
from functools import lru_cache

# 음성용 패키지가 없는 환경에서도 서버의 다른 엔드포인트(/process-mock 등)는 그대로 뜨도록
# import 실패는 여기서 삼키고, 실제로 음성을 만들 때 SpeechError로 알린다.
try:
    import edge_tts
    import aiohttp  # edge-tts가 내부에서 쓰는 HTTP 라이브러리. 네트워크 예외를 구분하려고 가져온다
except ImportError:
    edge_tts = aiohttp = None

try:
    import imageio_ffmpeg
except ImportError:
    imageio_ffmpeg = None


# ─────────────────────────────────────────────
# 음성 설정
# ─────────────────────────────────────────────
VOICE = "ko-KR-SunHiNeural"  # 한국어 여성
# VOICE = "ko-KR-InJoonNeural"  # 한국어 남성

# ─────────────────────────────────────────────
# 오디오 형식 — 펌웨어 1 재생 코드와 반드시 일치해야 한다.
# 바꾸려면 _mp3_to_pcm()의 ffmpeg 출력 옵션(-f u8)도 함께 바꿀 것.
# ─────────────────────────────────────────────
SAMPLE_RATE = 8000   # Hz
SAMPLE_WIDTH = 1     # 샘플 하나의 바이트 수 (1 = 8비트)
CHANNELS = 1         # 모노
BYTES_PER_SECOND = SAMPLE_RATE * SAMPLE_WIDTH * CHANNELS  # 8000
SILENCE = 128        # 8비트 unsigned에서 무음(파형의 중앙)은 0이 아니라 128

# ─────────────────────────────────────────────
# 제한 시간
# 인터넷이 끊겼을 때 요청이 끝없이 매달려 있지 않도록 외부 작업마다 상한을 둔다.
# ─────────────────────────────────────────────
TTS_CONNECT_TIMEOUT_SECONDS = 5  # 음성 서버 연결. 끊긴 네트워크를 빨리 알아채도록 짧게
TTS_TIMEOUT_SECONDS = 15         # 음성 생성 전체. 200자 문장을 기준으로 여유 있게 잡은 값이며, 실측 후 조정할 것
FFMPEG_TIMEOUT_SECONDS = 10      # MP3 → PCM 변환

# ─────────────────────────────────────────────
# 캐시 항목 수 상한
# 항목 하나는 보통 1~3초(8~24KB)이고 200자 문장이라도 수백 KB 수준이라,
# 전부 긴 문장으로 가득 차도 메모리는 수십 MB 이내다.
# ─────────────────────────────────────────────
CACHE_MAX_ENTRIES = 64


class SpeechError(Exception):
    """음성 생성 실패. main.py가 error_code를 보고 HTTP 상태 코드를 정한다.

    tts_unavailable   : 음성 서버(edge-tts)를 쓸 수 없음
                        — 인터넷 끊김, 시간 초과, 음성 서버의 거부·이상 응답, edge-tts 미설치
    conversion_failed : MP3 → PCM 변환 실패 — ffmpeg 없음, 변환 오류
    """

    def __init__(self, error_code: str, reason: str):
        self.error_code = error_code
        self.reason = reason
        super().__init__(reason)


# ─────────────────────────────────────────────
# 동기 코드에서 async 함수 실행
# ─────────────────────────────────────────────
def _run_async(make_coroutine):
    """async 함수를 동기 코드에서 실행하고 결과를 돌려준다.

    edge-tts는 async 전용인데 text_to_pcm()은 동기 함수여야 해서 필요하다.
      - 이벤트 루프가 없는 스레드(스크립트 실행, FastAPI의 def 엔드포인트)
        → 그 자리에서 asyncio.run()
      - 이벤트 루프가 이미 도는 스레드(async def 안에서 호출)
        → 거기서 asyncio.run()을 부르면 RuntimeError가 나므로 새 스레드에서 실행하고 기다린다.
          기다리는 동안 그 루프가 멈추므로 async 코드에서는 가급적 부르지 말 것.
    어느 경우든 안에서 난 예외는 호출한 쪽으로 그대로 올라온다.

    edge-tts에도 동기용 stream_sync()가 있지만 쓰지 않는다. 내부 스레드에서 예외가 나면
    끝났다는 신호가 전달되지 않아 호출한 쪽이 영원히 멈춘다(7.2.8 기준).
    인터넷이 끊겼을 때가 바로 그 경우다.
    """
    try:
        asyncio.get_running_loop()
    except RuntimeError:
        return asyncio.run(make_coroutine())

    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        return pool.submit(lambda: asyncio.run(make_coroutine())).result()


# ─────────────────────────────────────────────
# 1단계: 텍스트 → MP3 (edge-tts, 인터넷 필요)
# ─────────────────────────────────────────────
async def _download_mp3(text: str) -> bytes:
    """Microsoft 음성 서버에서 MP3를 조각조각 받아 하나로 합친다."""
    communicate = edge_tts.Communicate(text, VOICE, connect_timeout=TTS_CONNECT_TIMEOUT_SECONDS)
    chunks = []
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":  # 나머지는 문장 경계 메타데이터라 필요 없다
            chunks.append(chunk["data"])
    return b"".join(chunks)


def _synthesize_mp3(text: str) -> bytes:
    """텍스트 → MP3 바이트. 음성 서버를 쓸 수 없으면 SpeechError("tts_unavailable")."""
    if edge_tts is None:
        raise SpeechError(
            "tts_unavailable",
            "서버에 edge-tts가 설치되어 있지 않습니다. pip install -r requirements.txt 를 실행하세요.",
        )

    try:
        # 연결은 됐는데 응답이 멈춘 경우까지 막으려고 전체 시간에 상한을 둔다
        mp3 = _run_async(lambda: asyncio.wait_for(_download_mp3(text), TTS_TIMEOUT_SECONDS))
    # TimeoutError는 OSError의 하위 클래스라 아래 줄보다 먼저 잡아야 메시지가 구분된다
    except asyncio.TimeoutError as e:
        raise SpeechError("tts_unavailable", "음성 서버 응답 시간이 초과되었습니다.") from e
    except (edge_tts.exceptions.EdgeTTSException, aiohttp.ClientError, OSError) as e:
        # OSError           : DNS 조회 실패, 연결 거부 등 인터넷이 끊겼을 때 나는 예외의 공통 부모
        # aiohttp.ClientError: 연결은 됐지만 음성 서버가 거부(403 등)한 경우
        # EdgeTTSException  : 음성 서버가 오디오를 보내지 않았거나 알 수 없는 응답을 준 경우
        raise SpeechError("tts_unavailable", f"음성 서버에 연결하지 못했습니다: {e}") from e

    if not mp3:
        raise SpeechError("tts_unavailable", "음성 서버가 오디오를 보내지 않았습니다.")
    return mp3


# ─────────────────────────────────────────────
# 2단계: MP3 → raw PCM (ffmpeg)
# ─────────────────────────────────────────────
@lru_cache(maxsize=1)
def _find_ffmpeg() -> str:
    """ffmpeg 실행 파일 경로. 한 번 찾으면 기억해 둔다 (못 찾았을 때는 기억하지 않고 다음에 다시 찾는다).

    1순위 imageio-ffmpeg: pip으로 받은 ffmpeg. 이 패키지가 알아서
          IMAGEIO_FFMPEG_EXE 환경변수 → 패키지에 든 실행 파일 → 시스템 ffmpeg 순으로 찾는다.
          경로를 직접 지정하고 싶으면 IMAGEIO_FFMPEG_EXE 환경변수를 쓰면 된다.
    2순위 시스템 PATH의 ffmpeg (imageio-ffmpeg가 설치되지 않은 경우)
    """
    if imageio_ffmpeg is not None:
        try:
            return imageio_ffmpeg.get_ffmpeg_exe()
        except RuntimeError:
            pass

    path = shutil.which("ffmpeg")
    if path:
        return path

    raise SpeechError(
        "conversion_failed",
        "ffmpeg를 찾지 못했습니다. pip install -r requirements.txt 로 imageio-ffmpeg를 설치하세요.",
    )


def _mp3_to_pcm(mp3: bytes) -> bytes:
    """MP3 바이트 → 8kHz / 8비트 unsigned / 모노 raw PCM. 임시 파일 없이 파이프로만 주고받는다."""
    command = [
        _find_ffmpeg(),
        "-hide_banner", "-loglevel", "error",  # 에러가 아니면 아무것도 출력하지 않는다
        "-f", "mp3", "-i", "pipe:0",           # 입력: 표준입력으로 넣어준 MP3
        "-ac", str(CHANNELS),                  # 모노
        "-ar", str(SAMPLE_RATE),               # 8000Hz로 리샘플링
        "-f", "u8",                            # 출력 형식: 헤더 없는 8비트 unsigned raw PCM
        "pipe:1",                              # 출력: 표준출력
    ]
    try:
        result = subprocess.run(command, input=mp3, capture_output=True, timeout=FFMPEG_TIMEOUT_SECONDS)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise SpeechError("conversion_failed", f"ffmpeg를 실행하지 못했습니다: {e}") from e

    if result.returncode != 0 or not result.stdout:
        detail = result.stderr.decode("utf-8", errors="replace").strip() or "출력 없음"
        raise SpeechError("conversion_failed", f"MP3 → PCM 변환에 실패했습니다: {detail}")

    return result.stdout


# ─────────────────────────────────────────────
# 캐시
# FastAPI가 요청마다 다른 스레드에서 실행하므로 락으로 보호한다.
# OrderedDict의 순서 = 사용 순서. 맨 앞이 가장 오래 안 쓴 항목이다.
# ─────────────────────────────────────────────
_cache: OrderedDict[str, bytes] = OrderedDict()
_cache_lock = threading.Lock()


# ─────────────────────────────────────────────
# 공개 함수
# ─────────────────────────────────────────────
def text_to_pcm(text: str) -> bytes:
    """한국어 텍스트 → 8kHz / 8비트 unsigned / 모노 raw PCM 바이트.

    앞뒤 공백은 떼고 처리하며, 같은 텍스트는 캐시에서 바로 돌려준다.
    텍스트가 비어 있으면 ValueError (호출하는 쪽에서 미리 걸러야 한다).
    음성 생성에 실패하면 SpeechError (error_code는 SpeechError 설명 참고).
    """
    key = text.strip()
    if not key:
        raise ValueError("변환할 텍스트가 비어 있습니다")

    with _cache_lock:
        cached = _cache.get(key)
        if cached is not None:
            _cache.move_to_end(key)  # 방금 쓴 항목으로 표시
            return cached

    # 생성은 락 밖에서 한다. 수 초 걸리는 네트워크 작업 동안 다른 텍스트의 요청까지 막으면 안 된다.
    # 같은 텍스트가 동시에 들어오면 두 번 생성될 수 있지만 결과가 같으므로 문제없다.
    pcm = _mp3_to_pcm(_synthesize_mp3(key))

    with _cache_lock:
        _cache[key] = pcm
        _cache.move_to_end(key)
        while len(_cache) > CACHE_MAX_ENTRIES:
            _cache.popitem(last=False)  # 가장 오래 안 쓴 항목부터 버린다

    return pcm


def estimate_duration(pcm: bytes) -> float:
    """PCM 바이트 수 → 재생 시간(초). 1초 = 8000바이트이므로 len(pcm) / 8000."""
    return len(pcm) / BYTES_PER_SECOND


# ─────────────────────────────────────────────
# 자체 테스트: 실제로 음성을 만들어 길이를 확인하고, 들어볼 수 있게 WAV로 저장한다.
# 인터넷 연결과 edge-tts, imageio-ffmpeg 설치가 필요하다.
# ─────────────────────────────────────────────
if __name__ == "__main__":
    import time
    import wave
    from pathlib import Path

    samples = [
        "우유입니다",                        # 짧은 라벨 이름
        "흰색 종이팩에 우유 표기가 있습니다",  # 실제 판단 근거(reason) 길이의 문장
    ]

    print(f"음성: {VOICE}")
    print()

    pcms = []
    for text in samples:
        started = time.perf_counter()
        try:
            pcm = text_to_pcm(text)
        except SpeechError as e:
            print(f"[실패] '{text}' → {e.error_code}: {e.reason}")
            raise SystemExit(1)
        elapsed = time.perf_counter() - started
        pcms.append(pcm)

        print(f"'{text}' ({len(text)}자)")
        print(f"  바이트 수      : {len(pcm):,} bytes")
        print(f"  예상 재생 시간 : {estimate_duration(pcm):.3f} 초")
        # 형식이 unsigned로 제대로 나왔다면 평균이 무음값 128 근처에 온다
        print(f"  샘플 값        : {min(pcm)} ~ {max(pcm)}, 평균 {sum(pcm) / len(pcm):.1f} (무음 = {SILENCE})")
        print(f"  생성 소요      : {elapsed:.2f} 초")
        print()

    # 같은 텍스트를 다시 요청하면 생성 없이 캐시에서 바로 나와야 한다
    started = time.perf_counter()
    again = text_to_pcm(samples[0])
    elapsed_ms = (time.perf_counter() - started) * 1000
    print(f"[캐시] '{samples[0]}' 재요청: {elapsed_ms:.2f} ms, 캐시된 결과와 동일: {again is pcms[0]}")

    # 문장 사이에 0.5초 무음을 넣어 한 파일로 저장한다
    gap = bytes([SILENCE]) * (BYTES_PER_SECOND // 2)
    out_path = Path(__file__).with_name("test_output.wav")
    with wave.open(str(out_path), "wb") as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(SAMPLE_WIDTH)  # WAV 규격에서 8비트 샘플은 unsigned라 PCM을 변환 없이 그대로 쓴다
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(gap.join(pcms))
    print(f"[저장] {out_path}")
