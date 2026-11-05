/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * link.cpp — ESP32-CAM 에서 오는 프레임 받기 (ATmega 쪽)
 *
 * 프레임: [0xAA][종류][길이 2B][내용][검사값]   (자세한 약속은 common/protocol.h)
 *
 * 바이트가 하나씩 들어올 때마다 상태 기계로 조립합니다.
 *   0xAA 대기 → 종류 → 길이(하위) → 길이(상위) → 내용 → 검사값
 * 종류·길이가 이상하거나 검사값이 틀리면 그 프레임을 버리고 다시 0xAA 를 기다립니다.
 *
 * 이 파일은 USB Serial 출력도, delay() 도 쓰지 않습니다.
 * loop() 가 멈추지 않아야 음성 수신이 끊기지 않기 때문입니다.
 */

#include <Arduino.h>

#include "link.h"
#include "speech.h"

// ESP32 와 연결된 하드웨어 시리얼 (D19 = RX1, D18 = TX1)
static HardwareSerial& LINK = Serial1;

// ─────────────────────────────────────────────
// 프레임 조립 상태
// ─────────────────────────────────────────────
enum ParseState : uint8_t {
  WAIT_HEADER,
  WAIT_TYPE,
  WAIT_LEN_LO,
  WAIT_LEN_HI,
  WAIT_PAYLOAD,
  WAIT_CHECKSUM,
};

static ParseState parseState = WAIT_HEADER;
static uint8_t    frameType  = 0;
static uint16_t   frameLen   = 0;
static uint16_t   framePos   = 0;
static uint8_t    framePayload[FRAME_PAYLOAD_MAX];   // 검사값을 확인할 때까지 내용을 모아 두는 곳
static uint32_t   lastByteMs = 0;

// ─────────────────────────────────────────────
// 받은 작업
// ─────────────────────────────────────────────
static BrailleJob job;
static bool       newJob       = false;
static bool       jobOpen      = false;   // 'D'/'E' 를 아직 안 보낸 작업이 있음
static uint8_t    pendingAlert = 0;

// ─────────────────────────────────────────────
// 음성 수신
// ─────────────────────────────────────────────
static LinkAudioState audioState    = LINK_AUDIO_NONE;
static uint32_t       audioTotal    = 0;      // 음성 시작 프레임이 알려 준 전체 바이트 수
static uint32_t       audioReceived = 0;
static uint32_t       lastAudioMs   = 0;      // 마지막으로 조각을 받은 시각
static bool           requestPending = false; // 'R' 을 보내고 조각을 기다리는 중
static uint32_t       requestMs      = 0;

static uint16_t framesOk  = 0;
static uint16_t framesBad = 0;

// 함수 선언
static void feedByte(uint8_t b, uint32_t now);
static void handleFrame(uint32_t now);
static bool storeBraille(const uint8_t* p, uint16_t len);
static void requestMoreAudio(uint32_t now);
static void finishAudio();
static bool isKnownType(uint8_t type);


void linkBegin() {
  LINK.begin(LINK_BAUD);
}


void linkPoll() {
  uint32_t now = millis();

  // 프레임 중간에 바이트가 끊겼으면 그 프레임은 버립니다.
  // (바이트 하나가 빠진 채로 다음 프레임 바이트를 이어 붙이면 둘 다 깨지기 때문)
  if (parseState != WAIT_HEADER && now - lastByteMs > LINK_BYTE_TIMEOUT_MS) {
    parseState = WAIT_HEADER;
    framesBad++;
  }

  while (LINK.available() > 0) {
    feedByte((uint8_t)LINK.read(), now);
  }

  if (audioState == LINK_AUDIO_RECEIVING) {
    if (audioReceived >= audioTotal || now - lastAudioMs > LINK_AUDIO_STALL_MS) {
      // 다 받았는데 끝 프레임이 깨졌거나, 조각이 오래 안 오면 끝난 것으로 봅니다
      finishAudio();
    } else {
      requestMoreAudio(now);
    }
  }
}


// ─────────────────────────────────────────────
// 바이트 하나 넣기 — 프레임 조립 상태 기계
// ─────────────────────────────────────────────
static void feedByte(uint8_t b, uint32_t now) {
  lastByteMs = now;

  switch (parseState) {
    case WAIT_HEADER:
      if (b == FRAME_HEADER) {
        parseState = WAIT_TYPE;
      }
      break;

    case WAIT_TYPE:
      if (isKnownType(b)) {
        frameType  = b;
        parseState = WAIT_LEN_LO;
      } else {
        // 가짜 시작이었음. 이 바이트가 0xAA 면 진짜 시작일 수 있으니 이어서 봅니다
        framesBad++;
        parseState = (b == FRAME_HEADER) ? WAIT_TYPE : WAIT_HEADER;
      }
      break;

    case WAIT_LEN_LO:
      frameLen   = b;
      parseState = WAIT_LEN_HI;
      break;

    case WAIT_LEN_HI:
      frameLen |= (uint16_t)b << 8;
      if (!linkLengthValid(frameType, frameLen)) {
        framesBad++;
        parseState = WAIT_HEADER;
        break;
      }
      framePos   = 0;
      parseState = (frameLen > 0) ? WAIT_PAYLOAD : WAIT_CHECKSUM;
      break;

    case WAIT_PAYLOAD:
      framePayload[framePos++] = b;
      if (framePos >= frameLen) {
        parseState = WAIT_CHECKSUM;
      }
      break;

    case WAIT_CHECKSUM:
      parseState = WAIT_HEADER;
      if (b == linkChecksum(frameType, frameLen, framePayload)) {
        framesOk++;
        handleFrame(now);
      } else {
        framesBad++;
      }
      break;
  }
}


