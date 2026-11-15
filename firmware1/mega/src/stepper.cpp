/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * stepper.cpp — 스텝 모터 2개 움직이기 (헤드, 카드 공급 랙) — 28BYJ-48 + ULN2003
 *
 * 이 모터에는 코일이 4개 있고 ULN2003 보드의 IN1~IN4 가 하나씩 켜고 끕니다.
 * 켜는 코일을 아래 순서로 한 칸씩 바꾸면 한 스텝 돕니다 (하프 스텝: 1개 → 2개 → 1개 → …).
 *
 *      순서   0   1   2   3   4   5   6   7
 *      IN1    ●   ●   ·   ·   ·   ·   ·   ●
 *      IN2    ·   ●   ●   ●   ·   ·   ·   ·
 *      IN3    ·   ·   ·   ●   ●   ●   ·   ·
 *      IN4    ·   ·   ·   ·   ·   ●   ●   ●
 *
 * 순서를 앞으로 넘기면 한쪽으로, 거꾸로 넘기면 반대쪽으로 돕니다.
 * 넘기는 간격을 줄이면 빨라지지만, 이 모터는 초당 약 800번이 한계라 그보다 빠르면 떨기만 합니다.
 * 멈춰 있다가 갑자기 빠르게 돌려도 따라오지 못하므로, 천천히 출발해 조금씩 빠르게 하고 도착 전에 다시 늦춥니다.
 */

#include <Arduino.h>

#include "stepper.h"
#include "link.h"     // linkPoll()
#include "motion.h"   // 조정값
#include "pins.h"

// ─────────────────────────────────────────────
// 설정 (모터가 바뀌지 않으면 고칠 일이 없는 값)
// ─────────────────────────────────────────────
static const float    START_SPEED_STEPS = 300.0f;   // 출발·정지 속도 (스텝/초). 이보다 느린 이동은 처음부터 그 속도로
static const uint8_t  WAKE_MS           = 5;        // 코일을 켠 뒤 첫 스텝까지 기다리는 시간
static const uint8_t  LIMIT_HITS        = 3;        // 스위치가 이만큼 연속으로 눌려야 눌린 것으로 본다 (떨림 걸러내기)
static const uint8_t  STOP_ARM_MS       = 100;      // 버튼이 이만큼 떼어져 있어야 "다시 누름"을 멈춤으로 받는다
static const uint8_t  STOP_PRESS_MS     = 30;       // 버튼이 이만큼 계속 눌려야 멈춤으로 본다 (떨림 걸러내기)

// 하프 스텝 순서표. 비트 0 = IN1, 비트 1 = IN2, 비트 2 = IN3, 비트 3 = IN4
static const uint8_t HALF_STEP[8] = { 0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9 };

// ─────────────────────────────────────────────
// 축 하나의 상태
// ─────────────────────────────────────────────
struct Axis {
  const uint8_t* pins;   // IN1~IN4 핀 4개
  bool    invert;        // + 방향일 때 순서표를 거꾸로 넘길지
  float   stepsPerMm;
  float   maxSpeed;      // 스텝/초
  float   accel;         // 스텝/초²
  float   maxMm;         // 갈 수 있는 최대 거리
  long    pos;           // 지금 위치 (스텝)
  uint8_t phase;         // 순서표의 지금 자리 (0~7)
};

// 이동 하나 (축 하나가 몇 스텝을 어떤 속도로 가는 중인지). 두 축을 동시에 움직일 때는 두 개를 같이 돌린다
struct Run {
  Axis*    axis;
  long     remaining;    // 남은 스텝 수 (0 이면 끝)
  int8_t   dir;          // +1 / −1
  float    cur;          // 지금 속도 (스텝/초)
  float    startSpeed;
  float    speed;        // 가장 빠를 때의 속도
  uint32_t interval;     // 다음 스텝까지 (µs)
  uint32_t next;         // 다음 스텝을 보낼 시각 (micros)
};

enum RunResult : uint8_t { RUN_DONE, RUN_LIMIT, RUN_STOPPED };

static Axis axes[2];
static bool motorsOn   = false;
static bool headHomed  = false;   // 헤드 원점을 잡았는지 (코일을 끄면 false)
static bool cardZeroed = false;   // 랙 위치를 믿을 수 있는지
static bool stopped    = false;   // 움직이는 도중에 멈췄는지 (stepperClearStop 까지 유지)

