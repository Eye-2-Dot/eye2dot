#include "link.h"
#include "protocol.h"
#include "job.h"

static ProtoParser parser;
static uint32_t framesRx = 0;

static void sendFrame(uint8_t type, const uint8_t* p, uint8_t n) {
  uint8_t buf[PROTO_MAX_PAYLOAD + 5];
  uint16_t len = protoBuild(buf, type, p, n);
  Serial1.write(buf, len);
}

void linkInit() { Serial1.begin(LINK_BAUD); }

void linkSendAck(uint8_t type, uint8_t value)  { uint8_t p[2] = {type, value};  sendFrame(FT_ACK, p, 2); }
void linkSendNak(uint8_t type, uint8_t reason) { uint8_t p[2] = {type, reason}; sendFrame(FT_NAK, p, 2); }
void linkSendPageDone(uint8_t seq, uint8_t pg) { uint8_t p[2] = {seq, pg};      sendFrame(FT_PAGE_DONE, p, 2); }
void linkSendError(uint8_t code)               { sendFrame(FT_ERROR, &code, 1); }
void linkSendStatus()                          { uint8_t p[6]; jobFillStatus(p); sendFrame(FT_STATUS, p, 6); }

static void reply(uint8_t type, uint8_t nak, uint8_t value) {
  if (nak) linkSendNak(type, nak); else linkSendAck(type, value);
}

static void handle(uint8_t type, const uint8_t* p, uint16_t len) {
  switch (type) {
    case FT_PAGE_BEGIN:
      if (len < 4) { linkSendNak(type, NAK_RANGE); break; }
      reply(type, jobOnPageBegin(p[0], p[1], p[2], p[3]), p[1]);
      break;
    case FT_LINE:
      if (len < 2 || len != (uint16_t)(2 + p[1])) { linkSendNak(type, NAK_RANGE); break; }
      reply(type, jobOnLine(p[0], p[1], p + 2), p[0]);
      break;
    case FT_GO:
      reply(type, jobOnGo(len ? p[0] : 0), 0);
      break;
    case FT_CANCEL:
      jobOnCancel();
      linkSendAck(type, 0);
      break;
    case FT_QUERY:
      linkSendStatus();
      break;
    default: break;
  }
}

void linkUpdate() {
  uint8_t budget = 48;                         // 한 번에 너무 오래 붙잡지 않음 (모터 박자 유지)
  while (budget-- && Serial1.available()) {
    if (parser.feed((uint8_t)Serial1.read())) {
      framesRx++;
      handle(parser.type, parser.payload, parser.len);
    }
  }
}

uint32_t linkFramesRx()  { return framesRx; }
uint32_t linkBadFrames() { return parser.badFrames; }
