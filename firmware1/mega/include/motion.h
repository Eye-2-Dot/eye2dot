/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * motion.h — 기구 치수와 모터 조정값 모음
 *
 * 기구를 조립한 뒤 "실제로 재 보고" 고치는 숫자는 전부 이 파일에 있습니다.
 * stepper(모터 움직이기)와 punch(타각 순서), tests/stepper_test 가 같이 씁니다.
 *
 * ── 모터 ──
 *   28BYJ-48 (5V) + ULN2003 보드 2벌. 출력 부품(FA16·FA24 모터 자리, FA15·FA22 축 구멍)이 이 모터 치수입니다.
 *   감속 기어 때문에 힘은 충분하지만 느립니다 (출력축 1분에 10~15바퀴).
 *
 * ── 기구 (A 방식: 카드 정지 / 헤드 이동) ──
 *   모터 A = 헤드  : GT2 벨트 + 20잇 풀리(FA22). 헤드가 레일 위에서 좌우로 움직인다.
 *                    원점 스위치(D34, FA28)가 눌린 자리가 0mm, 스위치에서 멀어지는 쪽이 +.
 *   모터 B = 카드  : 랙(FA12) + 20잇 피니언(FA15). 랙 끝의 PET 날이 적층함 맨 아래 카드 한 장을
 *                    카드 고정 벽(FA10)까지 민다. 랙이 영점 멈춤(FA31)에 닿은 자리가 0mm, 벽 쪽이 +.
 *                    카드 랙에는 스위치가 없어서 멈춤 턱에 살짝 밀어 붙여 영점을 맞춘다.
 *
 * ── 처음 맞출 때 순서 (README "스텝 모터 맞추기") ──
 *   1) 방향      : USB 명령 H(헤드 원점)·Z(랙 영점)가 반대로 가면 *_DIR_INVERT 를 true 로
 *   2) 거리      : tests/stepper_test 의 m50 으로 50mm 가 맞는지 → *_STEPS_PER_MM
 *   3) 카드 공급 : USB 명령 F 로 카드가 벽에 딱 닿는지 → CARD_FEED_MM
 *   4) 첫 칸 자리: USB 명령 T(시험 라벨)로 찍어 보고 여백을 재서 → HEAD_FIRST_CELL_MM
 */

#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>

#include "protocol.h"   // BRAILLE_MAX_CELLS

// ─────────────────────────────────────────────
// 라벨 카드 (서버 server/main.py 의 LABEL_MAX_CELLS 와 같은 약속)
//   카드 가로 100mm − 양 끝 여백 5mm × 2 = 90mm,  90mm ÷ 6mm = 15칸
// ─────────────────────────────────────────────
constexpr uint8_t LABEL_MAX_CELLS = 15;     // 카드 한 장에 찍을 수 있는 칸 수
constexpr float   CELL_PITCH_MM   = 6.0f;   // 칸과 칸 사이 (칸 중심 ↔ 다음 칸 중심)

// ─────────────────────────────────────────────
// 모터 — 28BYJ-48 + ULN2003
// ─────────────────────────────────────────────
// 출력축이 한 바퀴 도는 데 필요한 스텝 수. 이 펌웨어는 "하프 스텝"(코일을 1개-2개-1개… 번갈아 켬)으로 돌린다.
//   모터 안쪽 32스텝 × 기어 약 63.68 = 2038 풀스텝 → 하프 스텝으로 4076
//   (흔히 4096 이라고 적지만 실제 기어비로는 4076 에 가깝다. 거리가 0.5% 어긋나면 이 값을 고친다)
constexpr float MOTOR_STEPS_PER_REV = 4076.0f;

// 이 모터가 놓치지 않고 따라오는 가장 빠른 속도 (하프 스텝/초). 5V 에서 약 800 이 한계라 여유를 둔다.
// 떨기만 하고 안 돌면 아래 *_MAX_SPEED_MM_S 를 줄인다.
constexpr float MOTOR_MAX_STEPS_PER_S = 750.0f;

