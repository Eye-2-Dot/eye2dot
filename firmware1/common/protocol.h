/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * protocol.h — ESP32-CAM ↔ ATmega2560 시리얼 약속 (두 칩 공용)
 *
 * 이 파일 하나를 mega/ 와 cam/ 이 함께 쓴다 (각 platformio.ini 의 -I ../common).
 * 값을 바꾸면 두 보드를 모두 다시 올려야 한다.
 *
 * AVR(ATmega)과 ESP32 양쪽 컴파일러가 모두 읽으므로
 * <stdint.h>, #define, static inline 만 쓴다 (std:: 와 Arduino 함수 금지).
 *
 * ─────────────────────────────────────────────
 * 배선 (8N1)
 * ─────────────────────────────────────────────
 *   ESP32 GPIO15 (TX) ──────────────────────▶ Mega D19 (RX1)
 *   ESP32 GPIO14 (RX) ◀── 전압 분배 ◀──────── Mega D18 (TX1)   (5V 직결 금지)
 *   GND ─────────────────────────────────── GND               (반드시 공통)
 *
 * ─────────────────────────────────────────────
 * 프레임 (ESP32 → ATmega)
 * ─────────────────────────────────────────────
 *   [0xAA][종류 1B][길이 2B little-endian][내용 0~256B][검사값 1B]
 *   검사값 = 종류 ^ 길이 하위 바이트 ^ 길이 상위 바이트 ^ 내용 전체   (XOR)
 *
 *   0x01 점자     내용 = [줄 수 1B] + 줄마다 [칸 수 1B][칸 값 …]
 *                 칸 값 = 6비트 마스크 (점1 = bit0 … 점6 = bit5)
 *                 줄 수 0 = 점자 없음 → "음성만 있는 작업" (실패 안내 등)
 *   0x02 음성 시작 내용 = [전체 바이트 수 4B little-endian]
 *   0x03 음성 조각 내용 = PCM 1~AUDIO_CHUNK_MAX 바이트 (8kHz, 8비트 unsigned, 모노, 무음 = 128)
 *   0x04 음성 끝   내용 없음
 *   0x05 알림음   내용 = [알림 종류 1B]  — 작업과 상관없이 바로 울린다. 응답 없음
 *
 * ─────────────────────────────────────────────
 * 응답 (ATmega → ESP32, 1바이트)
 * ─────────────────────────────────────────────
 *   'R' 다음 음성 조각을 보내 달라 (링 버퍼에 AUDIO_CHUNK_MAX 이상 여유가 있을 때)
 *   'D' 출력 완료 (확인 버튼 → 타각 성공, 또는 음성만 있는 작업의 재생 끝)
 *   'E' 실패·취소 (취소 버튼, 타각 실패, 시간 초과)
 *
 * ─────────────────────────────────────────────
 * 한 작업의 순서
 * ─────────────────────────────────────────────
 *   ESP32                                    ATmega
 *   점자 ──────────────────────────────────▶ 새 작업 (버튼 대기 시작)
 *   (음성 있으면)
 *   음성 시작 ─────────────────────────────▶ 링 버퍼 비우기
 *             ◀──────────────────────────── 'R'
 *   음성 조각 ─────────────────────────────▶ 링 버퍼에 저장 (절반 차면 재생 시작)
 *             ◀──────────────────────────── 'R'   … 반복
 *   음성 끝 (마지막 조각 바로 뒤, 'R' 없이) ▶ 남은 것 재생
 *             ◀──────────────────────────── 'D' 또는 'E'
 *
 *   - 음성 도중에 'D'/'E' 가 오면 (재생 중에 버튼을 누름)
 *     ESP32 는 남은 조각을 보내지 않고 바로 끝낸다. ATmega 는 이미 오고 있던 조각을 버린다.
 *   - 음성 끝 뒤에 오는 'R' 은 ESP32 가 무시한다.
 *   - 조각이 깨져 버려지면 ATmega 가 LINK_REQUEST_RETRY_MS 뒤에 'R' 을 다시 보낸다.
 *     (그 조각만큼 소리가 빠지지만 통신은 멈추지 않는다)
 *   - 음성 끝 프레임이 깨져도, 다 받았거나 LINK_AUDIO_STALL_MS 동안 새 조각이 없으면 끝난 것으로 본다.
 *   - 새 점자 프레임이 오면 이전 작업은 버리고 새 작업으로 바꾼다.
 *
 * 잘못된 프레임 (검사값 불일치, 길이 이상, 모르는 종류)은 조용히 버리고 다음 0xAA 를 기다린다.
 * PCM 안에도 0xAA 가 흔히 나오므로, 가짜 시작은 종류·길이 검사와 검사값으로 걸러진다.
 */

