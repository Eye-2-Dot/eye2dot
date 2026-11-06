/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * job.cpp — 작업 진행
 *
 *   점자 프레임 도착 → startJob()
 *   (음성이 오면 link 모듈이 받아서 speech 모듈이 재생)
 *   updateJob() 이 버튼·재생 끝·시간 초과를 보고 'D' 또는 'E' 로 마무리
 */

#include <Arduino.h>

#include "button.h"
#include "job.h"
#include "link.h"
#include "pins.h"
#include "punch.h"
#include "speech.h"

// ─────────────────────────────────────────────
// 작업 상태
// ─────────────────────────────────────────────
enum JobState : uint8_t {
  JOB_IDLE        = 0,   // 작업 없음
  JOB_WAIT_BUTTON = 1,   // 점자 있음 — 음성을 들려주고 확인·취소 버튼을 기다림
  JOB_AUDIO_ONLY  = 2,   // 점자 없음 — 안내 음성만 재생하고 끝나면 자동 'D'
};

static JobState jobState   = JOB_IDLE;
static uint32_t jobStartMs = 0;

// 점자 없는 작업에서 음성 시작이 이 시간 안에 안 오면 그냥 끝냅니다
static const uint16_t AUDIO_ONLY_WAIT_MS = 3000;

// 함수 선언
static void startJob(uint32_t now);
static void updateJob(uint32_t now);
static void stopJobAudio();
static void finishJob(bool done, const __FlashStringHelper* why);
static void updateLed(uint32_t now);


void jobInit() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
}


void jobUpdate(uint32_t now) {
  // ── ESP32 알림음 프레임 → 소리 신호 ──
  uint8_t alert = linkTakeAlert();
  if (alert != 0) {
    speechAlertPlay(alert);   // 소리 신호가 시험음·음성보다 우선
    Serial.print(F("[알림음] 종류 "));
    Serial.println(alert);
  }

  // ── 새 작업, 진행 중인 작업 ──
  if (linkTakeNewJob()) {
    startJob(now);
  }
  updateJob(now);

  updateLed(now);
}


bool jobIsActive() {
  return jobState != JOB_IDLE;
}


// switch 로 쓰면 컴파일러가 이름 주소 표를 만들어 RAM 에 올리므로(6바이트) if 로 씁니다
const __FlashStringHelper* jobStateName() {
  if (jobState == JOB_WAIT_BUTTON) {
    return F("버튼 대기");
  }
  if (jobState == JOB_AUDIO_ONLY) {
    return F("안내 음성");
  }
  return F("없음");
}


// ─────────────────────────────────────────────
// 작업 시작
// ─────────────────────────────────────────────
static void startJob(uint32_t now) {
  const BrailleJob& job = linkJob();

  if (jobState != JOB_IDLE) {
    Serial.println(F("[작업] 앞 작업을 끝내기 전에 새 작업이 왔습니다 — 새 작업으로 바꿉니다"));
  }

  // 시험음(사인파·DC)이 나던 중이면 멈춥니다. 작업 음성이 시험음보다 우선입니다.
  // 작업 음성은 이미 시작됐을 수 있고(같은 linkPoll 에서 음성 시작 프레임까지 처리됨),
  // 소리 신호는 시험음보다 우선이므로 둘 다 건드리지 않습니다.
  if (speechIsPlaying() && !speechStreamIsActive() && !speechAlertActive()) {
    speechStop();
  }

  // 작업 시작 전에 눌려 있던 버튼은 버립니다
  buttonsClearPresses();

  // 시작 시각은 millis() 를 새로 읽지 않고 이번 loop 의 now 를 씁니다.
  // 새로 읽으면 now 보다 1ms 늦을 수 있고, 그러면 아래 "now - jobStartMs" 가
  // 음수 → 아주 큰 수로 넘어가서 시작하자마자 시간 초과로 처리됩니다.
  jobStartMs = now;
  jobState   = (job.lineCount == 0) ? JOB_AUDIO_ONLY : JOB_WAIT_BUTTON;

  Serial.print(F("[작업] 새 작업: 점자 "));
  Serial.print(job.lineCount);
  Serial.print(F("줄"));
  if (jobState == JOB_AUDIO_ONLY) {
    Serial.println(F(" (점자 없음 — 안내 음성만 재생 후 자동 완료)"));
  } else {
    Serial.println(F(" — 확인(D32) = 타각, 취소(D33)"));
  }
}


// ─────────────────────────────────────────────
// 작업 진행 — 매 loop
// ─────────────────────────────────────────────
static void updateJob(uint32_t now) {
  bool okPressed     = buttonTakePress(BUTTON_OK);
  bool cancelPressed = buttonTakePress(BUTTON_CANCEL);

  if (jobState == JOB_IDLE) {
    return;   // 작업이 없을 때 누른 버튼은 버립니다
  }

  // 음성을 끝까지 재생했으면 정리
  if (speechStreamFinished()) {
    speechStop();
  }

  // ESP32 가 포기하기(120초) 조금 전에 먼저 'E' 를 보냅니다
  if (now - jobStartMs > LINK_JOB_TIMEOUT_MS) {
    finishJob(false, F("시간 초과 — 버튼을 누르지 않음"));
    return;
  }

  if (jobState == JOB_AUDIO_ONLY) {
    LinkAudioState audio = linkAudioState();
    bool played   = (audio == LINK_AUDIO_COMPLETE) && !speechStreamIsActive();
    bool noAudio  = (audio == LINK_AUDIO_NONE) && (now - jobStartMs > AUDIO_ONLY_WAIT_MS);
    // 안내 음성은 버튼을 누르면 바로 끝내도 됩니다
    if (played || noAudio || okPressed || cancelPressed) {
      finishJob(true, F("안내 음성 끝"));
    }
    return;
  }

  // JOB_WAIT_BUTTON — 음성이 재생 중이어도 버튼을 받습니다
  if (cancelPressed) {
    finishJob(false, F("취소 버튼"));
    return;
  }
  if (okPressed) {
    stopJobAudio();   // 재생 중이면 즉시 멈추고 타각
    bool ok = punchBraille(linkJob());
    finishJob(ok, ok ? F("확인 버튼 -> 타각 완료") : F("타각 실패"));
  }
}


// 작업 음성을 즉시 멈추고, 남은 음성은 더 받지 않습니다
static void stopJobAudio() {
  if (speechStreamIsActive()) {
    speechStop();
  }
  linkAudioAbort();
}


// 작업을 끝내고 ESP32 에 결과를 알립니다. done = true → 'D', false → 'E'
static void finishJob(bool done, const __FlashStringHelper* why) {
  stopJobAudio();
  if (done) {
    linkReportDone();
  } else {
    linkReportError();
  }
  jobState = JOB_IDLE;

  Serial.print(F("[작업] "));
  Serial.print(done ? F("'D' 완료") : F("'E' 실패/취소"));
  Serial.print(F(" — "));
  Serial.println(why);
}


// ─────────────────────────────────────────────
// 상태 LED
//   재생 중이면 켜짐 (소리가 안 들릴 때 코드가 도는지 눈으로 확인)
//   작업이 진행 중이면 깜빡임
// ─────────────────────────────────────────────
static void updateLed(uint32_t now) {
  bool waitingBlink = (jobState != JOB_IDLE) && ((now / 250) % 2 == 0);
  digitalWrite(PIN_LED, (speechIsPlaying() || waitingBlink) ? HIGH : LOW);
}
