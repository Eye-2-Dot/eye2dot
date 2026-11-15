// Eye 2 Dot - 통신 확인 프로그램 (ESP32 / ESP32-CAM)
// 확인하는 것: (1) 핫스팟이 보이는가  (2) Wi-Fi 에 붙는가  (3) 서버가 대답하는가  (4) Mega 와 선이 맞는가
// 5초마다 서버 /health 를 불러 성공·실패 횟수와 걸린 시간을 보여 줍니다. 오래 켜 두면 끊김을 찾을 수 있습니다.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"

#ifndef LINK_RX
#define LINK_RX 16
#define LINK_TX 17
#endif
#ifndef DEVICE_ID
#define DEVICE_ID "fw2"
#endif

static uint32_t okCount = 0, failCount = 0, maxMs = 0;
static unsigned long tHealth = 0;
static String rxLine;

#define SERVER_PORT 8000
static String base() { return String("http://") + SERVER_IP_VALUE + ":" + String(SERVER_PORT); }

static void help() {
  Serial.println("=== Eye2Dot 통신 확인 ===");
  Serial.println(" ?  도움말     n  주변 Wi-Fi 찾기     w  Wi-Fi 정보     h  /health 지금 한 번");
  Serial.println(" j  /jobs/next 를 불러 서버가 보내는 내용을 그대로 보기 (주의: 대기열의 작업을 하나 꺼냅니다)");
  Serial.println(" p  Mega 로 PING 보내기 (Mega 에는 mega_bench 가 올라가 있어야 함)");
}

static void scan() {
  Serial.println("주변 Wi-Fi 를 찾는 중...");
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n; i++) {
    bool me = WiFi.SSID(i) == WIFI_SSID_VALUE;
    found |= me;
    Serial.printf("  %s %-24s 신호 %d dBm  채널 %d\n", me ? "->" : "  ", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  }
  if (!found) Serial.println("  내 핫스팟이 목록에 없습니다 -> 핫스팟이 2.4GHz 인지, 이름 철자(대소문자·공백)를 확인");
}

static void info() {
  Serial.printf("Wi-Fi %s | 내 IP %s | 공유기 %s | 신호 %d dBm | 서버 %s\n",
                WiFi.status() == WL_CONNECTED ? "연결됨" : "끊김", WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(), WiFi.RSSI(), base().c_str());
  Serial.printf("/health 성공 %u  실패 %u  가장 오래 걸린 시간 %u ms\n", okCount, failCount, maxMs);
}

static int get(const String& path, String& body) {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(8000);
  if (!http.begin(base() + path)) return -1;
  int code = http.GET();
  if (code > 0) body = http.getString();
  else Serial.printf("  연결 실패: %s\n", http.errorToString(code).c_str());
  http.end();
  return code;
}

static void health() {
  String body;
  unsigned long t0 = millis();
  int code = get("/health", body);
  uint32_t ms = millis() - t0;
  if (code == 200) { okCount++; if (ms > maxMs) maxMs = ms; }
  else failCount++;
  Serial.printf("GET /health -> %d  %u ms  (성공 %u / 실패 %u)\n", code, ms, okCount, failCount);
  if (code < 0) {
    Serial.println("  서버에 닿지 못함. 순서대로 확인:");
    Serial.println("  1) 노트북과 ESP32 가 같은 핫스팟인가   2) secrets.h 의 SERVER_IP_VALUE 가 노트북의 지금 IP 인가 (ipconfig)");
    Serial.println("  3) 서버를 --host 0.0.0.0 으로 켰는가    4) Windows 방화벽에서 개인 네트워크를 허용했는가");
    Serial.println("  5) 같은 핫스팟의 휴대폰 브라우저에서 http://노트북IP:8000/health 가 열리는가");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  help();
  Serial2.begin(57600, SERIAL_8N1, LINK_RX, LINK_TX);
  Serial.printf("Mega 연결 핀: RX=GPIO%d  TX=GPIO%d  (57600)\n", LINK_RX, LINK_TX);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  scan();
  Serial.printf("연결 시도: %s\n", WIFI_SSID_VALUE);
  WiFi.begin(WIFI_SSID_VALUE, WIFI_PASSWORD_VALUE);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) info();
  else Serial.println("Wi-Fi 연결 실패 -> 비밀번호를 확인. 전원이 약해도 실패합니다(5V 에 470uF 콘덴서, 굵은 선).");
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '?') help();
    else if (c == 'n') scan();
    else if (c == 'w') info();
    else if (c == 'h') health();
    else if (c == 'p') { Serial2.print("PING\n"); Serial.println("Mega 로 PING 보냄 (Mega 화면에 표시되지는 않습니다. Mega 쪽에서 u 를 입력해 왕복을 확인)"); }
    else if (c == 'j') {
      String body;
      int code = get("/jobs/next?device=" DEVICE_ID, body);
      Serial.printf("GET /jobs/next?device=%s -> %d\n", DEVICE_ID, code);
      Serial.println(body.length() ? body : String("(본문 없음)"));
    }
  }
  while (Serial2.available()) {                 // Mega 가 PING 을 보내면 PONG 으로 대답
    char c = (char)Serial2.read();
    if (c == '\n') {
      Serial.print("Mega 에서 받음: "); Serial.println(rxLine);
      if (rxLine.indexOf("PING") >= 0) { Serial2.print("PONG\n"); Serial.println("  -> PONG 으로 대답함"); }
      rxLine = "";
    } else if (rxLine.length() < 60) rxLine += c;
  }
  if (millis() - tHealth >= 5000) {
    tHealth = millis();
    if (WiFi.status() == WL_CONNECTED) health();
    else { Serial.println("Wi-Fi 끊김 -> 다시 연결"); WiFi.disconnect(); WiFi.begin(WIFI_SSID_VALUE, WIFI_PASSWORD_VALUE); }
  }
}
