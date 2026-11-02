# eye2dot_cam — ESP32-CAM 카메라·통신 펌웨어 (PlatformIO)

촬영 → 서버 전송(`/process-file`, multipart) → 응답 파싱까지 담당.
음성·타각은 `src/main.cpp` 위쪽의 "연결 지점" 함수로 넘긴다.

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

## 자주 나는 문제

| 증상 | 해결 |
|---|---|
| `secrets.h` 관련 `#error` | 위 2번 |
| 업로드 시 `Failed to connect` | MB 보드면 IO0 버튼 누른 채 업로드 시작, FTDI면 IO0–GND 연결 후 리셋 |
| 모니터를 열면 아무것도 안 나옴 | MB 보드의 RST 버튼 한 번 |
| `Brownout detector was triggered` | 전원 부족 — 5V 2A 이상 |
| 업로드 포트를 못 찾음 | `platformio.ini` 에 `upload_port = COM5` 처럼 직접 지정 |
