/*
 * Eye 2 Dot — ESP32-CAM
 * link.cpp — ATmega2560 으로 점자·음성·알림음 보내기 (ESP32 쪽)
 *
 * 프레임을 만드는 일은 common/protocol.h 의 함수(두 칩 공용)가 하고,
 * 여기서는 Serial2 로 내보내고 응답을 기다리기만 한다.
 */

#include <Arduino.h>

#include "link.h"

// ATmega 와 연결된 시리얼. ESP32-CAM 은 기본 Serial2 핀(16·17)을 PSRAM 이 쓰므로 14·15 로 옮긴다
static HardwareSerial& LINK = Serial2;

// 함수 선언
static void sendFrame(uint8_t type, const uint8_t* payload, uint16_t len);
static int  readReply(uint32_t deadline);
static int  sendAudio(const uint8_t* audio, size_t len, uint32_t deadline);


void linkBegin() {
  LINK.begin(LINK_BAUD, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
  Serial.printf("ATmega 연결: Serial2 %lu bps (RX = GPIO%d, TX = GPIO%d)\n",
                (unsigned long)LINK_BAUD, PIN_LINK_RX, PIN_LINK_TX);
  if (ATMEGA_CONNECTED) {
    Serial.println("ATMEGA_CONNECTED = true — ATmega 응답('R'/'D'/'E')을 기다립니다");
  } else {
    Serial.println("ATMEGA_CONNECTED = false — ESP32 단독 시험 모드 (응답을 기다리지 않음)");
    Serial.println("  두 보드를 연결했다면 include/link.h 의 ATMEGA_CONNECTED 를 true 로 바꾸세요");
  }
}


// 프레임 하나를 만들어 보낸다
static void sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
  static uint8_t buf[FRAME_MAX];
  uint16_t n = linkBuildFrame(type, payload, len, buf);
  if (n == 0) {
    Serial.printf("[ATmega] 프레임을 만들지 못함 (종류 0x%02X, 길이 %u)\n", type, len);
    return;
  }
  LINK.write(buf, n);
}


// 응답 한 바이트를 기다린다. deadline 까지 안 오면 -1
static int readReply(uint32_t deadline) {
  while ((int32_t)(millis() - deadline) < 0) {
    if (LINK.available() > 0) {
      return LINK.read();
    }
    delay(1);
  }
  return -1;
}


void linkSendAlert(uint8_t kind) {
  uint8_t payload = kind;
  sendFrame(FRAME_ALERT, &payload, 1);
  Serial.printf("[ATmega] → 알림음 (종류 %u, 프레임 %u bytes)\n", kind, FRAME_OVERHEAD + 1);
}


bool linkSendJob(const BrailleData& braille, const uint8_t* audio, size_t audioLen) {
  // 지난 작업에서 늦게 도착한 'R' 등이 남아 있으면 버린다
  while (LINK.available() > 0) {
    LINK.read();
  }

  // ── 1) 점자 ──
  uint8_t payload[BRAILLE_PAYLOAD_MAX];
  uint16_t n = linkEncodeBraille(&braille.cells[0][0], BRAILLE_MAX_CELLS,
                                 braille.lineLen, braille.lineCount, payload);
  sendFrame(FRAME_BRAILLE, payload, n);
  Serial.printf("[ATmega] → 점자 %u줄 (프레임 %u bytes)\n", payload[0], n + FRAME_OVERHEAD);

  uint32_t deadline = millis() + LINK_REPLY_TIMEOUT_MS;
  int reply = -1;

  // ── 2) 음성 ──
  if (audio != nullptr && audioLen > 0) {
    reply = sendAudio(audio, audioLen, deadline);
  }

  if (!ATMEGA_CONNECTED) {
    Serial.println("[ATmega] 단독 시험 모드 — 응답을 기다리지 않고 성공으로 처리");
    return true;
  }

  // ── 3) 결과 기다리기 ('R' 이나 잡음은 무시) ──
  if (reply < 0) {
    Serial.printf("[ATmega] 확인/취소 버튼을 기다리는 중 (최대 %lu초)…\n",
                  (unsigned long)(LINK_REPLY_TIMEOUT_MS / 1000));
    while (true) {
      int c = readReply(deadline);
      if (c < 0 || c == REPLY_DONE || c == REPLY_ERROR) {
        reply = c;
        break;
      }
    }
  }

  if (reply == REPLY_DONE) {
    Serial.println("[ATmega] ← 'D' 출력 완료");
    return true;
  }
  if (reply == REPLY_ERROR) {
    Serial.println("[ATmega] ← 'E' 실패·취소");
    return false;
  }
  Serial.println("[ATmega] 응답 없음 — 시간 초과, 실패로 처리");
  return false;
}


// 음성을 보낸다. 'R' 을 받을 때마다 조각 하나.
// 도중에 'D'/'E' 가 오면 (재생 중에 버튼을 누름) 남은 조각은 보내지 않고 그 값을 돌려준다. 없으면 -1
static int sendAudio(const uint8_t* audio, size_t len, uint32_t deadline) {
  uint8_t start[4];
  linkPutU32(start, (uint32_t)len);
  sendFrame(FRAME_AUDIO_START, start, 4);
  Serial.printf("[ATmega] → 음성 시작 %u bytes (%.1f초)\n",
                (unsigned)len, len / (float)AUDIO_SAMPLE_RATE);

  size_t   sent   = 0;
  unsigned chunks = 0;
  uint32_t t0     = millis();
  int      reply  = -1;

  while (sent < len) {
    if (ATMEGA_CONNECTED) {
      int c = readReply(deadline);
      if (c < 0) {
        Serial.println("[ATmega] 'R' 이 오지 않음 — 음성 전송 중단");
        break;
      }
      if (c == REPLY_DONE || c == REPLY_ERROR) {
        reply = c;
        Serial.printf("[ATmega] 음성 도중 '%c' — 남은 조각은 보내지 않음\n", c);
        break;
      }
      if (c != REPLY_READY) {
        continue;   // 잡음
      }
    }
    size_t remain = len - sent;
    uint16_t n = (remain > AUDIO_CHUNK_MAX) ? AUDIO_CHUNK_MAX : (uint16_t)remain;
    sendFrame(FRAME_AUDIO_CHUNK, audio + sent, n);
    sent += n;
    chunks++;
  }

  // 조각마다 찍으면 수백 줄이 되므로 합계만 찍는다
  Serial.printf("[ATmega] → 음성 조각 %u개 (%u/%u bytes, 최대 %u bytes씩, %lums)\n",
                chunks, (unsigned)sent, (unsigned)len, (unsigned)AUDIO_CHUNK_MAX,
                (unsigned long)(millis() - t0));

  if (reply < 0 && sent >= len) {
    sendFrame(FRAME_AUDIO_END, nullptr, 0);   // 마지막 조각 바로 뒤에 ('R' 없이)
    Serial.println("[ATmega] → 음성 끝");
  }
  return reply;
}
