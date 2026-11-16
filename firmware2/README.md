# 펌웨어 2 — 책 점역기

펌웨어 1(라벨 출력기)과 같은 구조·같은 이름으로 만든다 (11/9 회의).
A4 종이에 책 점자를 찍는다. 서버에서 거울 점자를 받아 25줄(한 장)씩 찍고, 장이 바뀌면 "출력 계속" 버튼을 기다린다.

```
firmware2/
├── common/
│   └── protocol.h    ESP32 · Mega 가 같이 쓰는 약속 (프레임 모양·종류·오류 코드, 한 장 25줄 × 30칸)
├── esp/      ESP32 쪽 코드 (PlatformIO: VS Code 에서 이 esp 폴더를 연다)
│   ├── include/   net.h(서버), link.h(Mega 통신), json_scan.h(서버 답 읽기), secrets.example.h
│   └── src/       main.cpp(흐름), net.cpp, link.cpp
├── mega/     Arduino Mega 쪽 코드 (PlatformIO: VS Code 에서 이 mega 폴더를 연다)
│   ├── include/   .h  = 함수 "선언" (이름·쓰는 법만)
│   │   ├── pins.h        핀 번호
│   │   ├── config.h      기구 치수·모터 조정값 (조립 후 실측해서 고치는 숫자)
│   │   ├── solenoid.h    솔레노이드 시간 값·함수 선언
│   │   └── stepper.h, homing.h, punch.h, button.h, link.h, job.h, console.h
│   └── src/       .cpp = 함수 "정의"(실제 내용)와 호출
│       ├── main.cpp      setup()/loop(). 모듈을 차례로 돌리기만 한다
│       ├── solenoid.cpp  6점 동시 · 두 번 타각 · 과열 방지
│       ├── stepper.cpp   모터 2축 (X = 헤드 가로, Y = 종이판 세로)
│       ├── homing.cpp    원점 잡기 (스위치 2개)
│       ├── punch.cpp     한 장을 찍는 순서
│       ├── job.cpp       지금 상태 (대기·받는 중·버튼 대기·타각 중 …)
│       ├── link.cpp      ESP32 와 프레임 주고받기
│       ├── button.cpp    "출력 계속" 버튼
│       └── console.cpp   USB 시험 명령
├── tests/    부품 하나씩 시험하는 PlatformIO 프로젝트 (펌웨어 1 tests 와 같은 방식)
│   ├── _shared/mega_test.h   시험 공용 (솔레노이드 끄기, 한 줄 입력 받기)
│   ├── solenoid_test/        솔레노이드 6개 시험 (VS Code 에서 이 폴더를 연다)
│   ├── mega_bench/           부품을 하나 붙일 때마다 확인 (펌웨어 1 회로는 `-e fw1`, 펌웨어 2 회로는 `-e fw2`)
│   ├── esp32_net_check/      Wi-Fi·서버·Mega 연결 확인
│   └── host_sim/             보드 없이 PC 에서 두 칩의 코드를 돌려 보는 모의 시험
└── tools/
    └── solenoid_mask_test/  Arduino IDE 로 바로 올리는 솔레노이드 시험 스케치
```

- 스텝 모터 모듈은 펌웨어 1 과 같은 이름인 `stepper.h` / `stepper.cpp` 다 (처음 계획의 `step.h` 대신).
- 솔레노이드 배선·핀은 펌웨어 1 과 같다: D22~D27 → L298N #1 IN1~4, #2 IN1~2, 솔레노이드 다른 쪽은 GND.
- 칸 값은 서버가 거울로 바꿔 준 6비트 값을 **뒤집지 않고 그대로** 쓴다 (bit0 = 위에서 볼 때 왼쪽 위 = 1번).
- 업로드할 때는 배터리(전원)를 빼고 USB 만. 올린 뒤 전원을 연결한다.
  D22~D27 에 각각 10kΩ 풀다운(→ GND)을 달면 리셋·업로드 중에 솔레노이드가 켜지는 것을 막을 수 있다 (펌웨어 1 README 와 같음).
- **PC 에서 Mega 의 시리얼 모니터를 열면 Mega 가 다시 시작된다.** 타각 중에 열면 그 작업은 실패로 끝난다
  (ESP32 가 약 4초 안에 알아채고 서버에 실패로 보고한다).

## 찍는 방향 (스텝 모터 코드를 짤 때 꼭 지킬 것 — 펌웨어 1 punch.h 와 같은 약속)

