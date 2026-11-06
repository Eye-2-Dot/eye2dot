/*
 * Eye 2 Dot — ESP32-CAM
 * link.cpp — ATmega2560 과 주고받기 (ESP32 쪽)
 *
 * 프레임을 만드는 일은 common/protocol.h 의 함수(두 칩 공용)가 하고,
 * 여기서는 Serial2 로 내보내고, 응답과 촬영 요청('C')을 읽기만 한다.
 */

#include <Arduino.h>

#include "link.h"

// ATmega 와 연결된 시리얼. ESP32-CAM 은 기본 Serial2 핀(16·17)을 PSRAM 이 쓰므로 14·15 로 옮긴다
static HardwareSerial& LINK = Serial2;

// ATmega 가 보낸 'C' 를 아직 처리하지 않았음
static bool capturePending = false;

// 함수 선언
static void       sendFrame(uint8_t type, const uint8_t* payload, uint16_t len);
static int        readReply(uint32_t deadline);
static int        sendAudio(const uint8_t* audio, size_t len, uint32_t deadline);
static int        waitResult(uint32_t deadline);
static bool       captureFollows();
static LinkResult resultOf(int reply);
static void       scanForCapture();


void linkBegin() {
  LINK.begin(LINK_BAUD, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
  Serial.printf("ATmega 연결: Serial2 %lu bps (RX = GPIO%d, TX = GPIO%d)\n",
                (unsigned long)LINK_BAUD, PIN_LINK_RX, PIN_LINK_TX);
  if (ATMEGA_CONNECTED) {
    Serial.println("ATMEGA_CONNECTED = true — ATmega 응답('R'/'D'/'E')과 촬영 요청('C')을 받습니다");
  } else {
    Serial.println("ATMEGA_CONNECTED = false — ESP32 단독 시험 모드 (응답을 기다리지 않고 'C' 도 무시)");
    Serial.println("  두 보드를 연결해 쓸 때는 include/link.h 의 ATMEGA_CONNECTED 를 true(기본값)로 되돌리세요");
  }
}


// ─────────────────────────────────────────────
// 촬영 요청 ('C')
// ─────────────────────────────────────────────

// 쌓인 바이트를 훑어 'C' 가 있으면 표시한다. 나머지는 버린다 (작업 밖에서는 쓸모없는 찌꺼기)
static void scanForCapture() {
  while (LINK.available() > 0) {
    int c = LINK.read();
    // 단독 시험 모드에서는 선이 없어 잡음이 'C' 로 읽힐 수 있으므로 무시한다
    if (c == CMD_CAPTURE && ATMEGA_CONNECTED) {
      capturePending = true;
    }
  }
}


bool linkTakeCapture() {
  scanForCapture();
  bool c = capturePending;
  capturePending = false;
  return c;
}


bool linkCapturePending() {
  scanForCapture();
  return capturePending;
}


void linkDiscardInput() {
  while (LINK.available() > 0) {
    LINK.read();
  }
  capturePending = false;
}


// ─────────────────────────────────────────────
// 보내기
// ─────────────────────────────────────────────

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


LinkResult linkSendJob(const BrailleData& braille, const uint8_t* audio, size_t audioLen) {
  // 보내기 직전에 'C' 가 와 있으면 이 작업은 보내지 않는다 (재촬영이 우선).
  // 그 밖에 쌓인 바이트(지난 작업에서 늦게 도착한 'R' 등)는 여기서 버려진다.
  if (linkCapturePending()) {
    Serial.println("[ATmega] 촬영 요청('C')이 와 있음 — 이 작업은 보내지 않음");
    return LINK_CAPTURE;
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
    return LINK_DONE;
  }

  // ── 3) 결과 기다리기 ──
  if (reply < 0) {
    Serial.printf("[ATmega] 출력 버튼을 기다리는 중 (최대 %lu초)…\n",
                  (unsigned long)(LINK_REPLY_TIMEOUT_MS / 1000));
    reply = waitResult(deadline);
  }
  return resultOf(reply);
}


// 음성을 보낸다. 'R' 을 받을 때마다 조각 하나.
// 도중에 'D'/'E'/'C' 가 오면 (재생 중에 버튼을 누름) 남은 조각은 보내지 않고 그 값을 돌려준다. 없으면 -1
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
      if (c == REPLY_DONE || c == REPLY_ERROR || c == CMD_CAPTURE) {
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


// 'D'/'E'/'C' 중 하나가 오거나 시간이 다 될(-1) 때까지 기다린다. 'R' 과 잡음은 무시
static int waitResult(uint32_t deadline) {
  while (true) {
    int c = readReply(deadline);
    if (c < 0 || c == REPLY_DONE || c == REPLY_ERROR || c == CMD_CAPTURE) {
      return c;
    }
  }
}


// 'E' 바로 뒤에 'C' 가 따라오는지 잠깐 본다 (재촬영: ATmega 는 'E' 'C' 를 연달아 보낸다)
static bool captureFollows() {
  uint32_t until = millis() + LINK_CAPTURE_FOLLOW_MS;
  while (true) {
    int c = readReply(until);
    if (c < 0) {
      return false;
    }
    if (c == CMD_CAPTURE) {
      return true;
    }
  }
}


// 받은 응답 → 결과
static LinkResult resultOf(int reply) {
  if (reply == CMD_CAPTURE || (reply == REPLY_ERROR && captureFollows())) {
    capturePending = true;   // 다음 loop 에서 linkTakeCapture() 가 true → 새로 촬영
    Serial.println("[ATmega] ← 'C' 재촬영 — 이 작업은 취소");
    return LINK_CAPTURE;
  }
  if (reply == REPLY_DONE) {
    Serial.println("[ATmega] ← 'D' 출력 완료");
    return LINK_DONE;
  }
  if (reply == REPLY_ERROR) {
    Serial.println("[ATmega] ← 'E' 실패");
    return LINK_ERROR;
  }
  Serial.println("[ATmega] 응답 없음 — 시간 초과, 실패로 처리");
  return LINK_TIMEOUT;
}
