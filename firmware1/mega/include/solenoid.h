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
 */

#ifndef SOLENOID_H
#define SOLENOID_H

#include <Arduino.h>

// 코일을 켜 두는 시간과 한 번 찍은 뒤 쉬는 시간 (밀리초)
constexpr uint16_t SOL_PULSE_MS    = 30;    // 50 을 넘기면 코일이 뜨거워질 수 있음
constexpr uint16_t SOL_COOLDOWN_MS = 200;   // 코일 식히기 + 핀이 스프링으로 돌아올 시간

// 5V 솔레노이드에 약 9V 가 걸리므로 켜 두는 시간이 길면 코일이 탄다. 실수로 늘리면 컴파일을 멈춘다
static_assert(SOL_PULSE_MS <= 50, "SOL_PULSE_MS 가 50ms 를 넘으면 코일이 과열될 수 있습니다");

/* setup() 에서 한 번 (punchInit 이 부름). 6핀을 출력으로 만들고 모두 끕니다. */
void solenoidInit();

/* 6개를 즉시 모두 끕니다. */
void solenoidAllOff();

/*
 * 한 칸 찍기: mask 의 점을 동시에 켜고 → SOL_PULSE_MS 뒤 끄고 → SOL_COOLDOWN_MS 쉽니다.
 * mask 가 0(빈칸)이면 아무것도 안 하고 바로 돌아옵니다.
 * 기다리는 동안 linkPoll() 을 불러 ESP32 수신 버퍼가 넘치지 않게 합니다.
 */
void solenoidPunchCell(uint8_t mask);

#endif  // SOLENOID_H
