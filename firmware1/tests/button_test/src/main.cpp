/*
 * Eye 2 Dot — 펌웨어 1 부품 시험: 버튼 (button_test)
 *
 *   D31 촬영 버튼 (pins.h: PIN_BTN_CAPTURE)
 *   D32 출력 버튼 (pins.h: PIN_BTN_PRINT)
 *
 * 누를 때와 뗄 때마다 한 줄씩 찍는다.
 * 디바운스(접점이 몇 ms 동안 붙었다 떨어졌다 하는 떨림 무시) 30ms — 완성 펌웨어 mega/src/button.cpp 와 같은 값.
 * 버튼 한쪽은 핀, 다른 쪽은 GND (INPUT_PULLUP 이라 누르면 LOW).
 *
 * 시리얼 명령: ? = 지금 상태와 누른 횟수
 */

#include <Arduino.h>

#include "mega_test.h"

const uint8_t DEBOUNCE_MS = 30;

struct TestButton {
  uint8_t  pin;
  bool     stable;      // 떨림이 가라앉은 뒤의 상태 (true = 눌림)
  bool     lastRaw;     // 마지막으로 읽은 그대로의 값
  uint32_t changedMs;   // lastRaw 가 마지막으로 바뀐 시각
  uint16_t presses;     // 누른 횟수
};

TestButton buttons[] = {
  { PIN_BTN_CAPTURE, false, false, 0, 0 },
  { PIN_BTN_PRINT,   false, false, 0, 0 },
};
const uint8_t BUTTON_COUNT = sizeof(buttons) / sizeof(buttons[0]);

// F() 는 함수 안에서만 쓸 수 있어 이름은 여기서 고른다
void printName(uint8_t i) {
  if (i == 0) {
    Serial.print(F("D31 촬영 버튼 (pins.h: PIN_BTN_CAPTURE)"));
  } else {
    Serial.print(F("D32 출력 버튼 (pins.h: PIN_BTN_PRINT)"));
  }
}

void printState(uint8_t i) {
  printName(i);
  Serial.print(F(": "));
  Serial.print(buttons[i].stable ? F("눌림") : F("뗌"));
  Serial.print(F("  (지금까지 "));
  Serial.print(buttons[i].presses);
  Serial.println(F("번 누름)"));
}

void printHelp() {
  Serial.println(F("버튼을 누르고 떼 보세요. 누를 때·뗄 때마다 한 줄씩 나옵니다."));
  Serial.println(F("  ? : 지금 상태와 누른 횟수"));
  Serial.println(F("----------------------------------------"));
}

void setup() {
  testSolenoidsOff();   // 가장 먼저: 솔레노이드 핀 LOW
  testBegin(F("button_test — 버튼 시험 (디바운스 30ms)"));

  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    TestButton& b = buttons[i];
    pinMode(b.pin, INPUT_PULLUP);
    b.stable    = (digitalRead(b.pin) == LOW);
    b.lastRaw   = b.stable;
    b.changedMs = millis();
  }
  printHelp();
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    printState(i);
  }
}

void loop() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    TestButton& b = buttons[i];
    bool raw = (digitalRead(b.pin) == LOW);   // INPUT_PULLUP: 누르면 LOW
    if (raw != b.lastRaw) {
      b.lastRaw   = raw;
      b.changedMs = now;
    } else if (raw != b.stable && now - b.changedMs >= DEBOUNCE_MS) {
      b.stable = raw;
      if (raw) {
        b.presses++;
      }
      Serial.print(F("["));
      Serial.print(now);
      Serial.print(F(" ms] "));
      printName(i);
      Serial.println(raw ? F(": 눌림") : F(": 뗌"));
    }
  }

  String line;
  if (testReadLine(line)) {
    if (line == "?") {
      for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        printState(i);
      }
    } else {
      printHelp();
    }
  }
}