// 버튼으로 멈추기: 뗐다가 다시 눌러야 멈춤 (출력 버튼을 누른 채 타각이 시작되므로)
static bool     stopArmed       = false;
static uint32_t stopReleasedAt  = 0;
static uint32_t stopPressedAt   = 0;

// 함수 선언
static void      setupAxis(Axis& a, const uint8_t* pins, bool invert,
                           float stepsPerMm, float maxSpeedMm, float accelMm, float maxMm);
static void      writeCoils(const Axis& a, uint8_t bits);
static void      coilsOff();
static void      runBegin(Run& r, Axis& a, long steps, float speed);
static void      runStep(Run& r);
static RunResult runLoop(Run* r1, Run* r2, bool stopOnLimit);
static RunResult runOne(Axis& a, long steps, float speed, bool stopOnLimit);
static bool      stopRequested();
static void      waitMs(uint16_t ms);
static long      mmToSteps(const Axis& a, float mm);
static float     clampMm(const Axis& a, float mm);


// ─────────────────────────────────────────────
// 시작
// ─────────────────────────────────────────────
void stepperInit() {
  setupAxis(axes[AXIS_HEAD], PIN_HEAD_IN, HEAD_DIR_INVERT,
            HEAD_STEPS_PER_MM, HEAD_MAX_SPEED_MM_S, HEAD_ACCEL_MM_S2, HEAD_MAX_TRAVEL_MM);
  setupAxis(axes[AXIS_CARD], PIN_CARD_IN, CARD_DIR_INVERT,
            CARD_STEPS_PER_MM, CARD_MAX_SPEED_MM_S, CARD_ACCEL_MM_S2, CARD_MAX_TRAVEL_MM);

  pinMode(PIN_HEAD_LIMIT, INPUT_PULLUP);    // 원점 스위치: 평소 HIGH, 눌리면 LOW
  pinMode(PIN_CARD_SENSOR, INPUT_PULLUP);   // 카드 감지 센서 (선택 사항)

  motorsOn   = false;
  headHomed  = false;
  cardZeroed = false;
  stopped    = false;
}


static void setupAxis(Axis& a, const uint8_t* pins, bool invert,
                      float stepsPerMm, float maxSpeedMm, float accelMm, float maxMm) {
  a.pins       = pins;
  a.invert     = invert;
  a.stepsPerMm = stepsPerMm;
  a.maxSpeed   = maxSpeedMm * stepsPerMm;
  a.accel      = accelMm * stepsPerMm;
  a.maxMm      = maxMm;
  a.pos        = 0;
  a.phase      = 0;

  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(pins[i], LOW);     // 코일 꺼짐
    pinMode(pins[i], OUTPUT);
  }
}


// 그 축의 코일 4개를 bits 대로 켜고 끈다 (비트 0 = IN1 …)
static void writeCoils(const Axis& a, uint8_t bits) {
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(a.pins[i], (bits & (1 << i)) ? HIGH : LOW);
  }
}


static void coilsOff() {
  writeCoils(axes[AXIS_HEAD], 0);
  writeCoils(axes[AXIS_CARD], 0);
  motorsOn  = false;
  headHomed = false;      // 전류를 끄면 위치를 믿지 않는다 (다음에 원점부터)
}


// ─────────────────────────────────────────────
// 코일 전류 켜기·끄기
// ─────────────────────────────────────────────
void stepperEnable(bool on) {
  if (on == motorsOn) {
    return;
  }
  if (on) {
    // 마지막에 켜 두었던 자리의 코일을 다시 켠다 (모터가 그 자리에서 버틴다)
    writeCoils(axes[AXIS_HEAD], HALF_STEP[axes[AXIS_HEAD].phase]);
    writeCoils(axes[AXIS_CARD], HALF_STEP[axes[AXIS_CARD].phase]);
    motorsOn = true;
    waitMs(WAKE_MS);
  } else {
    coilsOff();
  }
}


bool stepperEnabled() {
  return motorsOn;
}


// ─────────────────────────────────────────────
// 멈춤 표시
// ─────────────────────────────────────────────
bool stepperStopped() {
  return stopped;
}


