# 펌웨어 1 — 라벨 출력기 (ESP32-CAM + ATmega2560)

라벨 출력기 안에는 칩이 2개 있고, 각각 독립된 PlatformIO 프로젝트다.

| 칩 | 폴더 | 맡은 일 |
|---|---|---|
| **ESP32-CAM** | `cam/` | 카메라 촬영, Wi-Fi, 서버 통신 전부. 서버의 JSON·음성을 작은 바이트 묶음(프레임)으로 바꿔 ATmega 로 넘김 |
| **ATmega2560** (Arduino Mega) | `mega/` | 스피커(음성 재생), 확인·취소 버튼, 솔레노이드(타각). JSON 은 다루지 않음 |

ESP32 의 경로는 두 가지다.

1. **방아쇠 → 촬영** → `/process-file` 로 전송 → 결과 바로 받기
2. **폴링** — 1초마다 `/jobs/next` 로 출력 작업을 확인 (앱의 "출력하기")

## 데이터 흐름

```
 서버 ──Wi-Fi(HTTP)──▶ ESP32-CAM ──UART(전선 3가닥)──▶ ATmega2560 ──▶ 스피커 / 솔레노이드
   ▲   /process-file      │  점자·음성을 프레임으로        │   'R' 다음 음성 조각
   │   /jobs/next         │                                 │   'D' 출력 완료
   │   /speech            ◀─────────────────────────────────┘   'E' 실패·취소
   └── /jobs/{id}/done ◀── 결과 보고
```

한 작업: 점자 읽기 → 음성 받기("{text}. {reason}") → ATmega 로 점자·음성 → ATmega 가 음성을 들려줌 →
**확인 버튼 = 타각 후 'D'**, **취소 버튼 = 'E'** → (폴링 작업이면) 서버에 완료 보고.
서버가 실패로 답하면 "인식하지 못했어요. 다시 찍어 주세요" 음성만, 서버에 닿지 못하면 삐 소리(알림음).

## 폴더 구조

```
firmware1/
├── README.md               이 파일
├── common/
│   └── protocol.h          두 칩 공통 시리얼 약속 (프레임 종류, 통신 속도, 크기 한도)
├── mega/                   ATmega2560 — 독립 PlatformIO 프로젝트 [env:mega]
│   ├── platformio.ini
│   ├── upload_ports.example.ini
│   ├── include/            pins.h, speech.h, link.h
│   └── src/                main.cpp (작업 흐름·버튼·USB 시험 명령), speech.cpp (소리), link.cpp (프레임 받기)
├── cam/                    ESP32-CAM — 독립 PlatformIO 프로젝트 [env:esp32cam]
│   ├── platformio.ini
│   ├── upload_ports.example.ini
│   ├── include/            cam.h, net.h, link.h, secrets.example.h, (secrets.h — 내 컴퓨터에만)
│   └── src/                main.cpp (전체 흐름), cam.cpp (카메라), net.cpp (서버), link.cpp (ATmega 로 보내기)
└── tools/
    └── poll_test_server.py 실제 서버 없이 폴링·음성을 시험하는 미니 서버
```

API·프레임 자세한 규격: 저장소 루트의 [`docs/POLLING_API.md`](../docs/POLLING_API.md).

> **VS Code 에서는 `firmware1/mega` 또는 `firmware1/cam` 폴더를 열어야** PlatformIO 가 프로젝트로 인식한다.
> 저장소 루트나 `firmware1/` 을 열면 인식하지 않는다. 두 칩을 번갈아 만질 때는 VS Code 창을 두 개 띄우면 편하다.

## 배선

| ESP32-CAM | | Arduino Mega | 비고 |
|---|---|---|---|
| GPIO15 (TX) | → | D19 (RX1) | 직결. ATmega 는 3.3V 를 HIGH 로 인식 |
| GPIO14 (RX) | ← 전압 분배 ← | D18 (TX1) | **5V 직결 금지 — ESP32 가 망가진다** |
| GND | — | GND | **반드시 공통** (없으면 통신이 안 됨) |

전압 분배 (Mega 5V → 약 3.3V):

```
Mega D18 ──[ 1kΩ ]──┬──▶ ESP32 GPIO14
                    │
                  [ 2kΩ ]
                    │
                   GND
```

- Mega 의 `Serial`(D0·D1)은 USB 업로드·시험용이라 ESP32 연결에 쓰지 않는다.
- ESP32-CAM 의 GPIO14·15 는 microSD 슬롯과 같은 핀이다 → **SD 카드를 꽂지 말 것.**
- 버튼: 한쪽은 Mega 핀, 다른 쪽은 GND (누르면 LOW, 내부 풀업 사용)

