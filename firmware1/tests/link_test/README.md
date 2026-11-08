# link_test — ESP32-CAM 연결선 시험 (Mega 쪽)

Mega 와 ESP32-CAM 사이 선 3가닥(TX→RX, RX←TX, GND)과 전압 분배가 맞는지 확인한다.
ESP32-CAM 에는 [`../cam_link_test`](../cam_link_test) 를 올려서 같이 쓴다.

## 연결

| ESP32-CAM | | Arduino Mega | 비고 |
|---|---|---|---|
| GPIO15 (TX) | → | D19 RX1 (pins.h: `PIN_LINK_RX`) | 직결 |
| GPIO14 (RX) | ← 전압 분배 ← | D18 TX1 (pins.h: `PIN_LINK_TX`) | **1kΩ + 2kΩ 분압 필수 (5V 직결 금지)** |
| GND | — | GND | 반드시 공통 |

통신 속도는 두 칩 공용 `common/protocol.h` 의 `LINK_BAUD` (115200).

## 올리기

```bash
cd firmware1/tests/link_test
pio run -t upload
pio device monitor
```

두 보드를 같이 꽂았으면 포트를 지정한다: `pio run -t upload --upload-port COM5`, `pio device monitor -p COM5`
(번호는 `pio device list`). ESP32 모니터는 다른 터미널에서 따로 연다.

## 시리얼에서

| 입력 | 동작 |
|---|---|
| (아무것도 안 해도) | Serial1 로 받은 바이트가 16진수와 글자로 찍힌다 |
| 글자 + Enter | 그 줄을 Serial1 로 보낸다 (끝에 `\n`) |

## 성공이면

**ESP32 → Mega** (ESP32 가 1초마다 보내는 `hello N`):

```
[12034 ms] 받음 8B: 68 65 6C 6C 6F 20 31 0A  | hello 1.
```

**Mega → ESP32**: 여기서 `abc` + Enter → ESP32 모니터에 `받음: abc` 가 나온다.

| 증상 | 볼 곳 |
|---|---|
| 아무것도 안 받음 | ESP32 GPIO15 → Mega D19 선, GND 공통, ESP32 에 cam_link_test 가 올라갔는지 |
| 글자가 깨짐 (이상한 16진수) | 속도가 다름 — 두 쪽 모두 `LINK_BAUD`. 선이 길면 짧게 |
| ESP32 가 Mega 글자를 못 받음 | Mega D18 → 분압 → GPIO14 선과 저항 값 (1kΩ 위, 2kΩ 아래) |