- 솔레노이드는 종이를 **위에서 아래로** 찍는다. 점은 아랫면으로 튀어나오므로 사람은 종이를 **좌우로 뒤집어** 읽는다.
- 칸 안의 좌우(점 1↔4, 2↔5, 3↔6)는 **서버가 이미 바꿔서** 보낸다. 펌웨어는 칸 값을 뒤집지 않는다.
- 대신 **칸 순서**는 펌웨어(스텝 모터)가 맞춘다: 한 줄의 첫 칸을 위에서 볼 때 **오른쪽 끝**에 찍고, 칸마다 **왼쪽으로** 간다.
  줄은 위에서 아래로. → 뒤집어 읽으면 첫 칸이 왼쪽 끝에 온다.
  (X 원점 스위치가 오른쪽 끝에 있고, `punch.cpp` 가 첫 칸을 원점 쪽에 찍는다)
- 빈칸(값 0)은 솔레노이드가 쉬지만 **헤드는 한 칸 이동**해야 한다 (`punch.cpp` 는 빈칸을 건너뛰어 다음 점이 있는 칸의 자리로 바로 간다).

## 전원 주의

- 전원은 18650 × 3 (11.1V) 이다 (펌웨어 1 과 같음). L298N 을 거치면 5V 솔레노이드에 펌웨어 1 과 같은 약 9V(11.1V − L298N 약 2V)가 걸린다.
  `SOL_PULSE_MS` 는 30ms 에서 올리지 말고, 코일이 뜨거워지는지 손으로 확인하면서 시험한다.
- 스텝 모터는 **17HS3430 (NEMA17, 12V, 정격 0.4A)** + A4988·DRV8825 계열 드라이버다.
  드라이버의 전류 제한을 **0.4A** 에 맞춘다 — A4988 은 Vref 약 0.32V, DRV8825 는 약 0.2V (보드의 감지 저항이 0.1Ω 일 때).
  더 높게 두면 모터가 뜨거워진다. 드라이버 MS1·MS2·MS3 은 모두 5V (1/16 마이크로스텝).

## 핀 (Mega) — `mega/include/pins.h`

| 핀 | 연결 |
| --- | --- |
| D18 (TX1) | 1kΩ/2kΩ 분압 → ESP32 GPIO16 |
| D19 (RX1) | ESP32 GPIO17 (직결) |
| D22~D27 | L298N #1 IN1~IN4, L298N #2 IN1~IN2 → 솔레노이드 1~6 |
| D32 | "출력 계속" 버튼 → GND |
| D34 / D35 | X 원점 스위치 / Y 원점 스위치 → GND |
| D36·D37·D38 | X 모터 드라이버 STEP·DIR·EN |
| D40·D41·D42 | Y 모터 드라이버 STEP·DIR·EN |
| D9 | (선택) 능동 부저 |

11/3 초안의 스텝 모터 핀(D2·D3·D5·D6·D8)과 원점 스위치 핀(D9·D10)에서 바뀌었다. 펌웨어 1 과 같은 자리를 쓴다.

## 올리기

```
cd firmware2/mega  &&  pio run -t upload  &&  pio device monitor -b 115200
cd firmware2/esp   &&  cp include/secrets.example.h include/secrets.h   # 채운 뒤
pio run -t upload  &&  pio device monitor -b 115200
```

## 조립 뒤 맞출 값 — `mega/include/config.h`

전원을 켜도 모터는 바로 움직이지 않는다 (`AUTO_HOME_ON_BOOT false`). 찍기 직전에 원점을 잡는다.
**모터가 움직이는 중에 시리얼로 아무 글자나 보내거나 버튼을 누르면 그 자리에 멈춘다** — 반대로 가면 바로 멈출 것.

0. Mega 시리얼에서 `l` — 원점 스위치 2개와 버튼을 손으로 눌러 가며 "눌림"으로 바뀌는지 먼저 본다.
1. `X_DIR_INVERT`, `Y_DIR_INVERT` — `h`(원점 잡기)를 시켰을 때 스위치 반대쪽으로 가면 `true`.
2. `X_STEPS_PER_MM`, `Y_STEPS_PER_MM` — `e`(모터 켜 두기) → `h` → `x 100` 을 시키고 자로 잰 거리가 100mm 가 아니면 `40 × 100 ÷ 잰 값`.
3. `X_FIRST_CELL_MM`, `Y_FIRST_LINE_MM` — `t`(시험 쪽)를 찍어 보고 종이 여백에 맞게.
4. 점이 약하면 `solenoid.h` 의 `SOL_STRIKES`(누르는 횟수)를 본다. `SOL_PULSE_MS` 는 50ms 를 넘길 수 없다.

숫자를 잘못 고쳐 마지막 칸·줄이 갈 수 있는 거리를 넘으면 컴파일이 멈춘다 (`config.h` 끝의 `static_assert`).
한 장의 줄 수(25)와 한 줄의 칸 수(30)는 `common/protocol.h` 한 곳에만 있다 — 고치면 두 보드를 모두 다시 올린다.

