#include "job.h"
#include "pins.h"
#include "config.h"
#include "protocol.h"
#include "stepper.h"
#include "homing.h"
#include "solenoid.h"
#include "punch.h"
#include "button.h"
#include "link.h"

static uint8_t state = ST_BOOT;
static PageBuf buf;
static uint8_t curSeq = 0, curPage = 0, pageCount = 1, doneCount = 0, lastErr = ERR_NONE;
static bool    pendingPrint = false;
static bool    cancelPending = false;   // 취소 요청이 왔음 (jobUpdate 가 처리)
static bool    ignoreRelease = false;   // 버튼으로 멈춘 뒤, 그 버튼을 떼는 것은 "짧게 누름"으로 치지 않는다
static bool    keepMotorsOn  = false;   // USB 명령 e 로 켜 둔 동안에는 대기에서도 모터 전류를 끄지 않는다
static unsigned long tRx = 0;

// ---- 부저 (선택) ----
static uint8_t beepLeft = 0; static bool beepOn = false; static unsigned long tBeep = 0;
static void beep(uint8_t n) { if (USE_BUZZER) { beepLeft = n; beepOn = false; tBeep = millis() - 200; } }
static void beepUpdate() {
  if (!USE_BUZZER) return;
  if (!beepLeft && !beepOn) return;
  if (millis() - tBeep < 120) return;
  tBeep = millis();
  if (beepOn) { digitalWrite(PIN_BUZZER, LOW); beepOn = false; }
  else if (beepLeft) { digitalWrite(PIN_BUZZER, HIGH); beepOn = true; beepLeft--; }
}

// ---- LED: 상태를 깜빡임으로 ----
static void ledUpdate() {
  unsigned long m = millis();
  bool on;
  switch (state) {
    case ST_IDLE:        on = (m % 2000) < 100; break;   // 가끔 한 번
    case ST_WAIT_BUTTON: on = (m % 500) < 250;  break;   // 천천히 깜빡 = 버튼을 기다림
    case ST_ERROR:       on = (m % 120) < 60;   break;   // 빠르게 깜빡 = 오류
    default:             on = true;             break;   // 움직이는 중
  }
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

static void setState(uint8_t s) {
  if (s == state) return;
  state = s;
  Serial.print(F("[상태] ")); Serial.println(jobStateName());
}

static void enterError(uint8_t e) {
  lastErr = e;
  pendingPrint = false;
  punchAbort();
  stepperEnable(false);
  linkSendError(e);
  beep(5);
  Serial.print(F("[오류] 코드 ")); Serial.println(e);
  setState(ST_ERROR);
}

static void goIdle() {
  if (MOTOR_OFF_WHEN_IDLE && !keepMotorsOn) stepperEnable(false);
  setState(ST_IDLE);
}

static void beginPrint() {
  lastErr = ERR_NONE;
  if (REHOME_EACH_PAGE || !stepperHomed()) {
    pendingPrint = true;
    homingStart();
    setState(ST_HOMING);
  } else {
    punchStart(&buf);
    setState(ST_PRINTING);
  }
}

void jobInit() {
  pinMode(PIN_LED, OUTPUT);
  if (USE_BUZZER) { pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW); }
  buf.lineCount = 0;
  if (AUTO_HOME_ON_BOOT) { homingStart(); setState(ST_HOMING); }
  else setState(ST_IDLE);
}

void jobStartHoming() {
  if (state != ST_IDLE && state != ST_ERROR) return;
  lastErr = ERR_NONE; pendingPrint = false;
  homingStart();
  setState(ST_HOMING);
}

