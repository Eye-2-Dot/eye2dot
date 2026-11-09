/*
 * Eye 2 Dot — 펌웨어 1 부품 시험: 솔레노이드 (solenoid_test)
 *
 * tools/solenoid_mask_test/solenoid_mask_test.ino 를 PlatformIO 로 옮긴 것.
 * 숫자 하나(0~63)를 6비트로 보고, 1인 비트의 솔레노이드를 "동시에" 한 번 찍는다.
 *
 *     비트 번호      :  5     4     3     2     1     0
 *     솔레노이드 번호 :  6번   5번   4번   3번   2번   1번
 *     Mega 핀        :  D27   D26   D25   D24   D23   D22   (pins.h: PIN_SOL)
 *     (1번 = 위에서 볼 때 왼쪽 위. 1·2·3 왼쪽 열, 4·5·6 오른쪽 열)
 *
 * 숫자는 솔레노이드 비트 그대로다 (서버가 기기에 주는 거울 값과 같은 규칙).
 * 켜는 시간·누르는 횟수·쉬는 시간은 완성 펌웨어와 같은 solenoid.h 의 SOL_PULSE_MS·SOL_STRIKES·SOL_RESTRIKE_GAP_MS·SOL_COOLDOWN_MS.
 * 과열 방지: 최근 BURST_WINDOW_MS 동안 BURST_MAX 번을 넘게 찍으면 거절한다.
 *
 * 시리얼 명령 (입력 뒤 Enter)
 *   0~63 : 그 솔레노이드들을 동시에 1번
 *   y    : 1번 → 6번 하나씩 (배선·번호 확인)
 *   ?    : 도움말
 */

#include <Arduino.h>

#include "mega_test.h"
#include "solenoid.h"   // SOL_PULSE_MS, SOL_STRIKES, SOL_RESTRIKE_GAP_MS, SOL_COOLDOWN_MS (함수는 쓰지 않고 값만 쓴다)

// PORTA 0~5번 비트 = D22~D27 = 솔레노이드 1~6번
const uint8_t SOL_MASK = 0x3F;

// PORTA 에 한 번 써서 6개를 동시에 켜는 방식은 PIN_SOL 이 D22~D27(PA0~PA5) 순서일 때만 맞다
static_assert(PIN_SOL[0] == 22 && PIN_SOL[1] == 23 && PIN_SOL[2] == 24 &&
              PIN_SOL[3] == 25 && PIN_SOL[4] == 26 && PIN_SOL[5] == 27,
              "PIN_SOL 이 D22~D27(PORTA 0~5번 비트)이 아니면 이 시험을 고쳐야 합니다");

// 과열 방지: 최근 10초 동안 40번까지 (완성 펌웨어가 한 줄을 찍는 최대 칸 수와 같음)
const uint8_t  BURST_MAX       = 40;
const uint32_t BURST_WINDOW_MS = 10000;

uint32_t punchTimes[BURST_MAX];   // 최근에 찍은 시각들 (돌아가며 덮어씀)
uint8_t  punchHead  = 0;          // 다음에 쓸 자리 = 가득 찼을 때 가장 오래된 기록
uint8_t  punchCount = 0;          // 기록된 개수 (최대 BURST_MAX)

// 지금 찍어도 되는지. 안 되면 몇 ms 뒤에 되는지를 waitMs 에 담는다
bool punchAllowed(uint32_t& waitMs) {
  if (punchCount < BURST_MAX) {
    return true;
  }
  uint32_t age = millis() - punchTimes[punchHead];   // 10초 창 안의 가장 오래된 기록
  if (age >= BURST_WINDOW_MS) {
    return true;
  }
  waitMs = BURST_WINDOW_MS - age;
  return false;
}

void recordPunch() {
  punchTimes[punchHead] = millis();
  punchHead = (punchHead + 1) % BURST_MAX;
  if (punchCount < BURST_MAX) {
    punchCount++;
  }
}

void allOff() {
  PORTA &= ~SOL_MASK;   // PA6·PA7(D28·D29)은 건드리지 않는다
}