// ─────────────────────────────────────────────
// 모터 A — 헤드 (ULN2003 IN1~IN4 = D36~D39, 원점 스위치 D34)
// ─────────────────────────────────────────────
// GT2 벨트(이 간격 2mm) × 20잇 풀리 = 한 바퀴에 40mm  →  4076 ÷ 40 = 101.9 스텝/mm
constexpr float HEAD_STEPS_PER_MM   = MOTOR_STEPS_PER_REV / 40.0f;
constexpr bool  HEAD_DIR_INVERT     = false;    // 원점 잡기(H)가 스위치 반대쪽으로 가면 true
constexpr float HEAD_MAX_SPEED_MM_S = 7.0f;     // 가장 빠를 때 (mm/초). 7mm/초 = 약 713스텝/초
constexpr float HEAD_ACCEL_MM_S2    = 40.0f;    // 가속 (mm/초²)
constexpr float HEAD_MAX_TRAVEL_MM  = 125.0f;   // 원점에서 갈 수 있는 최대 거리 (레일 210 − 캐리지 76 − 여유)

// 원점 스위치: COM → GND, NO → D34. 눌리면 LOW. (NC 로 배선했으면 false)
constexpr bool  HEAD_LIMIT_ACTIVE_LOW = true;
constexpr float HOME_FAST_MM_S   = 6.0f;    // 스위치를 찾으러 갈 때
constexpr float HOME_SLOW_MM_S   = 2.0f;    // 물러났다가 다시 천천히 누를 때 (정확한 위치)
constexpr float HOME_BACKOFF_MM  = 2.0f;    // 스위치에서 물러나는 거리
constexpr float HOME_PULLOFF_MM  = 1.0f;    // 원점을 잡은 뒤 스위치에서 떨어져 기다리는 자리

// 원점(스위치가 눌린 자리)에서 "원점에 가장 가까운 칸"까지의 거리.  ★ 가장 중요한 조정값
//   설계값: 원점에서 캐리지 중심 X=120, 카드 벽 X=100, 여백 5mm, 칸 반쪽 3mm → 120 − 92 = 28mm
//   시험 라벨(T)을 찍어 여백이 5mm 가 되도록 고친다.
constexpr float HEAD_FIRST_CELL_MM = 28.0f;

// 원점 스위치가 "위에서 볼 때 오른쪽 끝"에 있으면 true.
//   팀 약속: 줄의 첫 칸은 위에서 볼 때 오른쪽 끝, 칸마다 왼쪽으로 (punch.h).
//   true  → 첫 칸을 원점 쪽에 찍고 멀어지며 진행
//   false → 첫 칸을 원점에서 가장 먼 칸(15번째 자리)에 찍고 원점 쪽으로 진행
constexpr bool  HEAD_HOME_AT_RIGHT = true;

constexpr uint16_t HEAD_SETTLE_MS = 40;     // 멈춘 뒤 찍기 전 기다림 (흔들림 가라앉히기)

// ─────────────────────────────────────────────
// 모터 B — 카드 공급 랙 (ULN2003 IN1~IN4 = D40~D43)
// ─────────────────────────────────────────────
// 피니언 20잇 × 모듈 1 = 피치원 지름 20mm → 한 바퀴에 π × 20 = 62.83mm → 4076 ÷ 62.83 = 64.87 스텝/mm
constexpr float CARD_STEPS_PER_MM   = MOTOR_STEPS_PER_REV / 62.832f;
constexpr bool  CARD_DIR_INVERT     = false;    // 랙 영점(Z)이 멈춤 턱 반대쪽으로 가면 true
constexpr float CARD_MAX_SPEED_MM_S = 10.0f;    // 10mm/초 = 약 649스텝/초
constexpr float CARD_ACCEL_MM_S2    = 50.0f;
constexpr float CARD_MAX_TRAVEL_MM  = 120.0f;   // 영점에서 갈 수 있는 최대 거리

// 랙 영점에서 카드 앞끝이 고정 벽에 닿을 때까지 미는 거리.  ★ 실측할 것
//   너무 길면 카드가 벽에 눌려 휘고, 짧으면 칸이 한쪽으로 쏠린다. USB 명령 F 와 , . 로 찾는다.
constexpr float CARD_FEED_MM = 105.0f;

