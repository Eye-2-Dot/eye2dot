/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * punch.cpp — 점자 타각 (라벨 카드 한 장)
 *
 * 순서
 *   ① 헤드 원점 잡기              (스위치 D34)
 *   ② 카드 랙 영점 → 카드 한 장을 고정 벽까지 밀기
 *   ③ 칸마다: 헤드를 그 칸 위로 → 잠깐 기다림 → 솔레노이드로 찍기   (빈칸은 건너뜀)
 *   ④ 헤드를 원점 옆 대기 자리로
 *   ⑤ 카드 랙을 영점으로 → 모터 끄기
 *
 * 핀은 직접 만지지 않습니다. 모터는 stepper, 솔레노이드는 solenoid 가 맡습니다.
 * 거리·속도 같은 조정값은 motion.h 에 있습니다.
 */

#include <Arduino.h>

#include "punch.h"
#include "motion.h"
#include "solenoid.h"
#include "stepper.h"

// 원점 스위치가 왼쪽 끝일 때(HEAD_HOME_AT_RIGHT = false): 첫 칸만 반대 방향에서 다가가게 되어
// 벨트 유격만큼 어긋난다. 그래서 첫 칸보다 이만큼 더 갔다가 되돌아와, 모든 칸을 같은 방향에서 다가간다.
constexpr float APPROACH_EXTRA_MM = 2.0f;
static_assert(HEAD_HOME_AT_RIGHT ||
              HEAD_FIRST_CELL_MM + (LABEL_MAX_CELLS - 1) * CELL_PITCH_MM + APPROACH_EXTRA_MM <= HEAD_MAX_TRAVEL_MM,
              "원점 스위치가 왼쪽 끝일 때는 마지막 칸 뒤로 2mm 여유가 더 필요합니다 (HEAD_MAX_TRAVEL_MM)");

// ─────────────────────────────────────────────
// 가장 오래 걸릴 때의 타각 시간 — 컴파일할 때 검사
//   ATmega 가 시간 초과(LINK_JOB_TIMEOUT_MS) 직전에 출력 버튼을 받아 타각하면 그만큼 늦게 'D' 가 나간다.
//   그 시간이 ESP32 의 기다림(LINK_REPLY_TIMEOUT_MS)과의 차이 안에 들어가야 한다.
//   속도·거리(motion.h)나 솔레노이드 시간(solenoid.h)을 고쳐 여기서 멈추면
//   common/protocol.h 의 LINK_REPLY_TIMEOUT_MS 를 함께 늘리고 mega·cam 을 둘 다 다시 올릴 것.
// ─────────────────────────────────────────────
// 거리 mm 를 최고 속도 v, 가속 a 로 갈 때 걸리는 시간의 상한 (가속·감속 구간 포함)
constexpr float moveSec(float mm, float v, float a) {
  return mm / v + v / a;
}

// 솔레노이드가 한 칸을 찍는 시간. feature/firmware1/solenoid-final(두 번 타각)을 합치면
// 이 줄을 solenoid.h 의 SOL_CELL_MS 로 바꾼다 (지금 값 230ms → 320ms, 아래 여유 안에 들어간다)
constexpr float SOL_CELL_SEC = (SOL_PULSE_MS + SOL_COOLDOWN_MS) / 1000.0f;

constexpr float PUNCH_WORST_SEC =
    // ① 헤드 원점: 가장 먼 곳에서 찾아오기 + 물러나기 2번 + 천천히 다시 누르기 + 대기 자리
    moveSec(HEAD_MAX_TRAVEL_MM + 10.0f, HOME_FAST_MM_S, HEAD_ACCEL_MM_S2)
    + 2.0f * moveSec(HOME_BACKOFF_MM, HOME_FAST_MM_S, HEAD_ACCEL_MM_S2)
    + (HOME_BACKOFF_MM + 2.0f) / HOME_SLOW_MM_S
    + moveSec(HOME_PULLOFF_MM, HEAD_MAX_SPEED_MM_S, HEAD_ACCEL_MM_S2)
    // ② 랙 영점(끝까지 뒤로) + 카드 밀기  (헤드가 첫 칸으로 가는 것은 카드 밀기와 동시지만 따로 센다)
    + moveSec(CARD_MAX_TRAVEL_MM + 5.0f, CARD_ZERO_SPEED_MM_S, CARD_ACCEL_MM_S2)
    + moveSec(CARD_FEED_MM, CARD_MAX_SPEED_MM_S, CARD_ACCEL_MM_S2)
    // ③ 헤드가 가장 먼 칸까지 갔다 오는 거리 (첫 칸까지 + 칸 사이 + 돌아오기를 모두 합쳐도 왕복을 넘지 않는다)
    + 2.0f * moveSec(HEAD_FIRST_CELL_MM + (LABEL_MAX_CELLS - 1) * CELL_PITCH_MM + APPROACH_EXTRA_MM,
                     HEAD_MAX_SPEED_MM_S, HEAD_ACCEL_MM_S2)
    // ④ 칸마다: 출발·정지에 드는 시간 + 기다림 + 솔레노이드
    + LABEL_MAX_CELLS * (HEAD_MAX_SPEED_MM_S / HEAD_ACCEL_MM_S2 + HEAD_SETTLE_MS / 1000.0f + SOL_CELL_SEC)
    // ⑤ 랙 되돌리기 + 밀어 붙이기  (헤드 돌아오기와 동시지만 따로 센다)
    + moveSec(CARD_FEED_MM, CARD_MAX_SPEED_MM_S, CARD_ACCEL_MM_S2)
    + moveSec(CARD_ZERO_BUMP_MM, CARD_ZERO_SPEED_MM_S, CARD_ACCEL_MM_S2);

