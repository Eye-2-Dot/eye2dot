/*
 * Eye 2 Dot — ESP32-CAM
 * net.h — Wi-Fi 와 서버 통신 (촬영 결과 받기, 폴링, 완료 보고, 음성 받기)
 *
 * Wi-Fi 와 서버 주소는 include/secrets.h 에 넣는다 (secrets.example.h 복사).
 * 폴링 API 와 음성 API 규격은 저장소 루트의 docs/POLLING_API.md 참고.
 */

#pragma once

#include <Arduino.h>
#include "esp_camera.h"

// ─────────────────────────────────────────────
// 설정
// ─────────────────────────────────────────────
const int         SERVER_PORT = 8000;
const bool        USE_MOCK    = true;      // 개발 중 true → 완성 후 false
const char* const DEVICE_MODE = "label";   // 펌웨어 1 = "label", 펌웨어 2 = "book"
const char* const DEVICE_ID   = "fw1";     // 폴링 때 서버에 밝히는 이름 (펌웨어 2 = "fw2")

// 음성: 이 시간 안에 다 못 받으면 음성 없이 진행. 이보다 크면 앞부분만 쓴다
//   200자 문장은 약 30~40초(240~320KB)라 여유를 두고 512KB. 버퍼는 PSRAM 에 잡는다(net.cpp)
const uint32_t SPEECH_TIMEOUT_MS = 10000;
const size_t   SPEECH_MAX_BYTES  = 512 * 1024;   // 8kHz 기준 약 64초

// Wi-Fi 에 연결한다 (연결될 때까지 기다림)
void netBegin();

String baseUrl();

// GET /health — 서버가 켜져 있는지 시리얼에 찍는다
void checkHealth();

// 사진을 multipart 로 보내고 응답 본문을 받는다. 서버가 응답했으면 true (400·500 포함)
// 연결 실패일 때만 최대 3번까지 다시 보낸다
bool postPhoto(camera_fb_t* fb, String& response);

// GET /jobs/next. 돌려주는 값 = HTTP 코드 (0 이하 = 연결 실패, errorText 에 이유)
// 200 이면 본문을 body 에 담는다
int fetchNextJob(String& body, String& errorText);

// POST /jobs/{id}/done  {"device","ok","error"}
void reportDone(const char* jobId, bool ok, const char* error);

// ─────────────────────────────────────────────
// 음성 받기
//   speechId 가 있으면 GET /speech/{speechId}, 없으면 GET /speech?text={text}
//   raw PCM (8kHz, 8비트 unsigned, 모노) 을 PSRAM 에 받는다.
//   실패하면 false (음성 없이 진행하면 된다). 다 쓴 뒤 freeSpeech() 로 돌려줄 것
// ─────────────────────────────────────────────
struct SpeechAudio {
  uint8_t* data = nullptr;
  size_t   len  = 0;
};

bool fetchSpeech(const char* speechId, const String& text, SpeechAudio& out);
void freeSpeech(SpeechAudio& audio);
