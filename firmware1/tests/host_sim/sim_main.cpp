// 펌웨어 1 (ATmega 쪽) PC 모의 시험
//   mega/src 의 실제 코드(main·job·link·button·console·punch·solenoid·stepper)를 그대로 컴파일해서
//   가짜 기계에 연결하고, "ESP32 가 점자를 보냄 → 출력 버튼 → 타각 → 'D'" 를 처음부터 끝까지 돌려 본다.
//   소리(speech.cpp)만 AVR 타이머를 직접 쓰기 때문에 빈 함수로 바꿨다.
//
//   ./sim <시나리오>   (run.sh 가 차례로 돌린다. 시나리오 하나 = 전원을 한 번 켠 것)
#include <Arduino.h>

#include "link.h"
#include "motion.h"
#include "pins.h"
#include "protocol.h"
#include "punch.h"
#include "sim.h"
#include "solenoid.h"
#include "stepper.h"

void setup();
void loop();

static int failures = 0;

static void check(bool ok, const char* what) {
  printf("  %s %s\n", ok ? "[통과]" : "[실패]", what);
  if (!ok) failures++;
}

static void checkNear(double got, double want, double tol, const char* what) {
  bool ok = fabs(got - want) <= tol;
  printf("  %s %s: %.2f (기대 %.2f ±%.2f)\n", ok ? "[통과]" : "[실패]", what, got, want, tol);
  if (!ok) failures++;
}

// loop() 를 가짜 시간으로 ms 만큼 돌린다
static void runLoop(double ms) {
  uint64_t end = simNowUs + (uint64_t)(ms * 1000);
  while (simNowUs < end) {
    loop();
    simNowUs += 40;
  }
}

// ESP32 가 보내는 것과 똑같은 점자 프레임을 Serial1 로 넣는다 (common/protocol.h 의 같은 함수 사용)
static void sendBraille(const std::vector<uint8_t>& cells, uint8_t lineCount = 1) {
  uint8_t grid[BRAILLE_MAX_LINES][BRAILLE_MAX_CELLS] = {};
  uint8_t lens[BRAILLE_MAX_LINES] = {};
  for (uint8_t row = 0; row < lineCount; row++) {
    lens[row] = (uint8_t)cells.size();
    for (size_t i = 0; i < cells.size(); i++) grid[row][i] = cells[i];
  }
  uint8_t payload[BRAILLE_PAYLOAD_MAX];
  uint16_t n = linkEncodeBraille(&grid[0][0], BRAILLE_MAX_CELLS, lens, lineCount, payload);
  uint8_t frame[FRAME_MAX];
  uint16_t len = linkBuildFrame(FRAME_BRAILLE, payload, n, frame);
  for (uint16_t i = 0; i < len; i++) Serial1.rx.push_back(frame[i]);
}

static void pressPrint() {
  machine.level[PIN_BTN_PRINT] = 99;
  runLoop(100);                       // 타각은 이 안에서 처음부터 끝까지 진행된다 (loop 가 멈춤)
  machine.level[PIN_BTN_PRINT] = 0;
  runLoop(100);
}

static char lastReply() {
  for (size_t i = Serial1.tx.size(); i > 0; i--) {
    uint8_t b = Serial1.tx[i - 1];
    if (b == REPLY_DONE || b == REPLY_ERROR) return (char)b;
  }
  return '-';
}

// 두 모터의 코일 8개가 모두 꺼져 있는지
static bool motorsOff() {
  for (int i = 0; i < 4; i++) {
    if (machine.level[PIN_HEAD_IN[i]] != LOW || machine.level[PIN_CARD_IN[i]] != LOW) return false;
  }
  return true;
}

// 칸 col(0 = 첫 칸)을 찍을 때 헤드가 있어야 하는 자리 — punch.cpp 와 따로 계산해서 서로 확인한다
static double expectedMm(size_t col) {
  size_t slot = HEAD_HOME_AT_RIGHT ? col : (LABEL_MAX_CELLS - 1 - col);
  return HEAD_FIRST_CELL_MM + slot * CELL_PITCH_MM;
}