// 계산값의 1.1배 + 3초(시리얼 로그·코일 켜기·두 번 타각 여유)를 본다
static_assert(PUNCH_WORST_SEC * 1.1f + 3.0f <= (LINK_REPLY_TIMEOUT_MS - LINK_JOB_TIMEOUT_MS) / 1000.0f,
              "타각 시간이 'D' 대기 여유(LINK_REPLY_TIMEOUT_MS - LINK_JOB_TIMEOUT_MS)보다 깁니다");

// 함수 선언
static bool  fitsOnCard(const BrailleJob& job);
static float cellPosMm(uint8_t col);
static void  printCell(uint8_t col, uint8_t mask);
static bool  failPunch(const __FlashStringHelper* why);
static void  waitMs(uint16_t ms);


void punchInit() {
  solenoidInit();   // 솔레노이드 6개 출력 + 모두 끔 (가장 먼저)
  stepperInit();    // 스텝 모터 핀 출력 + 코일 모두 끔, 원점 스위치·카드 센서 입력
}


bool punchBraille(const BrailleJob& job) {
  Serial.println(F("[타각] 시작 — 멈추려면 버튼을 다시 누르거나 USB 로 아무 글자"));
  stepperClearStop();

  // ── 카드에 들어가는지 ── (서버가 이미 걸러 주지만, 잘못된 점자를 찍느니 실패로 끝낸다)
  if (!fitsOnCard(job)) {
    return failPunch(F("카드 한 장에 들어가지 않는 점자"));
  }

  // 찍을 칸을 따로 적어 둔다. 타각 중에도 linkPoll() 이 돌아서, 그사이 새 점자 프레임이 오면
  // job(link 의 배열)이 바뀔 수 있기 때문이다.
  uint8_t cellCount = job.lineLen[0];
  uint8_t cells[LABEL_MAX_CELLS];
  bool    anyDot = false;
  for (uint8_t col = 0; col < cellCount; col++) {
    cells[col] = job.cells[0][col] & 0x3F;
    if (cells[col] != 0) {
      anyDot = true;
    }
  }
  // 찍을 점이 하나도 없으면(빈칸뿐) 카드를 쓰지 않고 끝낸다
  if (!anyDot) {
    Serial.println(F("[타각] 끝 — 찍을 점이 없어 카드를 내지 않음"));
    return true;
  }

  // 점이 있는 첫 칸 (헤드를 카드 밀기와 동시에 미리 보내 둘 자리)
  uint8_t firstCol = 0;
  while (cells[firstCol] == 0) {
    firstCol++;
  }
  float firstMm = cellPosMm(firstCol) + (HEAD_HOME_AT_RIGHT ? 0.0f : APPROACH_EXTRA_MM);

  // ── ① 헤드 원점 ──
  Serial.println(F("  헤드 원점 잡는 중"));
  if (!stepperHomeHead()) {
    return failPunch(stepperStopped() ? F("멈춤 요청") : F("헤드 원점을 못 잡음"));
  }

  // ── ② 카드 한 장 공급 (그동안 헤드는 첫 칸 쪽으로) ──
  bool fullZero = CARD_FULL_ZERO_EVERY_JOB || !stepperCardZeroed();
  Serial.println(fullZero ? F("  카드 랙 영점 (끝까지 뒤로 — 턱에 닿으면 드르륵 소리가 납니다)")
                          : F("  카드 랙 영점"));
  stepperZeroCard(fullZero);

  Serial.println(F("  카드 한 장 미는 중"));
  if (!stepperMoveBothTo(firstMm, CARD_FEED_MM)) {
    return failPunch(F("멈춤 요청"));
  }
  if (!stepperCardPresent()) {
    stepperMoveBothTo(HOME_PULLOFF_MM, 0.0f);
    return failPunch(F("카드가 안 보임 (적층함이 비었거나 걸림)"));
  }

  // ── ③ 칸마다 헤드를 옮겨 찍기 ──
  Serial.print(F("  1줄 ("));
  Serial.print(cellCount);
  Serial.println(F("칸)"));

  for (uint8_t col = 0; col < cellCount; col++) {
    uint8_t mask = cells[col];
    printCell(col, mask);
    if (mask == 0) {
      continue;   // 빈칸은 헤드를 세우지 않고 지나간다
    }
    if (!stepperMoveTo(AXIS_HEAD, cellPosMm(col))) {
      return failPunch(F("멈춤 요청"));
    }
    waitMs(HEAD_SETTLE_MS);        // 흔들림이 가라앉을 시간
    solenoidPunchCell(mask);       // 이 칸의 점을 동시에 찍기 (핀이 돌아올 시간까지 기다림)
  }
  solenoidAllOff();

  // ── ④ 헤드를 대기 자리로, ⑤ 랙을 영점으로 (동시에) ──
  Serial.println(F("  헤드·랙 되돌리는 중"));
  if (!stepperMoveBothTo(HOME_PULLOFF_MM, 0.0f)) {
    return failPunch(F("멈춤 요청"));
  }
  stepperZeroCard(false);
  if (stepperStopped()) {
    return failPunch(F("멈춤 요청"));
  }
  stepperEnable(false);   // 코일 전류 끄기 (발열·배터리)

  Serial.println(F("[타각] 끝 — 카드를 꺼내세요"));
  return true;
}


