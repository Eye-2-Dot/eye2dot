/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * 3단계: Timer PWM 오디오 출력 확인
 *
 * 확인하는 것
 *   - Timer1 PWM이 핀 11로 나오는가
 *   - Timer2 8kHz 인터럽트가 제때 도는가
 *   - RC 필터 → LM386 → 스피커 경로가 살아있는가
 *
 * 배선
 *   핀 11 (PB5, OC1A) → RC 필터 → LM386 앰프 → 스피커
 *
 * 사용법
 *   시리얼 모니터(115200)를 열고 0~5 중 한 글자를 보내면 동작이 바뀝니다.
 *   단계별로 확인하면 소리가 안 날 때 원인을 좁힐 수 있습니다.
 *
 * 4단계: ESP32-CAM 과 연결해 실제 작업 처리
 *   ESP32 가 점자·음성을 보내면 → 음성 재생 → 확인 버튼 = 타각 후 'D' / 취소 버튼 = 'E'
 *   점자 없이 음성만 오면(실패 안내) → 재생이 끝나면 자동으로 'D'
 *   ESP32 가 알림음을 보내면 → 사인파 삐 소리 (종류별로 횟수가 다름)
 *
 *   추가 배선
 *     D19 (RX1) ← ESP32 GPIO15 (직결)
 *     D18 (TX1) → 전압 분배 (1kΩ + 2kΩ) → ESP32 GPIO14   (5V 직결 금지)
 *     GND 공통
 *     D32 확인 버튼, D33 취소 버튼 — 한쪽은 핀, 다른 쪽은 GND (누르면 LOW)
 *
 *   작업 중에는 USB 시험 명령 0~5 를 실행하지 않습니다 (작업 음성과 섞이지 않게).
 */

#include <Arduino.h>

#include "pins.h"
#include "speech.h"
#include "link.h"

// 함수 선언 (정의는 파일 하단에)
int  freeRam();
void printHelp();
void printStatus();
void handleCommand(char cmd);
void startJob();
void updateJob(uint32_t now);
void finishJob(bool done, const __FlashStringHelper* why);
void stopJobAudio();
bool punchBraille(const BrailleJob& job);

// 시리얼 속도. platformio.ini 의 monitor_speed 와 같아야 합니다.
const uint32_t BAUD_RATE = 115200;

// 상태를 시리얼에 출력하는 간격 (밀리초)
const uint16_t STATUS_INTERVAL = 2000;

// ─────────────────────────────────────────────
// 현재 상태 (표시용)
//
// speech 모듈은 "재생 중인가"만 알려주므로,
// 무엇을 재생 중인지는 여기서 따로 기록해 둡니다.
// ─────────────────────────────────────────────
char     currentCmd  = '0';   // 마지막으로 받은 명령
uint16_t currentFreq = 0;     // 사인파 재생 중이면 그 주파수, 아니면 0
uint32_t lastStatus  = 0;     // 마지막으로 상태를 출력한 시각

// ─────────────────────────────────────────────
// 작업 상태 (ESP32 가 보낸 출력 작업)
// ─────────────────────────────────────────────
enum JobState : uint8_t {
  JOB_IDLE        = 0,   // 작업 없음
  JOB_WAIT_BUTTON = 1,   // 점자 있음 — 음성을 들려주고 확인·취소 버튼을 기다림
  JOB_AUDIO_ONLY  = 2,   // 점자 없음 — 안내 음성만 재생하고 끝나면 자동 'D'
};

JobState jobState   = JOB_IDLE;
uint32_t jobStartMs = 0;

// 점자 없는 작업에서 음성 시작이 이 시간 안에 안 오면 그냥 끝냅니다
const uint16_t AUDIO_ONLY_WAIT_MS = 3000;

// ─────────────────────────────────────────────
// 버튼 (누르면 GND → LOW). 떨림 방지 포함
//
// 버튼은 누르는 순간 접점이 몇 ms 동안 붙었다 떨어졌다를 반복합니다.
// 같은 값이 DEBOUNCE_MS 동안 유지될 때만 "바뀌었다"고 봅니다.
// ─────────────────────────────────────────────
const uint8_t DEBOUNCE_MS = 30;

struct Button {
  uint8_t  pin;
  bool     stable;      // 떨림이 가라앉은 뒤의 상태 (true = 눌림)
  bool     lastRaw;     // 마지막으로 읽은 그대로의 값
  uint32_t changedMs;   // lastRaw 가 마지막으로 바뀐 시각
  bool     pressed;     // 새로 눌렸음 (buttonTakePress 로 읽으면 지워짐)
};

Button btnOk     = { PIN_BTN_OK,     false, false, 0, false };
Button btnCancel = { PIN_BTN_CANCEL, false, false, 0, false };

