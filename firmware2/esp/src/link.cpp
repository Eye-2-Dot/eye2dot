#include "link.h"
#include "protocol.h"

static ProtoParser parser;
static bool gotAck = false, gotNak = false, gotStatus = false, gotDone = false, gotErr = false;
static uint8_t ackType = 0, ackVal = 0, nakType = 0, nakReason = 0, doneSeq = 0, donePage = 0, errCode = 0;
static MegaStatus lastStatus;

void linkBegin() { Serial2.begin(LINK_BAUD, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX); }

void linkPump() {
  while (Serial2.available()) {
    if (!parser.feed((uint8_t)Serial2.read())) continue;
    const uint8_t* p = parser.payload;
    switch (parser.type) {
      case FT_ACK:       if (parser.len >= 2) { gotAck = true; ackType = p[0]; ackVal = p[1]; } break;
      case FT_NAK:       if (parser.len >= 2) { gotNak = true; nakType = p[0]; nakReason = p[1]; } break;
      case FT_STATUS:    if (parser.len >= 6) { gotStatus = true; lastStatus = {p[0], p[1], p[2], p[3], p[4], p[5]}; } break;
      case FT_PAGE_DONE: if (parser.len >= 2) { gotDone = true; doneSeq = p[0]; donePage = p[1]; } break;
      case FT_ERROR:     if (parser.len >= 1) { gotErr = true; errCode = p[0]; } break;
      default: break;
    }
  }
}

static void send(uint8_t type, const uint8_t* p, uint8_t n) {
  uint8_t buf[PROTO_MAX_PAYLOAD + 5];
  uint16_t len = protoBuild(buf, type, p, n);
  Serial2.write(buf, len);
}

bool linkRequest(uint8_t type, const uint8_t* p, uint8_t n, uint8_t* nak, int expect, uint32_t timeoutMs, uint8_t tries) {
  if (nak) *nak = 0;
  for (uint8_t t = 0; t < tries; t++) {
    gotAck = gotNak = false;
    send(type, p, n);
    unsigned long t0 = millis();
    while (millis() - t0 < timeoutMs) {
      linkPump();
      if (gotAck) {
        if (ackType == type && (expect < 0 || ackVal == (uint8_t)expect)) return true;
        gotAck = false;                              // 앞 프레임에 대한 늦은 응답 -> 무시하고 계속 기다림
      }
      if (gotNak && nakType == type) { if (nak) *nak = nakReason; return false; }
      delay(1);
    }
  }
  return false;                                    // 대답이 없음 (*nak == 0)
}

bool linkQuery(MegaStatus& st, uint32_t timeoutMs) {
  gotStatus = false;
  send(FT_QUERY, nullptr, 0);
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    linkPump();
    if (gotStatus) { st = lastStatus; return true; }
    delay(1);
  }
  return false;
}

bool linkTakeDone(uint8_t& seq, uint8_t& page) {
  if (!gotDone) return false;
  gotDone = false; seq = doneSeq; page = donePage; return true;
}
bool linkTakeError(uint8_t& code) {
  if (!gotErr) return false;
  gotErr = false; code = errCode; return true;
}
void linkClearEvents() { gotDone = gotErr = false; }
uint32_t linkBadFrames() { return parser.badFrames; }
