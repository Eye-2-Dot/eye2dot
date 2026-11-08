# sensor_test — 원점 스위치·카드 감지 센서 시험

원점 스위치와 카드 감지 센서가 바뀔 때 Mega 가 제대로 읽는지, 어느 쪽이 HIGH/LOW 인지 본다.

## 연결

| 부품 | Mega 핀 | 다른 쪽 |
|---|---|---|
| 원점 스위치 (pins.h: `PIN_HEAD_LIMIT`) | D34 | GND (스위치 한쪽은 핀, 다른 쪽은 GND) |
| 카드 감지 센서 (pins.h: `PIN_LABEL_SENSOR`) | D35 | 센서 모듈의 출력(OUT). 모듈 VCC 5V·GND 는 Mega 에 |

- 입력 모드는 `src/main.cpp` 맨 위 상수다. 둘 다 기본 `INPUT_PULLUP`(칩 안의 저항으로 평소 HIGH).
  센서 모듈이 HIGH/LOW 를 직접 내보내면(푸시풀) `SENSOR_INPUT_MODE` 를 `INPUT` 으로 바꿔도 된다.
- **배터리 없이 USB 만** 꽂는다.

## 올리기

```bash
cd firmware1/tests/sensor_test
pio run -t upload
pio device monitor
```

## 시리얼에서

| 입력 | 동작 |
|---|---|
| (스위치를 누르고 뗀다, 카드를 대고 뗀다) | 바뀔 때마다 한 줄 |
| `?` 또는 `p` + Enter | 지금 상태와 바뀐 횟수 |

## 성공이면

```
[8120 ms] D34 원점 스위치 (pins.h: PIN_HEAD_LIMIT): LOW (풀업: GND 에 닿음)
[8402 ms] D34 원점 스위치 (pins.h: PIN_HEAD_LIMIT): HIGH (풀업: 떨어져 있음)
[9930 ms] D35 카드 감지 센서 (pins.h: PIN_LABEL_SENSOR): LOW (풀업: GND 에 닿음)
```

- 스위치는 누를 때 LOW, 뗄 때 HIGH 가 정상이다 (스위치 → GND 배선일 때).
- 카드 감지 센서는 **카드가 있을 때 HIGH 인지 LOW 인지 여기서 확인해 적어 둔다.** 완성 펌웨어를 만들 때 그 값을 쓴다.
- 손대지 않았는데 계속 바뀌면: 배선이 헐겁거나, 센서 모듈의 감도 가변저항을 조정한다.
- 아무것도 안 바뀌면: 핀 번호(D34·D35), 센서 모듈 전원(VCC·GND)을 확인한다.