void jobStartTestPage() {
  if (state != ST_IDLE) { Serial.println(F("대기 상태에서만 시험 쪽을 찍을 수 있습니다")); return; }
  // 1줄: 점 1~6 을 하나씩, 그리고 6점   2줄: 6점 10칸   3줄: 점 1·3·5 / 2·4·6 번갈아
  static const uint8_t l0[] = {1, 2, 4, 8, 16, 32, 63};
  buf.lineCount = 3;
  buf.len[0] = sizeof(l0); memcpy(buf.cell[0], l0, sizeof(l0));
  buf.len[1] = 10; for (uint8_t i = 0; i < 10; i++) buf.cell[1][i] = 63;
  buf.len[2] = 10; for (uint8_t i = 0; i < 10; i++) buf.cell[2][i] = (i & 1) ? 42 : 21;
  curSeq = 0xFF; curPage = 0; pageCount = 1; doneCount = 0;
  beginPrint();
}

uint8_t jobOnPageBegin(uint8_t seq, uint8_t page, uint8_t pc, uint8_t lc) {
  if (state == ST_ERROR) return NAK_NOTREADY;
  if (state != ST_IDLE && state != ST_RECEIVING) return NAK_BUSY;
  if (lc > MAX_LINES) return NAK_RANGE;
  if (seq != curSeq || page == 0) doneCount = 0;      // 새 작업(또는 다시 시작한 ESP32 의 첫 쪽)
  curSeq = seq; curPage = page; pageCount = pc;
  buf.lineCount = 0;
  for (uint8_t i = 0; i < MAX_LINES; i++) buf.len[i] = 0;
  lastErr = ERR_NONE;
  tRx = millis();
  setState(ST_RECEIVING);
  return 0;
}

uint8_t jobOnLine(uint8_t index, uint8_t n, const uint8_t* cells) {
  if (state != ST_RECEIVING) return NAK_ORDER;
  if (index >= MAX_LINES || n > MAX_CELLS) return NAK_RANGE;
  for (uint8_t i = 0; i < n; i++) buf.cell[index][i] = cells[i] & 0x3F;
  buf.len[index] = n;
  if (index + 1 > buf.lineCount) buf.lineCount = index + 1;
  tRx = millis();
  return 0;
}

uint8_t jobOnGo(uint8_t lineCount) {
  // 확인 응답이 사라져 G 가 다시 온 경우: 이미 시작했으면 그대로 "받음"
  if (state == ST_WAIT_BUTTON || state == ST_PRINTING || (state == ST_HOMING && pendingPrint)) return 0;
  if (state != ST_RECEIVING) return NAK_ORDER;
  if (lineCount > MAX_LINES) return NAK_RANGE;
  if (lineCount > buf.lineCount) buf.lineCount = lineCount;        // 끝쪽 빈 줄
  if (curPage > 0 || WAIT_BUTTON_FIRST_PAGE) {
    beep(1);
    Serial.println(F("종이를 바꾸고 [출력 계속] 버튼을 누르세요"));
    setState(ST_WAIT_BUTTON);
  } else {
    beginPrint();
  }
  return 0;
}

// 하던 일을 그 자리에서 멈춘다: 솔레노이드 끄기, 두 축 멈춤, 모터 전류 끄기 → 대기.
// 원점으로 돌아가지 않는다 (방향이 틀렸거나 무언가 끼어서 멈춘 것일 수 있으므로).
// 다음 작업은 찍기 전에 원점부터 다시 잡는다.
static void stopNow(const __FlashStringHelper* why) {
  bool hadJob = (state == ST_RECEIVING || state == ST_WAIT_BUTTON || state == ST_PRINTING ||
                 state == ST_PARKING || (state == ST_HOMING && pendingPrint));
  Serial.print(F("[멈춤] ")); Serial.println(why);
  punchAbort();
  homingAbort();
  stepperEnable(false);
  pendingPrint = false;
  lastErr = ERR_CANCELLED;
  if (hadJob) linkSendError(ERR_CANCELLED);
  beep(3);
  keepMotorsOn  = false;
  ignoreRelease = buttonIsDown();
  setState(ST_IDLE);
}

void jobKeepMotorsOn(bool on) {
  keepMotorsOn = on;
  stepperEnable(on);
}

