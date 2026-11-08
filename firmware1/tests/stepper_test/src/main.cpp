/*
 * Eye 2 Dot — 펌웨어 1 부품 시험: 스텝 모터 (stepper_test, 28BYJ-48 + ULN2003 판)
 *
 *   지금 쓰는 부품: 28BYJ-48 (5V 4상) × 2 + ULN2003 드라이버 보드 × 2
 *   원래 stepper_test 는 A4988·DRV8825 의 STEP/DIR/EN 방식이다. ULN2003 에는 STEP/DIR/EN 이 없어서
 *   Mega 가 코일 4개(IN1~IN4)를 직접 순서대로 켠다 (반스텝 8단계).
 *
 *   모터 1 (카드 이송) : D40 D41 D42 D43 → 드라이버 1 IN1 IN2 IN3 IN4
 *   모터 2             : D44 D45 D46 D47 → 드라이버 2 IN1 IN2 IN3 IN4
 *   드라이버 + / −     : 벅 5V 레일 / 공통 GND  (Mega 5V 핀에서 모터 전원을 뽑지 않는다)
 *
 * EN 대신: 움직이는 동안만 코일에 전류를 넣고, 끝나면 4선을 모두 LOW 로 해서 발열을 막는다.
 * 이동 중에 아무 키나 누르면 멈춘다.
 *
 * Arduino IDE 단독 파일 (mega_test.h 없이 돌아가게 도우미 함수를 아래에 넣음).
 * 시리얼 모니터: 115200 baud, 줄 끝 = "줄바꿈" 또는 "Both NL & CR"
 *
 * 시리얼 명령 (입력 뒤 Enter)
 *   1 / 2 / 12 : 움직일 모터 고르기 (모터 1 / 모터 2 / 둘 다). 처음에는 1
 *   s          : 한 스텝 (마지막 방향으로)
 *   f / b      : 한 바퀴 앞으로 / 뒤로
 *   + / -      : 속도 올리기 / 내리기
 *   m25        : 25mm 앞으로,  m-10 : 10mm 뒤로
 *   ?          : 도움말과 지금 설정
 */

#include <Arduino.h>

// ─────────────────────────────────────────────
// 맨 위 상수 — 모터·기구에 맞게 고칠 것
// ─────────────────────────────────────────────
const uint8_t  MOTOR_PINS[2][4] = {
  {40, 41, 42, 43},                    // 모터 1 → 드라이버 1 IN1~IN4
  {44, 45, 46, 47}                     // 모터 2 → 드라이버 2 IN1~IN4
};
const bool     MOTOR_REVERSE[2] = {false, false};  // "앞으로"가 반대로 돌면 그 모터만 true

const uint16_t STEPS_PER_REV = 4096;   // 28BYJ-48 반스텝 한 바퀴 (기어비 약 1:64, 실측하면 약 4076)
const float    STEPS_PER_MM  = 65.2;   // 카드가 1mm 움직이는 스텝 수 (임시값: 지름 20mm 롤러 가정
                                       //  = 4096 ÷ 62.8mm). 롤러가 정해지면 실측해서 고칠 것
const uint16_t SPEED_DEFAULT = 500;    // 반스텝/초 (약 7.3 RPM)
const uint16_t SPEED_MIN     = 100;
const uint16_t SPEED_MAX     = 1000;   // 5V 에서 이보다 빠르면 헛돌기(탈조) 쉬움 (약 14.6 RPM)
const uint16_t SPEED_STEP    = 100;    // + / - 한 번에 바뀌는 양
const float    MAX_MOVE_MM   = 300;    // 한 번 이동의 최대 거리 (잘못 입력해도 끝없이 돌지 않게)

const uint16_t COIL_SETTLE_MS = 5;     // 코일에 전류를 넣은 뒤 첫 스텝까지 기다리는 시간 (EN_WAKE_MS 자리)
const uint16_t SINGLE_STEP_HOLD_MS = 300;  // s(한 스텝) 뒤 LED 가 보이도록 코일을 유지하는 시간

