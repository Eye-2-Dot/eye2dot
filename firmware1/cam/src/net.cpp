/*
 * Eye 2 Dot — ESP32-CAM
 * net.cpp — Wi-Fi 와 서버 통신
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "net.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "include/secrets.example.h 를 include/secrets.h 로 복사하고 Wi-Fi·서버 값을 채우세요."
#endif

// ─────────────────────────────────────────────
// 설정
// ─────────────────────────────────────────────
static const char* WIFI_SSID     = WIFI_SSID_VALUE;      // include/secrets.h
static const char* WIFI_PASSWORD = WIFI_PASSWORD_VALUE;
static const char* SERVER_IP     = SERVER_IP_VALUE;

static const uint16_t HTTP_TIMEOUT_MS         = 45000;  // Gemini 응답 대기 (앱과 동일)
static const uint16_t POST_CONNECT_TIMEOUT_MS = 3000;   // 사진 전송: 서버에 연결하는 데 기다리는 최대 시간
static const int      MAX_RETRY               = 3;      // 연결 실패일 때만 재시도
static const uint16_t POLL_TIMEOUT_MS         = 2000;   // 폴링·완료 보고 한 번에 기다리는 최대 시간


void netBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);         // 절전 끄면 업로드가 빠르고 안정적
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Wi-Fi 연결 중");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nESP32 IP: %s\n", WiFi.localIP().toString().c_str());
}


String baseUrl() {
  String url = "http://";
  url += SERVER_IP;
  url += ":";
  url += SERVER_PORT;
  return url;
}


void checkHealth() {
  HTTPClient http;
  http.begin(baseUrl() + "/health");
  http.setTimeout(5000);
  int code = http.GET();
  if (code > 0) Serial.printf("서버 연결 확인: HTTP %d\n", code);
  else          Serial.printf("서버 연결 실패: %s\n", http.errorToString(code).c_str());
  http.end();
}


// 사진을 multipart로 보내고 응답 본문을 받는다. 성공하면 true.
bool postPhoto(camera_fb_t* fb, String& response) {
  const String boundary = "----Eye2DotBoundary7MA4YWxk";

  String head;
  head += "--";  head += boundary;  head += "\r\n";
  head += "Content-Disposition: form-data; name=\"mode\"\r\n\r\n";
  head += DEVICE_MODE;  head += "\r\n";
  head += "--";  head += boundary;  head += "\r\n";
  head += "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--";
  tail += boundary;
  tail += "--\r\n";

  // 본문을 한 덩어리로 만든다 (사진이 수십 KB라 PSRAM 사용)
  size_t total = head.length() + fb->len + tail.length();
  uint8_t* body = (uint8_t*)ps_malloc(total);
  if (!body) body = (uint8_t*)malloc(total);
  if (!body) {
    Serial.println("메모리 부족");
    return false;
  }
  memcpy(body, head.c_str(), head.length());
  memcpy(body + head.length(), fb->buf, fb->len);
  memcpy(body + head.length() + fb->len, tail.c_str(), tail.length());

  // mode를 쿼리에도 넣는다 — 목/실제 주소가 어느 쪽을 읽든 동작하도록
  String url = baseUrl();
  url += USE_MOCK ? "/process-mock" : "/process-file";
  url += "?mode=";
  url += DEVICE_MODE;

  // 가장 오래 걸리는 경우 (연결 실패 3초 × 2 + 쉼 3초 + 응답 기다림 45초 + 업로드) 약 59초.
  // ATmega 가 기다리는 시간(protocol.h 의 LINK_CAPTURE_TIMEOUT_MS)이 이 계산에 맞춰져 있으니,
  // 시간·횟수를 바꾸면 그 값도 함께 볼 것.
  int code = -1;
  for (int attempt = 1; attempt <= MAX_RETRY; attempt++) {
    HTTPClient http;
    http.begin(url);
    http.setConnectTimeout(POST_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary);

    unsigned long t0 = millis();
    code = http.POST(body, total);

    if (code > 0) {
      // 서버가 응답했다 (400·500 포함) → 재시도하지 않고 그대로 넘긴다
      response = http.getString();
      Serial.printf("HTTP %d, %lums\n", code, millis() - t0);
      http.end();
      break;
    }

    Serial.printf("전송 실패 (%d/%d): %s\n",
                  attempt, MAX_RETRY, http.errorToString(code).c_str());
    http.end();

    // 서버에 연결조차 못 한 경우만 다시 보낸다.
    // 응답을 기다리다 시간이 지난 경우 등은 서버가 이미 사진을 받아 처리 중일 수 있어서
    // 다시 보내지 않는다 (같은 사진으로 Gemini 를 또 부르고, 기다리는 시간만 길어짐).
    if (code != HTTPC_ERROR_CONNECTION_REFUSED) break;
    if (attempt < MAX_RETRY) delay(1000 * attempt);
  }

  free(body);
  return code > 0;
}


// ─────────────────────────────────────────────
// 폴링 — "출력할 것 있나?"
// ─────────────────────────────────────────────
int fetchNextJob(String& body, String& errorText) {
  String url = baseUrl();
  url += "/jobs/next?device=";
  url += DEVICE_ID;

  HTTPClient http;
  http.begin(url);
  http.setConnectTimeout(POLL_TIMEOUT_MS);
  http.setTimeout(POLL_TIMEOUT_MS);
  int code = http.GET();

  if (code <= 0)        errorText = http.errorToString(code);
  else if (code == 200) body = http.getString();
  http.end();
  return code;
}


// 완료 보고: POST /jobs/{id}/done  {"device","ok","error"}
void reportDone(const char* jobId, bool ok, const char* error) {
  String url = baseUrl();
  url += "/jobs/";
  url += jobId;
  url += "/done";

  JsonDocument body;
  body["device"] = DEVICE_ID;
  body["ok"]     = ok;
  body["error"]  = error;
  String json;
  serializeJson(body, json);

  for (int attempt = 1; attempt <= 2; attempt++) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(POLL_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(json);
    http.end();
    if (code > 0) {
      Serial.printf("[폴링] 완료 보고 %s → HTTP %d\n", jobId, code);
      return;
    }
    delay(500);
  }
  Serial.printf("[폴링] 완료 보고 실패 %s (서버가 나중에 다시 줄 수 있음)\n", jobId);
}


// ─────────────────────────────────────────────
// 음성 받기
// ─────────────────────────────────────────────

// UTF-8 문자열 → 퍼센트 인코딩. 영문·숫자와 - _ . ~ 는 그대로, 나머지 바이트는 %XX
static String urlEncode(const String& s) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    bool plain = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 c == '-' || c == '_' || c == '.' || c == '~';
    if (plain) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}


bool fetchSpeech(const char* speechId, const String& text, SpeechAudio& out) {
  freeSpeech(out);

  String url = baseUrl();
  if (speechId && speechId[0]) {
    url += "/speech/";
    url += speechId;
    Serial.printf("[음성] 요청: /speech/%s\n", speechId);
  } else if (text.length() > 0) {
    url += "/speech?text=";
    url += urlEncode(text);
    Serial.printf("[음성] 요청: \"%s\"\n", text.c_str());
  } else {
    Serial.println("[음성] 읽을 문장이 없음 — 음성 없이 진행");
    return false;
  }

  uint32_t t0 = millis();
  HTTPClient http;
  http.begin(url);
  http.setConnectTimeout(SPEECH_TIMEOUT_MS);
  http.setTimeout(SPEECH_TIMEOUT_MS);   // 서버가 음성을 만드는 동안 기다리는 시간 포함
  int code = http.GET();

  // 음성 API 는 성공일 때만 본문이 오디오다 (실패 본문은 JSON → 재생하면 잡음)
  if (code != 200) {
    if (code > 0) Serial.printf("[음성] HTTP %d — 음성 없이 진행\n", code);
    else          Serial.printf("[음성] 받기 실패: %s — 음성 없이 진행\n", http.errorToString(code).c_str());
    http.end();
    return false;
  }

  int total = http.getSize();   // Content-Length (모르면 -1)
  size_t want = SPEECH_MAX_BYTES;
  if (total == 0) {
    Serial.println("[음성] 빈 응답 — 음성 없이 진행");
    http.end();
    return false;
  }
  if (total > 0) {
    if ((size_t)total > SPEECH_MAX_BYTES) {
      Serial.printf("[음성] 경고: %d bytes 로 너무 김 — 앞 %u bytes 만 사용\n",
                    total, (unsigned)SPEECH_MAX_BYTES);
    } else {
      want = (size_t)total;
    }
  }

  // 음성 버퍼는 PSRAM(4MB)에 잡는다. 내부 RAM 은 300KB 남짓이라 512KB 는 들어가지 않는다.
  // PSRAM 이 없을 때만 내부 RAM 을 시도한다 (짧은 음성이면 될 수도 있음).
  uint8_t* buf = (uint8_t*)ps_malloc(want);
  bool inPsram = (buf != nullptr);
  if (!buf) buf = (uint8_t*)malloc(want);
  if (!buf) {
    Serial.println("[음성] 메모리 부족 — 음성 없이 진행");
    http.end();
    return false;
  }

  // 요청부터 SPEECH_TIMEOUT_MS 안에 다 받아야 한다
  WiFiClient* stream = http.getStreamPtr();
  size_t got = 0;
  while (got < want) {
    size_t avail = stream->available();
    if (avail > 0) {
      size_t n = want - got;
      if (avail < n) n = avail;
      got += stream->readBytes(buf + got, n);
      continue;
    }
    if (!http.connected() || millis() - t0 > SPEECH_TIMEOUT_MS) {
      break;
    }
    delay(1);
  }
  http.end();

  // Content-Length 를 알면 그만큼(또는 상한까지) 다 받아야 성공. 모르면 받은 만큼
  bool complete = (total > 0) ? (got >= want) : (got > 0);
  if (!complete) {
    Serial.printf("[음성] 받다가 끊김 (%u/%u bytes) — 음성 없이 진행\n",
                  (unsigned)got, (unsigned)want);
    free(buf);
    return false;
  }

  out.data = buf;
  out.len  = got;
  Serial.printf("[음성] %u bytes (%.1f초 분량, %s), %lums\n",
                (unsigned)got, got / 8000.0f, inPsram ? "PSRAM" : "내부 RAM",
                (unsigned long)(millis() - t0));
  return true;
}


void freeSpeech(SpeechAudio& audio) {
  if (audio.data) {
    free(audio.data);   // ps_malloc 으로 잡은 것도 free 로 돌려준다
  }
  audio.data = nullptr;
  audio.len  = 0;
}
