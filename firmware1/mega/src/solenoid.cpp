/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * solenoid.cpp — 솔레노이드 6개로 점자 한 칸 누르기   (담당: 세연)
 *
 * 켜기(PORTA 한 번 쓰기) → 30ms → 끄기 → 200ms 쉬기
 */

#include <Arduino.h>

#include "solenoid.h"
#include "link.h"   // linkPoll()

// PORTA 0~5번 비트 = D22~D27 = 점1~점6
static const uint8_t SOL_MASK = 0x3F;

// delay() 대신: 기다리는 동안에도 ESP32 에서 오는 바이트를 계속 받습니다.
static void waitMs(uint16_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    linkPoll();
  }
}


void solenoidAllOff() {
  PORTA &= ~SOL_MASK;
}


void solenoidInit() {
  solenoidAllOff();      // 출력으로 바꾸기 전에 먼저 LOW (켜지는 순간 튀지 않게)
  DDRA |= SOL_MASK;      // PA0~PA5 = 출력
}


void solenoidPunchCell(uint8_t mask) {
  mask &= SOL_MASK;      // 6비트만 사용
  if (mask == 0) {
    return;              // 빈칸
  }

  PORTA = (PORTA & ~SOL_MASK) | mask;   // ① 고른 점을 같은 순간에 켜기
  waitMs(SOL_PULSE_MS);                 // ② 누르는 시간
  solenoidAllOff();                     // ③ 바로 끄기 (코일 보호)
  waitMs(SOL_COOLDOWN_MS);              // ④ 쉬기
}