## 시험 명령 (시리얼 모니터 115200)

Mega: `?` 도움말, `l` 스위치 값, `h` 원점, `e`/`d` 모터 켜 두기/끄기, `x 30` / `y 50` 이동(원점을 잡은 뒤에만),
`s 63` 한 칸 찍기, `t` 시험 쪽, `c` 취소, `p` 상태.
모터가 움직이는 중에 보낸 글자는 명령이 아니라 **멈춤**이 된다 (상태는 5초마다 저절로 찍힌다).
ESP32: `w` Wi-Fi 정보, `h` 서버 확인, `s` Mega 상태, `t` 서버 없이 시험 쪽 보내기, `x` 취소.

솔레노이드만 따로 볼 때는 `tests/solenoid_test` 를 올린다.

## 동작

1. 전원을 켜면 Mega 는 대기한다 (모터는 꺼져 있다).
2. ESP32 는 Mega 가 대기 상태일 때만 1초마다 `GET /jobs/next?device=fw2` 를 부른다.
3. 작업을 받으면 25줄씩 `P`(쪽 시작) → `L`(줄) × n → `G`(시작) 순서로 보낸다. 프레임마다 확인 응답을 받고, 없으면 다시 보낸다.
4. 첫 장은 바로, 둘째 장부터는 버튼을 짧게 눌렀다 떼면 원점을 잡고 찍는다. 버튼 대기 중에 길게(1.5초) 누르면 작업 취소.
5. **움직이는 중(원점 잡기·타각·복귀)에 버튼을 누르면 그 자리에 멈추고 작업을 취소한다.** 원점으로 돌아가지 않고 모터를 끈다.
6. 한 장이 끝나면 원점으로 돌아가 `D`를 보낸다. 마지막 장이 끝나면 ESP32 가 `POST /jobs/{id}/done` 을 보낸다.
7. 한 작업은 400줄(16장)까지다. 넘거나 한 줄이 30칸을 넘으면 잘라 찍지 않고 `bad_braille` 로 실패 보고한다.

## 서버와의 약속

| 항목 | 서버 | 이 코드 |
| --- | --- | --- |
| 작업 받기 | `GET /jobs/next?device=fw2` → `{"ok":true,"job":{"id","mode":"book","braille":[[…],…],"cell_count","mirrored":true}}`, 없으면 `"job":null` | `id` 와 2차원 `braille` 을 읽음 |
| 완료 보고 | `POST /jobs/{id}/done` 본문 `{"device","ok","error"}` (`server/main.py` 의 `DoneReport`) | `{"device":"fw2","ok":true,"error":""}`. 실패는 `print_failed` / `cancelled` / `bad_braille` |
| 한 줄 칸 수 | `server/braille.py` 의 `MAX_CELLS_PER_LINE = 30` | `common/protocol.h` 의 `LINE_MAX_CELLS 30` |
| Wi-Fi 설정 | `secrets.h` 의 `WIFI_SSID_VALUE`, `WIFI_PASSWORD_VALUE`, `SERVER_IP_VALUE` | 같은 이름 (`firmware1/cam` 의 것과 같은 내용) |

펌웨어 1 과 다른 점 (일부러 둔 것)

- 두 칩 사이 통신 속도는 57600 이다 (펌웨어 1 은 115200). 16MHz Mega 에서 오차가 더 작은 값이다.
- Mega → ESP32 방향도 프레임이고, 프레임마다 확인 응답(ACK)이 있다.
- 한 쪽(25줄)을 먼저 다 보낸 뒤 찍는다. 찍는 도중에 Wi-Fi 가 끊겨도 그 쪽은 끝까지 찍힌다.
- 음성 안내는 없다 (서버도 `fw2` 에는 `speech_id` 를 주지 않는다).

## 서버 없이 / 앱 없이 시험

```
# 서버만 있을 때: 책 작업 하나 넣기 (원래 모양 점자. 서버가 거울로 바꿔서 기기에 줌)
curl -X POST http://127.0.0.1:8000/print -H "Content-Type: application/json" \
  -d "{\"mode\":\"book\",\"text\":\"시험\",\"reason\":\"\",\"braille\":[[1,63],[63,63,63]]}"
```

서버가 없으면 ESP32 시리얼에서 `t` (시험 쪽을 Mega 로 보냄), Mega 만 있으면 Mega 시리얼에서 `t`.

보드가 없으면 `tests/host_sim` (PC 의 g++ 로 두 칩의 코드를 그대로 컴파일해 가상 UART 로 잇는다).

실제 기구에서는 아직 돌려 보지 않았다 (모터·스위치·통신 부분은 PC 모의 시험만 통과).