| 버튼 | Mega 핀 |
|---|---|
| 확인 | D32 |
| 취소 | D33 |
| 모드 | D31 (아직 쓰지 않음) |

나머지 핀(스피커 D11, 솔레노이드 D22~27 등)은 `mega/include/pins.h`.

## 처음 한 번

1. VS Code 에 PlatformIO 확장을 설치한다.
2. `cam/include/secrets.example.h` 를 같은 폴더에 `secrets.h` 로 복사하고 Wi-Fi·노트북 IP를 채운다.
   `secrets.h` 는 `.gitignore` 에 있어 커밋되지 않는다. **견본 파일에는 진짜 값을 넣지 말 것.**

## 빌드·업로드

각 폴더(`firmware1/mega`, `firmware1/cam`)에서:

| 작업 | 명령 | VS Code 하단 바 |
|---|---|---|
| 빌드 | `pio run` | ✓ |
| 업로드 | `pio run -t upload` | → |
| 시리얼 모니터 | `pio device monitor` | 🔌 |

(`pio` 가 PATH 에 없으면 `~/.platformio/penv/Scripts/platformio.exe`)

**COM 포트** — 저장소에는 적지 않는다 (컴퓨터마다 다름).

- 보드를 **하나만** 꽂으면 PlatformIO 가 자동으로 찾는다.
- **둘 다** 꽂아서 다른 보드에 올라가거나 모니터가 엉뚱한 보드를 열면:
  그 폴더의 `upload_ports.example.ini` 를 `upload_ports.ini` 로 복사하고, `;` 를 지우고 번호를 적는다.
  (`upload_ports.ini` 는 커밋되지 않는다. 번호는 장치 관리자 → 포트, 또는 `pio device list`)
- **한 번만** 바꿔 올리려면 `pio run -t upload --upload-port COM5`

목 서버 ↔ 실제 AI 전환은 `cam/include/net.h` 의 `USE_MOCK`, 라벨 ↔ 책은 `DEVICE_MODE`.

## 시험 방법

### 1. ATmega 혼자 — USB 명령으로 소리 시험

`mega` 를 올리고 시리얼 모니터(115200)에서 한 글자를 보낸다.

| 명령 | 동작 |
|---|---|
| `0` | 정지 |
| `1` | 고정값 128 — 무음이어야 정상 |
| `2` | 고정값 200 — "툭" 소리가 나면 스피커 경로 정상 |
| `3` / `4` / `5` | 440 / 1000 / 220Hz 사인파 |
| `?` | 도움말 |

ESP32 작업이 진행 중일 때는 0~5 를 실행하지 않고 "작업 중" 이라고만 답한다.
2초마다 상태(재생, 작업, 받은 프레임 수, 남은 RAM)가 찍힌다.

### 2. ESP32 혼자 — `ATMEGA_CONNECTED = false` (기본값)

`cam/include/link.h` 의 `ATMEGA_CONNECTED` 가 `false` 면 ESP32 는 ATmega 로 프레임을 보내기만 하고
응답을 기다리지 않고 성공으로 처리한다. ATmega 없이 촬영·폴링·음성 받기를 시험할 때 쓴다.
보낸 프레임의 종류·크기는 ESP32 시리얼 모니터에 찍힌다.

### 3. 두 보드 연결 + 시험 서버 — 전체 흐름

1. 배선한 뒤 `cam/include/link.h` 의 `ATMEGA_CONNECTED` 를 **`true`** 로 바꿔 ESP32 를 다시 올린다.
   (또는 `cam/platformio.ini` 의 `build_flags` 에 `-DATMEGA_CONNECTED=true` 한 줄)
2. 시험 서버를 띄운다 (저장소 루트에서, 처음이면 `.venv` 를 만든다).

```bash
# 처음 한 번 (저장소 루트에서)
python -m venv .venv
source .venv/Scripts/activate
pip install fastapi "uvicorn[standard]"

# 터미널 1 — 시험 서버
source .venv/Scripts/activate
cd firmware1/tools
uvicorn poll_test_server:app --host 0.0.0.0 --port 8000

# 터미널 2 — 출력 작업 하나 넣기 (기본값: 우유, speech_id 포함)
curl -X POST http://127.0.0.1:8000/print -H "Content-Type: application/json" -d "{}"
# speech_id 없이 (/speech?text= 경로 시험)
curl -X POST http://127.0.0.1:8000/print -H "Content-Type: application/json" -d "{\"with_speech_id\": false}"
```

3. 1초 안에 ESP32 가 작업을 가져가고 → 스피커에서 1초짜리 "삐"(440Hz) → **확인 버튼(D32)** 을 누르면
   Mega 시리얼에 칸 값·점 번호가 찍히고 → 터미널 1에 `[완료] … ok=True` 가 뜨면 성공.
   **취소 버튼(D33)** 이면 `ok=False print_failed`. 상태는 브라우저로 `/jobs`.

