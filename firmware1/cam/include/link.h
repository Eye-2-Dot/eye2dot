/*
 * Eye 2 Dot — ESP32-CAM
 * link.h — ATmega2560 과 주고받기 (ESP32 쪽)
 *   ESP32 → ATmega : 점자·음성·알림음 프레임
 *   ATmega → ESP32 : 'C' 촬영 요청, 'R' 다음 음성 조각, 'D' 완료, 'E' 실패·취소
 *
 * 배선: ESP32 GPIO15 (TX) → Mega D19 (RX1),  Mega D18 (TX1) → 전압 분배 → ESP32 GPIO14 (RX),  GND 공통
 * 프레임 약속·순서·응답 규칙은 firmware1/common/protocol.h 에 있다.
 */

#pragma once

#include <Arduino.h>

#include "protocol.h"

// ─────────────────────────────────────────────
// ATmega 연결 여부 — 기본값 true (두 보드를 연결해 쓰는 평소 상태)
//   true  : 'R' 에 맞춰 음성을 보내고 'D'/'E' 를 기다리며, ATmega 의 'C' 로 촬영한다.
//   false : ESP32 만 따로 시험할 때(USB 의 c 명령으로 촬영)만 쓴다.
//           프레임은 보내되 응답('R'/'D'/'E')을 기다리지 않고 성공으로 처리하고,
//           Serial2 로 들어오는 'C' 는 무시한다 (선이 없으면 잡음이 'C' 로 읽힐 수 있어서).
//           음성 조각도 쉬지 않고 보내므로, 이 상태로 ATmega 를 연결하면 소리가 깨진다.
// 코드를 고치지 않고 바꾸려면 platformio.ini 의 build_flags 에
//   -DATMEGA_CONNECTED=false
// 를 넣는다. 시험이 끝나면 되돌릴 것. 지금 값은 부팅할 때 시리얼에 찍힌다.
// ─────────────────────────────────────────────
#ifndef ATMEGA_CONNECTED
#define ATMEGA_CONNECTED true
#endif

const int PIN_LINK_RX = 14;   // ← Mega D18 (TX1), 전압 분배를 거쳐서
const int PIN_LINK_TX = 15;   // → Mega D19 (RX1)

// 보낼 점자 (라벨은 1줄). lineCount = 0 이면 "점자 없음" (음성만 있는 작업)
struct BrailleData {
  uint8_t lineCount;
  uint8_t lineLen[BRAILLE_MAX_LINES];
  uint8_t cells[BRAILLE_MAX_LINES][BRAILLE_MAX_CELLS];   // 칸 값 = 6비트 마스크
};

// 작업 하나를 보낸 결과
enum LinkResult {
  LINK_DONE,      // 'D' 출력 완료 (단독 시험 모드면 항상 이것)
  LINK_ERROR,     // 'E' 실패 (타각 실패, ATmega 시간 초과)
  LINK_TIMEOUT,   // LINK_REPLY_TIMEOUT_MS 안에 답이 없음
  LINK_CAPTURE,   // 'C' — 재촬영으로 취소됨. 이어서 linkTakeCapture() 가 true 를 돌려준다
};

// Serial2 를 LINK_BAUD 로 연다
void linkBegin();

// ATmega 가 'C'(촬영)를 보냈는지. 쌓인 바이트 중 'C' 가 아닌 것('R' 등 지난 작업의 찌꺼기)은 버린다.
//   linkTakeCapture()    : 있었으면 true 를 돌려주고 지운다 (촬영을 시작할 때)
//   linkCapturePending() : 지우지 않고 확인만 한다 (작업을 처리하기 전에 "그새 눌렸나" 볼 때)
bool linkTakeCapture();
bool linkCapturePending();

// 쌓인 바이트를 모두 버린다 ('C' 포함). 부팅이 끝났을 때 쓴다
void linkDiscardInput();

// 작업 하나를 보낸다: 점자 → (음성 있으면) 음성 시작 → 'R' 마다 조각 → 음성 끝 → 'D'/'E' 대기
//   보내기 직전에 'C' 가 와 있으면 아무것도 보내지 않고 LINK_CAPTURE.
//   도중에 'C' 가 오거나 'E' 바로 뒤에 'C' 가 오면 LINK_CAPTURE (재촬영).
// ATMEGA_CONNECTED 가 false 면 응답을 기다리지 않고 LINK_DONE.
// audio 가 nullptr 이거나 audioLen 이 0 이면 점자만 보낸다.
LinkResult linkSendJob(const BrailleData& braille, const uint8_t* audio, size_t audioLen);

// 알림음 프레임을 보낸다 (응답 없음). kind = ALERT_NETWORK / ALERT_CAMERA / ALERT_GENERIC
// ATmega 는 종류와 상관없이 실패음을 낸다.
void linkSendAlert(uint8_t kind);