void stepperClearStop() {
  stopped        = false;
  stopArmed      = false;   // 지금 누르고 있는 버튼은 멈춤으로 치지 않는다 (뗐다가 다시 눌러야 함)
  stopReleasedAt = 0;
  stopPressedAt  = 0;
}


// ─────────────────────────────────────────────
// 이동
// ─────────────────────────────────────────────
bool stepperMoveTo(StepAxis axis, float mm) {
  Axis& a = axes[axis];

  if (stopped) {
    return false;
  }
  if (axis == AXIS_HEAD && !headHomed) {
    return false;           // 원점을 모르는 채로 움직이면 끝에 부딪힌다
  }
  stepperEnable(true);
  return runOne(a, mmToSteps(a, clampMm(a, mm)) - a.pos, a.maxSpeed, false) == RUN_DONE;
}


bool stepperMoveBothTo(float headMm, float cardMm) {
  Axis& head = axes[AXIS_HEAD];
  Axis& card = axes[AXIS_CARD];

  if (stopped || !headHomed) {
    return false;
  }
  stepperEnable(true);

  Run r1, r2;
  runBegin(r1, head, mmToSteps(head, clampMm(head, headMm)) - head.pos, head.maxSpeed);
  runBegin(r2, card, mmToSteps(card, clampMm(card, cardMm)) - card.pos, card.maxSpeed);
  return runLoop(&r1, &r2, false) == RUN_DONE;
}


void stepperJog(StepAxis axis, float mm, float speedMmS) {
  Axis& a = axes[axis];
  float speed = speedMmS * a.stepsPerMm;
  if (speed > a.maxSpeed) {
    speed = a.maxSpeed;
  }
  if (stopped) {
    return;
  }
  stepperEnable(true);
  runOne(a, mmToSteps(a, mm), speed, false);
}


float stepperPos(StepAxis axis) {
  return axes[axis].pos / axes[axis].stepsPerMm;
}


// ─────────────────────────────────────────────
// 헤드 원점 잡기
// ─────────────────────────────────────────────
bool stepperHeadLimitPressed() {
  bool level = digitalRead(PIN_HEAD_LIMIT);
  return HEAD_LIMIT_ACTIVE_LOW ? !level : level;
}


bool stepperHeadHomed() {
  return headHomed;
}


bool stepperHomeHead() {
  Axis& a = axes[AXIS_HEAD];
  float fast = HOME_FAST_MM_S * a.stepsPerMm;
  float slow = HOME_SLOW_MM_S * a.stepsPerMm;

  headHomed = false;
  if (stopped) {
    return false;
  }
  stepperEnable(true);

  // ① 이미 눌려 있으면 먼저 물러난다
  if (stepperHeadLimitPressed()) {
    runOne(a, mmToSteps(a, HOME_BACKOFF_MM), fast, false);
    if (!stopped && stepperHeadLimitPressed()) {
      Serial.println(F("[모터] 헤드 원점 실패 — 물러나도 스위치가 계속 눌려 있음 (걸림·NC 배선·방향 확인)"));
      return false;
    }
  }

  // ② 스위치를 찾을 때까지 원점 쪽으로 (최대 거리 + 여유만큼만)
  RunResult found = runOne(a, -mmToSteps(a, HEAD_MAX_TRAVEL_MM + 10.0f), fast, true);
  if (found == RUN_DONE) {
    Serial.println(F("[모터] 헤드 원점 실패 — 끝까지 갔는데 스위치가 안 눌림 (D34 배선, HEAD_DIR_INVERT 확인)"));
  }
  if (found != RUN_LIMIT) {
    return false;
  }

  // ③ 조금 물러났다가
  runOne(a, mmToSteps(a, HOME_BACKOFF_MM), fast, false);
  if (stopped) {
    return false;
  }
  if (stepperHeadLimitPressed()) {
    Serial.println(F("[모터] 헤드 원점 실패 — 물러나도 스위치가 계속 눌려 있음 (걸림·NC 배선 확인)"));
    return false;
  }

  // ④ 천천히 다시 눌러 정확한 자리를 찾는다 → 여기가 0mm
  found = runOne(a, -mmToSteps(a, HOME_BACKOFF_MM + 2.0f), slow, true);
  if (found == RUN_DONE) {
    Serial.println(F("[모터] 헤드 원점 실패 — 다시 누를 때 스위치가 안 눌림"));
  }
  if (found != RUN_LIMIT) {
    return false;
  }
  a.pos     = 0;
  headHomed = true;

  // ⑤ 스위치에서 살짝 떨어져서 기다린다
  return stepperMoveTo(AXIS_HEAD, HOME_PULLOFF_MM);
}