시험 서버는 실제 서버와 포트(8000)가 같으니 하나만 켤 것.

## 시리얼 약속 요약

값은 모두 `common/protocol.h` 에 있다 (두 칩이 같은 파일을 쓴다. 바꾸면 두 보드 모두 다시 올릴 것).

- 115200bps 8N1, 프레임 = `[0xAA][종류][길이 2B][내용][검사값(XOR)]`
- ESP32 → ATmega: `0x01` 점자, `0x02` 음성 시작, `0x03` 음성 조각(최대 256B), `0x04` 음성 끝, `0x05` 알림음
- ATmega → ESP32: `'R'` 다음 조각, `'D'` 출력 완료, `'E'` 실패·취소
- 음성: 8kHz 8비트 unsigned 모노. ATmega 는 1KB 링 버퍼에 받으면서 재생한다 (8KB RAM 에 음성 전체가 안 들어가므로)

알림음: 짧게 2번 = 서버 연결 실패, 짧게 3번 = 촬영 실패, 낮게 길게 1번 = 그 밖의 실패.

## 알려진 점

- **통신 속도**: 16MHz ATmega 는 115200bps 를 약 2% 오차로 만든다. 짧은 배선에서는 보통 문제없고,
  깨진 프레임은 검사값으로 버린다. **하드웨어 시험에서 통신 오류가 잦으면** (Mega 상태 출력의 "버림" 숫자가
  계속 늘면) `common/protocol.h` 의 `LINK_BAUD` 를 **250000**(ATmega 오차 0%)으로 바꾸고 두 보드를 다시 올릴 것.
- **음성이 가끔 빠질 수 있다**: 음성을 10초 안에 못 받으면 점자만 출력한다. "{text}. {reason}" 은 매번 다른
  문장이라 서버 캐시가 잘 안 맞고, 서버가 음성을 만드는 데 길게는 20초 이상 걸릴 수 있다.
- **`ATMEGA_CONNECTED` 를 false 로 둔 채 두 보드를 연결하면** 음성 조각이 쉬지 않고 쏟아져 소리가 깨지고,
  버튼 결과와 상관없이 성공으로 보고된다. ESP32 부팅 로그에 지금 값이 찍힌다.
- ESP32 는 작업 하나를 ATmega 가 끝낼 때까지(최대 2분) 기다리므로, 그동안은 폴링·방아쇠를 처리하지 않는다.
- PC 에서 Mega 시리얼 모니터를 열면 Mega 가 리셋된다. 작업 도중이면 ESP32 가 2분간 기다린 뒤 실패로 처리한다.

## 자주 나는 문제

| 증상 | 해결 |
|---|---|
| `secrets.h` 관련 `#error` | 위 "처음 한 번" 2번 |
| ESP32 업로드 시 `Failed to connect` | MB 보드면 IO0 버튼 누른 채 업로드 시작, FTDI면 IO0–GND 연결 후 리셋 |
| ESP32 모니터를 열면 아무것도 안 나옴 | MB 보드의 RST 버튼 한 번 |
| `Brownout detector was triggered` | 전원 부족 — 5V 2A 이상 |
| `[폴링] 서버에 /jobs/next 가 없습니다` | 실제 서버에 폴링 API가 아직 없음 → 시험 서버 사용 |
| 업로드 포트를 못 찾음 / 다른 보드에 올라감 | 위 "COM 포트" — `upload_ports.ini` 또는 `--upload-port` |
| 소리가 안 남 (작업 중) | Mega 시리얼의 "프레임 정상/버림" 확인. 정상이 안 늘면 배선(TX↔RX 교차, GND 공통) |
| PlatformIO 가 프로젝트를 못 찾음 | VS Code 에서 `firmware1/mega` 또는 `firmware1/cam` 폴더를 열었는지 |

## 남은 일

- **솔레노이드·스텝 모터 연결**: `mega/src/main.cpp` 의 `punchBraille()` 자리에 넣는다
  (함수 원형·넘겨받는 점자 형식은 그 함수 위 주석). 지금은 칸 값만 시리얼에 찍는다.
- **단위 시험**: PlatformIO 의 시험 폴더는 `mega/test/` 다 (기본 이름이 `test`).
- **서버**: `speech_id` 와 `GET /speech/{speech_id}` 구현 (`docs/POLLING_API.md` 5·6장).
  지금은 ESP32 가 `GET /speech?text=` 로 대신 받는다.
- **폴링 API**: `/print`, `/jobs/next`, `/jobs/{id}/done` 을 `server/main.py` 에 옮기기 (지금은 시험 서버에만 있음).
