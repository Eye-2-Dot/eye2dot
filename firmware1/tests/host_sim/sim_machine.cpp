// 가짜 Arduino 의 몸통 + 가짜 기계 + 소리 모듈 대역
//   - 시계: micros()·delay() 를 부를 때마다 가짜 시계가 간다 (진짜로 기다리지 않아 몇 초 만에 끝난다)
//   - 핀  : ULN2003 IN1~IN4 코일 순서(phaseOf)를 읽어 헤드·랙 위치를 계산하고, 원점 스위치는 그 위치로 눌림을 정한다
//   - 소리: speech.cpp 는 AVR 타이머를 직접 쓰므로 PC 에서는 이름만 같은 빈 함수로 바꾼다
#include <Arduino.h>

#include "motion.h"
#include "pins.h"
#include "sim.h"
#include "speech.h"

Machine  machine;
uint64_t simNowUs = 0;
std::vector<std::string> simBeeps;

Reg8 PORTA, DDRA;
HardwareSerial Serial, Serial1;

// console.cpp 의 freeRam() 이 찾는 AVR 기호 (값은 의미 없음)
int  __heap_start;
int* __brkval;

static double   lastStepUs[2] = {-1, -1};
static uint64_t solenoidOnUs  = 0;

// ─────────────────────────────────────────────
// 기계
// ─────────────────────────────────────────────
static const double HEAD_HARD_MIN_MM = -2.5;    // 스위치가 눌린 뒤 더 갈 수 있는 거리
static const double HEAD_HARD_MAX_MM = 134.0;   // 레일 210 − 캐리지 76
static const double CARD_HARD_MAX_MM = 128.0;

double Machine::headMm() const { return headSteps / (double)HEAD_STEPS_PER_MM; }
double Machine::cardMm() const { return cardSteps / (double)CARD_STEPS_PER_MM; }

void Machine::reset() {
  headSteps = lround(headStartMm * HEAD_STEPS_PER_MM);
  cardSteps = lround(cardStartMm * CARD_STEPS_PER_MM);
  for (int i = 0; i < 70; i++) {
    level[i] = 0;
    mode[i]  = INPUT;
  }
}

void simPowerOn(uint64_t startUs) {
  simNowUs = startUs;
  machine.reset();
  PORTA.onWrite = [](uint8_t before, uint8_t after) {
    before &= 0x3F;
    after  &= 0x3F;
    if (before == 0 && after != 0) {
      machine.punches.push_back({after, machine.headMm(), machine.cardMm(), 0});
      solenoidOnUs = simNowUs;
    } else if (before != 0 && after == 0 && !machine.punches.empty()) {
      double ms = (simNowUs - solenoidOnUs) / 1000.0;
      machine.punches.back().onMs = ms;
      if (ms > machine.maxSolenoidOnMs) machine.maxSolenoidOnMs = ms;
    }
  };
}

// ULN2003 의 IN1~IN4 에 걸린 값(4비트)이 하프 스텝 순서표의 몇 번째인지. 표에 없으면 −1 (꺼짐 포함)
static int phaseOf(uint8_t bits) {
  static const uint8_t table[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};
  for (int i = 0; i < 8; i++) {
    if (table[i] == bits) return i;
  }
  return -1;
}

static int  lastPhase[2]  = {0, 0};   // 코일이 마지막으로 켜져 있던 자리 (꺼져도 모터는 그 자리에 있다)
static bool coilsDirty[2] = {false, false};

// 코일 핀 4개를 다 쓴 뒤(다음에 시계를 볼 때) 부른다. 순서표에서 한 칸 넘어갔으면 모터가 한 스텝 돈 것.
// 핀을 하나씩 쓰는 몇 µs 사이의 중간 모양은 모터가 따라가지 못하므로 보지 않는다.
static void coilsChanged(int axis) {
  bool head = (axis == 0);
  const uint8_t* pins = head ? PIN_HEAD_IN : PIN_CARD_IN;
  bool invert = head ? HEAD_DIR_INVERT : CARD_DIR_INVERT;

  uint8_t bits = 0;
  for (int i = 0; i < 4; i++) {
    if (machine.mode[pins[i]] == OUTPUT && machine.level[pins[i]] == HIGH) bits |= (uint8_t)(1 << i);
  }
  if (bits == 0) return;                       // 꺼짐: 움직이지 않음
  int phase = phaseOf(bits);
  if (phase < 0) { machine.badCoilPattern++; return; }
  int delta = (phase - lastPhase[axis] + 8) % 8;
  if (delta == 0) return;                      // 같은 자리를 다시 켬
  lastPhase[axis] = phase;
  if (delta != 1 && delta != 7) { machine.skippedPhase++; return; }   // 한 칸씩만 넘겨야 한다

  simNowUs += machine.cpuStepCostUs;
  if ((PORTA.value & 0x3F) != 0) {
    machine.stepsWhileSolenoidOn++;
  }
  if (lastStepUs[axis] >= 0) {
    double gap = (double)simNowUs - lastStepUs[axis];
    if (gap < machine.minStepIntervalUs[axis]) machine.minStepIntervalUs[axis] = gap;
  }
  lastStepUs[axis] = (double)simNowUs;

  int dir = ((delta == 1) != invert) ? 1 : -1;
  if (head) {
    machine.headStepCount++;
    long next = machine.headSteps + dir;
    if (next < lround(HEAD_HARD_MIN_MM * HEAD_STEPS_PER_MM) || next > lround(HEAD_HARD_MAX_MM * HEAD_STEPS_PER_MM)) {
      machine.headStall++;
    } else {
      machine.headSteps = next;
    }
  } else {
    machine.cardStepCount++;
    long next = machine.cardSteps + dir;
    if (next < 0) {
      machine.cardStallAtZero++;          // 멈춤 턱에 닿아 헛돎
    } else if (next > lround(CARD_HARD_MAX_MM * CARD_STEPS_PER_MM)) {
      machine.cardCrash++;
    } else {
      machine.cardSteps = next;
      if (machine.cardMm() > machine.cardMaxMm) machine.cardMaxMm = machine.cardMm();
    }
  }
}