// ─────────────────────────────────────────────
// 카드 랙 영점, 카드 감지
// ─────────────────────────────────────────────
void stepperZeroCard(bool full) {
  Axis& a = axes[AXIS_CARD];
  float speed = CARD_ZERO_SPEED_MM_S * a.stepsPerMm;

  if (stopped) {
    return;
  }
  stepperEnable(true);
  if (full) {
    // 어디 있는지 모르므로 최대 거리만큼 뒤로. 멈춤 턱에 닿으면 모터가 헛돌며 "드르륵" 소리가 난다
    runOne(a, -mmToSteps(a, CARD_MAX_TRAVEL_MM + 5.0f), speed, false);
  } else {
    runOne(a, -a.pos, a.maxSpeed, false);                         // 0mm 로
    runOne(a, -mmToSteps(a, CARD_ZERO_BUMP_MM), speed, false);    // 조금 더 밀어 붙이기
  }
  if (stopped) {
    return;               // 도중에 멈췄으면 영점을 정하지 않는다
  }
  a.pos      = 0;
  cardZeroed = true;
}


bool stepperCardZeroed() {
  return cardZeroed;
}


bool stepperCardPresent() {
  if (!CARD_SENSOR_INSTALLED) {
    return true;
  }
  bool level = digitalRead(PIN_CARD_SENSOR);
  return CARD_SENSOR_ACTIVE_LOW ? !level : level;
}


// ─────────────────────────────────────────────
// 스텝 보내기 — 이 파일의 핵심
// ─────────────────────────────────────────────

// 이동 하나를 준비한다. steps = 보낼 스텝 수(+ / −), speed = 가장 빠를 때의 속도(스텝/초)
static void runBegin(Run& r, Axis& a, long steps, float speed) {
  r.axis      = &a;
  r.dir       = (steps >= 0) ? 1 : -1;
  r.remaining = (steps >= 0) ? steps : -steps;
  r.speed     = speed;
  // 출발 속도에서 시작해 speed 까지 올린다. 느린 이동은 처음부터 그 속도로 (가감속 없음)
  r.startSpeed = (speed < START_SPEED_STEPS) ? speed : START_SPEED_STEPS;
  r.cur        = r.startSpeed;
  r.interval   = (uint32_t)(1000000.0f / r.cur);
  r.next       = micros() + r.interval;
}


// 한 스텝 보내고 다음 스텝의 속도·시각을 정한다
static void runStep(Run& r) {
  Axis& a = *r.axis;

  // ── 한 스텝: 순서표를 한 칸 넘겨 그 코일을 켠다 ──
  uint32_t steppedAt = micros();
  bool forward = (r.dir > 0) != a.invert;
  a.phase = (uint8_t)((a.phase + (forward ? 1 : 7)) & 7);
  writeCoils(a, HALF_STEP[a.phase]);
  a.pos += r.dir;
  r.remaining--;

  // ── 다음 속도: 남은 거리가 "멈추는 데 필요한 거리"보다 짧아지면 늦추고, 아니면 올린다 ──
  if (r.speed > r.startSpeed) {
    float dt       = r.interval * 0.000001f;                                         // 방금 지난 시간 (초)
    float stopDist = (r.cur * r.cur - r.startSpeed * r.startSpeed) / (2.0f * a.accel);   // 멈추는 데 필요한 스텝 수
    if ((float)r.remaining <= stopDist) {
      r.cur -= a.accel * dt;
      if (r.cur < r.startSpeed) {
        r.cur = r.startSpeed;
      }
    } else if (r.cur < r.speed) {
      r.cur += a.accel * dt;
      if (r.cur > r.speed) {
        r.cur = r.speed;
      }
    }
    r.interval = (uint32_t)(1000000.0f / r.cur);
  }

  // ── 다음 스텝 시각 ──
  // 원래 박자(next + interval)를 지키되, 이번 스텝이 늦게 나갔으면 다음 스텝을 너무 바짝 붙이지 않는다.
  // (이 모터는 스텝 간격이 갑자기 짧아지면 놓친다. 방금 스텝에서 최소 간격의 7/8 은 띄운다)
  uint32_t ideal   = r.next + r.interval;
  uint32_t soonest = steppedAt + r.interval - (r.interval >> 3);
  r.next = ((int32_t)(ideal - soonest) >= 0) ? ideal : soonest;
}


