/*
 * Eye 2 Dot — ESP32-CAM 카메라·통신 펌웨어
 *
 * 담당 범위: 서버와의 통신 전부
 *   · 음성(speak.py)과 타각은 다른 담당이다.
 *   · 결과는 아래 "연결 지점" 함수 세 개로 넘겨준다.
 *
 * 두 가지 경로
 *   ① 방아쇠 → 촬영 → POST /process-file → 결과 바로 받기
 *   ② 폴링 — 1초마다 GET /jobs/next 로 "출력할 것 있나?" 묻기
 *      (앱에서 "출력하기"를 누른 작업이 이쪽으로 온다)
 *      작업이 오면 출력하고 POST /jobs/{id}/done 으로 완료 보고
 *   폴링 API 규격은 POLLING_API.md 참고.
 *
 * 점자 형식: 라벨은 [13, 41], 책은 [[...], [...]] (칸 = 비트마스크 정수)
 *
 * PlatformIO 프로젝트 — 보드·PSRAM·라이브러리 설정은 platformio.ini 에 있다.
 * Wi-Fi와 서버 주소는 include/secrets.h 에 넣는다 (secrets.example.h 복사).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "include/secrets.example.h 를 include/secrets.h 로 복사하고 Wi-Fi·서버 값을 채우세요."
#endif

// ─────────────────────────────────────────────
// 설정
// ─────────────────────────────────────────────
const char* WIFI_SSID     = WIFI_SSID_VALUE;      // include/secrets.h
const char* WIFI_PASSWORD = WIFI_PASSWORD_VALUE;
const char* SERVER_IP     = SERVER_IP_VALUE;
const int   SERVER_PORT   = 8000;

const bool  USE_MOCK    = true;     // 개발 중 true → 완성 후 false
const char* DEVICE_MODE = "label";  // 펌웨어 1 = "label", 펌웨어 2 = "book"
const char* DEVICE_ID   = "fw1";    // 폴링 때 서버에 밝히는 이름 (펌웨어 2 = "fw2")

const uint16_t HTTP_TIMEOUT_MS = 45000;  // Gemini 응답 대기 (앱과 동일)
const int      MAX_RETRY       = 3;      // 네트워크 오류일 때만 재시도

// 폴링
const uint32_t POLL_INTERVAL_MS         = 1000;  // 평소: 1초마다
const uint32_t POLL_INTERVAL_OFFLINE_MS = 5000;  // 서버가 안 받을 때: 5초마다
const uint16_t POLL_TIMEOUT_MS          = 2000;  // 폴링 한 번에 기다리는 최대 시간

// ─────────────────────────────────────────────
// 핀
// ─────────────────────────────────────────────
const int PIN_TRIGGER = 13;  // 방아쇠 버튼 (누르면 GND)
const int PIN_FLASH   = 4;   // 보드 내장 LED

// AI Thinker ESP32-CAM 카메라 핀 (고정값, 건드리지 말 것)
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// ─────────────────────────────────────────────
// 받은 점자 데이터 (라벨도 "한 줄짜리"로 저장해 형태를 통일)
// ─────────────────────────────────────────────
const int MAX_LINES = 25;  // A4 한 쪽
const int MAX_COLS  = 40;

uint8_t g_lines[MAX_LINES][MAX_COLS];
uint8_t g_lineLen[MAX_LINES];
int     g_lineCount = 0;

// ─────────────────────────────────────────────
// 함수 선언 — .cpp 에서는 쓰기 전에 선언이 필요하다
// ─────────────────────────────────────────────
void printCell(uint8_t m);
void copyLine(JsonArray src, int row);
bool loadBraille(JsonVariant br);

// ─────────────────────────────────────────────
// 상태
// ─────────────────────────────────────────────
volatile bool g_triggerFlag = false;   // 방아쇠가 눌렸다는 표시 (인터럽트가 세움)
uint32_t      g_lastPoll    = 0;
bool          g_serverOnline = true;   // 폴링 상태 변화만 시리얼에 찍기 위해
int           g_lastPollCode = 200;

void IRAM_ATTR onTriggerPressed() {
  g_triggerFlag = true;   // 폴링 중에 눌러도 놓치지 않도록 표시만 해 둔다
}


// ═════════════════════════════════════════════
// 연결 지점 — 다른 담당에게 넘겨주는 곳
// ═════════════════════════════════════════════

// 1) 인식 성공 → 음성 담당(speak.py)에게
void onRecognized(const char* text, const char* reason) {
  // TODO: 음성 담당과 전달 방식 확정 후 연결
  Serial.printf("[음성으로 넘길 내용] %s / %s\n", text, reason);
}

// 2) 점자 데이터 준비 완료 → 타각 담당에게 (g_lines, g_lineLen, g_lineCount)
//    출력이 끝나면 true, 실패하면 false 를 돌려준다 (폴링 작업의 완료 보고에 쓰임)
bool onBrailleReady() {
  // TODO: 타각 담당과 연결 — 타각이 끝날 때까지 기다린 뒤 결과를 돌려줄 것
  for (int i = 0; i < g_lineCount; i++) {
    Serial.printf("[점자 %d줄] ", i + 1);
    for (int j = 0; j < g_lineLen[i]; j++) printCell(g_lines[i][j]);
    Serial.print("  (");
    for (int j = 0; j < g_lineLen[i]; j++) Serial.printf("%d ", g_lines[i][j]);
    Serial.println(")");
  }
  return true;
}

// 3) 실패 → 음성 담당에게 (오류 안내)
void onFailed(const char* error, const char* reason) {
  // TODO: 음성 담당과 연결 (예: "다시 찍어 주세요")
  Serial.printf("[실패] %s / %s\n", error, reason);
}


// ─────────────────────────────────────────────
// 카메라
// ─────────────────────────────────────────────
bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk     = XCLK_GPIO_NUM;
  c.pin_pclk     = PCLK_GPIO_NUM;
  c.pin_vsync    = VSYNC_GPIO_NUM;
  c.pin_href     = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn     = PWDN_GPIO_NUM;
  c.pin_reset    = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;    // 하드웨어가 바로 JPEG를 만든다 (PNG 아님)
  c.grab_mode    = CAMERA_GRAB_LATEST; // 오래된 프레임 대신 최신 프레임

  if (psramFound()) {
    c.frame_size   = FRAMESIZE_XGA;   // 1024×768 — 서버 전처리 크기와 같아 전송량 절약
    c.jpeg_quality = 12;              // 낮을수록 고화질 (10~15 권장)
    c.fb_count     = 2;
    c.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    c.frame_size   = FRAMESIZE_VGA;   // PSRAM 없으면 해상도를 낮춘다
    c.jpeg_quality = 15;
    c.fb_count     = 1;
    c.fb_location  = CAMERA_FB_IN_DRAM;
    Serial.println("경고: PSRAM 없음. Tools → PSRAM 설정을 확인하세요.");
  }

  return esp_camera_init(&c) == ESP_OK;
}

camera_fb_t* capturePhoto() {
  digitalWrite(PIN_FLASH, HIGH);
  delay(200);                          // 노출이 LED 밝기에 맞춰질 시간

  camera_fb_t* fb = esp_camera_fb_get();  // 첫 장은 LED 켜기 전 노출일 수 있어 버림
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();

  digitalWrite(PIN_FLASH, LOW);
  return fb;
}


// ─────────────────────────────────────────────
// 서버 통신
// ─────────────────────────────────────────────
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

  int code = -1;
  for (int attempt = 1; attempt <= MAX_RETRY; attempt++) {
    HTTPClient http;
    http.begin(url);
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

    // 여기까지 오면 네트워크 오류 → 잠시 쉬고 재시도
    Serial.printf("전송 실패 (%d/%d): %s\n",
                  attempt, MAX_RETRY, http.errorToString(code).c_str());
    http.end();
    if (attempt < MAX_RETRY) delay(1000 * attempt);
  }

  free(body);
  return code > 0;
}

// 응답 JSON을 읽어 연결 지점으로 넘긴다
void handleResponse(const String& response) {
  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    onFailed("bad_response", "서버 응답을 읽지 못했습니다.");
    return;
  }

  if (!(doc["ok"] | false)) {
    onFailed(doc["error"] | "unknown", doc["reason"] | "");
    return;
  }

  onRecognized(doc["text"] | "", doc["reason"] | "");

  if (!loadBraille(doc["braille"])) {
    onFailed("bad_braille", "점자 데이터가 비어 있거나 형식이 다릅니다.");
    return;
  }
  onBrailleReady();
}

// braille(1차원 또는 2차원) → g_lines 로 옮긴다. 비어 있으면 false
bool loadBraille(JsonVariant brv) {
  g_lineCount = 0;
  if (!brv.is<JsonArray>()) return false;
  JsonArray br = brv.as<JsonArray>();
  if (br.size() == 0) return false;

  if (br[0].is<JsonArray>()) {
    // 책: [[..], [..]]
    for (JsonArray line : br) {
      if (g_lineCount >= MAX_LINES) {
        Serial.println("경고: 줄 수 초과, 나머지 생략");
        break;
      }
      copyLine(line, g_lineCount++);
    }
  } else {
    // 라벨: [..] → 한 줄짜리로
    copyLine(br, g_lineCount++);
  }
  return true;
}

void copyLine(JsonArray src, int row) {
  int n = 0;
  for (JsonVariant v : src) {
    if (n >= MAX_COLS) {
      Serial.println("경고: 한 줄 칸 수 초과, 나머지 생략");
      break;
    }
    g_lines[row][n++] = (uint8_t)((v | 0) & 0x3F);  // 6점 = 하위 6비트
  }
  g_lineLen[row] = n;
}

// 비트마스크를 점자 유니코드로 출력 (U+2800 + 값, 시리얼 확인용)
void printCell(uint8_t m) {
  char s[4] = { (char)0xE2, (char)0xA0, (char)(0x80 | (m & 0x3F)), 0 };
  Serial.print(s);
}


// ─────────────────────────────────────────────
// 폴링 — "출력할 것 있나?"
// ─────────────────────────────────────────────

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

// 받은 작업 하나를 처리한다
void handleJob(JsonObject job) {
  const char* id = job["id"] | "";
  if (id[0] == '\0') {
    Serial.println("[폴링] id 없는 작업 — 무시");
    return;
  }
  Serial.printf("\n── 출력 작업 %s ──\n", id);

  onRecognized(job["text"] | "", job["reason"] | "");

  if (!loadBraille(job["braille"])) {
    onFailed("bad_braille", "점자 데이터가 비어 있거나 형식이 다릅니다.");
    reportDone(id, false, "bad_braille");
    return;
  }

  bool printed = onBrailleReady();
  reportDone(id, printed, printed ? "" : "print_failed");
}

// 서버 상태가 바뀔 때만 시리얼에 알린다 (1초마다 같은 오류를 찍지 않도록)
void notePollStatus(int code, const String& detail) {
  bool online = code > 0;
  if (online != g_serverOnline) {
    if (online) Serial.println("[폴링] 서버 다시 연결됨");
    else        Serial.printf("[폴링] 서버 응답 없음: %s (5초마다 재시도)\n", detail.c_str());
    g_serverOnline = online;
  }
  if (online && code != g_lastPollCode) {
    if (code == 404)      Serial.println("[폴링] 서버에 /jobs/next 가 없습니다 — POLLING_API.md 참고");
    else if (code != 200) Serial.printf("[폴링] HTTP %d\n", code);
    g_lastPollCode = code;
  }
}

void pollJob() {
  String url = baseUrl();
  url += "/jobs/next?device=";
  url += DEVICE_ID;

  HTTPClient http;
  http.begin(url);
  http.setConnectTimeout(POLL_TIMEOUT_MS);
  http.setTimeout(POLL_TIMEOUT_MS);
  int code = http.GET();

  if (code <= 0) {
    notePollStatus(code, http.errorToString(code));
    http.end();
    return;
  }
  notePollStatus(code, "");
  if (code != 200) {
    http.end();
    return;
  }

  String response = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    Serial.println("[폴링] 응답을 읽지 못함");
    return;
  }
  JsonVariant job = doc["job"];
  if (job.isNull()) return;          // 할 일 없음 — 가장 흔한 경우
  handleJob(job.as<JsonObject>());
}


// ─────────────────────────────────────────────
// 메인
// ─────────────────────────────────────────────
void runOnce() {
  Serial.println("\n── 촬영 ──");
  camera_fb_t* fb = capturePhoto();
  if (!fb) {
    onFailed("camera_error", "사진을 찍지 못했습니다.");
    return;
  }
  Serial.printf("사진 %u bytes (%ux%u)\n", (unsigned)fb->len, (unsigned)fb->width, (unsigned)fb->height);

  String response;
  bool sent = postPhoto(fb, response);
  esp_camera_fb_return(fb);  // 복사가 끝났으니 바로 반납

  if (!sent) {
    onFailed("network_error", "서버에 연결하지 못했습니다.");
    return;
  }
  handleResponse(response);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Eye 2 Dot 카메라 ===");

  pinMode(PIN_TRIGGER, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TRIGGER), onTriggerPressed, FALLING);
  pinMode(PIN_FLASH, OUTPUT);
  digitalWrite(PIN_FLASH, LOW);

  if (!initCamera()) {
    Serial.println("카메라 초기화 실패. 리본 케이블과 전원을 확인하세요.");
    while (true) delay(1000);
  }
  Serial.println("카메라 준비 완료");

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
  Serial.printf("서버: %s (%s)\n", baseUrl().c_str(),
                USE_MOCK ? "목" : "실제 AI");

  checkHealth();
  Serial.printf("방아쇠를 누르면 촬영합니다. 출력 작업은 %lu ms마다 확인합니다 (기기 이름: %s).\n",
                (unsigned long)POLL_INTERVAL_MS, DEVICE_ID);
}

void loop() {
  // ① 방아쇠가 우선
  if (g_triggerFlag) {
    delay(30);                 // 떨림이 끝나기를 기다림
    runOnce();
    g_triggerFlag = false;     // 처리하는 동안 눌린 것·떨림은 버린다 (중복 촬영 방지)
    g_lastPoll = millis();
  }

  // ② 폴링 — 서버가 응답하지 않으면 간격을 늘린다
  uint32_t interval = g_serverOnline ? POLL_INTERVAL_MS : POLL_INTERVAL_OFFLINE_MS;
  if (millis() - g_lastPoll >= interval) {
    g_lastPoll = millis();
    if (WiFi.status() == WL_CONNECTED) pollJob();
  }

  delay(10);
}