// ─────────────────────────────────────────────
// Arduino 함수
// ─────────────────────────────────────────────
static void settleCoils();
unsigned long millis() { settleCoils(); simNowUs += 2; return (unsigned long)(uint32_t)(simNowUs / 1000); }
unsigned long micros() {
  settleCoils();
  simNowUs += 6;
  if (machine.usbKeyAtUs && simNowUs >= machine.usbKeyAtUs) {
    machine.usbKeyAtUs = 0;
    Serial.rx.push_back('q');
  }
  return (unsigned long)(uint32_t)simNowUs;
}
void delay(unsigned long ms) { simNowUs += (uint64_t)ms * 1000; }
void delayMicroseconds(unsigned int us) { simNowUs += us; }

void pinMode(uint8_t pin, uint8_t mode) { machine.mode[pin] = mode; }

void digitalWrite(uint8_t pin, uint8_t value) {
  simNowUs += 5;
  int before = machine.level[pin];
  machine.level[pin] = value ? HIGH : LOW;
  if (machine.mode[pin] != OUTPUT || before == machine.level[pin]) return;
  for (int i = 0; i < 4; i++) {
    if (pin == PIN_HEAD_IN[i]) coilsDirty[0] = true;
    if (pin == PIN_CARD_IN[i]) coilsDirty[1] = true;
  }
}

static void settleCoils() {
  for (int axis = 0; axis < 2; axis++) {
    if (coilsDirty[axis]) {
      coilsDirty[axis] = false;
      coilsChanged(axis);
    }
  }
}

int digitalRead(uint8_t pin) {
  simNowUs += 5;
  if (pin == PIN_HEAD_LIMIT) {
    bool pressed = machine.limitMode == LIMIT_STUCK ||
                   (machine.limitMode == LIMIT_NORMAL && machine.headSteps <= 0);
    return (pressed == HEAD_LIMIT_ACTIVE_LOW) ? LOW : HIGH;
  }
  if (pin == PIN_CARD_SENSOR) {
    return (machine.cardSensorSees == CARD_SENSOR_ACTIVE_LOW) ? LOW : HIGH;
  }
  if (machine.mode[pin] == INPUT_PULLUP || machine.mode[pin] == INPUT) {
    if (machine.level[pin] == 99) return LOW;       // 99 = 시험이 "버튼 누름"으로 표시한 값
    if (pin == PIN_BTN_CAPTURE && machine.capFromUs && simNowUs >= machine.capFromUs &&
        (!machine.capUntilUs || simNowUs < machine.capUntilUs)) {
      return LOW;
    }
    if (pin == PIN_BTN_PRINT) {
      bool held  = machine.heldFromUs && simNowUs >= machine.heldFromUs &&
                   (!machine.releaseAtUs || simNowUs < machine.releaseAtUs);
      bool again = machine.pressAgainAtUs && simNowUs >= machine.pressAgainAtUs;
      if (held || again) return LOW;
    }
    return HIGH;
  }
  return machine.level[pin];
}

// ─────────────────────────────────────────────
// 소리 모듈 대역 (speech.h 와 같은 이름, 내용 없음)
// ─────────────────────────────────────────────
void speechInit() {}
void speechPlayTone(uint16_t) {}
void speechPlayDC(uint8_t) {}
void speechStop() {}
bool speechIsPlaying() { return false; }
void speechStreamBegin() {}
uint16_t speechStreamFree() { return SPEECH_STREAM_BUFFER; }
bool speechStreamWrite(const uint8_t*, uint16_t) { return true; }
void speechStreamEnd() {}
bool speechStreamIsActive() { return false; }
bool speechStreamFinished() { return false; }
void speechBeep(SpeechBeep kind) {
  static const char* names[] = {"READY", "WRONG", "FAIL", "DONE", "LOW_BATTERY"};
  simBeeps.push_back(names[kind]);
}
void speechAlertPlay(uint8_t) { simBeeps.push_back("FAIL"); }
bool speechAlertActive() { return false; }
void speechUpdate() {}
