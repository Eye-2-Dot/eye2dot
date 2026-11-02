/*
 * Eye 2 Dot — ESP32-CAM 카메라·통신 펌웨어
 *
 * 담당 범위: 촬영 → 서버 전송 → 응답 수신·파싱
 *   · 음성(speak.py)과 타각은 다른 담당이다.
 *   · 결과는 아래 "연결 지점" 함수 세 개로 넘겨준다.
 *
 * 팀 규격
 *   POST /process-file (또는 /process-mock)
 *   multipart/form-data — mode: label|book, image: JPEG
 *   응답 braille: 라벨은 [13, 41], 책은 [[...], [...]] (칸 = 비트마스크 정수)
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

const uint16_t HTTP_TIMEOUT_MS = 45000;  // Gemini 응답 대기 (앱과 동일)
const int      MAX_RETRY       = 3;      // 네트워크 오류일 때만 재시도

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


// ═════════════════════════════════════════════
// 연결 지점 — 다른 담당에게 넘겨주는 곳
// ═════════════════════════════════════════════

// 1) 인식 성공 → 음성 담당(speak.py)에게
void onRecognized(const char* text, const char* reason) {
  // TODO: 음성 담당과 전달 방식 확정 후 연결
  Serial.printf("[음성으로 넘길 내용] %s / %s\n", text, reason);
}

// 2) 점자 데이터 준비 완료 → 타각 담당에게 (g_lines, g_lineLen, g_lineCount)
void onBrailleReady() {
  // TODO: 타각 담당과 연결
  for (int i = 0; i < g_lineCount; i++) {
    Serial.printf("[점자 %d줄] ", i + 1);
    for (int j = 0; j < g_lineLen[i]; j++) printCell(g_lines[i][j]);
    Serial.print("  (");
    for (int j = 0; j < g_lineLen[i]; j++) Serial.printf("%d ", g_lines[i][j]);
    Serial.println(")");
  }
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

  // braille → g_lines 로 옮기기
  JsonArray br = doc["braille"].as<JsonArray>();
  g_lineCount = 0;

  if (br.size() > 0 && br[0].is<JsonArray>()) {
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

  onBrailleReady();
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
  Serial.println("방아쇠를 누르면 촬영합니다.");
}

void loop() {
  if (digitalRead(PIN_TRIGGER) == LOW) {
    delay(30);                                  // 떨림 방지
    if (digitalRead(PIN_TRIGGER) == LOW) {
      runOnce();
      while (digitalRead(PIN_TRIGGER) == LOW) delay(10);  // 손 뗄 때까지 대기
    }
  }
  delay(10);
}
