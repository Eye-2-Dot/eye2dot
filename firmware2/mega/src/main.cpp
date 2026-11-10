/*
 * Eye 2 Dot — 펌웨어 2 (책 점역기) — ATmega2560 쪽
 * main.cpp — 모듈을 시작하고(setup) 차례로 돌리기만(loop) 합니다.
 *
 * 모듈 (펌웨어 1 과 같은 이름)
 *   solenoid : 솔레노이드 6개로 한 칸 찍기 (두 번 타각)     solenoid.h   — 세연, 완료
 *   step     : 헤드 가로(X)·종이 세로(Y) 이동               step.h       — 스텝 모터 담당, 아직 없음
 *   link     : ESP32 와 통신 (Serial1)                      link.h       — 아직 없음
 * 핀 번호는 pins.h 에 있습니다.
 *
 * 지금은 다른 모듈이 없어서, USB 시리얼로 솔레노이드를 시험하는 명령만 들어 있습니다.
 * (시리얼 모니터 115200, 줄 끝 "새 줄")
 *   0~63 + Enter : 그 솔레노이드들을 동시에 한 칸 찍기 (두 번 누름)
 *   y            : 솔레노이드 1번 → 6번 하나씩 (배선·번호 확인)
 *   t            : 시험 칸 [1, 63] — 첫 칸 1번 하나, 둘째 칸 6개 전부
 *   ?            : 도움말
 */

#include <Arduino.h>

#include "pins.h"
#include "solenoid.h"
// #include "step.h"   // TODO(스텝 모터 담당): step 모듈이 생기면

static const uint32_t BAUD_RATE = 115200;

static char    lineBuf[8];
static uint8_t lineLen = 0;

static void printHelp() {
  Serial.println();
  Serial.println(F("=== Eye 2 Dot - Firmware 2 (ATmega2560) : 솔레노이드 시험 ==="));
  Serial.print(F("한 칸 = "));
  Serial.print(SOL_PULSE_MS);
  Serial.print(F("ms x "));
  Serial.print(SOL_STRIKES);
  Serial.print(F("번 (사이 "));
  Serial.print(SOL_RESTRIKE_GAP_MS);
  Serial.print(F("ms) + 쉬기 "));
  Serial.print(SOL_COOLDOWN_MS);
  Serial.println(F("ms"));
  Serial.println(F("  0~63 + Enter : 그 솔레노이드들을 동시에 한 칸 찍기"));
  Serial.println(F("     예) 7 -> 1·2·3번,  63 -> 6개 전부,  1 -> 1번만"));
  Serial.println(F("  y : 1번 -> 6번 하나씩 (배선 확인)"));
  Serial.println(F("  t : 시험 칸 [1, 63]"));
  Serial.println(F("  ? : 도움말"));
}

static void punchAndPrint(uint8_t mask) {
  Serial.print(F("칸 "));
  Serial.print(mask);
  Serial.print(F(" -> 솔레노이드 "));
  if (mask == 0) {
    Serial.print(F("없음 (빈칸)"));
  }
  for (uint8_t dot = 0; dot < 6; dot++) {
    if (mask & (1 << dot)) {
      Serial.print(dot + 1);
      Serial.print(F("(D"));
      Serial.print(PIN_SOL[dot]);
      Serial.print(F(") "));
    }
  }
  Serial.println();
  solenoidPunchCell(mask);
}

static void handleLine() {
  lineBuf[lineLen] = '\0';
  if (lineLen == 0) {
    return;
  }
  char c = lineBuf[0];
  if (c == '?' || c == 'h') {
    printHelp();
  } else if (c == 'y') {
    for (uint8_t dot = 0; dot < 6; dot++) {
      punchAndPrint(1 << dot);
      delay(200);
    }
  } else if (c == 't') {
    punchAndPrint(1);
    punchAndPrint(63);
  } else if (c >= '0' && c <= '9') {
    int value = atoi(lineBuf);
    if (value < 0 || value > 63) {
      Serial.println(F("0~63 사이 숫자만 됩니다"));
    } else {
      punchAndPrint((uint8_t)value);
    }
  } else {
    Serial.println(F("알 수 없는 명령 (? 를 보내면 도움말)"));
  }
}


void setup() {
  // 솔레노이드 핀을 가장 먼저 LOW(끔)로 잡습니다.
  // 리셋 직후 핀이 떠 있으면 L298N 이 HIGH 로 읽어 솔레노이드가 켜질 수 있기 때문입니다.
  // (업로드 중에는 코드로 못 막으므로 업로드할 때는 배터리를 빼고 USB 만 꽂으세요)
  solenoidInit();
  // TODO(스텝 모터 담당): stepInit();

  Serial.begin(BAUD_RATE);
  delay(500);   // 업로드 직후 첫 출력이 잘리는 것 방지
  printHelp();
}


void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handleLine();
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}
