/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * job.cpp — 작업 진행 (상태와 버튼 표는 job.h)
 */

#include <Arduino.h>

#include "button.h"
#include "job.h"
#include "link.h"
#include "pins.h"
#include "punch.h"
#include "speech.h"

// ─────────────────────────────────────────────
// 상태
// ─────────────────────────────────────────────
enum JobState : uint8_t {
  JOB_IDLE        = 0,   // 대기 — 작업 없음
  JOB_RECOGNIZING = 1,   // 인식 중 — 'C' 를 보내고 ESP32 의 결과를 기다림
  JOB_PLAYING     = 2,   // 안내 재생 — 작업 음성을 들려주는 중
  JOB_WAIT_PRINT  = 3,   // 출력 대기 — 재생이 끝나고 출력 버튼을 기다림
  JOB_PUNCHING    = 4,   // 타각 중
};

static JobState jobState = JOB_IDLE;

// 시간을 재기 시작한 시각
//   인식 중          : 'C' 를 보낸 시각
//   안내 재생·출력 대기 : 점자 프레임을 받은 시각 (재생이 끝나 출력 대기로 가도 그대로)
static uint32_t startMs = 0;

// 지금 작업에 점자가 있는지 (없으면 실패 안내처럼 음성만 있는 작업)
static bool jobHasBraille = false;

// 점자 프레임을 받은 뒤 이 시간 안에 음성 시작이 안 오면 음성 없는 작업으로 봅니다
static const uint16_t AUDIO_START_WAIT_MS = 3000;

// 함수 선언
static void startJob(uint32_t now);
static void startCapture(uint32_t now);
static void recapture(uint32_t now);
static void punch();
static void updateIdle(uint32_t now, bool capture, bool print);
static void updateRecognizing(uint32_t now);
static void updatePlaying(uint32_t now, bool capture, bool print);
static void updateWaitPrint(uint32_t now, bool capture, bool print);
static bool playbackDone(uint32_t now);
static void stopJobAudio();
static void finishJob(bool done, const __FlashStringHelper* why);
static void failJob(const __FlashStringHelper* why);
static void updateLed(uint32_t now);


void jobInit() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
}


void jobUpdate(uint32_t now) {
  // ── 1) ESP32 알림음 프레임 (촬영·서버 실패 등) → 실패음 ──
  uint8_t alert = linkTakeAlert();
  if (alert != 0) {
    speechAlertPlay(alert);   // 종류와 상관없이 BEEP_FAIL (speech.cpp 맨 위 표)
    Serial.print(F("[알림음] 종류 "));
    Serial.println(alert);
    if (jobState == JOB_RECOGNIZING) {
      jobState = JOB_IDLE;
      Serial.println(F("[촬영] ESP32 가 실패를 알림 — 대기로"));
    }
  }

  // ── 2) 새 점자 프레임 = 새 작업. 어느 상태에서든 갈아탑니다 ──
  if (linkTakeNewJob()) {
    startJob(now);
  }

  // ── 3) 버튼과 상태별 진행 ──
  bool capture = buttonTakePress(BUTTON_CAPTURE);
  bool print   = buttonTakePress(BUTTON_PRINT);

  switch (jobState) {
    case JOB_IDLE:        updateIdle(now, capture, print);         break;
    case JOB_RECOGNIZING: updateRecognizing(now);                  break;   // 버튼 무시
    case JOB_PLAYING:     updatePlaying(now, capture, print);      break;
    case JOB_WAIT_PRINT:  updateWaitPrint(now, capture, print);    break;
    case JOB_PUNCHING:                                             break;   // punch() 안에서만 머무름
  }

  updateLed(now);
}


bool jobIsActive() {
  return jobState != JOB_IDLE;
}


// switch 로 쓰면 컴파일러가 이름 주소 표를 만들어 RAM 에 올리므로 if 로 씁니다
const __FlashStringHelper* jobStateName() {
  if (jobState == JOB_RECOGNIZING) {
    return F("인식 중");
  }
  if (jobState == JOB_PLAYING) {
    return F("안내 재생");
  }
  if (jobState == JOB_WAIT_PRINT) {
    return F("출력 대기");
  }
  if (jobState == JOB_PUNCHING) {
    return F("타각 중");
  }
  return F("대기");
}


