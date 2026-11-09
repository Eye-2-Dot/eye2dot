/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * solenoid.h — 솔레노이드 6개로 점자 한 칸 누르기   (담당: 세연)
 *
 * 한 칸의 점(최대 6개)을 "동시에" 한 번 누릅니다.
 *   칸 값 = 서버가 거울로 바꾼 6비트 값 = 솔레노이드 비트   (punch.h 와 같은 규칙)
 *           bit0 = 위에서 볼 때 왼쪽 위 솔레노이드(1번) … bit5 = 6번. 뒤집지 않고 그대로 쓴다
 *   예) 7 = 0b000111 → 솔레노이드 1·2·3 (위에서 볼 때 왼쪽 열)
 *
 * 핀: pins.h 의 PIN_SOL[0..5] = D22~D27 = PA0~PA5 → L298N #1 IN1~4, #2 IN1~2
 *     D22~D27 이 칩 안에서 PORTA 0~5번 비트로 줄지어 있어서
 *     PORTA 에 한 번 쓰면 6개가 정확히 같은 순간에 켜집니다.
 *     (PA6·PA7 = D28·D29 는 건드리지 않습니다)
 *
 * 세기 조절은 하지 않습니다. L298N ENA·ENB 점퍼는 꽂은 상태 그대로.
 * 5V 솔레노이드에 약 9V(배터리 11.1V − L298N 약 2V)가 걸리므로 짧게만 켭니다.
 *
 * 2026-11-07 Arduino IDE 시험 스케치(firmware1/tools/solenoid_mask_test)로 6점 동작 확인.
 * 2026-11-09 최종 정리: 같은 칸 반복 타각(SOL_STRIKES), 과열 방지(열 저금통) 추가.
 */

#ifndef SOLENOID_H
#define SOLENOID_H

#include <Arduino.h>

// ─────────────────────────────────────────────
// 시간 값 (밀리초) — 실물 라벨에 찍어 보고 여기만 고치면 됩니다
// ─────────────────────────────────────────────
// 점이 약하게 나오면: ① SOL_PULSE_MS 를 5ms 씩 올려 보고(최대 50)
//                   ② 그래도 약하면 SOL_RESTRIKE_GAP_MS 를 늘려 핀이 끝까지 올라온 뒤 다시 누르게
constexpr uint16_t SOL_PULSE_MS         = 30;    // 코일을 켜 두는 시간. 50 을 넘기면 코일이 뜨거워질 수 있음
constexpr uint8_t  SOL_STRIKES          = 2;     // 한 칸을 몇 번 누를지 (1~3). 라벨지가 두꺼워서 2번 (11/9 팀 결정)
constexpr uint16_t SOL_RESTRIKE_GAP_MS  = 60;    // 같은 칸을 다시 누르기 전 쉬는 시간 (핀이 스프링으로 올라올 시간)
constexpr uint16_t SOL_COOLDOWN_MS      = 200;   // 한 칸을 다 찍은 뒤 쉬는 시간 (코일 식히기 + 다음 칸 준비)

// 한 칸을 찍는 데 걸리는 시간 (punch·protocol 의 시간 계산에 씀)
constexpr uint32_t SOL_CELL_MS = (uint32_t)SOL_STRIKES * SOL_PULSE_MS
                               + (uint32_t)(SOL_STRIKES - 1) * SOL_RESTRIKE_GAP_MS
                               + SOL_COOLDOWN_MS;

// ─────────────────────────────────────────────
// 과열 방지 (열 저금통)
// ─────────────────────────────────────────────
// 코일을 켤 때마다 켠 시간(ms)만큼 "열"이 쌓이고, 시간이 지나면 SOL_HEAT_DUTY_PCT % 속도로 빠집니다.
// 쌓인 열이 SOL_HEAT_MAX_MS 를 넘으려 하면 열이 빠질 때까지 기다렸다가 찍습니다(거부하지 않음).
//   - 평균 켜짐 비율을 20% 아래로 묶음. 5V 코일에 약 9V 가 걸리면 열은 (9/5)² ≈ 3.2배 →
//     20% × 3.2 ≈ 0.65 → 5V 로 계속 켜 둔 것보다 덜 뜨거움
//   - 보통 타각(30ms 켜기 + 200ms 쉬기 = 13%)은 절대 기다리지 않음.
//     시험한다고 아주 빠르게 계속 찍을 때만 걸림
constexpr uint8_t  SOL_HEAT_DUTY_PCT = 20;
constexpr uint16_t SOL_HEAT_MAX_MS   = 2000;   // 차가운 상태에서 쉬지 않고 쓸 수 있는 켜짐 시간 합

// 5V 솔레노이드에 약 9V 가 걸리므로 켜 두는 시간이 길면 코일이 탄다. 실수로 늘리면 컴파일을 멈춘다
static_assert(SOL_PULSE_MS <= 50, "SOL_PULSE_MS 가 50ms 를 넘으면 코일이 과열될 수 있습니다");
static_assert(SOL_STRIKES >= 1 && SOL_STRIKES <= 3, "SOL_STRIKES 는 1~3 으로 두세요");
static_assert(SOL_RESTRIKE_GAP_MS >= 30, "다시 누르기 전에 핀이 올라올 시간(30ms 이상)이 필요합니다");
// 보통 타각 리듬이 과열 방지 한도보다 낮아야 정상 동작 중에 멈칫하지 않는다
static_assert((uint32_t)SOL_STRIKES * SOL_PULSE_MS * 100 <= (uint32_t)SOL_HEAT_DUTY_PCT * SOL_CELL_MS,
              "보통 타각만으로도 과열 방지가 걸립니다. SOL_COOLDOWN_MS 를 늘리세요");

/* setup() 에서 한 번 (punchInit 이 부름). 6핀을 출력으로 만들고 모두 끕니다. */
void solenoidInit();

/* 6개를 즉시 모두 끕니다. */
void solenoidAllOff();

/*
 * 한 칸 찍기: mask 의 점을 동시에 켜고 → SOL_PULSE_MS 뒤 끄고 (SOL_STRIKES 번 반복) → SOL_COOLDOWN_MS 쉽니다.
 * mask 가 0(빈칸)이면 아무것도 안 하고 바로 돌아옵니다.
 * 과열 방지 한도에 걸리면 찍기 전에 열이 빠질 때까지 기다립니다.
 * 기다리는 동안 linkPoll() 을 불러 ESP32 수신 버퍼가 넘치지 않게 합니다.
 */
void solenoidPunchCell(uint8_t mask);

#endif  // SOLENOID_H
