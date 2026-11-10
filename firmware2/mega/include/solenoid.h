/*
 * Eye 2 Dot — 펌웨어 2 (책 점역기)
 * solenoid.h — 솔레노이드 6개로 점자 한 칸 누르기   (담당: 세연)
 *
 * 펌웨어 1 의 solenoid 모듈(firmware1/mega/include/solenoid.h)과 같은 방식·같은 이름입니다.
 *
 * 한 칸의 점(최대 6개)을 "동시에" 누르고, 종이가 두꺼워서 같은 칸을 두 번 누릅니다.
 *   칸 값 = 서버가 거울로 바꾼 6비트 값 = 솔레노이드 비트 (펌웨어는 뒤집지 않고 그대로 씀)
 *           bit0 = 위에서 볼 때 왼쪽 위 솔레노이드(1번) … bit5 = 6번
 *   예) 7 = 0b000111 → 솔레노이드 1·2·3 (위에서 볼 때 왼쪽 열)
 *
 * 핀: pins.h 의 PIN_SOL[0..5] = D22~D27 = PA0~PA5 → L298N #1 IN1~4, #2 IN1~2
 *     PORTA 에 한 번 쓰면 6개가 정확히 같은 순간에 켜집니다. (PA6·PA7 = D28·D29 는 건드리지 않음)
 *
 * 세기 조절은 켜는 시간(SOL_PULSE_MS)과 누르는 횟수(SOL_STRIKES)로만 합니다. L298N ENA·ENB 점퍼는 꽂은 상태.
 *
 * 2026-11-10 펌웨어 1 최종 코드(두 번 타각 + 과열 방지)를 바탕으로 작성.
 */

#ifndef SOLENOID_H
#define SOLENOID_H

#include <Arduino.h>

// ─────────────────────────────────────────────
// 시간 값 (밀리초) — 실물 종이에 찍어 보고 여기만 고치면 됩니다
// ─────────────────────────────────────────────
// 점이 약하게 나오면: ① SOL_PULSE_MS 를 5ms 씩 올려 보고(최대 50)
//                   ② 그래도 약하면 SOL_RESTRIKE_GAP_MS 를 늘려 핀이 끝까지 올라온 뒤 다시 누르게
constexpr uint16_t SOL_PULSE_MS         = 30;    // 코일을 켜 두는 시간. 50 을 넘기면 코일이 뜨거워질 수 있음
constexpr uint8_t  SOL_STRIKES          = 2;     // 한 칸을 몇 번 누를지 (1~3). 종이가 두꺼워서 2번 (11/9 팀 결정)
constexpr uint16_t SOL_RESTRIKE_GAP_MS  = 60;    // 같은 칸을 다시 누르기 전 쉬는 시간 (핀이 올라올 시간)
constexpr uint16_t SOL_COOLDOWN_MS      = 200;   // 한 칸을 다 찍은 뒤 쉬는 시간 (코일 식히기 + 다음 칸 준비)

// 한 칸을 찍는 데 걸리는 시간 (스텝 모터·통신 쪽 시간 계산에 쓰세요) = 30·2 + 60 + 200 = 320ms
constexpr uint32_t SOL_CELL_MS = (uint32_t)SOL_STRIKES * SOL_PULSE_MS
                               + (uint32_t)(SOL_STRIKES - 1) * SOL_RESTRIKE_GAP_MS
                               + SOL_COOLDOWN_MS;

// ─────────────────────────────────────────────
// 과열 방지 (열 저금통)
// ─────────────────────────────────────────────
// 코일을 켤 때마다 켠 시간(ms)만큼 "열"이 쌓이고, 시간이 지나면 SOL_HEAT_DUTY_PCT % 속도로 빠집니다.
// 쌓인 열이 SOL_HEAT_MAX_MS 를 넘으려 하면 열이 빠질 때까지 기다렸다가 찍습니다(거부하지 않음).
//   - 평균 켜짐 비율을 20% 아래로 묶음 (5V 코일에 약 9V → 열 약 3.2배 → 20% × 3.2 ≈ 0.65배)
//   - 보통 타각(한 칸 320ms 중 60ms = 19%)은 기다리지 않음. 책은 칸이 많아도 이 리듬이면 계속 찍을 수 있음
constexpr uint8_t  SOL_HEAT_DUTY_PCT = 20;
constexpr uint16_t SOL_HEAT_MAX_MS   = 2000;   // 차가운 상태에서 쉬지 않고 쓸 수 있는 켜짐 시간 합

static_assert(SOL_PULSE_MS <= 50, "SOL_PULSE_MS 가 50ms 를 넘으면 코일이 과열될 수 있습니다");
static_assert(SOL_STRIKES >= 1 && SOL_STRIKES <= 3, "SOL_STRIKES 는 1~3 으로 두세요");
static_assert(SOL_RESTRIKE_GAP_MS >= 30, "다시 누르기 전에 핀이 올라올 시간(30ms 이상)이 필요합니다");
static_assert((uint32_t)SOL_STRIKES * SOL_PULSE_MS * 100 <= (uint32_t)SOL_HEAT_DUTY_PCT * SOL_CELL_MS,
              "보통 타각만으로도 과열 방지가 걸립니다. SOL_COOLDOWN_MS 를 늘리세요");

/* setup() 맨 처음에 한 번. 6핀을 출력으로 만들고 모두 끕니다. (리셋 직후 L298N 입력이 떠서 켜지는 것 방지) */
void solenoidInit();

/* 6개를 즉시 모두 끕니다. */
void solenoidAllOff();

/*
 * 한 칸 찍기: mask 의 점을 동시에 켜고 → SOL_PULSE_MS 뒤 끄고 (SOL_STRIKES 번 반복) → SOL_COOLDOWN_MS 쉽니다.
 * mask 가 0(빈칸)이면 아무것도 안 하고 바로 돌아옵니다. (헤드·종이 이동은 스텝 모터 쪽에서)
 * 과열 방지 한도에 걸리면 찍기 전에 열이 빠질 때까지 기다립니다.
 */
void solenoidPunchCell(uint8_t mask);

/*
 * 기다리는 동안(켜 둔 30ms, 쉬는 200ms 등) 계속 불리는 함수.
 * 지금은 아무것도 안 합니다. 나중에 ESP32 통신 모듈이 생기면 그 모듈에서
 *     void solenoidIdle() { linkPoll(); }
 * 처럼 "같은 이름으로 한 번 더" 만들면 그쪽이 자동으로 쓰입니다(약한 정의, weak).
 * → 타각 중에도 ESP32 에서 오는 바이트를 놓치지 않게 하기 위함 (펌웨어 1 은 linkPoll() 을 직접 부름)
 */
void solenoidIdle();

#endif  // SOLENOID_H
