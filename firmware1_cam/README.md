# eye2dot_cam — ESP32-CAM 카메라·통신 펌웨어 (PlatformIO)

ESP32-CAM 과 서버 사이의 통신 전부를 담당. 경로는 두 가지다.

1. **방아쇠 → 촬영** → `/process-file` 로 전송 → 결과 바로 받기
2. **폴링** — 1초마다 `/jobs/next` 로 출력 작업을 확인 (앱의 "출력하기")

음성·타각은 `src/main.cpp` 위쪽의 "연결 지점" 함수로 넘긴다.
폴링 규격은 `POLLING_API.md`.

## 처음 한 번

1. VS Code에서 이 폴더를 연다 (PlatformIO 확장 설치 필요).
2. `include/secrets.example.h` 를 `include/secrets.h` 로 복사하고 Wi-Fi·노트북 IP를 채운다.
   `secrets.h` 는 `.gitignore` 에 있어 커밋되지 않는다.

## 매번

| 작업 | 명령 | VS Code 하단 바 |
|---|---|---|
| 빌드 | `pio run` | ✓ |
| 업로드 | `pio run -t upload` | → |
| 시리얼 모니터 | `pio device monitor` | 🔌 |

목 서버 ↔ 실제 AI 전환은 `src/main.cpp` 의 `USE_MOCK`,
라벨 ↔ 책은 `DEVICE_MODE`.

## 폴링 시험 (실제 서버 없이)

```bash
# 터미널 1 — 가상환경 켠 상태에서
cd firmware1_cam/tools
uvicorn poll_test_server:app --host 0.0.0.0 --port 8000

# 터미널 2 — 출력 작업 하나 넣기 (기본값: 우유)
curl -X POST http://127.0.0.1:8000/print -H "Content-Type: application/json" -d "{}"
```

1초 안에 ESP32 시리얼에 `── 출력 작업 … ──` 과 점자가 찍히고,
터미널 1에 `[전달]` → `[완료]` 가 뜨면 성공. 상태는 브라우저로 `/jobs`.
실제 서버와 포트가 같으니 하나만 켤 것.

## 자주 나는 문제

| 증상 | 해결 |
|---|---|
| `secrets.h` 관련 `#error` | 위 2번 |
| 업로드 시 `Failed to connect` | MB 보드면 IO0 버튼 누른 채 업로드 시작, FTDI면 IO0–GND 연결 후 리셋 |
| 모니터를 열면 아무것도 안 나옴 | MB 보드의 RST 버튼 한 번 |
| `Brownout detector was triggered` | 전원 부족 — 5V 2A 이상 |
| `[폴링] 서버에 /jobs/next 가 없습니다` | 실제 서버에 폴링 API가 아직 없음 → 시험 서버 사용 |
| 업로드 포트를 못 찾음 | `platformio.ini` 에 `upload_port = COM5` 처럼 직접 지정 |
