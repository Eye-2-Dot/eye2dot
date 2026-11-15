/*
 * Eye 2 Dot — 펌웨어 1 부품 시험: 스텝 모터 2개 (stepper_test)
 *
 *   모터 A = 헤드       (pins.h: PIN_HEAD_IN[4] = D36~D39 → ULN2003 IN1~IN4, 원점 스위치 PIN_HEAD_LIMIT)
 *   모터 B = 카드 랙    (pins.h: PIN_CARD_IN[4] = D40~D43 → ULN2003 IN1~IN4)
 *   모터는 28BYJ-48 (5V), 드라이버는 ULN2003 보드.
 *
 * 모터가 도는지, 방향이 맞는지, 1mm 가 몇 스텝인지 잰다.
 * 스텝 수·방향은 완성 펌웨어와 같은 firmware1/mega/include/motion.h 의 값을 쓴다
 * — 여기서 맞는 값을 찾으면 motion.h 를 고친다 (이 파일에는 고칠 숫자가 없다).
 *
 * 움직이는 동안에만 코일을 켜고, 끝나면 바로 꺼서 모터·드라이버가 뜨거워지지 않게 한다.
 * 이동 중에 아무 키나 누르면 멈춘다. 이 시험은 원점 스위치를 "읽어 보여 주기만" 하고
 * 스위치에서 멈추지 않는다 — 끝에 부딪히지 않게 짧은 거리부터 움직일 것.
 *
 * 시리얼 명령 (입력 뒤 Enter)
 *   a        : 축 바꾸기 (헤드 ↔ 카드 랙)
 *   s        : 한 스텝 (마지막 방향으로)
 *   f / b    : 한 바퀴 + 방향 / − 방향 (약 6~8초)
 *   + / -    : 속도 올리기 / 내리기
 *   m25      : 25mm + 방향,  m-10 : 10mm − 방향
 *   l        : 원점 스위치(D34) 상태
 *   ?        : 도움말과 지금 설정
 *
 *   + 방향 = 헤드는 원점 스위치에서 멀어지는 쪽, 카드 랙은 카드 고정 벽 쪽 (motion.h)
 */

#include <Arduino.h>

#include "mega_test.h"
#include "motion.h"   // 스텝/mm, 방향 — 완성 펌웨어와 같은 값

// ─────────────────────────────────────────────
// 이 시험만의 설정
// ─────────────────────────────────────────────
const uint16_t SPEED_DEFAULT = 500;    // 스텝/초 (헤드 약 5mm/초, 카드 랙 약 8mm/초)
const uint16_t SPEED_MIN     = 100;
const uint16_t SPEED_MAX     = (uint16_t)MOTOR_MAX_STEPS_PER_S;   // 이 모터의 한계. 가감속 없이 바로 출발하므로 떨면 낮출 것
const uint16_t SPEED_STEP    = 50;     // + / - 한 번에 바뀌는 양
const float    MAX_MOVE_MM   = 150;    // 한 번 이동의 최대 거리 (잘못 입력해도 끝없이 돌지 않게)

// 하프 스텝 순서표 (stepper.cpp 와 같다). 비트 0 = IN1 … 비트 3 = IN4
const uint8_t HALF_STEP[8] = { 0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9 };

// ─────────────────────────────────────────────
// 축 두 개
// ─────────────────────────────────────────────
struct TestAxis {
  const __FlashStringHelper* name;
  const uint8_t* pins;  // IN1~IN4
  bool    invert;       // motion.h 의 *_DIR_INVERT
  float   stepsPerMm;   // motion.h 의 *_STEPS_PER_MM
  uint8_t phase;        // 순서표의 지금 자리
};

TestAxis axes[2];
uint8_t  cur      = 0;                 // 지금 고른 축 (0 = 헤드, 1 = 카드 랙)
uint16_t speedSps = SPEED_DEFAULT;
bool     forward  = true;              // 마지막 방향 (s 가 이 방향으로 간다)

const uint32_t STEPS_PER_TURN = (uint32_t)MOTOR_STEPS_PER_REV;

void writeCoils(const TestAxis& a, uint8_t bits) {
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(a.pins[i], (bits & (1 << i)) ? HIGH : LOW);
  }
}

bool limitPressed() {
  bool level = digitalRead(PIN_HEAD_LIMIT);
  return HEAD_LIMIT_ACTIVE_LOW ? !level : level;
}

// steps 만큼 움직인다. 아무 키나 오면 멈춘다. 돌려주는 값 = 실제로 움직인 스텝 수
uint32_t moveSteps(uint32_t steps, bool fwd) {
  TestAxis& a = axes[cur];
  forward = fwd;
  writeCoils(a, HALF_STEP[a.phase]);   // 코일 켜기 (지금 자리)
  delay(5);

  const uint32_t intervalUs = 1000000UL / speedSps;
  uint32_t next  = micros() + intervalUs;
  uint32_t moved = 0;
  while (moved < steps) {
    if (Serial.available() > 0) {   // 아무 키나 누르면 멈춤
      int c = Serial.read();
      // 명령 뒤에 따라온 줄 끝(Enter 의 CR·LF 중 남은 것)은 키로 치지 않는다
      if (c != '\r' && c != '\n') {
        while (Serial.available() > 0) {
          Serial.read();
        }
        Serial.println(F("  멈춤 (키 입력)"));
        break;
      }
    }
    while ((int32_t)(micros() - next) < 0) {
      // 다음 스텝 시각까지 기다림
    }
    // 한 스텝: 순서표를 한 칸 넘겨 그 코일을 켠다
    a.phase = (uint8_t)((a.phase + ((fwd != a.invert) ? 1 : 7)) & 7);
    writeCoils(a, HALF_STEP[a.phase]);
    next += intervalUs;
    moved++;
  }

  delay(5);
  writeCoils(a, 0);   // 멈추면 바로 끈다 (발열 방지). 기어 때문에 꺼도 그 자리에 있다
  return moved;
}

