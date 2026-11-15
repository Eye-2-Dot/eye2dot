// 조정값 모음 - 기구를 조립한 뒤 실제로 재 보고 여기 숫자를 맞춥니다.
#pragma once

#include "protocol.h"   // PAGE_MAX_LINES, LINE_MAX_CELLS (두 칩이 같이 쓰는 값)

// ---------- 종이 위의 간격 (서버와의 약속) ----------
#define CELL_PITCH_MM   6.0f     // 칸과 칸 사이
#define LINE_PITCH_MM   10.0f    // 줄과 줄 사이
#define MAX_CELLS       LINE_MAX_CELLS    // 한 줄 최대 칸 수 (common/protocol.h)
#define MAX_LINES       PAGE_MAX_LINES    // 한 장 최대 줄 수 (common/protocol.h)

// ---------- 모터 ----------
// 17HS3430 (NEMA17, 1.8도 = 한 바퀴 200스텝, 정격 0.4A) + A4988·DRV8825 계열 드라이버.
// 드라이버의 전류 제한은 0.4A 에 맞춥니다 (A4988: Vref 약 0.32V, DRV8825: Vref 약 0.2V).
// 1mm 가는 데 필요한 스텝 수 = (200 스텝 x 마이크로스텝) / (풀리 잇수 x 2mm)
//   GT2 40잇(B07) + 1/16 마이크로스텝  ->  3200 / 80 = 40
//   마이크로스텝을 안 쓰면(풀스텝)     ->   200 / 80 = 2.5
#define X_STEPS_PER_MM  40.0f
#define Y_STEPS_PER_MM  40.0f

// "원점 스위치에서 멀어지는 쪽"이 + 방향입니다.
// 원점 잡기를 시켰는데 스위치 반대쪽으로 가면 true 로 바꿉니다.
#define X_DIR_INVERT    false
#define Y_DIR_INVERT    false

// 드라이버 EN 핀: A4988 / DRV8825 는 LOW 일 때 켜짐
#define MOTOR_EN_ACTIVE_LOW  true
// 타각하지 않을 때 모터 전류를 끌지 (발열 감소. 끄면 손으로 밀 수 있음)
#define MOTOR_OFF_WHEN_IDLE  true

#define X_MAX_SPEED_MM_S   60.0f
#define Y_MAX_SPEED_MM_S   40.0f
#define X_ACCEL_MM_S2      500.0f
#define Y_ACCEL_MM_S2      300.0f
#define MIN_SPEED_MM_S     4.0f    // 출발·정지 속도

#define X_MAX_TRAVEL_MM    195.0f  // 원점에서 갈 수 있는 최대 거리 (소프트 리밋)
#define Y_MAX_TRAVEL_MM    265.0f

// ---------- 원점 잡기 ----------
// 스위치: COM -> GND, NO -> 핀.  눌리면 LOW.  NC 로 배선했다면 false.
#define LIMIT_ACTIVE_LOW   true
#define HOME_FAST_MM_S     25.0f
#define HOME_SLOW_MM_S     4.0f
#define HOME_BACKOFF_MM    3.0f
#define HOME_PULLOFF_MM    1.0f    // 원점을 잡은 뒤 스위치에서 떨어져 있을 거리
// 전원을 켜자마자 원점을 잡을지. 방향·스위치 배선을 확인하기 전에 모터가 움직이지 않도록 꺼 둡니다.
// (찍기 전에 장마다 원점을 잡으므로 꺼 두어도 됩니다)
#define AUTO_HOME_ON_BOOT  false
#define REHOME_EACH_PAGE   true    // 장마다 원점을 다시 잡아 누적 오차를 없앰

// ---------- 첫 칸 위치 (가장 중요한 조정값) ----------
// 원점(스위치가 눌린 지점)에서 첫 칸·첫 줄까지의 거리.
// 시험 쪽을 찍어 본 뒤 종이의 여백에 맞게 고칩니다.
#define X_FIRST_CELL_MM    8.0f
#define Y_FIRST_LINE_MM    8.0f

// 뒤로 돌아갈 때(- 방향) 벨트 유격을 없애려고 조금 더 갔다가 되돌아옵니다.
#define X_BACKLASH_MM      1.0f
#define MOVE_SETTLE_MS     40      // 멈춘 뒤 타각 전 기다림 (흔들림 가라앉힘)

// ---------- 솔레노이드 ----------
// 켜는 시간·두 번 타각·과열 방지는 solenoid.h 에 있습니다 (담당: 세연. SOL_PULSE_MS, SOL_STRIKES …).

// ---------- 버튼·동작 ----------
#define BTN_DEBOUNCE_MS        30
#define BTN_LONG_MS            1500   // 버튼 대기 중에 길게 누르면 작업 취소
#define WAIT_BUTTON_FIRST_PAGE false  // 첫 장도 버튼을 기다릴지
#define PARK_X_MM              0.0f   // 한 장을 끝낸 뒤 기다리는 자리
#define PARK_Y_MM              0.0f

#define USE_BUZZER             true

// ---------- 컴파일할 때 검사 - 숫자를 잘못 고치면 여기서 멈춥니다 ----------
static_assert(X_FIRST_CELL_MM + (MAX_CELLS - 1) * CELL_PITCH_MM <= X_MAX_TRAVEL_MM,
              "마지막 칸이 X 가 갈 수 있는 거리(X_MAX_TRAVEL_MM)를 넘습니다");
static_assert(Y_FIRST_LINE_MM + (MAX_LINES - 1) * LINE_PITCH_MM <= Y_MAX_TRAVEL_MM,
              "마지막 줄이 Y 가 갈 수 있는 거리(Y_MAX_TRAVEL_MM)를 넘습니다");
static_assert(PARK_X_MM >= 0 && PARK_X_MM <= X_MAX_TRAVEL_MM && PARK_Y_MM >= 0 && PARK_Y_MM <= Y_MAX_TRAVEL_MM,
              "기다리는 자리(PARK_*)가 갈 수 있는 범위 밖입니다");
// 스텝 펄스는 loop 안에서 시간을 재며 보냅니다. 한 스텝 계산에 0.1~0.2ms 가 걸려 초당 3000스텝이 한계입니다.
static_assert(X_MAX_SPEED_MM_S * X_STEPS_PER_MM <= 3000.0f && Y_MAX_SPEED_MM_S * Y_STEPS_PER_MM <= 3000.0f,
              "모터 속도가 너무 빠릅니다 (초당 3000스텝 이하로)");
static_assert(MAX_CELLS + 2 <= PROTO_MAX_PAYLOAD, "한 줄이 프레임 하나에 들어가지 않습니다");