void buttonBegin(Button& b) {
  pinMode(b.pin, INPUT_PULLUP);
  b.stable    = (digitalRead(b.pin) == LOW);
  b.lastRaw   = b.stable;
  b.changedMs = millis();
  b.pressed   = false;
}

void buttonUpdate(Button& b, uint32_t now) {
  bool raw = (digitalRead(b.pin) == LOW);   // INPUT_PULLUP: 누르면 LOW
  if (raw != b.lastRaw) {
    b.lastRaw   = raw;
    b.changedMs = now;
  } else if (raw != b.stable && now - b.changedMs >= DEBOUNCE_MS) {
    b.stable = raw;
    if (raw) {
      b.pressed = true;
    }
  }
}

bool buttonTakePress(Button& b) {
  bool p = b.pressed;
  b.pressed = false;
  return p;
}


// ─────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(BAUD_RATE);
  delay(500);                 // 업로드 직후 첫 출력이 잘리는 것 방지

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  buttonBegin(btnOk);
  buttonBegin(btnCancel);

  // 타이머 설정. 이 시점부터 핀 11에 62.5kHz PWM이 나갑니다.
  // 듀티비는 50%(무음)으로 고정되어 있어 소리는 나지 않습니다.
  speechInit();

  // ESP32 연결 (Serial1). 이 시점부터 프레임을 받을 수 있습니다.
  linkBegin();

  printHelp();
}


// ─────────────────────────────────────────────
// loop()
//
// 소리는 인터럽트가 알아서 내보내므로
// loop()는 명령 처리와 상태 출력만 하면 됩니다.
//
// 4단계부터는 ESP32 프레임 받기·버튼·작업 진행도 여기서 합니다.
// 음성을 받는 동안 loop()가 오래 멈추면 수신이 밀리므로 delay()는 쓰지 않습니다.
// ─────────────────────────────────────────────
void loop() {
  // ── ESP32 에서 온 바이트를 프레임으로 조립 (매번) ──
  linkPoll();

  // ── 알림음 패턴 진행 ──
  speechUpdate();

  uint8_t alert = linkTakeAlert();
  if (alert != 0) {
    speechAlertPlay(alert);   // 알림음이 시험음·음성보다 우선
    currentFreq = 0;
    Serial.print(F("[알림음] 종류 "));
    Serial.println(alert);
  }

  // ── 버튼·작업 ──
  uint32_t loopNow = millis();
  buttonUpdate(btnOk, loopNow);
  buttonUpdate(btnCancel, loopNow);

  if (linkTakeNewJob()) {
    startJob();
  }
  updateJob(loopNow);

  // ── 시리얼 명령 처리 ──
  while (Serial.available() > 0) {
    char c = Serial.read();

    // 시리얼 모니터가 같이 보내는 줄바꿈은 무시합니다.
    if (c == '\n' || c == '\r') {
      continue;
    }
    handleCommand(c);
  }

  // ── 주기적 상태 출력 ──
  // delay() 대신 millis()를 쓰는 이유는 기다리는 동안에도
  // 시리얼 명령을 받아야 하기 때문입니다.
  uint32_t now = millis();
  if (now - lastStatus >= STATUS_INTERVAL) {
    lastStatus = now;
    printStatus();
  }

  // 재생 중이면 LED 점등 (소리가 안 들릴 때 코드가 도는지 눈으로 확인)
  // 작업이 버튼을 기다리는 중이면 깜빡임
  bool waitingBlink = (jobState != JOB_IDLE) && ((now / 250) % 2 == 0);
  digitalWrite(PIN_LED, (speechIsPlaying() || waitingBlink) ? HIGH : LOW);
}