#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────
// 통신 속도 — 이 한 곳에서만 정한다.
//   16MHz ATmega 는 115200 을 약 2% 오차로 만든다 (보통은 문제없음).
//   하드웨어 시험에서 통신 오류가 잦으면 250000 (ATmega 오차 0%) 으로 바꿀 것.
// ─────────────────────────────────────────────
#define LINK_BAUD 115200UL

// ─────────────────────────────────────────────
// 프레임 종류
// ─────────────────────────────────────────────
#define FRAME_HEADER       0xAA
#define FRAME_BRAILLE      0x01
#define FRAME_AUDIO_START  0x02
#define FRAME_AUDIO_CHUNK  0x03
#define FRAME_AUDIO_END    0x04
#define FRAME_ALERT        0x05

// ─────────────────────────────────────────────
// 응답 (ATmega → ESP32)
// ─────────────────────────────────────────────
#define REPLY_READY  'R'
#define REPLY_DONE   'D'
#define REPLY_ERROR  'E'

// ─────────────────────────────────────────────
// 알림음 종류 (0x05 프레임의 내용)
// ─────────────────────────────────────────────
#define ALERT_NETWORK  1   // 서버 연결 실패·시간 초과, 실패 안내 음성도 못 받음
#define ALERT_CAMERA   2   // 촬영 실패
#define ALERT_GENERIC  3   // 그 밖의 실패 (점자 데이터 이상 등)

// ─────────────────────────────────────────────
// 음성 형식 — 서버(server/speech.py)와 반드시 같아야 한다
// ─────────────────────────────────────────────
#define AUDIO_CHUNK_MAX    256    // 조각 하나의 최대 크기. ATmega 시리얼 수신 버퍼(256B)와 맞춘 값
#define AUDIO_SAMPLE_RATE  8000
#define AUDIO_SILENCE      128

// ─────────────────────────────────────────────
// 점자 배열 크기 (양쪽이 같은 크기로 잡는다)
//   라벨은 항상 한 줄이다. 여유로 4줄까지 받는다.
// ─────────────────────────────────────────────
#define BRAILLE_MAX_LINES  4
#define BRAILLE_MAX_CELLS  40     // 한 줄 최대 칸 수

// 점자 프레임 내용의 최대 길이 = 줄 수 1B + 줄마다 (칸 수 1B + 칸들)
#define BRAILLE_PAYLOAD_MAX  (1 + BRAILLE_MAX_LINES * (1 + BRAILLE_MAX_CELLS))

// 프레임 한 개의 최대 크기
#define FRAME_PAYLOAD_MAX  AUDIO_CHUNK_MAX
#define FRAME_OVERHEAD     5      // 0xAA + 종류 + 길이 2 + 검사값
#define FRAME_MAX          (FRAME_OVERHEAD + FRAME_PAYLOAD_MAX)

// ─────────────────────────────────────────────
// 시간 규칙 (밀리초)
// ─────────────────────────────────────────────
#define LINK_REPLY_TIMEOUT_MS   120000UL  // ESP32 가 'D'/'E' 를 기다리는 최대 시간 (음성 듣고 버튼 누르는 시간 포함)
#define LINK_JOB_TIMEOUT_MS     115000UL  // ATmega 가 버튼을 기다리다 포기하고 'E' 를 보내는 시간 (ESP32 보다 조금 먼저)
#define LINK_REQUEST_RETRY_MS   200UL     // 'R' 을 보냈는데 조각이 안 오면 다시 요청하는 간격
#define LINK_AUDIO_STALL_MS     1000UL    // 이만큼 새 조각이 없으면 음성 수신이 끝난 것으로 본다
#define LINK_BYTE_TIMEOUT_MS    20UL      // 프레임 중간에 이만큼 바이트가 끊기면 그 프레임을 버린다

