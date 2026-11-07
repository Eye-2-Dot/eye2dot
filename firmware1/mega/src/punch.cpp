/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * punch.cpp — 점자 타각
 *
 * 지금: 칸마다 칸 값을 USB 시리얼에 출력하고, solenoid 로 그 칸의 점을 동시에 찍습니다.
 * TODO(준석): stepper 모듈이 생기면 punch.h 머리 주석의 순서대로
 *             헤드 원점 → 칸마다 이동 → 테이프 이송을 넣습니다.
 */

#include <Arduino.h>

#include "punch.h"
#include "solenoid.h"


void punchInit() {
  solenoidInit();   // 솔레노이드 6개 출력 + 모두 끔
  // TODO(준석): stepperInit();
}


bool punchBraille(const BrailleJob& job) {
  Serial.println(F("[타각] 시작"));

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

      // TODO(준석): 여기서 헤드를 이 칸 위치로 옮깁니다.
      solenoidPunchCell(mask);   // 이 칸의 점을 동시에 찍기 (빈칸이면 그냥 지나감)
    }
  }
  solenoidAllOff();
  Serial.println(F("[타각] 끝"));
  return true;
}
