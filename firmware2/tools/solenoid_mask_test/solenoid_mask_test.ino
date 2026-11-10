/*
 * Eye 2 Dot — 펌웨어 2 (책 점역기)
 * 솔레노이드 6비트 마스크 타각 테스트   (담당: 세연) (Arduino IDE 용)
 *
 * ── 하는 일 ──────────────────────────────────────────
 *  시리얼 모니터로 숫자 하나(0~63)를 보내면,
 *  그 숫자를 6비트로 보고 1인 비트의 솔레노이드를 "동시에" 한 번 찍습니다.
 *  숫자는 솔레노이드 비트 그대로입니다 (서버가 기기에 주는 거울 값과 같은 규칙).
 *
 *     비트 번호     :   5     4     3     2     1     0
 *     솔레노이드 번호:  6번   5번   4번   3번   2번   1번
 *     Mega 핀       :  D27   D26   D25   D24   D23   D22
 *     (1번 = 위에서 볼 때 왼쪽 위. 1·2·3 왼쪽 열, 4·5·6 오른쪽 열)
 *
 *  예)  7 = 0b000111 → 솔레노이드 1·2·3   (위에서 볼 때 왼쪽 열 전부)
 *      63 = 0b111111 → 6개 전부
 *       1 = 0b000001 → 솔레노이드 1번만
 *
 * ── 핀 (펌웨어 2 핀 초안 = 펌웨어 1 과 같음) ─────────
 *  D22~D27 = PA0~PA5 → L298N #1 IN1~IN4, #2 IN1~IN2 → 솔레노이드 1~6번
 *  L298N ENA·ENB 점퍼는 꽂은 상태 그대로 (세기 조절 안 함)
 *
 *  D22~D27 이 칩 안에서 PORTA 의 0~5번 비트로 줄지어 있어서,
 *  PORTA 레지스터에 한 번 쓰면 6개가 정확히 같은 순간에 켜집니다.
 *  → 받은 숫자의 비트 배치와 핀 배치가 똑같아서 그대로 써도 됩니다.
 *
 * ── 시리얼 모니터 (115200, 줄 끝: "새 줄" 또는 "Both NL & CR") ───────────────────────────
 *   0~63 숫자 + Enter : 그 솔레노이드들을 동시에 1번 타각
 *   y                 : 솔레노이드 1번 → 6번 하나씩 (배선·번호 확인용)
 *   ?                 : 도움말
 */

#include <Arduino.h>

// ─────────────────────────────────────────────
// 설정
// ─────────────────────────────────────────────
// 이 스케치는 Arduino IDE 용이라 펌웨어 헤더를 include 하지 않고 값을 따로 적었습니다.
// 아래 값을 바꾸면 펌웨어의 같은 값도 맞출 것:
//   PIN_SOL      ↔ firmware2/mega/include/pins.h 의 PIN_SOL
//   PULSE_MS     ↔ firmware2/mega/include/solenoid.h 의 SOL_PULSE_MS
//   STRIKES      ↔ firmware2/mega/include/solenoid.h 의 SOL_STRIKES
//   RESTRIKE_GAP_MS ↔ firmware2/mega/include/solenoid.h 의 SOL_RESTRIKE_GAP_MS
//   COOLDOWN_MS  ↔ firmware2/mega/include/solenoid.h 의 SOL_COOLDOWN_MS
const uint8_t  PIN_SOL[6]  = { 22, 23, 24, 25, 26, 27 };  // 솔레노이드 1~6번 (안내 출력용) — pins.h 와 맞출 것
const uint8_t  SOL_MASK    = 0x3F;   // PORTA 0~5번 비트 = 0b00111111
const uint16_t PULSE_MS    = 30;     // 켜 두는 시간. 5V 솔레노이드에 약 9V가 걸리므로 짧게! — solenoid.h 와 맞출 것
const uint8_t  STRIKES     = 2;      // 한 칸을 몇 번 누를지. 종이가 두꺼워서 2번 — solenoid.h 와 맞출 것
const uint16_t RESTRIKE_GAP_MS = 60; // 같은 칸을 다시 누르기 전 쉬는 시간 (핀이 올라올 시간) — solenoid.h 와 맞출 것
const uint16_t COOLDOWN_MS = 200;    // 한 칸을 다 찍은 뒤 쉬는 시간 (코일 식히기 + 핀 복귀) — solenoid.h 와 맞출 것

// ─────────────────────────────────────────────
// 솔레노이드 기본 동작
// ─────────────────────────────────────────────

// 6개 모두 끄기. PA6·PA7(D28·D29)은 건드리지 않습니다.
void solenoidAllOff() {
  PORTA &= ~SOL_MASK;
}

