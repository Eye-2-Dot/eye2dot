/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * solenoid.cpp — 솔레노이드 6개로 점자 한 칸 누르기   (담당: 세연)
 *
 * 켜기(PORTA 한 번 쓰기) → 30ms → 끄기 → 200ms 쉬기
 */

#include <Arduino.h>

#include "solenoid.h"
#include "link.h"   // linkPoll(), protocol.h 의 시간·크기 상수
#include "pins.h"   // PIN_SOL

// PORTA 0~5번 비트 = D22~D27 = 솔레노이드 1~6 (1번 = 위에서 볼 때 왼쪽 위)
static const uint8_t SOL_MASK = 0x3F;

// PORTA 에 한 번 써서 6개를 동시에 켜는 방식은 PIN_SOL 이 D22~D27(PA0~PA5) 순서일 때만 맞다.
// pins.h 의 배치가 바뀌면 여기서 컴파일을 멈춘다.
static_assert(PIN_SOL[0] == 22 && PIN_SOL[1] == 23 && PIN_SOL[2] == 24 &&
              PIN_SOL[3] == 25 && PIN_SOL[4] == 26 && PIN_SOL[5] == 27,
              "PIN_SOL 이 D22~D27(PORTA 0~5번 비트)이 아니면 PORTA 로 한 번에 쓰는 방식을 고쳐야 합니다");

// 한 줄(BRAILLE_MAX_CELLS 칸)을 다 찍는 가장 긴 시간 + 여유 1초가
// 'D' 대기의 차이(ESP32 LINK_REPLY_TIMEOUT_MS − ATmega LINK_JOB_TIMEOUT_MS) 안에 들어가야 한다.
// ATmega 가 시간 초과 직전에 출력 버튼을 받아 타각하면, 그만큼 늦게 'D' 가 나가기 때문이다.
// 솔레노이드 시간을 늘려 여기서 멈추면 common/protocol.h 의 LINK_REPLY_TIMEOUT_MS 를 함께 늘릴 것.
static_assert((uint32_t)BRAILLE_MAX_CELLS * (SOL_PULSE_MS + SOL_COOLDOWN_MS) + 1000UL
                  <= LINK_REPLY_TIMEOUT_MS - LINK_JOB_TIMEOUT_MS,
              "한 줄 타각 시간이 'D' 대기 여유(LINK_REPLY_TIMEOUT_MS - LINK_JOB_TIMEOUT_MS)보다 깁니다");

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
