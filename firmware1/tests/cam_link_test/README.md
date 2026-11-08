# cam_link_test — Mega 연결선 시험 (ESP32-CAM 쪽)

ESP32-CAM 에서 Mega 로 1초마다 `hello N` 을 보내고, Mega 에서 온 글자를 USB 에 찍는다.
Mega 에는 [`../link_test`](../link_test) 를 올려서 같이 쓴다.
Wi-Fi·서버를 쓰지 않으므로 **`secrets.h` 없이 빌드된다.**

## 연결

| ESP32-CAM | | Arduino Mega | 비고 |
|---|---|---|---|
| GPIO15 TX (link.h: `PIN_LINK_TX`) | → | D19 RX1 | 직결 |
| GPIO14 RX (link.h: `PIN_LINK_RX`) | ← 전압 분배 ← | D18 TX1 | **1kΩ + 2kΩ 분압 필수 (5V 직결 금지)** |
| GND | — | GND | 반드시 공통 |

- 핀은 완성 cam 펌웨어의 `cam/include/link.h`, 통신 속도는 `common/protocol.h` 의 `LINK_BAUD` 를 그대로 쓴다.
- GPIO14·15 는 microSD 슬롯과 같은 핀이다 → **SD 카드를 꽂지 말 것.**

## 올리기

```bash
cd firmware1/tests/cam_link_test
pio run -t upload
pio device monitor
```

- 업로드가 안 되면(`Failed to connect`): MB 보드면 IO0 버튼을 누른 채 업로드 시작.
- 모니터에 아무것도 안 나오면 MB 보드의 RST 버튼 한 번.
- 두 보드를 같이 꽂았으면 `--upload-port COM6`, `-p COM6` 로 포트를 지정한다.

## 시리얼에서

입력할 것은 없다. 켜면 바로 1초마다 보낸다.

## 성공이면

```
[3001 ms] 보냄: hello 3
[3520 ms] 받음: abc          ← Mega link_test 에서 abc + Enter
```

- **ESP32 → Mega**: Mega link_test 모니터에 `받음 8B: 68 65 6C 6C 6F 20 31 0A  | hello 1.` 이 1초마다 나온다.
- **Mega → ESP32**: Mega 모니터에서 입력한 줄이 여기에 `받음: …` 으로 나온다.
- `받음:` 이 안 나오면 Mega D18 → 분압 → GPIO14 선을, 글자가 `\xNN` 으로 깨지면 속도·GND·선 길이를 본다.