// 28BYJ-48 반스텝 순서 (IN1, IN2, IN3, IN4)
const uint8_t HALF_STEP[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

uint16_t speedSps = SPEED_DEFAULT;
bool     forward  = true;              // 마지막 방향 (s 가 이 방향으로 간다)
uint8_t  selected = 0b01;              // bit0 = 모터 1, bit1 = 모터 2
uint8_t  phase[2] = {0, 0};            // 각 모터의 지금 단계 (0~7)

// ─────────────────────────────────────────────
// mega_test.h 대신 (PlatformIO tests 폴더에 넣을 때는 이 묶음을 지우고 #include "mega_test.h")
// ─────────────────────────────────────────────
const uint8_t SOLENOID_PINS[6] = {22, 23, 24, 25, 26, 27};

void testSolenoidsOff() {
  for (uint8_t i = 0; i < 6; i++) {
    digitalWrite(SOLENOID_PINS[i], LOW);
    pinMode(SOLENOID_PINS[i], OUTPUT);
  }
}

void testBegin(const __FlashStringHelper* title) {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(title);
  Serial.println(F("----------------------------------------"));
}

// 한 줄이 다 들어오면 true. 빈 줄(Enter 만)은 무시한다
bool testReadLine(String& out) {
  static String buf;
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (buf.length() == 0) continue;
      out = buf;
      out.trim();
      buf = "";
      return true;
    }
    if (buf.length() < 32) buf += c;
  }
  return false;
}

// ─────────────────────────────────────────────
// 모터
// ─────────────────────────────────────────────
bool isSelected(uint8_t m) {
  return selected & (1 << m);
}

void coilsWrite(uint8_t m, uint8_t p) {
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(MOTOR_PINS[m][i], HALF_STEP[p][i] ? HIGH : LOW);
  }
}

void coilsOff(uint8_t m) {
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(MOTOR_PINS[m][i], LOW);
  }
}

void stepOnce(uint8_t m, bool fwd) {
  bool dir = fwd != MOTOR_REVERSE[m];
  phase[m] = dir ? (phase[m] + 1) & 7 : (phase[m] + 7) & 7;
  coilsWrite(m, phase[m]);
}

// 고른 모터를 steps 만큼 같이 움직인다. 아무 키나 오면 멈춘다. 돌려주는 값 = 실제로 움직인 스텝 수
uint32_t moveSteps(uint32_t steps, bool fwd) {
  forward = fwd;
  for (uint8_t m = 0; m < 2; m++) {
    if (isSelected(m)) coilsWrite(m, phase[m]);   // 지금 위치에 먼저 전류를 넣어 자리를 잡음
  }
  delay(COIL_SETTLE_MS);

  const uint32_t intervalUs = 1000000UL / speedSps;
  uint32_t next  = micros();
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
    for (uint8_t m = 0; m < 2; m++) {
      if (isSelected(m)) stepOnce(m, fwd);
    }
    next += intervalUs;
    moved++;
  }

  if (steps == 1) {
    delay(SINGLE_STEP_HOLD_MS);     // 한 스텝일 때는 드라이버 LED 를 눈으로 볼 수 있게 잠깐 유지
  }
  for (uint8_t m = 0; m < 2; m++) {
    coilsOff(m);                    // 멈추면 바로 끈다 (발열 방지)
  }
  return moved;
}

// ─────────────────────────────────────────────
// 출력
// ─────────────────────────────────────────────
void printSelected() {
  if (selected == 0b11)      Serial.print(F("모터 1 + 2"));
  else if (selected == 0b10) Serial.print(F("모터 2"));
  else                       Serial.print(F("모터 1(카드)"));
}