// ─────────────────────────────────────────────
// 새 작업 — 점자 프레임이 도착함 (촬영 결과 또는 앱 작업)
// ─────────────────────────────────────────────
static void startJob(uint32_t now) {
  const BrailleJob& job = linkJob();

  // 앞 작업에는 'D'/'E' 를 보내지 않습니다.
  // ESP32 는 앞 작업을 끝낸 뒤에만 새 점자 프레임을 보내고,
  // link 는 이 프레임을 받는 순간 앞 작업의 음성을 멈추고 새 작업을 열었습니다.
  if (jobState == JOB_PLAYING || jobState == JOB_WAIT_PRINT) {
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
  // 새로 읽으면 now 보다 1ms 늦을 수 있고, 그러면 "now - startMs" 가
  // 음수 → 아주 큰 수로 넘어가서 시작하자마자 시간 초과로 처리됩니다.
  startMs       = now;
  jobHasBraille = (job.lineCount > 0);
  jobState      = JOB_PLAYING;

  Serial.print(F("[작업] 새 작업: 점자 "));
  Serial.print(job.lineCount);
  Serial.print(F("줄"));
  if (jobHasBraille) {
    Serial.println(F(" — 안내 재생 (D32 출력 = 타각, D31 = 재촬영)"));
  } else {
    Serial.println(F(" (점자 없음 — 안내 음성만 재생 후 자동 완료)"));
  }
}


// ─────────────────────────────────────────────
// 촬영 요청
// ─────────────────────────────────────────────
static void startCapture(uint32_t now) {
  linkSendCapture();          // ESP32 에 'C'
  speechBeep(BEEP_READY);     // 눌림을 알리는 준비음
  startMs  = now;
  jobState = JOB_RECOGNIZING;
  Serial.println(F("[촬영] 'C' 보냄 — 인식 중"));
}


// 재촬영: 지금 작업을 취소('E')하고 바로 새로 촬영('C')
static void recapture(uint32_t now) {
  finishJob(false, F("재촬영으로 취소"));   // 음성 멈춤 + 'E'
  startCapture(now);                         // 'C' + 준비음 → 인식 중
}


// ─────────────────────────────────────────────
// 타각
//   punchBraille() 이 끝날 때까지 loop 가 멈추므로 "타각 중" 상태는 이 함수 안에서만 있습니다.
//   (그동안 버튼은 읽히지 않습니다 = 무시)
// ─────────────────────────────────────────────
static void punch() {
  stopJobAudio();             // 재생 중이면 즉시 멈추고 타각
  jobState = JOB_PUNCHING;
  digitalWrite(PIN_LED, HIGH);
  Serial.println(F("[타각] 시작"));

  bool ok = punchBraille(linkJob());

  buttonsClearPresses();      // 타각 중에 눌린 것은 버립니다
  if (ok) {
    finishJob(true, F("타각 완료"));
    speechBeep(BEEP_DONE);
  } else {
    failJob(F("타각 실패"));
  }
}


// ─────────────────────────────────────────────
// 상태별 진행 — 매 loop
// ─────────────────────────────────────────────
static void updateIdle(uint32_t now, bool capture, bool print) {
  if (capture) {
    startCapture(now);
  } else if (print) {
    speechBeep(BEEP_WRONG);
    Serial.println(F("[버튼] 대기 중 출력 버튼 — 출력할 작업이 없음"));
  }
}


static void updateRecognizing(uint32_t now) {
  if (now - startMs > LINK_RECOGNIZE_TIMEOUT_MS) {
    speechBeep(BEEP_FAIL);
    jobState = JOB_IDLE;
    Serial.println(F("[촬영] 인식 시간 초과 — ESP32 응답 없음, 대기로"));
  }
}


static void updatePlaying(uint32_t now, bool capture, bool print) {
  // 음성을 끝까지 재생했으면 정리
  if (speechStreamFinished()) {
    speechStop();
  }

  // ESP32 가 포기하기(130초) 전에 먼저 'E' 를 보냅니다 (그 차이 안에서 타각이 끝나도록, protocol.h)
  if (now - startMs > LINK_JOB_TIMEOUT_MS) {
    failJob(F("시간 초과"));
    return;
  }

  if (capture) {
    recapture(now);
    return;
  }
  if (print && jobHasBraille) {
    punch();
    return;
  }
  // 점자 없는 작업(실패 안내)에서 출력 버튼은 무시합니다 (안내 음성을 끊지 않게).
  // 재생이 끝나 대기로 돌아간 뒤에 누르면 잘못 누름 소리가 납니다.

  if (playbackDone(now)) {
    if (jobHasBraille) {
      jobState = JOB_WAIT_PRINT;   // startMs 는 그대로 (시간 초과는 작업 시작부터)
      Serial.println(F("[작업] 안내 재생 끝 — 출력 대기"));
    } else {
      finishJob(true, F("안내 음성 끝"));
    }
  }
}


static void updateWaitPrint(uint32_t now, bool capture, bool print) {
  if (now - startMs > LINK_JOB_TIMEOUT_MS) {
    failJob(F("시간 초과 — 출력 버튼을 누르지 않음"));
    return;
  }
  if (capture) {
    recapture(now);
    return;
  }
  if (print) {
    punch();
  }
}


// 안내 음성이 끝났는지
static bool playbackDone(uint32_t now) {
  LinkAudioState audio = linkAudioState();
  if (audio == LINK_AUDIO_NONE) {
    return now - startMs > AUDIO_START_WAIT_MS;   // 음성이 오지 않는 작업
  }
  if (audio == LINK_AUDIO_RECEIVING) {
    return false;
  }
  // 다 받았거나 그만 받기로 함 → 링 버퍼에 남은 것까지 재생했으면 끝
  return !speechStreamIsActive();
}


// ─────────────────────────────────────────────
// 마무리
// ─────────────────────────────────────────────

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


// 실패로 끝냄: 'E' + 실패음
static void failJob(const __FlashStringHelper* why) {
  finishJob(false, why);
  speechBeep(BEEP_FAIL);
}


// ─────────────────────────────────────────────
// 상태 LED
//   재생 중이면 켜짐 (소리가 안 들릴 때 코드가 도는지 눈으로 확인)
//   대기가 아니면 깜빡임 (인식 중, 안내 재생, 출력 대기)
// ─────────────────────────────────────────────
static void updateLed(uint32_t now) {
  bool busyBlink = (jobState != JOB_IDLE) && ((now / 250) % 2 == 0);
  digitalWrite(PIN_LED, (speechIsPlaying() || busyBlink) ? HIGH : LOW);
}
