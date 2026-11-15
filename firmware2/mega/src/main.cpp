/*
 * Eye 2 Dot — 펌웨어 2 (책 점역기) — ATmega2560 쪽
 * main.cpp — 모듈을 시작하고(setup) 차례로 돌리기만(loop) 합니다.
 *
 * 모듈
 *   solenoid : 솔레노이드 6개로 한 칸 찍기 (두 번 타각, 과열 방지)   solenoid.h — 세연
 *   stepper  : 헤드 가로(X)·종이판 세로(Y) 이동                      stepper.h
 *   homing   : 원점 잡기 (스위치 2개)                                 homing.h
 *   punch    : 한 장을 찍는 순서 (칸으로 이동 → 기다림 → 찍기)        punch.h
 *   button   : "출력 계속" 버튼                                        button.h
 *   link     : ESP32 와 통신 (Serial1, 프레임 + 확인 응답)            link.h, ../common/protocol.h
 *   job      : 지금 상태(대기·받는 중·타각 중 …)를 정함               job.h
 *   console  : USB 시험 명령과 상태 출력                               console.h
 * 핀 번호는 pins.h, 조정값은 config.h 에 있습니다.
 *
 * 규칙: 어느 모듈도 delay() 로 멈추지 않습니다. 솔레노이드가 한 칸을 찍는 동안(약 0.3초)만
 *       solenoidPunchCell() 이 돌아오지 않는데, 그동안에는 아래 solenoidIdle() 이 계속 불립니다.
 *
 * USB 시험 명령은 console.cpp 에 있습니다 ('?' 로 목록).
 * 솔레노이드만 따로 시험하려면 tests/solenoid_test 를 올립니다.
 */

#include <Arduino.h>

#include "button.h"
#include "console.h"
#include "homing.h"
#include "job.h"
#include "link.h"
#include "solenoid.h"
#include "stepper.h"

/*
 * 솔레노이드가 기다리는 동안(켜 둔 30ms, 쉬는 200ms 등) 계속 불리는 함수.
 * solenoid.cpp 의 기본(빈) 함수 대신 이것이 쓰입니다 (solenoid.h 설명 참고).
 * ESP32 가 보내는 프레임(상태 질문·취소)을 타각 중에도 받아 답하려고 link 를 돌립니다.
 */
void solenoidIdle() {
  stepperUpdate();
  linkUpdate();
}

void setup() {
  solenoidInit();          // 가장 먼저: 솔레노이드 핀을 LOW 로
  Serial.begin(115200);
  stepperInit();
  homingInit();
  buttonInit();
  linkInit();
  consoleInit();
  jobInit();
}

void loop() {
  stepperUpdate();
  linkUpdate();
  stepperUpdate();         // 모터 박자를 고르게 하려고 한 번 더
  buttonUpdate();
  homingUpdate();
  jobUpdate();
  consoleUpdate();
}