// 취소 요청 (ESP32 의 X 프레임, USB 명령). 타각 함수 안에서 불릴 수 있어서 여기서는 표시만 하고
// 실제로 멈추는 일은 jobUpdate 가 한다. 솔레노이드만은 바로 끈다.
void jobOnCancel() {
  if (state == ST_IDLE || state == ST_BOOT || state == ST_ERROR) return;
  solenoidAllOff();
  cancelPending = true;
}

bool jobIsMoving() {
  return state == ST_HOMING || state == ST_PRINTING || state == ST_PARKING;
}

void jobFillStatus(uint8_t out[6]) {
  out[0] = state; out[1] = curSeq; out[2] = curPage;
  out[3] = punchLine(); out[4] = doneCount; out[5] = lastErr;
}

uint8_t jobState()     { return state; }
uint8_t jobLastError() { return lastErr; }

const char* jobStateName() {
  switch (state) {
    case ST_BOOT:        return "시작";
    case ST_HOMING:      return "원점 잡는 중";
    case ST_IDLE:        return "대기";
    case ST_RECEIVING:   return "쪽 받는 중";
    case ST_WAIT_BUTTON: return "버튼 대기";
    case ST_PRINTING:    return "타각 중";
    case ST_PARKING:     return "원점으로 복귀";
    case ST_ERROR:       return "오류";
  }
  return "?";
}

void jobUpdate() {
  ledUpdate();
  beepUpdate();
  bool shortPress = buttonShortEvent();
  bool longPress  = buttonLongEvent();

  if (ignoreRelease) {
    if (!buttonIsDown()) ignoreRelease = false;
    shortPress = false;
    longPress  = false;
  }

  if (cancelPending) {
    cancelPending = false;
    if (state != ST_IDLE && state != ST_ERROR) stopNow(F("취소 요청"));
    return;
  }
  // 모터가 움직이는 동안 버튼을 누르면 즉시 멈춘다 (비상 정지).
  // 버튼 대기에서 "짧게 눌렀다 뗀" 뒤에 움직이기 시작하므로, 움직이는 중의 눌림은 새로 누른 것이다.
  if (jobIsMoving() && buttonIsDown()) {
    stopNow(F("버튼"));
    return;
  }

  switch (state) {
    case ST_HOMING:
      if (homingDone()) {
        if (pendingPrint) { pendingPrint = false; punchStart(&buf); setState(ST_PRINTING); }
        else goIdle();
      } else if (homingFailed()) {
        enterError(homingError());
      }
      break;

    case ST_IDLE:
      if (shortPress) jobStartHoming();              // 대기 중 버튼 = 원점 다시 잡기
      break;

    case ST_RECEIVING:
      if (millis() - tRx > 5000UL) { Serial.println(F("쪽을 받다가 끊김")); goIdle(); }
      break;

    case ST_WAIT_BUTTON:
      if (shortPress) beginPrint();
      else if (longPress) stopNow(F("버튼 길게 누름 — 작업 취소"));
      break;

    case ST_PRINTING:
      punchUpdate();
      if (cancelPending) break;                      // 찍는 사이에 취소가 옴 → 다음 바퀴에서 멈춤
      if (!punchBusy()) {
        stepperMoveToMm(AX_X, PARK_X_MM);
        stepperMoveToMm(AX_Y, PARK_Y_MM);
        setState(ST_PARKING);
      }
      break;

    case ST_PARKING:
      if (!stepperAnyBusy()) {
        doneCount++;
        linkSendPageDone(curSeq, curPage);
        Serial.print(F("[완료] 쪽 ")); Serial.print(curPage + 1);
        Serial.print(F(" / ")); Serial.print(pageCount);
        Serial.print(F("  찍은 칸 ")); Serial.println(punchCount());
        beep((uint8_t)(curPage + 1 >= pageCount ? 2 : 1));
        goIdle();
      }
      break;

    case ST_ERROR:
      if (shortPress) jobStartHoming();              // 원인을 고친 뒤 버튼 = 다시 시도
      break;

    default: break;
  }
}