// 이동 하나 또는 둘을 끝날 때까지 돌린다.
//   stopOnLimit : true 면 헤드 원점 스위치가 눌리는 순간 끝낸다 (r1 이 헤드일 때만 씀)
//   돌려주는 값 : RUN_DONE 끝까지 감 / RUN_LIMIT 스위치가 눌려서 끝남 / RUN_STOPPED 멈춤 요청으로 끝남
static RunResult runLoop(Run* r1, Run* r2, bool stopOnLimit) {
  uint8_t hits = 0;

  while (r1->remaining > 0 || (r2 != nullptr && r2->remaining > 0)) {
    linkPoll();                       // 기다리는 동안 ESP32 에서 오는 바이트를 받는다

    if (stopRequested()) {
      coilsOff();
      stopped    = true;
      cardZeroed = false;             // 어디서 멈췄는지 모르므로 다음에 랙 영점을 끝까지 다시 맞춘다
      Serial.println(F("[모터] 멈춤 — 버튼 또는 USB 입력"));
      return RUN_STOPPED;
    }

    uint32_t now = micros();
    if (r1->remaining > 0 && (int32_t)(now - r1->next) >= 0) {
      if (stopOnLimit) {
        if (stepperHeadLimitPressed()) {
          hits++;
          if (hits >= LIMIT_HITS) {
            return RUN_LIMIT;
          }
        } else {
          hits = 0;
        }
      }
      runStep(*r1);
    }
    if (r2 != nullptr && r2->remaining > 0 && (int32_t)(now - r2->next) >= 0) {
      runStep(*r2);
    }
  }
  return RUN_DONE;
}


static RunResult runOne(Axis& a, long steps, float speed, bool stopOnLimit) {
  if (stopped) {
    return RUN_STOPPED;
  }
  Run r;
  runBegin(r, a, steps, speed);
  return runLoop(&r, nullptr, stopOnLimit);
}


// 움직이는 도중에 멈추라는 요청이 왔는지
//   USB : 아무 글자 (줄 끝 CR·LF 는 명령 뒤에 따라온 것일 수 있어 치지 않는다). 받은 글자는 버린다
//   버튼: 촬영·출력 아무거나. 뗐다가 다시 눌러야 한다
static bool stopRequested() {
  bool usb = false;
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c != '\r' && c != '\n') {
      usb = true;
    }
  }
  if (usb) {
    return true;
  }

  if (!MOTION_STOP_ON_BUTTON) {
    return false;
  }
  bool pressed = (digitalRead(PIN_BTN_CAPTURE) == LOW) || (digitalRead(PIN_BTN_PRINT) == LOW);
  uint32_t now = millis() | 1;        // 0 은 "아직 안 잼"으로 쓰므로 피한다
  if (!pressed) {
    stopPressedAt = 0;
    if (!stopArmed) {
      if (stopReleasedAt == 0) {
        stopReleasedAt = now;
      } else if (now - stopReleasedAt >= STOP_ARM_MS) {
        stopArmed = true;
      }
    }
    return false;
  }
  stopReleasedAt = 0;
  if (!stopArmed) {
    return false;                     // 타각을 시작시킨 버튼을 아직 누르고 있는 것
  }
  if (stopPressedAt == 0) {
    stopPressedAt = now;
    return false;
  }
  return (now - stopPressedAt) >= STOP_PRESS_MS;
}


// delay() 대신: 기다리는 동안에도 ESP32 에서 오는 바이트를 계속 받는다
static void waitMs(uint16_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    linkPoll();
  }
}


// mm → 스텝 (반올림)
static long mmToSteps(const Axis& a, float mm) {
  float s = mm * a.stepsPerMm;
  return (long)(s + (s >= 0.0f ? 0.5f : -0.5f));
}


// 0 ~ 최대 거리로 자른다
static float clampMm(const Axis& a, float mm) {
  if (mm < 0.0f) {
    return 0.0f;
  }
  if (mm > a.maxMm) {
    return a.maxMm;
  }
  return mm;
}
