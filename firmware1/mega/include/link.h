/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * link.h — ESP32-CAM 에서 오는 프레임 받기 (ATmega 쪽)
 *
 * 배선: Mega D19 (RX1) ← ESP32 GPIO15,  Mega D18 (TX1) → 전압 분배 → ESP32 GPIO14,  GND 공통
 * 프레임 약속·순서·응답 규칙은 firmware1/common/protocol.h 에 있습니다.
 *
 * ATmega 는 JSON 을 다루지 않습니다. ESP32 가 점자·음성을 작은 바이트 묶음(프레임)으로
 * 바꿔 보내 주면, 이 모듈이 조립해서 점자 배열과 음성 링 버퍼(speech 모듈)에 나눠 넣습니다.
 */

#ifndef LINK_H
#define LINK_H

#include <Arduino.h>

#include "protocol.h"

// ─────────────────────────────────────────────
// 받은 점자 (마지막으로 받은 점자 프레임)
// ─────────────────────────────────────────────
struct BrailleJob {
  uint8_t lineCount;                                    // 줄 수. 0 이면 "음성만 있는 작업"
  uint8_t lineLen[BRAILLE_MAX_LINES];                   // 줄별 칸 수
  // 칸 값 = 서버가 거울로 바꾼 6비트 값. bit0 = 위에서 볼 때 왼쪽 위 솔레노이드(1번) … bit5 = 6번.
  //         뒤집지 않고 그대로 솔레노이드에 쓴다 (docs/api-spec.md 거울 약속)
  uint8_t cells[BRAILLE_MAX_LINES][BRAILLE_MAX_CELLS];
};

// ─────────────────────────────────────────────
// 지금 작업의 음성 수신 상태
// ─────────────────────────────────────────────
enum LinkAudioState : uint8_t {
  LINK_AUDIO_NONE      = 0,   // 음성 시작을 아직 못 받음 (음성 없는 작업일 수도 있음)
  LINK_AUDIO_RECEIVING = 1,   // 받는 중
  LINK_AUDIO_COMPLETE  = 2,   // 다 받음 (끝 프레임, 전체 길이 도달, 또는 오래 끊김)
  LINK_AUDIO_ABORTED   = 3,   // 그만 받기로 함 (버튼을 누름) — 뒤늦게 오는 조각은 버린다
};

/* setup() 에서 한 번. Serial1 을 LINK_BAUD 로 엽니다. */
void linkBegin();

/*
 * loop() 에서 delay() 없이 매번 부릅니다.
 *   - Serial1 에 들어온 바이트를 프레임으로 조립 (깨진 프레임은 버림)
 *   - 링 버퍼에 자리가 생기면 ESP32 에 'R' (다음 조각) 요청
 */
void linkPoll();

/* 새 점자 프레임(= 새 작업)이 왔으면 한 번만 true */
bool linkTakeNewJob();

/* 마지막으로 받은 점자 */
const BrailleJob& linkJob();

/* 받은 알림음 종류 (protocol.h 의 ALERT_*). 없으면 0. 읽으면 지워집니다 */
uint8_t linkTakeAlert();

/* 지금 작업의 음성 수신 상태 */
LinkAudioState linkAudioState();

/* 지금 작업의 음성을 그만 받습니다 ('R' 을 더 보내지 않고, 오고 있던 조각은 버림) */
void linkAudioAbort();

/* 작업 결과 보고. 작업마다 한 번만 보내집니다 (두 번째 호출은 무시) */
void linkReportDone();    // 'D' 출력 완료
void linkReportError();   // 'E' 실패·취소

/* ESP32 에 촬영을 요청합니다 ('C'). 재촬영이면 linkReportError() 다음에 부릅니다 */
void linkSendCapture();

/* 상태 출력용 */
uint16_t linkFramesOk();      // 정상으로 받은 프레임 수
uint16_t linkFramesBad();     // 버린 프레임 수 (검사값·길이 이상, 중간에 끊김)

#endif  // LINK_H
