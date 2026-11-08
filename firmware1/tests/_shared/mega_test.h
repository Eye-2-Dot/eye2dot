/*
 * Eye 2 Dot — 펌웨어 1 부품 시험 공용 (mega 시험 5개가 같이 쓴다)
 * mega_test.h
 *
 *   testSolenoidsOff() : 솔레노이드 6핀(D22~D27, pins.h: PIN_SOL)을 LOW 출력으로 잡는다.
 *                        모든 mega 시험의 setup() 맨 처음에 부른다.
 *   testBegin(title)   : USB 시리얼(115200)을 열고 제목을 찍는다.
 *   testReadLine(line) : USB 에서 한 줄을 받는다 (Enter, 또는 1초 동안 입력이 없으면 끝).
 *                        받은 줄을 "> ..." 로 되돌려 찍는다 — PlatformIO 모니터는 입력한 글자를 보여 주지 않는다.
 *
 * 핀 번호는 완성 펌웨어와 같은 firmware1/mega/include/pins.h 를 쓴다
 * (각 시험의 platformio.ini 에 -I ../../mega/include).
 */

#pragma once

#include <Arduino.h>

#include "pins.h"

constexpr uint32_t TEST_BAUD         = 115200;
constexpr uint16_t TEST_LINE_IDLE_MS = 1000;   // 이만큼 입력이 없으면 Enter 없이도 한 줄로 본다
constexpr uint8_t  TEST_LINE_MAX     = 64;     // 한 줄 최대 글자 수 (넘는 글자는 버린다)

// 솔레노이드가 켜진 채로 남지 않게 가장 먼저 부른다.
// 리셋 직후 핀은 떠 있는 입력(High-Z)이라, L298N 이 HIGH 로 읽으면 솔레노이드가 켜질 수 있다.
inline void testSolenoidsOff() {
  for (uint8_t i = 0; i < 6; i++) {
    digitalWrite(PIN_SOL[i], LOW);   // 출력으로 바꾸기 전에 먼저 LOW (바꾸는 순간 튀지 않게)
    pinMode(PIN_SOL[i], OUTPUT);
  }
}

inline void testBegin(const __FlashStringHelper* title) {
  Serial.begin(TEST_BAUD);
  delay(300);   // 업로드 직후 첫 출력이 잘리지 않게 (setup 안이라 괜찮다)
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(title);
  Serial.println(F("========================================"));
}

// 한 줄이 들어왔으면 true 와 함께 line 에 담는다 (앞뒤 공백은 뗀다). 없으면 false.
// loop() 에서 매번 부른다. 기다리지 않는다.
inline bool testReadLine(String& line) {
  static String   buf;
  static uint32_t lastCharMs = 0;

  bool done = false;
  while (Serial.available() > 0 && !done) {
    char c = Serial.read();
    lastCharMs = millis();
    if (c == '\r' || c == '\n') {
      done = buf.length() > 0;   // 빈 줄(CRLF 의 두 번째 글자 등)은 무시
    } else if (buf.length() < TEST_LINE_MAX) {
      buf += c;
    }
  }
  if (!done && buf.length() > 0 && millis() - lastCharMs > TEST_LINE_IDLE_MS) {
    done = true;   // 모니터의 줄 끝 설정이 "없음"이어도 동작하게
  }
  if (!done) {
    return false;
  }

  line = buf;
  line.trim();
  buf = "";
  Serial.print(F("> "));
  Serial.println(line);
  return line.length() > 0;
}