// 한 칸 찍기. 과열 방지에 걸리면 찍지 않고 false
bool punchMask(uint8_t mask) {
  mask &= SOL_MASK;
  if (mask == 0) {
    return true;   // 빈칸 — 아무것도 안 찍음
  }
  uint32_t waitMs = 0;
  if (!punchAllowed(waitMs)) {
    Serial.print(F("  과열 방지: 최근 10초 동안 40번을 찍었습니다. "));
    Serial.print((waitMs + 999) / 1000);
    Serial.println(F("초 뒤에 다시 하세요."));
    return false;
  }

  for (uint8_t strike = 0; strike < SOL_STRIKES; strike++) {   // 같은 칸을 SOL_STRIKES 번 (solenoid.h)
    if (strike > 0) {
      delay(SOL_RESTRIKE_GAP_MS);       // 핀이 올라올 시간 (solenoid.h)
    }
    PORTA = (PORTA & ~SOL_MASK) | mask; // ① 고른 솔레노이드를 같은 순간에 켜기
    delay(SOL_PULSE_MS);                // ② 누르는 시간 (solenoid.h)
    allOff();                           // ③ 바로 끄기
    recordPunch();                      // 과열 방지는 누른 횟수로 센다
  }
  delay(SOL_COOLDOWN_MS);               // ④ 쉬기 (solenoid.h)
  return true;
}

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
  for (uint8_t i = 0; i < 6; i++) {
    if (mask & (1 << i)) {
      Serial.print(F("솔레노이드"));
      Serial.print(i + 1);
      Serial.print(F("(D"));
      Serial.print(PIN_SOL[i]);
      Serial.print(F(") "));
    }
  }
  Serial.println();
}

void printHelp() {
  Serial.print(F("켜는 시간 "));
  Serial.print(SOL_PULSE_MS);
  Serial.print(F("ms x "));
  Serial.print(SOL_STRIKES);
  Serial.print(F("번 (사이 "));
  Serial.print(SOL_RESTRIKE_GAP_MS);
  Serial.print(F("ms), 쉬는 시간 "));
  Serial.print(SOL_COOLDOWN_MS);
  Serial.println(F("ms (solenoid.h), 과열 방지 10초에 40번 누름까지"));
  Serial.println(F("  0~63 + Enter : 그 솔레노이드들을 동시에 한 칸 찍기 (SOL_STRIKES 번 누름)"));
  Serial.println(F("       예) 1 → 1번만,  7 → 1·2·3번(왼쪽 열),  63 → 6개 전부"));
  Serial.println(F("  y + Enter    : 1번 → 6번 하나씩 (배선·번호 확인)"));
  Serial.println(F("  ?            : 도움말"));
  Serial.println(F("----------------------------------------"));
}

void setup() {
  testSolenoidsOff();   // 가장 먼저: 솔레노이드 핀 LOW 출력
  testBegin(F("solenoid_test — 솔레노이드 6개 시험 (D22~D27, pins.h: PIN_SOL)"));
  printHelp();
}

void loop() {
  String line;
  if (!testReadLine(line)) {
    return;
  }

  if (line == "y") {
    Serial.println(F("솔레노이드 1번 → 6번 하나씩"));
    for (uint8_t i = 0; i < 6; i++) {
      Serial.print(F("  "));
      printMask(1 << i);
      if (!punchMask(1 << i)) {
        break;
      }
      delay(400);   // 눈으로 구분할 수 있게
    }
    return;
  }

  bool isNumber = line.length() > 0 && line.length() <= 3;
  for (uint8_t i = 0; i < line.length(); i++) {
    if (!isDigit(line[i])) {
      isNumber = false;
    }
  }
  if (isNumber) {
    long value = line.toInt();
    if (value > 63) {
      Serial.println(F("0~63 사이 숫자만 됩니다 (6비트)"));
      return;
    }
    Serial.print(F("타각! "));
    printMask((uint8_t)value);
    punchMask((uint8_t)value);
    return;
  }

  printHelp();
}