// ─────────────────────────────────────────────
// 명령 처리
//
// 아래 순서대로 확인하면 문제 지점을 좁힐 수 있습니다.
//   '2'에서 "툭" 소리 → 타이머부터 스피커까지 경로는 살아있음
//   '3'이 맑게 들림   → RC 필터가 62.5kHz 캐리어를 잘 깎고 있음
//   '3'이 웅웅거림    → 필터 상수 확인 (컷오프 3~5kHz 권장)
// ─────────────────────────────────────────────
void handleCommand(char cmd) {
  // 작업 중이거나 알림음이 나는 중에는 소리 시험 명령을 실행하지 않습니다.
  // (작업 음성·알림음과 시험음이 서로 끼어들지 않게 — 도움말 '?' 는 언제나 됨)
  if (cmd >= '0' && cmd <= '5') {
    if (jobState != JOB_IDLE) {
      Serial.println(F("작업 중 — 소리 시험 명령은 작업이 끝난 뒤에 보내세요"));
      return;
    }
    if (speechAlertActive()) {
      Serial.println(F("알림음 재생 중 — 끝난 뒤에 보내세요"));
      return;
    }
  }

  switch (cmd) {
    case '0':
      speechStop();
      currentFreq = 0;
      Serial.println(F("[0] 정지 — 무음 (OCR1A = 128)"));
      break;

    case '1':
      // 128은 듀티비 50%. 스피커 입장에서는 아무 변화가 없습니다.
      speechPlayDC(128);
      currentFreq = 0;
      Serial.println(F("[1] 고정값 128 — 무음이어야 정상입니다"));
      break;

    case '2':
      // 128 → 200으로 전압이 한 번 튑니다.
      // 이때 "툭" 소리가 나면 필터~앰프~스피커가 연결된 것입니다.
      speechPlayDC(200);
      currentFreq = 0;
      Serial.println(F("[2] 고정값 200 — \"툭\" 소리가 나면 경로 정상"));
      break;

    case '3':
      speechPlayTone(440);
      currentFreq = 440;
      Serial.println(F("[3] 440Hz 사인파 — 라(A4). 맑게 들리면 필터 정상"));
      break;

    case '4':
      speechPlayTone(1000);
      currentFreq = 1000;
      Serial.println(F("[4] 1000Hz 사인파 — 고음 확인"));
      break;

    case '5':
      speechPlayTone(220);
      currentFreq = 220;
      Serial.println(F("[5] 220Hz 사인파 — 저음 확인"));
      break;

    case '?':
    case 'h':
      printHelp();
      return;   // 상태는 바뀌지 않았으므로 currentCmd 갱신 안 함

    default:
      Serial.print(F("알 수 없는 명령: '"));
      Serial.print(cmd);
      Serial.println(F("'  (? 를 보내면 도움말)"));
      return;
  }

  currentCmd = cmd;
}


// ─────────────────────────────────────────────
// 도움말
// ─────────────────────────────────────────────
void printHelp() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  Eye 2 Dot - Firmware 1"));
  Serial.println(F("  3단계: Timer PWM 오디오 출력"));
  Serial.println(F("========================================"));
  Serial.print  (F("오디오 핀   : "));
  Serial.print  (PIN_AUDIO);
  Serial.println(F(" (PB5 / OC1A)"));
  Serial.println(F("PWM 캐리어  : 62500 Hz  (16MHz / 256)"));
  Serial.println(F("샘플레이트  : 8000 Hz   (16MHz / 8 / 250)"));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("명령 (시리얼로 한 글자 전송)"));
  Serial.println(F("  0 : 정지 (무음)"));
  Serial.println(F("  1 : 고정값 128   — 무음 확인"));
  Serial.println(F("  2 : 고정값 200   — DC. 툭 소리가 나면 경로 연결됨"));
  Serial.println(F("  3 : 440Hz 사인파 — 라 음. 맑게 들리면 필터 정상"));
  Serial.println(F("  4 : 1000Hz 사인파"));
  Serial.println(F("  5 : 220Hz 사인파 — 저음 확인"));
  Serial.println(F("  ? : 이 도움말"));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("순서대로 2 -> 3 으로 확인하는 것을 권합니다."));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("ESP32 연결: Serial1 (D19 RX1 / D18 TX1)"));
  Serial.println(F("  작업이 오면 음성 재생 -> D32 확인 = 타각 / D33 취소"));
  Serial.println(F("  작업 중에는 0~5 시험 명령을 받지 않습니다."));
  Serial.println();
}


// ─────────────────────────────────────────────
// 현재 상태 + 남은 RAM
// ─────────────────────────────────────────────
void printStatus() {
  Serial.print(F("["));
  Serial.print(millis() / 1000);
  Serial.print(F("초] 상태: "));

  if (!speechIsPlaying()) {
    Serial.print(F("정지"));
  } else if (speechAlertActive()) {
    Serial.print(F("알림음"));
  } else if (speechStreamIsActive()) {
    Serial.print(F("작업 음성"));
  } else if (currentFreq > 0) {
    Serial.print(currentFreq);
    Serial.print(F("Hz 사인파"));
  } else {
    Serial.print(F("DC 출력"));
  }

  Serial.print(F(" (명령 '"));
  Serial.print(currentCmd);
  Serial.print(F("') | 작업: "));
  switch (jobState) {
    case JOB_IDLE:        Serial.print(F("없음"));        break;
    case JOB_WAIT_BUTTON: Serial.print(F("버튼 대기"));   break;
    case JOB_AUDIO_ONLY:  Serial.print(F("안내 음성"));   break;
  }
  Serial.print(F(" | 프레임 정상 "));
  Serial.print(linkFramesOk());
  Serial.print(F(" / 버림 "));
  Serial.print(linkFramesBad());
  Serial.print(F(" | 남은 RAM: "));
  Serial.print(freeRam());
  Serial.println(F(" bytes"));
}


