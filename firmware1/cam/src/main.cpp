/*
 * Eye 2 Dot — ESP32-CAM 카메라·통신 펌웨어
 *
 * 담당 범위: 촬영, 서버와의 통신 전부, ATmega 로 결과 넘기기
 *   · 음성 재생·버튼·타각은 ATmega2560(firmware1/mega)이 한다.
 *   · ESP32 는 서버의 JSON·음성을 작은 바이트 묶음(프레임)으로 바꿔 Serial2 로 넘긴다.
 *
 * 두 가지 경로
 *   ① 방아쇠 → 촬영 → POST /process-file → 결과 바로 받기
 *   ② 폴링 — 1초마다 GET /jobs/next 로 "출력할 것 있나?" 묻기
 *      (앱에서 "출력하기"를 누른 작업이 이쪽으로 온다)
 *      작업이 오면 출력하고 POST /jobs/{id}/done 으로 완료 보고
 *   폴링·음성 API 규격은 저장소 루트의 docs/POLLING_API.md 참고.
 *
 * 결과 하나를 처리하는 순서
 *   점자 읽기 → 음성 받기 ("{text}. {reason}", 실패하면 음성 없이) → ATmega 로 점자·음성 → 'D'/'E'
 *   서버가 실패로 답하면 → 고정 안내 문구 음성만 (줄 수 0 점자 프레임 + 음성)
 *   서버에 닿지 않거나 촬영 실패 → 알림음 프레임
 *
 * 점자 형식: 라벨은 [13, 41], 책은 [[...], [...]] (칸 = 비트마스크 정수)
 *
 * 파일 구성
 *   main.cpp  전체 흐름 (이 파일)      cam.cpp  카메라·LED
 *   net.cpp   Wi-Fi·서버·음성 받기     link.cpp ATmega 로 보내기
 *
 * PlatformIO 프로젝트 — 보드·PSRAM·라이브러리 설정은 platformio.ini 에 있다.
 * Wi-Fi와 서버 주소는 include/secrets.h 에 넣는다 (secrets.example.h 복사).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "cam.h"
#include "net.h"
#include "link.h"

// ─────────────────────────────────────────────
// 설정
// ─────────────────────────────────────────────

// 폴링
const uint32_t POLL_INTERVAL_MS         = 1000;  // 평소: 1초마다
const uint32_t POLL_INTERVAL_OFFLINE_MS = 5000;  // 서버가 안 받을 때: 5초마다

// 음성으로 읽을 문장
const int         SPEECH_MAX_CHARS = 60;   // "{text}. {reason}" 이 이보다 길면 뒤를 자른다 (글자 수)
const char* const FAIL_SPEECH_TEXT = "인식하지 못했어요. 다시 찍어 주세요";

// ─────────────────────────────────────────────
// 핀
// ─────────────────────────────────────────────
const int PIN_TRIGGER = 13;  // 방아쇠 버튼 (누르면 GND)

// ─────────────────────────────────────────────
// 받은 점자 데이터 (라벨도 "한 줄짜리"로 저장해 형태를 통일)
// 크기 한도는 common/protocol.h 의 BRAILLE_MAX_LINES·BRAILLE_MAX_CELLS (ATmega 와 같음)
// ─────────────────────────────────────────────
BrailleData g_braille;

// ─────────────────────────────────────────────
// 함수 선언 — .cpp 에서는 쓰기 전에 선언이 필요하다
// ─────────────────────────────────────────────
void        printCell(uint8_t m);
void        copyLine(JsonArrayConst src, int row);
bool        loadBraille(JsonVariantConst br);
const char* processResult(JsonVariantConst r);

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


// ─────────────────────────────────────────────
// 실패 기록
// ─────────────────────────────────────────────
void logFailure(const char* error, const char* reason) {
  Serial.printf("[실패] %s / %s\n", error, reason);
}


// ─────────────────────────────────────────────
// 서버 응답 읽기 — 쓰는 항목만 남기는 필터 (메모리 절약)
//   text·reason 은 화면용이지만 펌웨어 1은 음성 문장을 만드는 데 쓴다
// ─────────────────────────────────────────────
void addResultFields(JsonObject f) {
  f["ok"]         = true;
  f["error"]      = true;
  f["braille"]    = true;
  f["cell_count"] = true;
  f["speech_id"]  = true;
  f["text"]       = true;
  f["reason"]     = true;
}

// speech_id 는 문자열이든 숫자든 받아 들인다 (서버가 아직 정하지 않음)
String speechIdOf(JsonVariantConst v) {
  if (v.is<const char*>()) return String(v.as<const char*>());
  if (v.is<long>())        return String(v.as<long>());
  return String();
}


// "{text}. {reason}" 을 SPEECH_MAX_CHARS 글자 안으로 만든다.
// 글자 수는 UTF-8 기준으로 센다 (한글 한 글자 = 3바이트). 글자 중간에서 자르지 않는다.
// text 만으로 넘치면 reason 은 빠지고 text 도 잘린다.
String buildSpeechText(const char* text, const char* reason) {
  String s = text;
  s.trim();
  String r = reason;
  r.trim();
  if (r.length() > 0) {
    if (s.length() > 0) s += ". ";
    s += r;
  }

  int chars = 0;
  for (size_t i = 0; i < s.length(); i++) {
    if (((uint8_t)s[i] & 0xC0) != 0x80) {   // 글자의 첫 바이트 (이어지는 바이트는 10xxxxxx)
      if (chars == SPEECH_MAX_CHARS) {
        s = s.substring(0, i);
        s.trim();
        Serial.printf("[음성] 문장이 %d자를 넘어 뒤를 잘랐습니다\n", SPEECH_MAX_CHARS);
        break;
      }
      chars++;
    }
  }
  return s;
}


// ─────────────────────────────────────────────
// 점자
// ─────────────────────────────────────────────

// braille(1차원 또는 2차원) → g_braille 로 옮긴다. 비어 있으면 false
bool loadBraille(JsonVariantConst brv) {
  g_braille.lineCount = 0;
  if (!brv.is<JsonArrayConst>()) return false;
  JsonArrayConst br = brv.as<JsonArrayConst>();
  if (br.size() == 0) return false;

  if (br[0].is<JsonArrayConst>()) {
    // 책: [[..], [..]]
    for (JsonVariantConst line : br) {
      if (g_braille.lineCount >= BRAILLE_MAX_LINES) {
        Serial.println("경고: 줄 수 초과, 나머지 생략");
        break;
      }
      copyLine(line.as<JsonArrayConst>(), g_braille.lineCount++);
    }
  } else {
    // 라벨: [..] → 한 줄짜리로
    copyLine(br, g_braille.lineCount++);
  }
  return true;
}

void copyLine(JsonArrayConst src, int row) {
  int n = 0;
  for (JsonVariantConst v : src) {
    if (n >= BRAILLE_MAX_CELLS) {
      Serial.println("경고: 한 줄 칸 수 초과, 나머지 생략");
      break;
    }
    g_braille.cells[row][n++] = (uint8_t)((v | 0) & 0x3F);  // 6점 = 하위 6비트
  }
  g_braille.lineLen[row] = n;
}

// 서버가 알려 준 칸 수와 실제 칸 수가 다르면 경고만 남기고 계속한다
void checkCellCount(JsonVariantConst cellCount) {
  if (cellCount.isNull()) return;
  int actual = 0;
  for (int i = 0; i < g_braille.lineCount; i++) actual += g_braille.lineLen[i];
  int expected = cellCount.as<int>();
  if (expected != actual) {
    Serial.printf("경고: cell_count(%d) 와 실제 칸 수(%d)가 다릅니다 — 그대로 진행\n", expected, actual);
  }
}

// 받은 점자를 시리얼에 찍는다 (확인용)
void printBraille() {
  for (int i = 0; i < g_braille.lineCount; i++) {
    Serial.printf("[점자 %d줄] ", i + 1);
    for (int j = 0; j < g_braille.lineLen[i]; j++) printCell(g_braille.cells[i][j]);
    Serial.print("  (");
    for (int j = 0; j < g_braille.lineLen[i]; j++) Serial.printf("%d ", g_braille.cells[i][j]);
    Serial.println(")");
  }
}

// 비트마스크를 점자 유니코드로 출력 (U+2800 + 값, 시리얼 확인용)
void printCell(uint8_t m) {
  char s[4] = { (char)0xE2, (char)0xA0, (char)(0x80 | (m & 0x3F)), 0 };
  Serial.print(s);
}


// ─────────────────────────────────────────────
// 결과 처리 → ATmega
// ─────────────────────────────────────────────

// 서버가 실패로 답했을 때: 안내 음성만 보낸다 (줄 수 0 점자 + 음성).
// speech_id 가 있으면 서버가 준 음성, 없으면 고정 문구. 음성도 못 받으면 알림음
void sendFailureNotice(const String& speechId) {
  SpeechAudio audio;
  if (!fetchSpeech(speechId.c_str(), FAIL_SPEECH_TEXT, audio)) {
    linkSendAlert(ALERT_NETWORK);
    return;
  }
  BrailleData none = {};   // lineCount = 0 → "점자 없음"
  linkSendJob(none, audio.data, audio.len);
  freeSpeech(audio);
}


// 서버 결과 하나(촬영 응답 또는 폴링 작업)를 처리해 ATmega 로 보낸다.
// 돌려주는 값: 출력 성공이면 nullptr, 아니면 오류 이름 (폴링 완료 보고에 씀)
const char* processResult(JsonVariantConst r) {
  // 폴링 작업에는 ok 가 없다 → 없으면 성공으로 본다
  bool ok = r["ok"].isNull() ? true : r["ok"].as<bool>();
  String speechId = speechIdOf(r["speech_id"]);

  if (!ok) {
    logFailure(r["error"] | "unknown", r["reason"] | "");
    sendFailureNotice(speechId);
    return "recognition_failed";
  }

  if (!loadBraille(r["braille"])) {
    logFailure("bad_braille", "점자 데이터가 비어 있거나 형식이 다릅니다.");
    linkSendAlert(ALERT_GENERIC);
    return "bad_braille";
  }
  checkCellCount(r["cell_count"]);
  printBraille();

  // 음성: 실패하면 음성 없이 점자만 보낸다
  String text = buildSpeechText(r["text"] | "", r["reason"] | "");
  SpeechAudio audio;
  fetchSpeech(speechId.c_str(), text, audio);

  bool printed = linkSendJob(g_braille, audio.data, audio.len);
  freeSpeech(audio);
  return printed ? nullptr : "print_failed";
}


// 촬영 응답 JSON을 읽어 처리한다
void handleResponse(const String& response) {
  JsonDocument filter;
  addResultFields(filter.to<JsonObject>());

  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter))) {
    logFailure("bad_response", "서버 응답을 읽지 못했습니다.");
    sendFailureNotice(String());
    return;
  }
  processResult(doc.as<JsonVariantConst>());
}


// ─────────────────────────────────────────────
// 폴링 — "출력할 것 있나?"
// ─────────────────────────────────────────────

// 받은 작업 하나를 처리한다
void handleJob(JsonObjectConst job) {
  const char* id = job["id"] | "";
  if (id[0] == '\0') {
    Serial.println("[폴링] id 없는 작업 — 무시");
    return;
  }
  Serial.printf("\n── 출력 작업 %s ──\n", id);

  const char* error = processResult(job);
  reportDone(id, error == nullptr, error ? error : "");
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
    if (code == 404)      Serial.println("[폴링] 서버에 /jobs/next 가 없습니다 — docs/POLLING_API.md 참고");
    else if (code != 200) Serial.printf("[폴링] HTTP %d\n", code);
    g_lastPollCode = code;
  }
}

void pollJob() {
  String body, errorText;
  int code = fetchNextJob(body, errorText);
  notePollStatus(code, errorText);
  if (code != 200) return;

  JsonDocument filter;
  filter["ok"] = true;
  JsonObject jobFilter = filter["job"].to<JsonObject>();
  addResultFields(jobFilter);
  jobFilter["id"] = true;   // 완료 보고(POST /jobs/{id}/done)에 필요

  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    Serial.println("[폴링] 응답을 읽지 못함");
    return;
  }
  JsonVariantConst job = doc["job"];
  if (job.isNull()) return;          // 할 일 없음 — 가장 흔한 경우
  handleJob(job.as<JsonObjectConst>());
}


// ─────────────────────────────────────────────
// 메인
// ─────────────────────────────────────────────
void runOnce() {
  Serial.println("\n── 촬영 ──");
  camera_fb_t* fb = camCapture();
  if (!fb) {
    logFailure("camera_error", "사진을 찍지 못했습니다.");
    linkSendAlert(ALERT_CAMERA);
    return;
  }
  Serial.printf("사진 %u bytes (%ux%u)\n", (unsigned)fb->len, (unsigned)fb->width, (unsigned)fb->height);

  String response;
  bool sent = postPhoto(fb, response);
  camRelease(fb);  // 복사가 끝났으니 바로 반납

  if (!sent) {
    logFailure("network_error", "서버에 연결하지 못했습니다.");
    linkSendAlert(ALERT_NETWORK);
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

  linkBegin();   // 카메라가 안 켜져도 ATmega 에 알림음을 보낼 수 있도록 먼저

  if (!camInit()) {
    Serial.println("카메라 초기화 실패. 리본 케이블과 전원을 확인하세요.");
    linkSendAlert(ALERT_CAMERA);
    while (true) delay(1000);
  }
  Serial.println("카메라 준비 완료");

  netBegin();
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
