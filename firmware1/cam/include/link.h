/*
 * Eye 2 Dot — ESP32-CAM
 * link.h — ATmega2560 으로 점자·음성·알림음 보내기 (ESP32 쪽)
 *
 * 배선: ESP32 GPIO15 (TX) → Mega D19 (RX1),  Mega D18 (TX1) → 전압 분배 → ESP32 GPIO14 (RX),  GND 공통
 * 프레임 약속·순서·응답 규칙은 firmware1/common/protocol.h 에 있다.
 */

#pragma once

#include <Arduino.h>

#include "protocol.h"

// ─────────────────────────────────────────────
// ATmega 연결 여부
//   false : 프레임은 보내되 응답('R'/'D'/'E')을 기다리지 않고 성공으로 처리한다 (ESP32 단독 시험용).
//           음성 조각도 쉬지 않고 보내므로, 이 상태로 ATmega 를 연결하면 소리가 깨진다.
//   true  : 두 보드를 연결했을 때. 'R' 에 맞춰 음성을 보내고 'D'/'E' 를 기다린다.
// 두 보드를 연결하면 true 로 바꿀 것. 코드를 고치지 않으려면 platformio.ini 의 build_flags 에
//   -DATMEGA_CONNECTED=true
// 를 넣어도 된다. 지금 값은 부팅할 때 시리얼에 찍힌다.
// ─────────────────────────────────────────────
#ifndef ATMEGA_CONNECTED
#define ATMEGA_CONNECTED false
#endif

const int PIN_LINK_RX = 14;   // ← Mega D18 (TX1), 전압 분배를 거쳐서
const int PIN_LINK_TX = 15;   // → Mega D19 (RX1)

// 보낼 점자 (라벨은 1줄). lineCount = 0 이면 "점자 없음" (음성만 있는 작업)
struct BrailleData {
  uint8_t lineCount;
  uint8_t lineLen[BRAILLE_MAX_LINES];
  uint8_t cells[BRAILLE_MAX_LINES][BRAILLE_MAX_CELLS];   // 칸 값 = 6비트 마스크
};

// Serial2 를 LINK_BAUD 로 연다
void linkBegin();

// 작업 하나를 보낸다: 점자 → (음성 있으면) 음성 시작 → 'R' 마다 조각 → 음성 끝 → 'D'/'E' 대기
// 'D' 면 true, 'E'·시간 초과(LINK_REPLY_TIMEOUT_MS)면 false.
// ATMEGA_CONNECTED 가 false 면 응답을 기다리지 않고 true.
// audio 가 nullptr 이거나 audioLen 이 0 이면 점자만 보낸다.
bool linkSendJob(const BrailleData& braille, const uint8_t* audio, size_t audioLen);

// 알림음 프레임을 보낸다 (응답 없음). kind = ALERT_NETWORK / ALERT_CAMERA / ALERT_GENERIC
void linkSendAlert(uint8_t kind);
