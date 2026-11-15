#include "stepper.h"
#include "pins.h"
#include "config.h"

struct Axis {
  uint8_t pinStep, pinDir, pinEn;
  bool    invert;
  float   stepsPerMm;
  float   vmax, vmin, accel;     // 스텝/초, 스텝/초^2
  long    maxSteps;
  long    pos, target;
  float   speed;                 // 지금 속도 (0 = 멈춤)
  float   jogSpeed;
  bool    constSpeed;
  int8_t  dir;
  unsigned long lastUs, intervalUs;
};

static Axis ax[2];
static bool motorsOn = false;
static bool homed = false;

static void setupAxis(Axis& a, uint8_t ps, uint8_t pd, uint8_t pe, bool inv,
                      float spm, float vmaxMm, float accMm, float travelMm) {
  a.pinStep = ps; a.pinDir = pd; a.pinEn = pe; a.invert = inv;
  a.stepsPerMm = spm;
  a.vmax  = vmaxMm * spm;
  a.vmin  = MIN_SPEED_MM_S * spm;
  a.accel = accMm * spm;
  a.maxSteps = (long)(travelMm * spm);
  a.pos = a.target = 0;
  a.speed = 0; a.jogSpeed = 0; a.constSpeed = false; a.dir = 1;
  a.lastUs = 0; a.intervalUs = 1000;
  pinMode(ps, OUTPUT); digitalWrite(ps, LOW);
  pinMode(pd, OUTPUT); digitalWrite(pd, LOW);
  pinMode(pe, OUTPUT);
}

void stepperInit() {
  setupAxis(ax[AX_X], PIN_X_STEP, PIN_X_DIR, PIN_X_EN, X_DIR_INVERT,
            X_STEPS_PER_MM, X_MAX_SPEED_MM_S, X_ACCEL_MM_S2, X_MAX_TRAVEL_MM);
  setupAxis(ax[AX_Y], PIN_Y_STEP, PIN_Y_DIR, PIN_Y_EN, Y_DIR_INVERT,
            Y_STEPS_PER_MM, Y_MAX_SPEED_MM_S, Y_ACCEL_MM_S2, Y_MAX_TRAVEL_MM);
  stepperEnable(false);
}

void stepperEnable(bool on) {
  motorsOn = on;
  uint8_t level = (on == MOTOR_EN_ACTIVE_LOW) ? LOW : HIGH;
  digitalWrite(PIN_X_EN, level);
  digitalWrite(PIN_Y_EN, level);
  if (!on) {                      // 전류를 끄면 위치를 믿을 수 없음
    ax[AX_X].target = ax[AX_X].pos; ax[AX_X].speed = 0;
    ax[AX_Y].target = ax[AX_Y].pos; ax[AX_Y].speed = 0;
    homed = false;
  }
}

bool stepperEnabled() { return motorsOn; }

static void updateAxis(Axis& a) {
  if (a.pos == a.target) { a.speed = 0; return; }
  unsigned long now = micros();
  int8_t want = (a.target > a.pos) ? 1 : -1;

  if (a.speed == 0 || want != a.dir) {          // 출발 (또는 방향 바꿈)
    a.dir = want;
    digitalWrite(a.pinDir, ((want > 0) != a.invert) ? HIGH : LOW);
    a.speed = a.constSpeed ? a.jogSpeed : a.vmin;
    if (a.speed < 1.0f) a.speed = 1.0f;
    a.intervalUs = (unsigned long)(1000000.0f / a.speed);
    a.lastUs = now;                             // 방향 신호가 자리 잡을 시간을 한 칸 줌
    return;
  }
  if ((unsigned long)(now - a.lastUs) < a.intervalUs) return;
  if ((unsigned long)(now - a.lastUs) > a.intervalUs * 4UL) a.lastUs = now;  // 많이 밀렸으면 다시 맞춤
  else a.lastUs += a.intervalUs;

  digitalWrite(a.pinStep, HIGH);
  delayMicroseconds(5);
  digitalWrite(a.pinStep, LOW);
  a.pos += a.dir;

  if (!a.constSpeed) {
    long dist = a.target - a.pos; if (dist < 0) dist = -dist;
    float dt = a.intervalUs * 0.000001f;
    float stopDist = (a.speed * a.speed) / (2.0f * a.accel);
    if ((float)dist <= stopDist) {
      a.speed -= a.accel * dt;
      if (a.speed < a.vmin) a.speed = a.vmin;
    } else if (a.speed < a.vmax) {
      a.speed += a.accel * dt;
      if (a.speed > a.vmax) a.speed = a.vmax;
    }
    a.intervalUs = (unsigned long)(1000000.0f / a.speed);
  }
}

void stepperUpdate() {
  updateAxis(ax[AX_X]);
  updateAxis(ax[AX_Y]);
}

void stepperMoveToMm(AxisId id, float mm) {
  Axis& a = ax[id];
  if (!motorsOn) { bool h = homed; stepperEnable(true); homed = h; }
  long t = (long)(mm * a.stepsPerMm + (mm >= 0 ? 0.5f : -0.5f));
  if (homed) {
    if (t < 0) t = 0;
    if (t > a.maxSteps) t = a.maxSteps;
  }
  a.constSpeed = false;
  a.target = t;
}

void stepperJogMm(AxisId id, float mm, float speedMmS) {
  Axis& a = ax[id];
  if (!motorsOn) stepperEnable(true);
  a.constSpeed = true;
  a.jogSpeed = speedMmS * a.stepsPerMm;
  a.speed = 0;                                   // 새 속도로 다시 출발
  a.target = a.pos + (long)(mm * a.stepsPerMm);
}

void stepperStop(AxisId id) {
  ax[id].target = ax[id].pos;
  ax[id].speed = 0;
}

bool  stepperBusy(AxisId id)    { return ax[id].pos != ax[id].target; }
bool  stepperAnyBusy()          { return stepperBusy(AX_X) || stepperBusy(AX_Y); }
float stepperPosMm(AxisId id)   { return ax[id].pos / ax[id].stepsPerMm; }
void  stepperSetPosMm(AxisId id, float mm) {
  ax[id].pos = ax[id].target = (long)(mm * ax[id].stepsPerMm);
  ax[id].speed = 0;
}
void  stepperSetHomed(bool h)   { homed = h; }
bool  stepperHomed()            { return homed; }