// 작업 하나를 넣고 출력 버튼을 눌러 끝까지 돌린다. 걸린 시간(초)을 돌려준다
static double runJob(const std::vector<uint8_t>& cells, uint8_t lineCount = 1) {
  Serial1.tx.clear();
  simBeeps.clear();
  machine.punches.clear();
  sendBraille(cells, lineCount);
  runLoop(AUDIO_CHUNK_MAX);           // 프레임 받기
  runLoop(3500);                      // 음성이 없는 작업 → 3초 뒤 출력 대기
  uint64_t t0 = simNowUs;
  pressPrint();
  return (simNowUs - t0) / 1e6 - 0.2;
}

// 찍힌 자리·순서·값이 맞는지
static void checkPunches(const std::vector<uint8_t>& cells) {
  std::vector<std::pair<size_t, uint8_t>> want;
  for (size_t i = 0; i < cells.size(); i++) {
    if (cells[i] & 0x3F) want.push_back({i, (uint8_t)(cells[i] & 0x3F)});
  }
  check(machine.punches.size() == want.size(), "찍은 횟수 = 빈칸이 아닌 칸 수");
  double worst = 0;
  bool masks = true, cardOk = true;
  for (size_t i = 0; i < want.size() && i < machine.punches.size(); i++) {
    const PunchEvent& p = machine.punches[i];
    double err = fabs(p.headMm - expectedMm(want[i].first));
    if (err > worst) worst = err;
    if (p.mask != want[i].second) masks = false;
    if (fabs(p.cardMm - CARD_FEED_MM) > 0.7) cardOk = false;
  }
  printf("  %s 칸 위치 오차 최대 %.3fmm (허용 0.05)\n", worst <= 0.05 ? "[통과]" : "[실패]", worst);
  if (worst > 0.05) failures++;
  check(masks, "칸마다 솔레노이드 값이 받은 값 그대로 (뒤집지 않음), 순서도 그대로");
  check(cardOk, "찍는 동안 카드 랙이 카드 공급 자리(CARD_FEED_MM)에 있음");
  if (!machine.punches.empty()) {
    printf("    첫 칸 %.2fmm → 마지막 %.2fmm (원점 스위치 = 0)\n",
           machine.punches.front().headMm, machine.punches.back().headMm);
  }
}

static void checkSafety() {
  check(machine.badCoilPattern == 0, "코일을 순서표에 없는 조합으로 켠 적 없음");
  check(machine.skippedPhase == 0, "순서표를 건너뛰지 않고 한 칸씩만 넘김");
  check(machine.stepsWhileSolenoidOn == 0, "솔레노이드가 눌린 채로 헤드·랙을 움직이지 않음");
  check(machine.cardCrash == 0, "카드 랙이 최대 거리 밖으로 나가지 않음");
  check(machine.maxSolenoidOnMs <= 50.0, "솔레노이드를 50ms 넘게 켜지 않음");
  check((PORTA.value & 0x3F) == 0, "끝난 뒤 솔레노이드 6개 모두 꺼짐");
  check(motorsOff(), "끝난 뒤 두 모터의 코일이 모두 꺼짐");
  double headRate = 1e6 / machine.minStepIntervalUs[0];
  double cardRate = 1e6 / machine.minStepIntervalUs[1];
  if (machine.headStepCount > 1) {
    printf("    헤드 가장 빠른 순간 %.0f스텝/초 = %.1fmm/초\n", headRate, headRate / HEAD_STEPS_PER_MM);
    check(headRate <= MOTOR_MAX_STEPS_PER_S * 1.2, "헤드 스텝 간격이 모터 한계 안");
  }
  if (machine.cardStepCount > 1) {
    printf("    랙 가장 빠른 순간 %.0f스텝/초 = %.1fmm/초\n", cardRate, cardRate / CARD_STEPS_PER_MM);
    check(cardRate <= MOTOR_MAX_STEPS_PER_S * 1.2, "랙 스텝 간격이 모터 한계 안");
  }
}

static const std::vector<uint8_t> FULL15 = {8, 63, 1, 2, 4, 16, 32, 21, 42, 7, 56, 9, 18, 36, 63};