#if FRAME_PAYLOAD_MAX < BRAILLE_PAYLOAD_MAX
#error "점자 프레임이 프레임 최대 크기보다 크다. BRAILLE_MAX_* 를 줄일 것"
#endif


// ═════════════════════════════════════════════
// 공용 함수 — 프레임 만들기·검사 (두 칩이 같은 코드를 쓴다)
// ═════════════════════════════════════════════

// 검사값: 종류, 길이 2바이트, 내용 전체의 XOR
static inline uint8_t linkChecksum(uint8_t type, uint16_t len, const uint8_t* payload) {
  uint8_t c = (uint8_t)(type ^ (uint8_t)(len & 0xFF) ^ (uint8_t)(len >> 8));
  for (uint16_t i = 0; i < len; i++) {
    c ^= payload[i];
  }
  return c;
}

// 종류별로 허용하는 내용 길이인지. 틀리면 그 프레임은 버린다.
static inline bool linkLengthValid(uint8_t type, uint16_t len) {
  switch (type) {
    case FRAME_BRAILLE:     return len >= 1 && len <= BRAILLE_PAYLOAD_MAX;
    case FRAME_AUDIO_START: return len == 4;
    case FRAME_AUDIO_CHUNK: return len >= 1 && len <= AUDIO_CHUNK_MAX;
    case FRAME_AUDIO_END:   return len == 0;
    case FRAME_ALERT:       return len == 1;
    default:                return false;
  }
}

// 프레임 하나를 out 에 만든다. out 은 FRAME_OVERHEAD + len 바이트 이상이어야 한다.
// 돌려주는 값 = 만든 바이트 수 (길이가 맞지 않으면 0)
static inline uint16_t linkBuildFrame(uint8_t type, const uint8_t* payload, uint16_t len,
                                      uint8_t* out) {
  if (!linkLengthValid(type, len)) {
    return 0;
  }
  out[0] = FRAME_HEADER;
  out[1] = type;
  out[2] = (uint8_t)(len & 0xFF);
  out[3] = (uint8_t)(len >> 8);
  for (uint16_t i = 0; i < len; i++) {
    out[4 + i] = payload[i];
  }
  out[4 + len] = linkChecksum(type, len, payload);
  return (uint16_t)(len + FRAME_OVERHEAD);
}

// 점자 프레임의 내용을 만든다.
//   cells     : 칸 값 배열의 시작 (줄마다 stride 칸씩 떨어져 있다)
//   lineLens  : 줄별 칸 수,  lineCount : 줄 수 (0 이면 "점자 없음")
// 한도(BRAILLE_MAX_*)를 넘는 줄·칸은 잘린다. 돌려주는 값 = 내용 바이트 수
static inline uint16_t linkEncodeBraille(const uint8_t* cells, uint16_t stride,
                                         const uint8_t* lineLens, uint8_t lineCount,
                                         uint8_t* out) {
  if (lineCount > BRAILLE_MAX_LINES) {
    lineCount = BRAILLE_MAX_LINES;
  }
  uint16_t n = 0;
  out[n++] = lineCount;
  for (uint8_t row = 0; row < lineCount; row++) {
    uint8_t count = lineLens[row];
    if (count > BRAILLE_MAX_CELLS) {
      count = BRAILLE_MAX_CELLS;
    }
    out[n++] = count;
    for (uint8_t col = 0; col < count; col++) {
      out[n++] = (uint8_t)(cells[row * stride + col] & 0x3F);
    }
  }
  return n;
}

static inline void linkPutU32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static inline uint32_t linkGetU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
