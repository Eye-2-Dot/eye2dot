/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기) — ATmega2560 쪽
 * main.cpp — 모듈을 시작하고(setup) 차례로 돌리기만(loop) 합니다.
 *
 * 모듈
 *   link    : ESP32-CAM 과 프레임 주고받기 (Serial1)              link.h
 *   speech  : 소리 — 음성 스트리밍, 소리 신호, 시험음               speech.h
 *   button  : 버튼 떨림 방지, "새로 눌림" (D31 촬영, D32 출력)     button.h
 *   job     : 촬영 요청('C'), 작업 진행 — 안내 재생 → 출력 버튼 → 타각 → 'D'/'E',
 *             상태 LED (상태와 버튼 표는 job.h)                    job.h
 *   punch   : 점자 타각 — 솔레노이드로 찍기 (헤드 이동은 아직 없음)  punch.h
 *   console : USB 시리얼 시험 명령, 상태 출력                      console.h
 * 핀 번호는 pins.h, ESP32 와의 약속은 common/protocol.h 에 있습니다.
 *
 * loop() 와 모든 Update 함수에서는 delay() 를 쓰지 않습니다.
 * loop() 가 멈추면 ESP32 가 보내는 음성 조각을 제때 못 받아 소리가 끊깁니다.
 */

#include <Arduino.h>

#include "button.h"
#include "console.h"
#include "job.h"
#include "link.h"
#include "punch.h"
#include "speech.h"


void setup() {
  // 솔레노이드 핀을 가장 먼저 LOW(끔)로 잡습니다.
  // 리셋 직후 모든 핀은 입력(High-Z, 전기적으로 떠 있는 상태)이라, L298N 입력이 떠 있으면
  // 솔레노이드가 켜질 수 있습니다. consoleInit() 의 500ms 기다림보다 먼저 해서 그 구간을 없앱니다.
  // (부트로더가 도는 동안과 업로드 중에는 코드로 막을 수 없으므로 D22~D27 풀다운 저항이 필요 — README)
  // punchInit() 은 Serial 을 쓰지 않으므로 순서를 바꿔도 로그 순서는 같습니다.
  punchInit();     // 솔레노이드 6개 출력 + 모두 끔

  consoleInit();   // USB 시리얼 + 도움말 (다른 모듈의 로그보다 먼저)
  jobInit();       // 상태 LED
  buttonsInit();   // 버튼 핀 (INPUT_PULLUP)

  // 타이머 설정. 이 시점부터 핀 11에 62.5kHz PWM이 나갑니다.
  // 듀티비는 50%(무음)으로 고정되어 있어 소리는 나지 않습니다.
  speechInit();

  // ESP32 연결 (Serial1). 이 시점부터 프레임을 받을 수 있습니다.
  linkBegin();
}


void loop() {
  uint32_t now = millis();

  linkPoll();           // ESP32 에서 온 바이트를 프레임으로 조립, 음성 조각 요청
  speechUpdate();       // 소리 신호의 켜짐·쉼 진행
  buttonsUpdate(now);   // 버튼 떨림 방지
  jobUpdate(now);       // 알림음, 새 작업, 버튼·재생 끝·시간 초과, LED
  consoleUpdate(now);   // USB 시험 명령, 2초마다 상태 출력
}
