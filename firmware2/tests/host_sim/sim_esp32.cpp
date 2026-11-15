// 가상 ESP32: Wi-Fi 는 늘 연결, 서버는 정해 둔 응답을 돌려줍니다.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <unistd.h>
void setup(); void loop();
void simLinkInit(HardwareSerial& s);
double simDropRate = 0;
WiFiClass WiFi;
void simOnPrint(const char*) {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return HIGH; }

static int served = 0;
static std::string jobJson() {
  // 실제 서버(server/main.py 의 jobs_next)가 fw2 에 주는 모양 그대로. 27줄 = 2쪽.
  // text 안에 대괄호·따옴표를 넣어, 글자 속의 [[ ]] 를 점자로 착각하지 않는지도 확인
  std::string j = "{\"ok\":true,\"job\":{\"id\":\"7cabc156cc10\",\"mode\":\"book\",\"text\":\"시험 [[1,2]] \\\"책\\\"\",\"reason\":\"OCR로 텍스트를 인식했습니다.\",\"braille\":[";
  for (int i = 0; i < 27; i++) {
    const char* l = "[0]";
    if (i == 0) l = "[1,0,63]"; else if (i == 2) l = "[5]"; else if (i == 24) l = "[0,0,7]";
    else if (i == 25) l = "[9]"; else if (i == 26) l = "[63,63]";
    j += l; if (i < 26) j += ",";
  }
  return j + "],\"cell_count\":33,\"mirrored\":true}}";
}
int HTTPClient::GET() {
  if (strstr(url.c_str(), "/health")) { resp = "{\"status\":\"ok\"}"; return 200; }
  if (strstr(url.c_str(), "/jobs/next")) {
    if (served == 0) { served = 1; resp = jobJson(); return 200; }
    resp = "{\"ok\":true,\"job\":null}"; return 200;
  }
  return 404;
}
int HTTPClient::POST(const String& body) {
  // 실제 서버의 DoneReport 처럼 device(문자열)·ok(참/거짓)가 없으면 422 로 거절
  bool good = strstr(body.c_str(), "\"device\":\"fw2\"") && (strstr(body.c_str(), "\"ok\":true") || strstr(body.c_str(), "\"ok\":false"));
  fprintf(stderr, "[가짜 서버] POST %s  본문 %s  -> %d\n", url.c_str(), body.c_str(), good ? 200 : 422);
  if (!good) { resp = "{\"detail\":\"field required\"}"; return 422; }
  resp = "{\"ok\":true}"; return 200;
}
int main() {
  if (getenv("DROP")) simDropRate = atof(getenv("DROP"));
  double secs = getenv("SIM_SECS") ? atof(getenv("SIM_SECS")) : 30;
  simLinkInit(Serial2);
  setup();
  while (millis() < secs * 1000) { loop(); usleep(200); }
  return 0;
}