bool punchTestLabel() {
  BrailleJob job;
  job.lineCount  = 1;
  job.lineLen[0] = LABEL_MAX_CELLS;
  for (uint8_t col = 0; col < LABEL_MAX_CELLS; col++) {
    job.cells[0][col] = 0;
  }
  for (uint8_t dot = 0; dot < 6; dot++) {
    job.cells[0][dot] = (uint8_t)(1 << dot);   // 1~6번째 칸: 솔레노이드 1~6 을 하나씩
  }
  job.cells[0][6]                   = 0x3F;    // 7번째 칸: 6개 모두
  job.cells[0][LABEL_MAX_CELLS - 1] = 0x3F;    // 마지막 칸: 6개 모두
  return punchBraille(job);
}


// ─────────────────────────────────────────────
// 내부 함수
// ─────────────────────────────────────────────

// 카드 한 장(한 줄, LABEL_MAX_CELLS 칸)에 들어가는지
static bool fitsOnCard(const BrailleJob& job) {
  if (job.lineCount != 1) {
    Serial.print(F("  줄 수 "));
    Serial.print(job.lineCount);
    Serial.println(F(" — 라벨 카드는 한 줄만 찍습니다"));
    return false;
  }
  if (job.lineLen[0] > LABEL_MAX_CELLS) {
    Serial.print(F("  칸 수 "));
    Serial.print(job.lineLen[0]);
    Serial.print(F(" — 카드 한 장은 최대 "));
    Serial.print(LABEL_MAX_CELLS);
    Serial.println(F("칸"));
    return false;
  }
  return true;
}


// 줄의 col 번째 칸(0 = 첫 칸)을 찍을 때 헤드가 있어야 하는 자리 (원점에서 mm)
//   첫 칸은 위에서 볼 때 오른쪽 끝, 칸마다 왼쪽으로 (punch.h "찍는 방향")
static float cellPosMm(uint8_t col) {
  uint8_t slot = HEAD_HOME_AT_RIGHT ? col : (uint8_t)(LABEL_MAX_CELLS - 1 - col);
  return HEAD_FIRST_CELL_MM + slot * CELL_PITCH_MM;
}


// "    칸 3 = 7 -> 솔레노이드 1,2,3 @ 40.0mm"
static void printCell(uint8_t col, uint8_t mask) {
  Serial.print(F("    칸 "));
  Serial.print(col + 1);
  Serial.print(F(" = "));
  Serial.print(mask);
  Serial.print(F(" -> 솔레노이드 "));

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
    Serial.println(F("없음 (빈칸)"));
    return;
  }
  Serial.print(F(" @ "));
  Serial.print(cellPosMm(col), 1);
  Serial.println(F("mm"));
}


// 실패로 끝낼 때: 솔레노이드와 모터를 끄고 이유를 찍는다. 항상 false 를 돌려준다
static bool failPunch(const __FlashStringHelper* why) {
  solenoidAllOff();
  stepperEnable(false);
  Serial.print(F("[타각] 실패 — "));
  Serial.println(why);
  return false;
}


// delay() 대신: 기다리는 동안에도 ESP32 에서 오는 바이트를 계속 받는다
static void waitMs(uint16_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    linkPoll();
  }
}
