/*
 * Eye 2 Dot — 펌웨어 1 부품 시험: ESP32-CAM 연결선 (link_test, Mega 쪽)
 *
 *   Serial1 = D19 RX1 (pins.h: PIN_LINK_RX) ← ESP32-CAM GPIO15(TX) 직결
 *             D18 TX1 (pins.h: PIN_LINK_TX) → 1kΩ+2kΩ 분압 → ESP32-CAM GPIO14(RX)
 *   통신 속도는 두 칩 공용 protocol.h 의 LINK_BAUD.
 *
 * Serial1 로 받은 바이트 → USB 에 16진수와 글자로 찍는다 (잠깐 끊기면 한 줄로 묶음).
 * USB 에서 입력한 줄 → Serial1 로 보낸다 (끝에 줄바꿈 \n 을 붙인다).
 * ESP32-CAM 에 cam_link_test 를 올려 두면 두 방향을 모두 확인할 수 있다.
 */

#include <Arduino.h>

#include "mega_test.h"
#include "protocol.h"   // LINK_BAUD

static HardwareSerial& LINK = Serial1;

const uint16_t GROUP_GAP_MS = 20;   // 이만큼 바이트가 안 오면 받은 것을 한 줄로 찍는다
const uint8_t  GROUP_MAX    = 16;   // 한 줄 최대 바이트 수

uint8_t  group[GROUP_MAX];
uint8_t  groupLen   = 0;
uint32_t lastRxMs   = 0;
uint32_t totalRx    = 0;
uint32_t totalTx    = 0;

void printHex(uint8_t b) {
  if (b < 0x10) {
    Serial.print('0');
  }
  Serial.print(b, HEX);
}

// 받은 묶음을 "[ms] 받음 5B: 68 65 6C 6C 6F  | hello" 로 찍는다
void flushGroup() {
  if (groupLen == 0) {
    return;
  }
  Serial.print(F("["));
  Serial.print(millis());
  Serial.print(F(" ms] 받음 "));
  Serial.print(groupLen);
  Serial.print(F("B: "));
  for (uint8_t i = 0; i < groupLen; i++) {
    printHex(group[i]);
    Serial.print(' ');
  }
  Serial.print(F(" | "));
  for (uint8_t i = 0; i < groupLen; i++) {
    char c = (char)group[i];
    Serial.print((c >= 0x20 && c < 0x7F) ? c : '.');   // 안 보이는 바이트(줄바꿈 등)는 .
  }
  Serial.println();
  groupLen = 0;
}

void setup() {
  testSolenoidsOff();   // 가장 먼저: 솔레노이드 핀 LOW
  testBegin(F("link_test — ESP32-CAM 연결선 (Mega Serial1)"));

  LINK.begin(LINK_BAUD);
  Serial.print(F("Serial1 "));
  Serial.print(LINK_BAUD);
  Serial.print(F("bps: D"));
  Serial.print(PIN_LINK_RX);
  Serial.print(F(" RX1 (pins.h: PIN_LINK_RX) ← ESP32 GPIO15,  D"));
  Serial.print(PIN_LINK_TX);
  Serial.println(F(" TX1 (pins.h: PIN_LINK_TX) → 분압 → ESP32 GPIO14"));
  Serial.println(F("  받은 바이트는 16진수와 글자로 찍습니다."));
  Serial.println(F("  글자를 입력하고 Enter → 그 줄을 Serial1 로 보냅니다 (끝에 \\n)."));
  Serial.println(F("----------------------------------------"));
}

void loop() {
  // ── Serial1 → USB ──
  while (LINK.available() > 0) {
    group[groupLen++] = (uint8_t)LINK.read();
    lastRxMs = millis();
    totalRx++;
    if (groupLen >= GROUP_MAX) {
      flushGroup();
    }
  }
  if (groupLen > 0 && millis() - lastRxMs > GROUP_GAP_MS) {
    flushGroup();
  }

  // ── USB → Serial1 ──
  String line;
  if (testReadLine(line)) {
    LINK.print(line);
    LINK.print('\n');
    totalTx += line.length() + 1;
    Serial.print(F("  Serial1 로 보냄 "));
    Serial.print(line.length() + 1);
    Serial.print(F("B (지금까지 보냄 "));
    Serial.print(totalTx);
    Serial.print(F("B, 받음 "));
    Serial.print(totalRx);
    Serial.println(F("B)"));
  }
}
