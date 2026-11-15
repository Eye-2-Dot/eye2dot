// 가상 Mega: 핀에 가상 모터·스위치·솔레노이드·버튼을 달아 줍니다.
//   환경 변수로 고장·사고를 흉내 냅니다 (run_sim.py 가 정해 줌):
//     NO_SWITCH=1   원점 스위치가 눌리지 않음
//     DROP=0.01     보내는 바이트의 1% 를 깨뜨림
//     PRESS_AT=6    6초에 버튼을 0.2초 누름 (움직이는 중이면 "멈춤")
//     RESET_AT=6    6초에 Mega 가 다시 시작됨 (전원 흔들림, 시리얼 모니터를 열 때의 리셋)
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include <unistd.h>
void setup(); void loop();
void simLinkInit(HardwareSerial& s);
double simDropRate = 0;
Reg8 PORTA, DDRA;

static uint8_t level[70];
static long phys[2] = {600, 400};       // 스위치가 눌리는 지점에서 떨어진 실제 스텝 수
static bool noSwitch = false;           // 스위치가 고장 난 상황 시험
static FILE* punchLog = nullptr;
static unsigned long pressAt = 0, releaseAt = 0;
static unsigned long pressInPunchAfter = 0;   // 이 시각 뒤 처음 솔레노이드가 켜질 때 짧게(80ms) 누름

void simOnPrint(const char* t) {
  if (strstr(t, "버튼을 누르세요")) { pressAt = millis() + 600; releaseAt = pressAt + 200; }
}
// 솔레노이드가 켜지는 순간(0 -> 켜짐)마다 그때의 헤드 자리와 점 값을 적는다 (두 번 타각이면 두 줄)
void simOnPortA(uint8_t before, uint8_t after) {
  before &= 0x3F; after &= 0x3F;
  if (before == 0 && after != 0) {
    if (pressInPunchAfter && !pressAt && millis() >= pressInPunchAfter) { pressAt = millis() + 40; releaseAt = pressAt + 80; }
    fprintf(punchLog, "%.3f %.3f %d\n", phys[0] / X_STEPS_PER_MM, phys[1] / Y_STEPS_PER_MM, after);
    fflush(punchLog);
  }
}
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) {
  if (pin == PIN_X_LIMIT) return (!noSwitch && phys[0] <= 0) ? LOW : HIGH;
  if (pin == PIN_Y_LIMIT) return (!noSwitch && phys[1] <= 0) ? LOW : HIGH;
  if (pin == PIN_BTN_CONTINUE) { unsigned long m = millis(); return (pressAt && m >= pressAt && m < releaseAt) ? LOW : HIGH; }
  return level[pin];
}
void digitalWrite(uint8_t pin, uint8_t v) {
  uint8_t old = level[pin]; level[pin] = v;
  if (pin == PIN_X_STEP && v && !old && level[PIN_X_EN] == LOW) phys[0] += level[PIN_X_DIR] ? 1 : -1;
  if (pin == PIN_Y_STEP && v && !old && level[PIN_Y_EN] == LOW) phys[1] += level[PIN_Y_DIR] ? 1 : -1;
}
int main(int, char** argv) {
  punchLog = fopen(getenv("PUNCH_LOG") ? getenv("PUNCH_LOG") : "punch.log", "a");
  if (getenv("NO_SWITCH")) noSwitch = true;
  if (getenv("DROP")) simDropRate = atof(getenv("DROP"));
  double secs    = getenv("SIM_SECS") ? atof(getenv("SIM_SECS")) : 30;
  double resetAt = getenv("RESET_AT") ? atof(getenv("RESET_AT")) : 0;
  if (getenv("PRESS_IN_PUNCH")) pressInPunchAfter = (unsigned long)(atof(getenv("PRESS_IN_PUNCH")) * 1000);
  if (getenv("PRESS_AT")) { pressAt = (unsigned long)(atof(getenv("PRESS_AT")) * 1000); releaseAt = pressAt + 200; }
  PORTA.hook = true;
  simLinkInit(Serial1);
  setup();
  while (millis() < secs * 1000) {
    loop(); usleep(15);
    if (resetAt > 0 && millis() >= resetAt * 1000) {
      // 다시 시작: 같은 프로그램을 처음부터 다시 실행한다 (변수는 모두 처음 값, 가상 UART 는 그대로 이어짐)
      fputs("\n[가상 Mega] ---- 다시 시작됨 ----\n", stderr);
      char left[32]; snprintf(left, sizeof left, "%.1f", secs - resetAt);
      setenv("SIM_SECS", left, 1); unsetenv("RESET_AT");
      execv(argv[0], argv);
    }
  }
  return 0;
}