// ─────────────────────────────────────────────
// 시나리오
// ─────────────────────────────────────────────
static void scenarioLabel(uint64_t startUs) {
  simPowerOn(startUs);
  setup();
  runLoop(200);
  check(motorsOff(), "전원을 켠 직후 두 모터 꺼짐");

  puts("── 작업 1: 15칸 (전원 켠 뒤 첫 작업 — 랙 영점을 끝까지 맞춤) ──");
  double sec = runJob(FULL15);
  printf("    걸린 시간 %.1f초\n", sec);
  check(lastReply() == 'D', "ESP32 에 'D'(완료)를 보냄");
  check(!simBeeps.empty() && simBeeps.back() == "DONE", "완료음");
  checkPunches(FULL15);
  check(machine.cardStallAtZero > 0, "랙 영점: 멈춤 턱에 밀어 붙임");
  checkNear(machine.cardMm(), 0.0, 0.05, "끝난 뒤 카드 랙 위치(mm)");
  checkNear(machine.headMm(), HOME_PULLOFF_MM, 0.05, "끝난 뒤 헤드 위치(mm)");
  check(machine.headStall == 0, "헤드가 레일 끝에 부딪히지 않음");

  puts("── 작업 2: 빈칸이 섞인 6칸 (랙은 조금만 밀어 붙임) ──");
  long stallBefore = machine.cardStallAtZero;
  std::vector<uint8_t> second = {8, 0, 63, 0, 0, 5};
  sec = runJob(second);
  printf("    걸린 시간 %.1f초\n", sec);
  check(lastReply() == 'D', "ESP32 에 'D'(완료)를 보냄");
  checkPunches(second);
  double bump = (machine.cardStallAtZero - stallBefore) / (double)CARD_STEPS_PER_MM;
  check(bump <= 2.0 * CARD_ZERO_BUMP_MM + 0.1, "두 번째 작업은 랙을 끝까지 뒤로 보내지 않음");
  checkNear(machine.cardMm(), 0.0, 0.05, "끝난 뒤 카드 랙 위치(mm)");
  checkSafety();
}

static void scenarioWorst() {
  machine.headStartMm = 125.0;
  machine.cardStartMm = 120.0;
  simPowerOn(0);
  setup();
  runLoop(200);
  puts("── 가장 오래 걸리는 경우: 헤드·랙이 반대쪽 끝에서 시작, 15칸 모두 6점 ──");
  std::vector<uint8_t> all(LABEL_MAX_CELLS, 63);
  double sec = runJob(all);
  double allowed = (LINK_REPLY_TIMEOUT_MS - LINK_JOB_TIMEOUT_MS) / 1000.0;
  printf("  %s 걸린 시간 %.1f초 (ESP32 가 더 기다려 주는 시간 %.0f초)\n",
         sec <= allowed ? "[통과]" : "[실패]", sec, allowed);
  if (sec > allowed) failures++;
  check(lastReply() == 'D', "ESP32 에 'D'(완료)를 보냄");
  checkPunches(all);
  checkSafety();
}

static void scenarioFault(LimitMode mode, const char* title) {
  machine.limitMode = mode;
  simPowerOn(0);
  setup();
  runLoop(200);
  puts(title);
  double sec = runJob(FULL15);
  printf("    걸린 시간 %.1f초\n", sec);
  check(lastReply() == 'E', "ESP32 에 'E'(실패)를 보냄");
  check(!simBeeps.empty() && simBeeps.back() == "FAIL", "실패음");
  check(machine.punches.empty(), "하나도 찍지 않음");
  check(machine.cardStepCount == 0, "카드를 내지 않음");
  check(motorsOff(), "두 모터 꺼짐");
  check(sec < 30.0, "30초 안에 포기");
}