static bool isKnownType(uint8_t type) {
  return type >= FRAME_BRAILLE && type <= FRAME_ALERT;
}


// ─────────────────────────────────────────────
// 검사값까지 맞은 프레임 처리
// ─────────────────────────────────────────────
static void handleFrame(uint32_t now) {
  switch (frameType) {
    case FRAME_BRAILLE:
      if (!storeBraille(framePayload, frameLen)) {
        framesOk--;
        framesBad++;
        return;
      }
      // 새 작업. 이전 작업의 음성이 아직 나오고 있으면 멈춥니다
      // (시험음·알림음은 건드리지 않음 — 그쪽은 main 이 정합니다)
      if (speechStreamIsActive()) {
        speechStop();
      }
      audioState     = LINK_AUDIO_NONE;
      requestPending = false;
      jobOpen        = true;
      newJob         = true;
      break;

    case FRAME_AUDIO_START:
      if (!jobOpen) {
        // 점자 프레임 없이 음성이 옴 (점자 프레임이 깨졌거나 이미 끝낸 작업).
        // 출력할 수 없으니 바로 실패를 알려 ESP32 가 오래 기다리지 않게 합니다.
        LINK.write(REPLY_ERROR);
        audioState = LINK_AUDIO_ABORTED;
        return;
      }
      if (audioState == LINK_AUDIO_ABORTED) {
        return;   // 이 작업은 이미 음성을 그만 받기로 함
      }
      audioTotal     = linkGetU32(framePayload);
      audioReceived  = 0;
      requestPending = false;
      lastAudioMs    = now;
      audioState     = LINK_AUDIO_RECEIVING;
      speechStreamBegin();
      break;

    case FRAME_AUDIO_CHUNK:
      if (audioState != LINK_AUDIO_RECEIVING) {
        return;   // 그만 받기로 한 뒤에 뒤늦게 온 조각 등은 버립니다
      }
      requestPending = false;
      lastAudioMs    = now;
      // 'R' 은 자리가 있을 때만 보내므로 보통은 항상 들어갑니다.
      // 못 넣었으면 그 조각만큼 소리가 빠질 뿐 계속 진행합니다.
      if (speechStreamWrite(framePayload, frameLen)) {
        audioReceived += frameLen;
      }
      break;

    case FRAME_AUDIO_END:
      if (audioState == LINK_AUDIO_RECEIVING) {
        finishAudio();
      }
      break;

    case FRAME_ALERT:
      pendingAlert = framePayload[0];
      break;
  }
}


// 점자 프레임 내용 → 점자 배열. 형식이 어긋나면 false (배열은 바뀌지 않음)
static bool storeBraille(const uint8_t* p, uint16_t len) {
  uint8_t lineCount = p[0];
  if (lineCount > BRAILLE_MAX_LINES) {
    return false;
  }

  // 먼저 형식만 검사합니다 (중간에 틀리면 이전 작업의 점자를 망가뜨리지 않도록)
  uint16_t pos = 1;
  for (uint8_t row = 0; row < lineCount; row++) {
    if (pos >= len) {
      return false;
    }
    uint8_t count = p[pos++];
    if (count > BRAILLE_MAX_CELLS || pos + count > len) {
      return false;
    }
    pos += count;
  }
  if (pos != len) {
    return false;
  }

  // 검사를 통과했으면 옮깁니다
  job.lineCount = lineCount;
  pos = 1;
  for (uint8_t row = 0; row < lineCount; row++) {
    uint8_t count = p[pos++];
    job.lineLen[row] = count;
    for (uint8_t col = 0; col < count; col++) {
      job.cells[row][col] = p[pos++] & 0x3F;
    }
  }
  return true;
}


// 링 버퍼에 조각 하나가 들어갈 자리가 있으면 'R' 로 다음 조각을 요청합니다.
// 한 번에 하나만 요청하고, LINK_REQUEST_RETRY_MS 동안 안 오면 다시 요청합니다.
static void requestMoreAudio(uint32_t now) {
  if (parseState != WAIT_HEADER) {
    return;   // 조각을 받는 도중이면 기다립니다
  }
  if (speechStreamFree() < AUDIO_CHUNK_MAX) {
    return;
  }
  if (!requestPending || now - requestMs >= LINK_REQUEST_RETRY_MS) {
    LINK.write(REPLY_READY);
    requestPending = true;
    requestMs      = now;
  }
}


static void finishAudio() {
  audioState     = LINK_AUDIO_COMPLETE;
  requestPending = false;
  speechStreamEnd();   // 남은 것을 다 재생하면 끝납니다
}


// ─────────────────────────────────────────────
// main 에 보여 주는 함수들
// ─────────────────────────────────────────────
bool linkTakeNewJob() {
  bool n = newJob;
  newJob = false;
  return n;
}


const BrailleJob& linkJob() {
  return job;
}


uint8_t linkTakeAlert() {
  uint8_t a = pendingAlert;
  pendingAlert = 0;
  return a;
}


LinkAudioState linkAudioState() {
  return audioState;
}


void linkAudioAbort() {
  if (audioState == LINK_AUDIO_NONE || audioState == LINK_AUDIO_RECEIVING) {
    audioState = LINK_AUDIO_ABORTED;
  }
  requestPending = false;
}


void linkReportDone() {
  if (!jobOpen) {
    return;
  }
  LINK.write(REPLY_DONE);
  jobOpen = false;
  linkAudioAbort();   // 작업이 끝났으니 혹시 남은 조각이 와도 버립니다
}


void linkReportError() {
  if (!jobOpen) {
    return;
  }
  LINK.write(REPLY_ERROR);
  jobOpen = false;
  linkAudioAbort();
}


uint16_t linkFramesOk() {
  return framesOk;
}


uint16_t linkFramesBad() {
  return framesBad;
}
