// =============================================================
// Eye 2 Dot - 펌웨어 2 (책 점역기)  ESP32 <-> ATmega2560 대화 규칙
// 두 칩이 "같은 파일"을 가져다 씁니다. 여기만 고치면 양쪽이 함께 바뀝니다.
//
// 프레임 모양 (펌웨어 1과 같은 뼈대)
//   [0xAA 시작] [종류 1] [길이 2, 낮은 바이트 먼저] [내용 0~64] [검사값 1]
//   검사값 = 종류, 길이 2바이트, 내용 전부를 XOR 한 값
//
// 펌웨어 1과 다른 점: Mega -> ESP32 방향도 프레임으로 보냅니다(확인 응답 포함).
// =============================================================
#pragma once
#include <stdint.h>
#include <string.h>

// ---- 한 쪽의 크기 (두 칩과 서버의 약속 — 여기 한 곳에만 적는다) ----
#define PAGE_MAX_LINES     25   // 한 장(A4)에 찍는 줄 수
#define LINE_MAX_CELLS     30   // 한 줄의 칸 수 (서버 server/braille.py 의 MAX_CELLS_PER_LINE 과 같아야 함)

#define PROTO_START        0xAA
#define PROTO_MAX_PAYLOAD  64
#define LINK_BAUD          57600UL   // Mega(16MHz)에서 오차가 작은 속도

// ---- ESP32 -> Mega ----
#define FT_PAGE_BEGIN  'P'  // [작업번호][쪽 번호][전체 쪽 수][이 쪽의 줄 수]
#define FT_LINE        'L'  // [줄 번호][칸 수][칸 값 ... (0~63)]
#define FT_GO          'G'  // [줄 수]  -> 이 쪽을 찍기 시작
#define FT_CANCEL      'X'  // 내용 없음 -> 중단하고 그 자리에 멈춤 (모터 전류 끔)
#define FT_QUERY       'Q'  // 내용 없음 -> Mega가 FT_STATUS로 대답

// ---- Mega -> ESP32 ----
#define FT_ACK         'A'  // [받은 종류][값]
#define FT_NAK         'N'  // [받은 종류][이유 NAK_*]
#define FT_STATUS      'S'  // [상태][작업번호][쪽][줄][끝낸 쪽 수][오류]
#define FT_PAGE_DONE   'D'  // [작업번호][쪽 번호]
#define FT_ERROR       'E'  // [오류 ERR_*]

// NAK 이유
#define NAK_BUSY     1   // 지금은 받을 수 없음 (타각 중 등)
#define NAK_RANGE    2   // 줄 번호/칸 수가 범위를 벗어남
#define NAK_ORDER    3   // P 없이 L/G 가 옴
#define NAK_NOTREADY 4   // 원점을 못 잡은 상태

// 오류 코드
#define ERR_NONE         0
#define ERR_HOME_X       1   // X 원점 스위치를 못 찾음
#define ERR_HOME_Y       2   // Y 원점 스위치를 못 찾음
#define ERR_CANCELLED    3   // 버튼으로 멈춤 / 취소 프레임 / USB 로 멈춤
#define ERR_SWITCH_STUCK 4   // 물러났는데도 스위치가 눌려 있음

// Mega 상태 (FT_STATUS 의 첫 바이트)
#define ST_BOOT        0
#define ST_HOMING      1
#define ST_IDLE        2
#define ST_RECEIVING   3
#define ST_WAIT_BUTTON 4
#define ST_PRINTING    5
#define ST_PARKING     6
#define ST_ERROR       7

static inline uint8_t protoChecksum(uint8_t type, uint16_t len, const uint8_t* p) {
  uint8_t c = type ^ (uint8_t)(len & 0xFF) ^ (uint8_t)(len >> 8);
  for (uint16_t i = 0; i < len; i++) c ^= p[i];
  return c;
}

// out 은 최소 len+5 바이트. 돌려주는 값 = 프레임 전체 길이
static inline uint16_t protoBuild(uint8_t* out, uint8_t type, const uint8_t* payload, uint16_t len) {
  out[0] = PROTO_START;
  out[1] = type;
  out[2] = (uint8_t)(len & 0xFF);
  out[3] = (uint8_t)(len >> 8);
  if (len) memcpy(out + 4, payload, len);
  out[4 + len] = protoChecksum(type, len, payload);
  return (uint16_t)(len + 5);
}

// 한 바이트씩 넣어 주면 프레임이 완성될 때 true
struct ProtoParser {
  uint8_t  state = 0;      // 0 시작 대기, 1 종류, 2 길이L, 3 길이H, 4 내용, 5 검사값
  uint8_t  type = 0;
  uint16_t len = 0;
  uint16_t got = 0;
  uint8_t  payload[PROTO_MAX_PAYLOAD];
  uint32_t badFrames = 0;  // 검사값이 틀렸거나 길이가 이상했던 횟수

  void reset() { state = 0; }

  bool feed(uint8_t b) {
    switch (state) {
      case 0: if (b == PROTO_START) state = 1; break;
      case 1: type = b; state = 2; break;
      case 2: len = b; state = 3; break;
      case 3:
        len |= ((uint16_t)b << 8);
        got = 0;
        if (len > PROTO_MAX_PAYLOAD) { badFrames++; state = 0; }
        else state = (len == 0) ? 5 : 4;
        break;
      case 4:
        payload[got++] = b;
        if (got >= len) state = 5;
        break;
      case 5:
        state = 0;
        if (b == protoChecksum(type, len, payload)) return true;
        badFrames++;
        break;
    }
    return false;
  }
};