// 랙 영점 맞추기 (스위치가 없어서 멈춤 턱 FA31 에 밀어 붙인다)
//   전원을 켠 뒤 첫 작업 : 랙이 어디 있는지 모르므로 최대 거리만큼 뒤로 (턱에 닿으면 모터가 헛돈다 — 정상)
//   그다음 작업부터       : 0mm 로 돌아온 뒤 CARD_ZERO_BUMP_MM 만큼만 더 밀어 붙인다
//   이 모터는 전류를 꺼도 기어 때문에 손으로 잘 밀리지 않아서, 작업 사이에는 조금만 밀어 붙여도 된다.
//   작업이 중간에 멈추거나 실패하면 다음 작업에서 다시 끝까지 맞춘다.
constexpr float CARD_ZERO_SPEED_MM_S = 8.0f;
constexpr float CARD_ZERO_BUMP_MM    = 1.5f;
constexpr bool  CARD_FULL_ZERO_EVERY_JOB = false;   // true 면 작업마다 최대 거리만큼 뒤로 (약 16초 더 걸리지만 가장 확실)

// 카드 감지 센서 (D35, 선택 사항). 달았으면 true — 카드를 민 뒤 카드가 없으면 실패('E')로 끝낸다.
//   센서가 없으면 적층함이 비어도 그대로 찍고 완료로 보고한다.
constexpr bool CARD_SENSOR_INSTALLED  = false;
constexpr bool CARD_SENSOR_ACTIVE_LOW = true;   // 카드가 있을 때 LOW 면 true

// ─────────────────────────────────────────────
// 움직이는 도중에 멈추기
// ─────────────────────────────────────────────
// 모터가 움직이는 동안 버튼(촬영·출력 아무거나)을 "뗐다가 다시" 누르면 즉시 멈추고 실패('E')로 끝낸다.
// USB 시리얼로 아무 글자나 보내도 멈춘다. 방향이 반대이거나 무언가 끼었을 때 쓰는 비상 정지.
constexpr bool MOTION_STOP_ON_BUTTON = true;

// ─────────────────────────────────────────────
// 컴파일할 때 검사 — 숫자를 잘못 고치면 여기서 멈춘다
// ─────────────────────────────────────────────
static_assert(LABEL_MAX_CELLS <= BRAILLE_MAX_CELLS,
              "LABEL_MAX_CELLS 가 통신으로 받을 수 있는 칸 수(BRAILLE_MAX_CELLS)보다 큽니다");
static_assert(HEAD_FIRST_CELL_MM + (LABEL_MAX_CELLS - 1) * CELL_PITCH_MM <= HEAD_MAX_TRAVEL_MM,
              "마지막 칸이 헤드가 갈 수 있는 거리(HEAD_MAX_TRAVEL_MM)를 넘습니다");
static_assert(CARD_FEED_MM <= CARD_MAX_TRAVEL_MM,
              "CARD_FEED_MM 이 랙이 갈 수 있는 거리(CARD_MAX_TRAVEL_MM)를 넘습니다");
static_assert(HEAD_MAX_SPEED_MM_S * HEAD_STEPS_PER_MM <= MOTOR_MAX_STEPS_PER_S &&
              HOME_FAST_MM_S * HEAD_STEPS_PER_MM <= MOTOR_MAX_STEPS_PER_S,
              "헤드 속도가 이 모터의 한계(MOTOR_MAX_STEPS_PER_S)를 넘습니다");
static_assert(CARD_MAX_SPEED_MM_S * CARD_STEPS_PER_MM <= MOTOR_MAX_STEPS_PER_S &&
              CARD_ZERO_SPEED_MM_S * CARD_STEPS_PER_MM <= MOTOR_MAX_STEPS_PER_S,
              "카드 랙 속도가 이 모터의 한계(MOTOR_MAX_STEPS_PER_S)를 넘습니다");

#endif  // MOTION_H
