/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * solenoid.cpp — 솔레노이드 6개로 점자 한 칸 누르기   (담당: 세연)
 *
 * 켜기(PORTA 한 번 쓰기) → 30ms → 끄기 → 200ms 쉬기
 * (SOL_STRIKES 가 2 이상이면 켜기·끄기를 SOL_RESTRIKE_GAP_MS 간격으로 반복한 뒤 쉬기)
 * 찍기 전에 과열 방지(열 저금통)를 확인합니다. 시간 값은 모두 solenoid.h 에 있습니다.
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
static_assert((uint32_t)BRAILLE_MAX_CELLS * SOL_CELL_MS + 1000UL
                  <= LINK_REPLY_TIMEOUT_MS - LINK_JOB_TIMEOUT_MS,
              "한 줄 타각 시간이 'D' 대기 여유(LINK_REPLY_TIMEOUT_MS - LINK_JOB_TIMEOUT_MS)보다 깁니다");

// delay() 대신: 기다리는 동안에도 ESP32 에서 오는 바이트를 계속 받습니다.
static void waitMs(uint16_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    linkPoll();
  }
}


// ─────────────────────────────────────────────
// 과열 방지 (열 저금통)
//   heatMs : 지금 쌓여 있는 열 (코일을 켠 시간 ms 로 셈)
//   켤 때마다 SOL_PULSE_MS 만큼 쌓이고, 1ms 마다 SOL_HEAT_DUTY_PCT/100 ms 씩 빠진다.
//   소수점을 피하려고 100배 단위로 저장한다.
// ─────────────────────────────────────────────
static uint32_t heatX100   = 0;   // 열 × 100
static uint32_t heatLastMs = 0;   // 마지막으로 열을 계산한 시각

static void heatCoolDown() {
  uint32_t now  = millis();
  uint32_t cool = (now - heatLastMs) * SOL_HEAT_DUTY_PCT;   // 그동안 빠진 열 × 100
  heatX100   = (cool >= heatX100) ? 0 : heatX100 - cool;
  heatLastMs = now;
}

// 이번에 SOL_PULSE_MS 만큼 켜도 한도를 안 넘을 때까지 기다린다 (보통 타각에서는 바로 통과)
static void heatWaitForRoom() {
  const uint32_t limitX100 = (uint32_t)SOL_HEAT_MAX_MS * 100;
  const uint32_t addX100   = (uint32_t)SOL_PULSE_MS * 100;

  heatCoolDown();
  if (heatX100 + addX100 <= limitX100) {
    return;
  }
  uint32_t needX100 = heatX100 + addX100 - limitX100;
  uint32_t waitMsTotal = needX100 / SOL_HEAT_DUTY_PCT + 1;
  Serial.print(F("[솔레노이드] 과열 방지: "));
  Serial.print(waitMsTotal);
  Serial.println(F("ms 쉬고 찍습니다"));
  while (waitMsTotal > 0) {                 // waitMs 는 16비트라 나눠서 기다린다
    uint16_t step = (waitMsTotal > 1000) ? 1000 : (uint16_t)waitMsTotal;
    waitMs(step);
    waitMsTotal -= step;
  }
  heatCoolDown();
}


void solenoidAllOff() {
  PORTA &= ~SOL_MASK;
}


void solenoidInit() {
  solenoidAllOff();      // 출력으로 바꾸기 전에 먼저 LOW (켜지는 순간 튀지 않게)
  DDRA |= SOL_MASK;      // PA0~PA5 = 출력
  heatX100   = 0;
  heatLastMs = millis();
}


void solenoidPunchCell(uint8_t mask) {
  mask &= SOL_MASK;      // 6비트만 사용
  if (mask == 0) {
    return;              // 빈칸
  }

  for (uint8_t strike = 0; strike < SOL_STRIKES; strike++) {
    if (strike > 0) {
      waitMs(SOL_RESTRIKE_GAP_MS);        // 핀이 올라올 시간
    }
    heatWaitForRoom();                    // 과열 방지 (보통은 바로 통과)

    PORTA = (PORTA & ~SOL_MASK) | mask;   // ① 고른 점을 같은 순간에 켜기
    waitMs(SOL_PULSE_MS);                 // ② 누르는 시간
    solenoidAllOff();                     // ③ 바로 끄기 (코일 보호)

    heatX100 += (uint32_t)SOL_PULSE_MS * 100;
  }
  waitMs(SOL_COOLDOWN_MS);                // ④ 쉬기
}
