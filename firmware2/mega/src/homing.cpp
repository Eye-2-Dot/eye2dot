#include "homing.h"
#include "pins.h"
#include "config.h"
#include "protocol.h"

enum HState : uint8_t { H_IDLE, H_FAST, H_BACK, H_SLOW, H_PULLOFF, H_DONE, H_FAIL };

static HState  st = H_IDLE;
static AxisId  cur = AX_X;
static uint8_t err = ERR_NONE;
static uint8_t hit[2] = {0, 0};

bool limitRaw(AxisId a) {
  bool v = digitalRead(a == AX_X ? PIN_X_LIMIT : PIN_Y_LIMIT);
  return LIMIT_ACTIVE_LOW ? !v : v;
}
bool limitPressed(AxisId a) { return hit[a] >= 3; }

static void sample() {
  for (uint8_t i = 0; i < 2; i++) {
    if (limitRaw((AxisId)i)) { if (hit[i] < 3) hit[i]++; }
    else hit[i] = 0;
  }
}

static float travel(AxisId a) { return a == AX_X ? X_MAX_TRAVEL_MM : Y_MAX_TRAVEL_MM; }

static void fail(uint8_t e) {
  stepperStop(AX_X); stepperStop(AX_Y);
  err = e; st = H_FAIL;
}

static void beginAxis(AxisId a) {
  cur = a;
  if (limitRaw(a)) {                       // 이미 눌려 있으면 먼저 물러남
    stepperJogMm(a, HOME_BACKOFF_MM, HOME_FAST_MM_S);
    st = H_BACK;
  } else {
    stepperJogMm(a, -(travel(a) + 15.0f), HOME_FAST_MM_S);
    st = H_FAST;
  }
}

void homingInit() {
  pinMode(PIN_X_LIMIT, INPUT_PULLUP);
  pinMode(PIN_Y_LIMIT, INPUT_PULLUP);
  st = H_IDLE; err = ERR_NONE;
}

void homingStart() {
  err = ERR_NONE;
  stepperSetHomed(false);
  stepperEnable(true);
  hit[0] = hit[1] = 0;
  beginAxis(AX_X);
}

void homingAbort() {
  stepperStop(AX_X); stepperStop(AX_Y);
  st = H_IDLE;
}

void homingUpdate() {
  sample();
  switch (st) {
    case H_FAST:
      if (limitPressed(cur)) {
        stepperStop(cur);
        stepperJogMm(cur, HOME_BACKOFF_MM, HOME_FAST_MM_S);
        st = H_BACK;
      } else if (!stepperBusy(cur)) {
        fail(cur == AX_X ? ERR_HOME_X : ERR_HOME_Y);     // 끝까지 갔는데 스위치가 안 눌림
      }
      break;
    case H_BACK:
      if (!stepperBusy(cur)) {
        if (limitRaw(cur)) fail(ERR_SWITCH_STUCK);
        else { stepperJogMm(cur, -(HOME_BACKOFF_MM + 3.0f), HOME_SLOW_MM_S); st = H_SLOW; }
      }
      break;
    case H_SLOW:
      if (limitPressed(cur)) {
        stepperStop(cur);
        stepperSetPosMm(cur, 0.0f);                       // 여기가 원점
        stepperJogMm(cur, HOME_PULLOFF_MM, HOME_FAST_MM_S);
        st = H_PULLOFF;
      } else if (!stepperBusy(cur)) {
        fail(cur == AX_X ? ERR_HOME_X : ERR_HOME_Y);
      }
      break;
    case H_PULLOFF:
      if (!stepperBusy(cur)) {
        if (cur == AX_X) beginAxis(AX_Y);
        else { stepperSetHomed(true); st = H_DONE; }
      }
      break;
    default: break;
  }
}

bool    homingBusy()   { return st == H_FAST || st == H_BACK || st == H_SLOW || st == H_PULLOFF; }
bool    homingDone()   { return st == H_DONE; }
bool    homingFailed() { return st == H_FAIL; }
uint8_t homingError()  { return err; }
