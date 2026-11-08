/*
 * Eye 2 Dot — 펌웨어 1 부품 시험: 카드 이송 스텝 모터 (stepper_test)
 *
 *   카드 이송 모터 (pins.h: PIN_TAPE_STEP / PIN_TAPE_DIR / PIN_TAPE_EN)
 *   드라이버(A4988·DRV8825 계열)의 STEP/DIR/EN 방식.
 *   (pins.h 의 "L298N 임시 연결" 4선 방식은 이 시험이 다루지 않는다)
 *
 * 움직이기 직전에만 EN 을 켜고, 끝나면 바로 꺼서 모터·드라이버가 뜨거워지지 않게 한다.
 * 이동 중에 아무 키나 누르면 멈춘다.
 *
 * 시리얼 명령 (입력 뒤 Enter)
 *   s        : 한 스텝 (마지막 방향으로)
 *   f / b    : 한 바퀴 앞으로 / 뒤로
 *   + / -    : 속도 올리기 / 내리기
 *   m25      : 25mm 앞으로,  m-10 : 10mm 뒤로
 *   ?        : 도움말과 지금 설정
 */

#include <Arduino.h>

#include "mega_test.h"

// ─────────────────────────────────────────────
// 맨 위 상수 — 모터·드라이버·기구에 맞게 고칠 것 (README "실측해서 고치기")
// ─────────────────────────────────────────────
const uint16_t STEPS_PER_REV = 200;    // 모터 한 바퀴 풀스텝 수 (1.8° 모터 = 200)
const uint8_t  MICROSTEPS    = 1;      // 드라이버 마이크로스텝 (MS 핀을 연결하지 않으면 1 = 풀스텝)
const float    STEPS_PER_MM  = 5.0;    // 카드가 1mm 움직이는 스텝 수 (마이크로스텝 포함, 임시값 — 실측할 것)
                                       // MICROSTEPS 를 바꾸면 이 값도 같은 배수로 바꿀 것
const uint8_t  EN_ACTIVE     = LOW;    // A4988·DRV8825 는 EN 이 LOW 일 때 켜진다
const uint8_t  DIR_FORWARD   = HIGH;   // 앞으로(카드를 내보내는 쪽)일 때의 DIR. 반대로 돌면 LOW 로
const uint16_t SPEED_DEFAULT = 200;    // 스텝/초
const uint16_t SPEED_MIN     = 50;
const uint16_t SPEED_MAX     = 1000;
const uint16_t SPEED_STEP    = 50;     // + / - 한 번에 바뀌는 양
const float    MAX_MOVE_MM   = 300;    // 한 번 이동의 최대 거리 (잘못 입력해도 끝없이 돌지 않게)

const uint8_t  STEP_PULSE_US = 5;      // STEP 펄스 폭 (드라이버 최소 1~2µs 보다 넉넉히)
const uint16_t EN_WAKE_MS    = 2;      // EN 을 켠 뒤 첫 스텝까지 기다리는 시간

uint16_t speedSps = SPEED_DEFAULT;
bool     forward  = true;              // 마지막 방향 (s 가 이 방향으로 간다)

void motorEnable(bool on) {
  digitalWrite(PIN_TAPE_EN, on ? EN_ACTIVE : !EN_ACTIVE);
}

// steps 만큼 움직인다. 아무 키나 오면 멈춘다. 돌려주는 값 = 실제로 움직인 스텝 수
uint32_t moveSteps(uint32_t steps, bool fwd) {
  forward = fwd;
  digitalWrite(PIN_TAPE_DIR, fwd ? DIR_FORWARD : !DIR_FORWARD);
  motorEnable(true);
  delay(EN_WAKE_MS);

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
      // 다음 스텝 시각까지 기다림 (delayMicroseconds 는 16ms 를 넘으면 부정확해서 직접 잰다)
    }
    digitalWrite(PIN_TAPE_STEP, HIGH);
    delayMicroseconds(STEP_PULSE_US);
    digitalWrite(PIN_TAPE_STEP, LOW);
    next += intervalUs;
    moved++;
  }

  motorEnable(false);   // 멈추면 바로 끈다 (발열 방지)
  return moved;
}

void reportMove(uint32_t moved) {
  Serial.print(F("  "));
  Serial.print(forward ? F("앞으로 ") : F("뒤로 "));
  Serial.print(moved);
  Serial.print(F("스텝 = 약 "));
  Serial.print(moved / STEPS_PER_MM, 1);
  Serial.print(F("mm, "));
  Serial.print((float)moved / (STEPS_PER_REV * MICROSTEPS), 2);
  Serial.println(F("바퀴 (EN 꺼짐)"));
}

void printSettings() {
  Serial.print(F("한 바퀴 "));
  Serial.print(STEPS_PER_REV);
  Serial.print(F(" × 마이크로스텝 "));
  Serial.print(MICROSTEPS);
  Serial.print(F(" = "));
  Serial.print((uint32_t)STEPS_PER_REV * MICROSTEPS);
  Serial.print(F("스텝, 1mm = "));
  Serial.print(STEPS_PER_MM, 2);
  Serial.print(F("스텝, 속도 "));
  Serial.print(speedSps);
  Serial.print(F("스텝/초, 방향 "));
  Serial.println(forward ? F("앞") : F("뒤"));
}

void printHelp() {
  Serial.println(F("  s        : 한 스텝 (마지막 방향)"));
  Serial.println(F("  f / b    : 한 바퀴 앞으로 / 뒤로"));
  Serial.println(F("  + / -    : 속도 올리기 / 내리기"));
  Serial.println(F("  m25      : 25mm 앞으로,  m-10 : 10mm 뒤로"));
  Serial.println(F("  (이동 중 아무 키나 누르면 멈춤)"));
  Serial.println(F("  ?        : 도움말과 지금 설정"));
  printSettings();
  Serial.println(F("----------------------------------------"));
}

void setup() {
  testSolenoidsOff();   // 가장 먼저: 솔레노이드 핀 LOW

  // 모터는 꺼진 상태로 시작
  digitalWrite(PIN_TAPE_EN, !EN_ACTIVE);
  pinMode(PIN_TAPE_EN, OUTPUT);
  pinMode(PIN_TAPE_STEP, OUTPUT);
  pinMode(PIN_TAPE_DIR, OUTPUT);
  digitalWrite(PIN_TAPE_STEP, LOW);

  testBegin(F("stepper_test — 카드 이송 모터 (pins.h: PIN_TAPE_STEP/DIR/EN)"));
  printHelp();
}

void loop() {
  String line;
  if (!testReadLine(line)) {
    return;
  }

  const uint32_t stepsPerRev = (uint32_t)STEPS_PER_REV * MICROSTEPS;

  if (line == "s") {
    reportMove(moveSteps(1, forward));
  } else if (line == "f") {
    reportMove(moveSteps(stepsPerRev, true));
  } else if (line == "b") {
    reportMove(moveSteps(stepsPerRev, false));
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
