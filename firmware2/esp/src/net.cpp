#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"

static unsigned long tRetry = 0;
static uint32_t okCount = 0, failCount = 0;

static String baseUrl() {
  return String("http://") + SERVER_IP_VALUE + ":" + String(SERVER_PORT);
}

void netBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // 절전을 끄면 응답이 빨라지고 끊김이 줄어듦
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID_VALUE, WIFI_PASSWORD_VALUE);
  Serial.print("Wi-Fi 연결 중: "); Serial.println(WIFI_SSID_VALUE);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000UL) { delay(300); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) netPrintInfo();
  else Serial.println("Wi-Fi 연결 실패 - 이름/비밀번호, 2.4GHz 인지 확인. 5초마다 다시 시도합니다");
}

void netPrintInfo() {
  Serial.print("Wi-Fi ");
  Serial.print(WiFi.status() == WL_CONNECTED ? "연결됨" : "끊김");
  Serial.print(" | 내 IP "); Serial.print(WiFi.localIP().toString());
  Serial.print(" | 신호 "); Serial.print(WiFi.RSSI()); Serial.print(" dBm (-70 보다 작으면 약함)");
  Serial.print(" | 서버 "); Serial.print(baseUrl());
  Serial.print(" | 성공 "); Serial.print(okCount); Serial.print(" 실패 "); Serial.println(failCount);
}

bool netIsUp() { return WiFi.status() == WL_CONNECTED; }

bool netEnsureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (millis() - tRetry >= 5000UL) {
    tRetry = millis();
    Serial.println("Wi-Fi 끊김 -> 다시 연결 시도");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID_VALUE, WIFI_PASSWORD_VALUE);
  }
  return false;
}

// 요청 한 번. 본문은 body 에, HTTP 코드(음수면 연결 실패)를 돌려줌
static int request(const String& url, const char* postBody, String& body) {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(url)) { failCount++; return -1; }
  int code;
  if (postBody) { http.addHeader("Content-Type", "application/json"); code = http.POST(String(postBody)); }
  else code = http.GET();
  if (code > 0) { body = http.getString(); okCount++; }
  else {
    failCount++;
    Serial.print("서버 연결 실패: "); Serial.print(http.errorToString(code));
    Serial.print("  ("); Serial.print(url); Serial.println(")");
  }
  http.end();
  return code;
}

bool netHealth() {
  String body;
  unsigned long t0 = millis();
  int code = request(baseUrl() + "/health", nullptr, body);
  Serial.print("GET /health -> "); Serial.print(code);
  Serial.print("  "); Serial.print(millis() - t0); Serial.println(" ms");
  return code == 200;
}

int netFetchJob(BookJob& job) {
  String body;
  int code = request(baseUrl() + "/jobs/next?device=" DEVICE_ID, nullptr, body);
  if (code < 0) return -1;
  if (code == 204 || code == 404) return 0;                 // 작업 없음
  if (code != 200) {
    Serial.print("GET /jobs/next -> "); Serial.print(code); Serial.print(" "); Serial.println(body);
    return -1;
  }
  int n = jsonParseJob(body.c_str(), job.info, job.cell, job.len, MAX_JOB_LINES);
  if (n <= 0) return 0;                                     // {"job": null} 같은 "없음" 응답
  Serial.println("---- 새 작업 ----");
  Serial.print("작업 번호: "); Serial.println(job.info.hasId ? job.info.id : "(못 찾음)");
  Serial.print("점자 배열 이름: \""); Serial.print(job.info.linesKey);
  Serial.print("\"  줄 수: "); Serial.println(n);
  if (!job.info.hasId) {
    Serial.println("주의: 작업 번호를 못 찾아 완료 보고를 할 수 없습니다. 받은 내용:");
    Serial.println(body);
  }
  // 넘치는 부분을 조용히 잘라 찍으면 책이 중간에 끊긴 채 "완료"가 된다. 찍지 않고 실패로 보고한다.
  if (job.info.truncated) {
    Serial.print("찍을 수 없음: 한 줄 "); Serial.print(JS_MAX_CELLS); Serial.print("칸을 넘는 줄이 ");
    Serial.print(job.info.truncated); Serial.println("개 있습니다 (서버의 줄 나눔 설정 확인)");
    return 2;
  }
  if (job.info.totalLines > MAX_JOB_LINES) {
    Serial.print("찍을 수 없음: "); Serial.print(job.info.totalLines); Serial.print("줄 — 한 작업은 ");
    Serial.print(MAX_JOB_LINES); Serial.println("줄까지입니다 (책을 나눠서 출력하세요)");
    return 2;
  }
  return 1;
}

bool netReportDone(const char* id, bool ok, const char* errorCode) {
  if (!id || !id[0]) return false;
  char buf[160];
  if (ok) snprintf(buf, sizeof(buf), "%s", DONE_BODY_OK);
  else    snprintf(buf, sizeof(buf), DONE_BODY_FAIL, (errorCode && errorCode[0]) ? errorCode : DONE_ERR_PRINT_FAILED);
  String url = baseUrl() + "/jobs/" + String(id) + "/done";
  for (uint8_t i = 0; i < 3; i++) {
    String body;
    int code = request(url, buf, body);
    Serial.print("POST /jobs/"); Serial.print(id); Serial.print("/done -> "); Serial.println(code);
    if (code >= 200 && code < 300) return true;
    if (code == 422 || code == 400) {       // 본문 모양이 서버와 다름 -> net.h 의 DONE_BODY_* 를 고침
      Serial.print("서버가 본문을 거절함: "); Serial.println(body);
      return false;
    }
    delay(500);
  }
  return false;
}