// 시작할 때 한 번: 6핀을 출력으로 만들고 꺼 둡니다.
void solenoidInit() {
  solenoidAllOff();      // 출력으로 바꾸기 전에 먼저 LOW 로 (켜지는 순간 튀지 않게)
  DDRA |= SOL_MASK;      // PA0~PA5 = 출력
}

/*
 * 한 칸 찍기
 *   mask : 6비트 숫자 (bit0 = 솔레노이드 1번 … bit5 = 6번)
 *   예) punchMask(7) → 솔레노이드 1·2·3 동시에
 */
void punchMask(uint8_t mask) {
  mask &= SOL_MASK;                  // 6비트만 사용 (64 이상이 와도 안전)
  if (mask == 0) return;             // 0 = 빈칸 → 아무것도 안 찍음

  for (uint8_t strike = 0; strike < STRIKES; strike++) {   // 같은 칸을 STRIKES 번
    if (strike > 0) delay(RESTRIKE_GAP_MS);   // 핀이 올라올 시간
    PORTA = (PORTA & ~SOL_MASK) | mask; // ① 켜기: 고른 점이 같은 순간에 켜짐
    delay(PULSE_MS);                    // ② 찍는 시간
    solenoidAllOff();                   // ③ 바로 끄기 (코일 보호)
  }
  delay(COOLDOWN_MS);                   // ④ 쉬기
}

// ─────────────────────────────────────────────
// 화면 출력
// ─────────────────────────────────────────────

// 예) 7 → "7 = 0b000111 → 솔레노이드1(D22) 솔레노이드2(D23) 솔레노이드3(D24)"
void printMask(uint8_t mask) {
  Serial.print(mask);
  Serial.print(F(" = 0b"));
  for (int8_t b = 5; b >= 0; b--) {
    Serial.print((mask >> b) & 1);
  }
  Serial.print(F(" → "));
  if (mask == 0) {
    Serial.print(F("빈칸 (안 찍음)"));
  }
  for (uint8_t dot = 0; dot < 6; dot++) {
    if (mask & (1 << dot)) {
      Serial.print(F("솔레노이드"));
      Serial.print(dot + 1);
      Serial.print(F("(D"));
      Serial.print(PIN_SOL[dot]);
      Serial.print(F(") "));
    }
  }
  Serial.println();
}

void printHelp() {
  Serial.println(F("────── 솔레노이드 6비트 마스크 테스트 ──────"));
  Serial.println(F("  0~63 숫자 + Enter : 그 솔레노이드들을 동시에 1번"));
  Serial.println(F("     예) 7 → 솔레노이드 1·2·3,  63 → 6개 전부,  1 → 1번만"));
  Serial.println(F("  y : 솔레노이드 1번 -> 6번 하나씩 (배선 확인)"));
  Serial.println(F("  ? : 도움말"));
}

// ─────────────────────────────────────────────
// 숫자 받기: 숫자를 모았다가 Enter(또는 1.5초 쉬면) 처리
//   → 시리얼 모니터 줄 끝 설정이 "없음"이어도 동작
// ─────────────────────────────────────────────
int16_t  number     = -1;   // -1 = 아직 숫자 안 받음
uint32_t lastCharAt = 0;

void handleNumber() {
  if (number < 0) return;
  if (number > 63) {
    Serial.print(number);
    Serial.println(F(" → 0~63 사이 숫자만 됩니다 (6비트)"));
  } else {
    Serial.print(F("타각! "));
    printMask((uint8_t)number);
    punchMask((uint8_t)number);
  }
  number = -1;
}

// ─────────────────────────────────────────────
void setup() {
  solenoidInit();          // 가장 먼저: 솔레노이드가 켜진 채로 있지 않게
  Serial.begin(115200);
  delay(300);
  printHelp();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    lastCharAt = millis();

    if (c >= '0' && c <= '9') {
      if (number < 0) number = 0;
      if (number < 1000) number = number * 10 + (c - '0');   // 너무 큰 수 방지
    }
    else if (c == '\r' || c == '\n') {
      handleNumber();
    }
    else if (c == 'y') {
      number = -1;
      Serial.println(F("솔레노이드 1번 -> 6번 하나씩"));
      for (uint8_t dot = 0; dot < 6; dot++) {
        Serial.print(F("  "));
        printMask(1 << dot);
        punchMask(1 << dot);
        delay(400);        // 눈으로 구분할 수 있게
      }
    }
    else if (c == '?') {
      number = -1;
      printHelp();
    }
  }

  // Enter 없이 숫자만 보냈을 때: 1.5초 지나면 처리
  if (number >= 0 && millis() - lastCharAt > 1500) {
    handleNumber();
  }
}