void reportMove(uint32_t moved) {
  Serial.print(F("  "));
  Serial.print(axes[cur].name);
  Serial.print(forward ? F(" + 방향 ") : F(" − 방향 "));
  Serial.print(moved);
  Serial.print(F("스텝 = 약 "));
  Serial.print(moved / axes[cur].stepsPerMm, 1);
  Serial.print(F("mm, "));
  Serial.print((float)moved / STEPS_PER_TURN, 2);
  Serial.print(F("바퀴 (코일 꺼짐)"));
  if (cur == 0) {
    Serial.print(limitPressed() ? F(" | 원점 스위치 눌림") : F(" | 원점 스위치 안 눌림"));
  }
  Serial.println();
}

void printSettings() {
  Serial.print(F("지금 축: "));
  Serial.print(axes[cur].name);
  Serial.print(F(" | 한 바퀴 "));
  Serial.print(STEPS_PER_TURN);
  Serial.print(F("스텝(하프 스텝), 1mm = "));
  Serial.print(axes[cur].stepsPerMm, 2);
  Serial.print(F("스텝, 속도 "));
  Serial.print(speedSps);
  Serial.print(F("스텝/초 (약 "));
  Serial.print(speedSps / axes[cur].stepsPerMm, 1);
  Serial.print(F("mm/초), 방향 "));
  Serial.println(forward ? F("+") : F("−"));
}

void printHelp() {
  Serial.println(F("  a        : 축 바꾸기 (헤드 <-> 카드 랙)"));
  Serial.println(F("  s        : 한 스텝 (마지막 방향)"));
  Serial.println(F("  f / b    : 한 바퀴 + 방향 / − 방향 (약 6~8초)"));
  Serial.println(F("  + / -    : 속도 올리기 / 내리기"));
  Serial.println(F("  m25      : 25mm + 방향,  m-10 : 10mm − 방향"));
  Serial.println(F("  l        : 원점 스위치(D34) 상태"));
  Serial.println(F("  (이동 중 아무 키나 누르면 멈춤. 원점 스위치에서 멈추지 않으니 짧게 움직일 것)"));
  Serial.println(F("  ?        : 도움말과 지금 설정"));
  Serial.println(F("  + 방향 = 헤드: 원점 스위치에서 멀어지는 쪽 / 카드 랙: 카드 고정 벽 쪽"));
  printSettings();
  Serial.println(F("----------------------------------------"));
}

void setupAxis(TestAxis& a, const __FlashStringHelper* name, const uint8_t* pins, bool invert, float stepsPerMm) {
  a.name       = name;
  a.pins       = pins;
  a.invert     = invert;
  a.stepsPerMm = stepsPerMm;
  a.phase      = 0;

  // 코일은 꺼진 상태로 시작
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(pins[i], LOW);
    pinMode(pins[i], OUTPUT);
  }
}

void setup() {
  testSolenoidsOff();   // 가장 먼저: 솔레노이드 핀 LOW

  setupAxis(axes[0], F("헤드(모터 A)"), PIN_HEAD_IN, HEAD_DIR_INVERT, HEAD_STEPS_PER_MM);
  setupAxis(axes[1], F("카드 랙(모터 B)"), PIN_CARD_IN, CARD_DIR_INVERT, CARD_STEPS_PER_MM);
  pinMode(PIN_HEAD_LIMIT, INPUT_PULLUP);

  testBegin(F("stepper_test — 28BYJ-48 스텝 모터 2개 (헤드 D36~39, 카드 랙 D40~43)"));
  printHelp();
}

void loop() {
  String line;
  if (!testReadLine(line)) {
    return;
  }

  if (line == "a") {
    cur = (cur == 0) ? 1 : 0;
    printSettings();
  } else if (line == "s") {
    reportMove(moveSteps(1, forward));
  } else if (line == "f") {
    reportMove(moveSteps(STEPS_PER_TURN, true));
  } else if (line == "b") {
    reportMove(moveSteps(STEPS_PER_TURN, false));
  } else if (line == "+") {
    speedSps = min((uint16_t)(speedSps + SPEED_STEP), SPEED_MAX);
    printSettings();
  } else if (line == "-") {
    speedSps = max((uint16_t)(speedSps - SPEED_STEP), SPEED_MIN);
    printSettings();
  } else if (line == "l") {
    Serial.println(limitPressed() ? F("  원점 스위치: 눌림") : F("  원점 스위치: 안 눌림"));
  } else if (line.length() > 1 && line[0] == 'm') {
    float mm = line.substring(1).toFloat();
    if (mm == 0) {
      Serial.println(F("mm 를 숫자로 적으세요. 예) m25, m-10"));
    } else if (fabs(mm) > MAX_MOVE_MM) {
      Serial.print(F("한 번에 "));
      Serial.print(MAX_MOVE_MM, 0);
      Serial.println(F("mm 까지만 움직입니다"));
    } else {
      uint32_t steps = (uint32_t)lround(fabs(mm) * axes[cur].stepsPerMm);
      Serial.print(F("  "));
      Serial.print(mm, 1);
      Serial.print(F("mm = "));
      Serial.print(steps);
      Serial.println(F("스텝"));
      reportMove(moveSteps(steps, mm > 0));
    }
  } else {
    printHelp();
  }
}
