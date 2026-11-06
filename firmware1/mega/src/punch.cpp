/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * punch.cpp — 점자 타각 (지금은 자리만 있습니다)
 *
 * TODO: solenoid·stepper 모듈이 생기면 punch.h 머리 주석의 순서대로
 *       실제로 찍는 코드를 넣습니다.
 *       지금은 칸 값과 점 번호를 USB 시리얼에 출력하고 true 를 돌려줍니다.
 */

#include <Arduino.h>

#include "punch.h"


void punchInit() {
  // 지금은 할 일 없음 (solenoid·stepper 의 Init 을 부를 자리)
}


bool punchBraille(const BrailleJob& job) {
  Serial.println(F("[타각] 솔레노이드 연결 전 — 칸 값만 출력합니다"));

  for (uint8_t row = 0; row < job.lineCount; row++) {
    Serial.print(F("  "));
    Serial.print(row + 1);
    Serial.print(F("줄 ("));
    Serial.print(job.lineLen[row]);
    Serial.println(F("칸)"));

    for (uint8_t col = 0; col < job.lineLen[row]; col++) {
      uint8_t mask = job.cells[row][col];
      Serial.print(F("    칸 "));
      Serial.print(col + 1);
      Serial.print(F(" = "));
      Serial.print(mask);
      Serial.print(F(" -> 점 "));

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
        Serial.print(F("없음 (빈칸)"));
      }
      Serial.println();
    }
  }
  return true;
}