static void scenarioBadJob() {
  simPowerOn(0);
  setup();
  runLoop(200);
  puts("── 16칸 (카드에 안 들어감) ──");
  std::vector<uint8_t> tooLong(LABEL_MAX_CELLS + 1, 63);
  runJob(tooLong);
  check(lastReply() == 'E', "ESP32 에 'E'(실패)를 보냄");
  check(machine.punches.empty() && machine.headStepCount == 0 && machine.cardStepCount == 0,
        "모터도 솔레노이드도 움직이지 않음");

  puts("── 두 줄 ──");
  runJob({63, 63}, 2);
  check(lastReply() == 'E', "ESP32 에 'E'(실패)를 보냄");
  check(machine.punches.empty() && machine.headStepCount == 0, "모터도 솔레노이드도 움직이지 않음");

  puts("── 빈칸뿐인 한 줄 ──");
  runJob({0, 0, 0});
  check(lastReply() == 'D', "ESP32 에 'D'(완료)를 보냄");
  check(machine.cardStepCount == 0, "카드를 내지 않음");

  puts("── 그 뒤 정상 작업 ──");
  runJob({8, 63});
  check(lastReply() == 'D', "ESP32 에 'D'(완료)를 보냄");
  checkPunches({8, 63});
  checkSafety();
}

static void scenarioNoCard() {
  if (!CARD_SENSOR_INSTALLED) {
    puts("  (CARD_SENSOR_INSTALLED = false 라 건너뜀)");
    return;
  }
  machine.cardSensorSees = false;
  simPowerOn(0);
  setup();
  runLoop(200);
  puts("── 카드 감지 센서가 카드를 못 봄 (적층함이 빔) ──");
  runJob(FULL15);
  check(lastReply() == 'E', "ESP32 에 'E'(실패)를 보냄");
  check(machine.punches.empty(), "하나도 찍지 않음");
  checkNear(machine.cardMm(), 0.0, 0.6, "랙을 되돌려 놓음(mm)");
  check(motorsOff(), "두 모터 꺼짐");
}

// 타각 도중에 멈추기: 출력 버튼을 눌러 시작 → 뗌 → 움직이는 중에 다시 누름
static void scenarioStop(bool byUsb) {
  simPowerOn(0);
  setup();
  runLoop(200);
  puts(byUsb ? "── 타각 도중 USB 로 글자를 보냄 ──" : "── 타각 도중 버튼을 뗐다가 다시 누름 ──");
  Serial1.tx.clear();
  simBeeps.clear();
  sendBraille(FULL15);
  runLoop(AUDIO_CHUNK_MAX);
  runLoop(3500);
  uint64_t t0 = simNowUs;
  if (byUsb) {
    machine.heldFromUs  = t0;                 // 버튼은 누른 채로 (멈춤으로 치면 안 됨)
    machine.usbKeyAtUs  = t0 + 9000000;       // 9초 뒤 USB 로 글자
  } else {
    machine.heldFromUs     = t0;
    machine.releaseAtUs    = t0 + 1500000;    // 1.5초 뒤 뗌
    machine.pressAgainAtUs = t0 + 9000000;    // 9초 뒤 다시 누름
  }
  runLoop(100);
  double sec = (simNowUs - t0) / 1e6;
  machine.heldFromUs = machine.releaseAtUs = machine.pressAgainAtUs = machine.usbKeyAtUs = 0;
  runLoop(300);
  printf("  %s 멈춘 시각 %.2f초 (요청 9.00초)\n", (sec > 9.0 && sec < 9.3) ? "[통과]" : "[실패]", sec);
  if (!(sec > 9.0 && sec < 9.3)) failures++;
  check(lastReply() == 'E', "ESP32 에 'E'(실패)를 보냄");
  check(!simBeeps.empty() && simBeeps.back() == "FAIL", "실패음");
  check(motorsOff(), "두 모터의 코일이 모두 꺼짐");
  check((PORTA.value & 0x3F) == 0, "솔레노이드 꺼짐");
  check(!stepperCardZeroed(), "랙 위치를 믿지 않음 (다음 작업에서 끝까지 다시 맞춤)");

  puts("── 그 뒤 정상 작업 ──");
  long stallBefore = machine.cardStallAtZero;
  runJob({8, 63});
  check(lastReply() == 'D', "ESP32 에 'D'(완료)를 보냄");
  checkPunches({8, 63});
  check(machine.cardStallAtZero - stallBefore > 100, "랙 영점을 끝까지 다시 맞춤");
  checkNear(machine.cardMm(), 0.0, 0.05, "끝난 뒤 카드 랙 위치(mm)");
}