// ─────────────────────────────────────────────
// 작업 — ESP32 가 보낸 출력 작업 하나
//
//   점자 프레임 도착 → startJob()
//   (음성이 오면 link 모듈이 받아서 speech 모듈이 재생)
//   updateJob() 이 버튼·재생 끝·시간 초과를 보고 'D' 또는 'E' 로 마무리
// ─────────────────────────────────────────────
void startJob() {
  const BrailleJob& job = linkJob();

  if (jobState != JOB_IDLE) {
    Serial.println(F("[작업] 앞 작업을 끝내기 전에 새 작업이 왔습니다 — 새 작업으로 바꿉니다"));
  }

  // 시험음(사인파·DC)이 나던 중이면 멈춥니다. 작업 음성이 시험음보다 우선입니다.
  // 작업 음성은 이미 시작됐을 수 있고(같은 linkPoll 에서 음성 시작 프레임까지 처리됨),
  // 알림음은 시험음보다 우선이므로 둘 다 건드리지 않습니다.
  if (speechIsPlaying() && !speechStreamIsActive() && !speechAlertActive()) {
    speechStop();
  }
  currentFreq = 0;

  // 작업 시작 전에 눌려 있던 버튼은 버립니다
  buttonTakePress(btnOk);
  buttonTakePress(btnCancel);

  jobStartMs = millis();
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


void updateJob(uint32_t now) {
  bool okPressed     = buttonTakePress(btnOk);
  bool cancelPressed = buttonTakePress(btnCancel);

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
void stopJobAudio() {
  if (speechStreamIsActive()) {
    speechStop();
  }
  linkAudioAbort();
}


// 작업을 끝내고 ESP32 에 결과를 알립니다. done = true → 'D', false → 'E'
void finishJob(bool done, const __FlashStringHelper* why) {
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
// 타각 — 지금은 자리만 있습니다
//
// TODO(솔레노이드 담당): 여기에 실제 타각 코드를 넣습니다.
//
//   함수 원형   : bool punchBraille(const BrailleJob& job)
//   넘겨받는 것 : job.lineCount       줄 수 (라벨은 1)
//                 job.lineLen[줄]     그 줄의 칸 수
//                 job.cells[줄][칸]   칸 값 = 6비트 마스크
//                                     bit0 = 점1, bit1 = 점2, bit2 = 점3,
//                                     bit3 = 점4, bit4 = 점5, bit5 = 점6
//                 (구조체는 include/link.h, 크기 한도는 common/protocol.h 의 BRAILLE_MAX_*)
//   솔레노이드  : pins.h 의 PIN_SOL[0..5] = 점1..점6
//   돌려줄 값   : 다 찍었으면 true → ESP32 에 'D'
//                 걸림·리밋 스위치 이상 등으로 실패하면 false → 'E'
//   주의        : 이 함수가 끝날 때까지 loop() 가 멈춥니다. 오래 걸리면 중간중간
//                 linkPoll() 을 불러 시리얼 수신 버퍼가 넘치지 않게 할 것.
//
// 지금은 칸 값과 점 번호를 USB 시리얼에 출력하고 true 를 돌려줍니다.
// ─────────────────────────────────────────────
bool punchBraille(const BrailleJob& job) {
  Serial.println(F("[타각] 솔레노이드 연결 전 — 칸 값만 출력합니다"));

  for (uint8_t row = 0; row < job.lineCount; row++) {
    Serial.print(F("  "));
    Serial.print(row + 1);
    Serial.print(F("줄 ("));
    Serial.print(job.lineLen[row]);
    Serial.println(F("칸)"));

    for (uint8_t col = 0; col < job.lineLen[row]; col++) {
      uint8_t mask = job.cells[row][col];
      Serial.print(F("    칸 "));
      Serial.print(col + 1);
      Serial.print(F(" = "));
      Serial.print(mask);
      Serial.print(F(" -> 점 "));

      bool any = false;
      for (uint8_t dot = 0; dot < 6; dot++) {
        if (mask & (1 << dot)) {
          if (any) {
            Serial.print(',');
          }
          Serial.print(dot + 1);
          any = true;
        }
      }
      if (!any) {
        Serial.print(F("없음 (빈칸)"));
      }
      Serial.println();
    }
  }
  return true;
}


// ─────────────────────────────────────────────
// 남은 RAM 확인
//
// ATmega2560의 RAM은 8KB뿐입니다.
// 4단계에서 서버가 보낸 PCM을 담을 버퍼를 잡아야 하므로
// 여유가 얼마나 있는지 계속 지켜봅니다.
//
// 사인 테이블은 PROGMEM에 있어서 여기 숫자에 영향을 주지 않습니다.
// ─────────────────────────────────────────────
int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
