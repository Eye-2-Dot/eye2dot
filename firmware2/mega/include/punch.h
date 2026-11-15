// 한 장을 찍는 순서: 점이 있는 칸으로 이동 -> 잠깐 기다림 -> 솔레노이드 -> 다음 칸
#pragma once
#include <Arduino.h>
#include "config.h"

struct PageBuf {
  uint8_t lineCount;
  uint8_t len[MAX_LINES];
  uint8_t cell[MAX_LINES][MAX_CELLS];   // 0~63. 서버가 이미 거울로 바꿔 준 값
};

void     punchStart(const PageBuf* pg);
void     punchUpdate();          // loop 마다
bool     punchBusy();
void     punchAbort();
uint8_t  punchLine();            // 지금 찍는 줄
uint16_t punchCount();           // 지금까지 찍은 칸 수
