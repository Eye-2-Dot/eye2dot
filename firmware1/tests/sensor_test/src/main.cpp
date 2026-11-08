/*
 * Eye 2 Dot — 펌웨어 1 부품 시험: 센서 (sensor_test)
 *
 *   D34 원점 스위치    (pins.h: PIN_HEAD_LIMIT)
 *   D35 카드 감지 센서 (pins.h: PIN_LABEL_SENSOR)
 *
 * 두 핀의 상태가 바뀔 때마다 한 줄씩 찍는다 (5ms 동안 같은 값이어야 바뀐 것으로 본다 — 떨림 무시).
 * 센서마다 "눌림·감지"가 HIGH 인지 LOW 인지 다를 수 있어서, 실제 HIGH/LOW 를 그대로 보여 준다.
 *
 * 시리얼 명령: ? 또는 p = 지금 상태
 */

#include <Arduino.h>

#include "mega_test.h"

// ─────────────────────────────────────────────
// 입력 모드 — 부품에 맞게 고칠 것
//   INPUT_PULLUP : 칩 안의 저항으로 평소 HIGH. 스위치 한쪽을 핀, 다른 쪽을 GND 에 달면 누를 때 LOW
//   INPUT        : 센서 모듈이 HIGH/LOW 를 직접 내보낼 때 (푸시풀 출력)
// ─────────────────────────────────────────────
const uint8_t LIMIT_INPUT_MODE  = INPUT_PULLUP;   // 원점 스위치 (스위치 → GND)
const uint8_t SENSOR_INPUT_MODE = INPUT_PULLUP;   // 카드 감지 센서 (모듈 출력이 푸시풀이면 INPUT 으로 바꿔도 됨)

const uint8_t DEBOUNCE_MS = 5;

struct TestInput {
  uint8_t  pin;
  uint8_t  mode;
  bool     stable;      // 떨림이 가라앉은 뒤의 값 (true = HIGH)
  bool     lastRaw;
  uint32_t changedMs;
  uint16_t changes;     // 바뀐 횟수
};

TestInput inputs[] = {
  { PIN_HEAD_LIMIT,   LIMIT_INPUT_MODE,  false, false, 0, 0 },
  { PIN_LABEL_SENSOR, SENSOR_INPUT_MODE, false, false, 0, 0 },
};
const uint8_t INPUT_COUNT = sizeof(inputs) / sizeof(inputs[0]);

void printName(uint8_t i) {
  if (i == 0) {
    Serial.print(F("D34 원점 스위치 (pins.h: PIN_HEAD_LIMIT)"));
  } else {
    Serial.print(F("D35 카드 감지 센서 (pins.h: PIN_LABEL_SENSOR)"));
  }
}

void printValue(uint8_t i) {
  const TestInput& in = inputs[i];
  Serial.print(in.stable ? F("HIGH") : F("LOW"));
  // INPUT_PULLUP 이면 LOW = GND 에 닿음 (스위치를 눌렀거나 센서가 LOW 를 냄)
  if (in.mode == INPUT_PULLUP) {
    Serial.print(in.stable ? F(" (풀업: 떨어져 있음)") : F(" (풀업: GND 에 닿음)"));
  }
}

void printState(uint8_t i) {
  printName(i);
  Serial.print(F(": "));
  printValue(i);
  Serial.print(F("  (바뀐 횟수 "));
  Serial.print(inputs[i].changes);
  Serial.println(F(")"));
}

void printHelp() {
  Serial.println(F("스위치를 누르거나 카드를 대고 떼 보세요. 바뀔 때마다 한 줄씩 나옵니다."));
  Serial.println(F("  ? 또는 p : 지금 상태"));
  Serial.println(F("----------------------------------------"));
}

void setup() {
  testSolenoidsOff();   // 가장 먼저: 솔레노이드 핀 LOW
  testBegin(F("sensor_test — 원점 스위치(D34) · 카드 감지 센서(D35)"));

  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    TestInput& in = inputs[i];
    pinMode(in.pin, in.mode);
    in.stable    = (digitalRead(in.pin) == HIGH);
    in.lastRaw   = in.stable;
    in.changedMs = millis();
  }
  printHelp();
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    printState(i);
  }
}

void loop() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    TestInput& in = inputs[i];
    bool raw = (digitalRead(in.pin) == HIGH);
    if (raw != in.lastRaw) {
      in.lastRaw   = raw;
      in.changedMs = now;
    } else if (raw != in.stable && now - in.changedMs >= DEBOUNCE_MS) {
      in.stable = raw;
      in.changes++;
      Serial.print(F("["));
      Serial.print(now);
      Serial.print(F(" ms] "));
      printName(i);
      Serial.print(F(": "));
      printValue(i);
      Serial.println();
    }
  }

  String line;
  if (testReadLine(line)) {
    if (line == "?" || line == "p") {
      for (uint8_t i = 0; i < INPUT_COUNT; i++) {
        printState(i);
      }
    } else {
      printHelp();
    }
  }
}
