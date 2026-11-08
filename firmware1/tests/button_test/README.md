# button_test — 버튼 시험

촬영 버튼(D31)과 출력 버튼(D32)이 제대로 눌리고 떼지는지, 떨림(디바운스)이 걸러지는지 본다.

## 연결

| 버튼 | Mega 핀 | 다른 쪽 |
|---|---|---|
| 촬영 버튼 (pins.h: `PIN_BTN_CAPTURE`) | D31 | GND |
| 출력 버튼 (pins.h: `PIN_BTN_PRINT`) | D32 | GND |

- 내부 풀업(`INPUT_PULLUP`)을 쓰므로 저항은 필요 없다. 누르면 LOW.
- **배터리 없이 USB 만** 꽂는다 (솔레노이드 핀은 시작하자마자 LOW 로 잡는다).

## 올리기

```bash
cd firmware1/tests/button_test
pio run -t upload
pio device monitor
```

(`pio` 가 PATH 에 없으면 `~/.platformio/penv/Scripts/platformio.exe`. 보드가 둘이면 `--upload-port COM5`, 모니터는 `-p COM5`)

## 시리얼에서

| 입력 | 동작 |
|---|---|
| (버튼을 누르고 뗀다) | 누를 때·뗄 때마다 한 줄 |
| `?` + Enter | 지금 상태와 누른 횟수 |

입력한 줄은 `> ?` 처럼 되돌려 찍힌다 (PlatformIO 모니터는 입력을 보여 주지 않는다).

## 성공이면

```
[5123 ms] D31 촬영 버튼 (pins.h: PIN_BTN_CAPTURE): 눌림
[5310 ms] D31 촬영 버튼 (pins.h: PIN_BTN_CAPTURE): 뗌
```

- 한 번 누르면 "눌림" 1줄, 떼면 "뗌" 1줄. **떨림 때문에 여러 줄이 나오면** 디바운스가 부족하거나 배선이 헐겁다.
- 아무것도 안 나오면: 버튼 다른 쪽이 GND 인지, 핀 번호(D31·D32)가 맞는지.
- 누르지 않았는데 "눌림" 이면: 그 핀이 GND 에 닿아 있다 (배선 합선).