void reportMove(uint32_t moved) {
  Serial.print(F("  "));
  printSelected();
  Serial.print(forward ? F(" 앞으로 ") : F(" 뒤로 "));
  Serial.print(moved);
  Serial.print(F("스텝 = 약 "));
  Serial.print(moved / STEPS_PER_MM, 1);
  Serial.print(F("mm, "));
  Serial.print((float)moved / STEPS_PER_REV, 2);
  Serial.println(F("바퀴 (코일 꺼짐)"));
}

void printSettings() {
  Serial.print(F("대상 "));
  printSelected();
  Serial.print(F(", 한 바퀴 "));
  Serial.print(STEPS_PER_REV);
  Serial.print(F("스텝(반스텝), 1mm = "));
  Serial.print(STEPS_PER_MM, 2);
  Serial.print(F("스텝, 속도 "));
  Serial.print(speedSps);
  Serial.print(F("스텝/초(약 "));
  Serial.print(speedSps * 60.0 / STEPS_PER_REV, 1);
  Serial.print(F(" RPM), 방향 "));
  Serial.println(forward ? F("앞") : F("뒤"));
}

void printHelp() {
  Serial.println(F("  1 / 2 / 12 : 움직일 모터 (모터 1 / 모터 2 / 둘 다)"));
  Serial.println(F("  s          : 한 스텝 (마지막 방향)"));
  Serial.println(F("  f / b      : 한 바퀴 앞으로 / 뒤로"));
  Serial.println(F("  + / -      : 속도 올리기 / 내리기"));
  Serial.println(F("  m25        : 25mm 앞으로,  m-10 : 10mm 뒤로"));
  Serial.println(F("  (이동 중 아무 키나 누르면 멈춤)"));
  Serial.println(F("  ?          : 도움말과 지금 설정"));
  printSettings();
  Serial.println(F("----------------------------------------"));
}

// ─────────────────────────────────────────────
void setup() {
  testSolenoidsOff();   // 가장 먼저: 솔레노이드 핀 LOW

  // 모터는 꺼진 상태로 시작 (코일 8선 모두 LOW)
  for (uint8_t m = 0; m < 2; m++) {
    for (uint8_t i = 0; i < 4; i++) {
      digitalWrite(MOTOR_PINS[m][i], LOW);
      pinMode(MOTOR_PINS[m][i], OUTPUT);
    }
  }

  testBegin(F("stepper_test — 28BYJ-48 + ULN2003 × 2 (D40~D43, D44~D47)"));
  printHelp();
}

void loop() {
  String line;
  if (!testReadLine(line)) {
    return;
  }

  if (line == "1") {
    selected = 0b01;
    printSettings();
  } else if (line == "2") {
    selected = 0b10;
    printSettings();
  } else if (line == "12") {
    selected = 0b11;
    printSettings();
  } else if (line == "s") {
    reportMove(moveSteps(1, forward));
  } else if (line == "f") {
    reportMove(moveSteps(STEPS_PER_REV, true));
  } else if (line == "b") {
    reportMove(moveSteps(STEPS_PER_REV, false));
  } else if (line == "+") {
    speedSps = min((uint16_t)(speedSps + SPEED_STEP), SPEED_MAX);
    printSettings();
  } else if (line == "-") {
    speedSps = max((uint16_t)(speedSps - SPEED_STEP), SPEED_MIN);
    printSettings();
  } else if (line.length() > 1 && line[0] == 'm') {
    float mm = line.substring(1).toFloat();
    if (mm == 0) {
      Serial.println(F("mm 를 숫자로 적으세요. 예) m25, m-10"));
    } else if (fabs(mm) > MAX_MOVE_MM) {
      Serial.print(F("한 번에 "));
      Serial.print(MAX_MOVE_MM, 0);
      Serial.println(F("mm 까지만 움직입니다"));
    } else {
      uint32_t steps = (uint32_t)lround(fabs(mm) * STEPS_PER_MM);
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
