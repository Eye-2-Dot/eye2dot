/*
 * Eye 2 Dot — 펌웨어 1 부품 시험: Mega 연결선 (cam_link_test, ESP32-CAM 쪽)
 *
 *   Serial2 = GPIO14 RX (link.h: PIN_LINK_RX) ← 전압 분배 ← Mega D18 TX1
 *             GPIO15 TX (link.h: PIN_LINK_TX) → Mega D19 RX1
 *   핀은 완성 cam 펌웨어의 cam/include/link.h, 통신 속도는 두 칩 공용 protocol.h 의 LINK_BAUD.
 *
 * 1초마다 "hello N" 을 Mega 로 보내고, Mega 에서 받은 것은 USB 에 찍는다.
 * Wi-Fi·서버를 쓰지 않으므로 net.h·secrets.h 를 include 하지 않는다 (secrets.h 없이 빌드된다).
 * Mega 에는 link_test 를 올려 둔다.
 */

#include <Arduino.h>

#include "link.h"   // PIN_LINK_RX, PIN_LINK_TX, LINK_BAUD (함수는 쓰지 않고 값만 쓴다)

static HardwareSerial& LINK = Serial2;

const uint32_t HELLO_INTERVAL_MS = 1000;
const uint16_t LINE_GAP_MS       = 50;    // 줄바꿈 없이 이만큼 끊기면 받은 것을 한 줄로 찍는다
const size_t   RX_LINE_MAX       = 80;    // (LINE_MAX 는 ESP32 C 라이브러리의 매크로라 다른 이름을 쓴다)

uint32_t lastHelloMs = 0;
uint32_t helloCount  = 0;
String   rxLine;
uint32_t lastRxMs    = 0;

// 받은 줄을 찍는다. 안 보이는 바이트는 \xNN 으로
void flushLine() {
  if (rxLine.length() == 0) {
    return;
  }
  String shown;
  for (size_t i = 0; i < rxLine.length(); i++) {
    uint8_t c = (uint8_t)rxLine[i];
    if (c >= 0x20 && c < 0x7F) {
      shown += (char)c;
    } else {
      char hex[5];
      snprintf(hex, sizeof(hex), "\\x%02X", c);
      shown += hex;
    }
  }
  Serial.printf("[%lu ms] 받음: %s\n", (unsigned long)millis(), shown.c_str());
  rxLine = "";
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("========================================");
  Serial.println("cam_link_test — Mega 연결선 (ESP32-CAM Serial2)");
  Serial.println("========================================");

  LINK.begin(LINK_BAUD, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
  Serial.printf("Serial2 %lu bps: RX = GPIO%d (link.h: PIN_LINK_RX) ← 분압 ← Mega D18,"
                "  TX = GPIO%d (link.h: PIN_LINK_TX) → Mega D19\n",
                (unsigned long)LINK_BAUD, PIN_LINK_RX, PIN_LINK_TX);
  Serial.println("1초마다 \"hello N\" 을 보내고, 받은 것을 찍습니다.");
  Serial.println("----------------------------------------");
}

void loop() {
  uint32_t now = millis();

  // ── 1초마다 hello ──
  if (now - lastHelloMs >= HELLO_INTERVAL_MS) {
    lastHelloMs = now;
    helloCount++;
    LINK.printf("hello %lu\n", (unsigned long)helloCount);
    Serial.printf("[%lu ms] 보냄: hello %lu\n", (unsigned long)now, (unsigned long)helloCount);
  }

  // ── Mega → USB ──
  while (LINK.available() > 0) {
    char c = (char)LINK.read();
    lastRxMs = millis();
    if (c == '\n') {
      flushLine();
    } else if (c != '\r') {
      rxLine += c;
      if (rxLine.length() >= RX_LINE_MAX) {
        flushLine();
      }
    }
  }
  if (rxLine.length() > 0 && millis() - lastRxMs > LINE_GAP_MS) {
    flushLine();
  }

  delay(1);
}
