/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * stepper.h — 스텝 모터 2개 움직이기 (헤드, 카드 공급 랙)
 *
 * 28BYJ-48 모터를 ULN2003 보드로 돌립니다. 보드의 IN1~IN4 에 코일 4개가 하나씩 이어져 있고,
 * 프로그램이 "어느 코일을 켤지"를 순서대로 바꿔 주면 모터가 한 스텝씩 돕니다.
 *   모터 A = 헤드       IN1~IN4 = D36~D39,  원점 스위치 D34
 *   모터 B = 카드 랙    IN1~IN4 = D40~D43
 * 거리·속도·방향 같은 조정값은 motion.h 에 있습니다.
 *
 * ── 쓰는 법 ──
 *   모든 이동 함수는 "다 움직일 때까지 기다렸다가" 돌아옵니다 (punchBraille 과 같은 방식).
 *   기다리는 동안 linkPoll() 을 불러 ESP32 수신 버퍼가 넘치지 않게 합니다.
 *   그래서 작업이 없을 때(대기)나 타각 중에만 부릅니다. 음성 재생 중에 부르면 소리가 끊깁니다.
 *
 * ── 움직이는 도중에 멈추기 ──
 *   움직이는 동안 버튼을 뗐다가 다시 누르거나 USB 로 아무 글자나 보내면 즉시 멈추고 코일을 끕니다.
 *   한 번 멈추면 stepperClearStop() 을 부를 때까지 모든 이동 함수가 움직이지 않고 바로 돌아옵니다.
 *   → 부르는 쪽은 순서 중간중간 stepperStopped() 를 보고 실패로 끝내면 됩니다.
 *
 * ── 타이머를 쓰지 않습니다 ──
 *   Timer1·Timer2 는 오디오 전용(pins.h)이라, 스텝 간격은 micros() 로 시간을 재서 맞춥니다.
 *   tone(), D9~D12 analogWrite 는 여기서도 쓰지 않습니다.
 *
 * ── 위치 ──
 *   헤드 : 원점 스위치가 눌린 자리 = 0mm, 스위치에서 멀어지는 쪽이 +
 *   카드 : 랙이 영점 멈춤(FA31)에 닿은 자리 = 0mm, 카드 고정 벽 쪽이 +
 */

#ifndef STEPPER_H
#define STEPPER_H

#include <Arduino.h>

enum StepAxis : uint8_t {
  AXIS_HEAD = 0,   // 모터 A — 헤드
  AXIS_CARD = 1,   // 모터 B — 카드 공급 랙
};

/*
 * setup() 에서 한 번 (punchInit 이 부름). 핀을 출력으로 만들고 두 모터의 코일을 모두 끕니다.
 * Serial.begin() 보다 먼저 불리므로 이 안에서는 Serial 로그를 쓰지 않습니다.
 */
void stepperInit();

/*
 * 두 모터의 코일 전류를 켜고 끕니다.
 * 끄면 발열·배터리 소모가 없습니다. 헤드는 다음에 원점부터 다시 잡습니다.
 */
void stepperEnable(bool on);
bool stepperEnabled();

/*
 * 그 축을 mm 자리로 옮깁니다 (천천히 출발 → 빠르게 → 천천히 멈춤).
 * 헤드는 원점을 잡은 뒤에만 움직이고, 0 ~ 최대 거리 밖으로는 나가지 않게 자릅니다.
 * 돌려주는 값: 끝까지 옮겼으면 true. 헤드 원점을 아직 안 잡았거나 도중에 멈췄으면 false.
 */
bool stepperMoveTo(StepAxis axis, float mm);

/*
 * 헤드와 카드 랙을 "동시에" 각자의 mm 자리로 옮깁니다 (시간을 아끼려고). 조건과 돌려주는 값은 위와 같습니다.
 */
bool stepperMoveBothTo(float headMm, float cardMm);

/*
 * 지금 자리에서 mm 만큼 옮깁니다 (+ / −). 범위 검사를 하지 않습니다.
 * 원점 잡기, 영점 밀어 붙이기, USB 명령으로 조금씩 옮기기에 씁니다.
 */
void stepperJog(StepAxis axis, float mm, float speedMmS);

/* 그 축의 지금 위치 (mm) */
float stepperPos(StepAxis axis);

/*
 * 헤드 원점 잡기: 스위치 쪽으로 가서 누르고 → 조금 물러났다가 → 천천히 다시 눌러 0mm 로 정한 뒤
 * HOME_PULLOFF_MM 자리에서 기다립니다.
 * 돌려주는 값: 성공 true. 끝까지 갔는데 스위치가 안 눌리거나(배선·방향),
 *              물러나도 계속 눌려 있거나(스위치 걸림·NC 배선), 도중에 멈췄으면 false.
 */
bool stepperHomeHead();
bool stepperHeadHomed();

/* 원점 스위치가 지금 눌려 있는지 (떨림 걸러내지 않은 값 — 상태 출력·시험용) */
bool stepperHeadLimitPressed();

/*
 * 카드 랙 영점 맞추기: 멈춤 턱(FA31)에 밀어 붙여 0mm 로 정합니다.
 *   full = true  : 랙이 어디 있는지 모를 때. 최대 거리만큼 천천히 뒤로 (턱에 닿으면 모터가 헛돈다 — 정상)
 *   full = false : 0mm 로 돌아온 뒤 CARD_ZERO_BUMP_MM 만큼만 더 민다 (작업 사이의 작은 오차 지우기)
 */
void stepperZeroCard(bool full);
bool stepperCardZeroed();   // 랙 위치를 믿을 수 있는지 (전원을 켠 뒤·도중에 멈춘 뒤에는 false)

/* 카드 감지 센서(D35)에 카드가 보이는지. 센서를 안 달았으면(motion.h) 항상 true */
bool stepperCardPresent();

/*
 * 움직이는 도중에 멈췄는지 / 멈춤 표시 지우기.
 * 새 작업(타각, USB 모터 명령)을 시작할 때 stepperClearStop() 을 한 번 부릅니다.
 */
bool stepperStopped();
void stepperClearStop();

#endif  // STEPPER_H