static void usb(const char* keys, double ms = 400) {
  for (const char* k = keys; *k; k++) Serial.rx.push_back((uint8_t)*k);
  runLoop(ms);
}

static void scenarioConsole() {
  simPowerOn(0);
  Serial.echo = true;
  setup();
  runLoop(100);
  usb("S");
  usb("H");
  check(stepperHeadHomed(), "H: 헤드 원점 잡음");
  checkNear(machine.headMm(), HOME_PULLOFF_MM, 0.05, "H 뒤 헤드 위치(mm)");
  usb("G");
  checkNear(machine.headMm(), HEAD_FIRST_CELL_MM, 0.05, "G 뒤 헤드 위치(mm)");
  usb(">"); usb(">"); usb(">");          // 한 글자씩 (움직이는 중에 온 글자는 "멈춤"이 된다)
  checkNear(machine.headMm(), HEAD_FIRST_CELL_MM + 3.0, 0.05, ">>> 뒤 헤드 위치(mm)");
  usb("<");
  checkNear(machine.headMm(), HEAD_FIRST_CELL_MM + 2.0, 0.05, "< 뒤 헤드 위치(mm)");
  usb("F");
  checkNear(machine.cardMm(), machine.cardStartMm, 0.05, "Z 전의 F 는 거절 — 랙이 움직이지 않음(mm)");
  usb("Z");
  checkNear(machine.cardMm(), 0.0, 0.05, "Z 뒤 랙 위치(mm)");
  usb("F");
  checkNear(machine.cardMm(), CARD_FEED_MM, 0.05, "F 뒤 랙 위치(mm)");
  usb("."); usb(","); usb(",");
  checkNear(machine.cardMm(), CARD_FEED_MM - 1.0, 0.05, ". , , 뒤 랙 위치(mm)");
  usb("B");
  checkNear(machine.cardMm(), 0.0, 0.05, "B 뒤 랙 위치(mm)");
  usb("M");
  check(motorsOff(), "M: 두 모터 꺼짐");
  machine.punches.clear();
  usb("T");
  check(machine.punches.size() == 8, "T: 시험 라벨 8번 찍음 (1~6번 하나씩 + 6점 2번)");
  if (machine.punches.size() == 8) {
    bool order = true;
    for (int i = 0; i < 6; i++) order = order && machine.punches[i].mask == (1 << i);
    check(order && machine.punches[6].mask == 63 && machine.punches[7].mask == 63, "T: 솔레노이드 순서 1→6, 그다음 6점");
    checkNear(machine.punches[7].headMm, expectedMm(LABEL_MAX_CELLS - 1), 0.05, "T: 마지막 칸 자리(mm)");
  }
  usb("?");
  checkSafety();
}

int main(int argc, char** argv) {
  std::string name = argc > 1 ? argv[1] : "label";
  printf("\n===== 시나리오: %s =====\n", name.c_str());

  if (name == "label")         scenarioLabel(0);
  else if (name == "wrap")     scenarioLabel(4294967296ULL - 9000000ULL);   // 타각 도중에 micros() 가 0 으로 넘어감
  else if (name == "worst")    scenarioWorst();
  else if (name == "noswitch") scenarioFault(LIMIT_NEVER, "── 원점 스위치가 안 눌림 (선이 빠졌거나 방향이 반대) ──");
  else if (name == "stuck")    scenarioFault(LIMIT_STUCK, "── 원점 스위치가 계속 눌려 있음 (걸림·NC 배선) ──");
  else if (name == "badjob")   scenarioBadJob();
  else if (name == "nocard")   scenarioNoCard();
  else if (name == "console")  scenarioConsole();
  else if (name == "stopbtn")  scenarioStop(false);
  else if (name == "stopusb")  scenarioStop(true);
  else { printf("모르는 시나리오\n"); return 2; }

  printf("===== %s: %s =====\n", name.c_str(), failures == 0 ? "모두 통과" : "실패 있음");
  return failures == 0 ? 0 : 1;
}
