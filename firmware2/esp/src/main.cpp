// Eye 2 Dot - 펌웨어 2 (책 점역기) ESP32
// 하는 일: 1초마다 서버에 "fw2 작업 있어?" 를 묻고, 받은 책 점자를 25줄씩(한 장) Mega 로 넘깁니다.
//          한 장이 끝났다는 신호(D)를 받으면 다음 장을 보내고, 다 끝나면 서버에 완료를 알립니다.
#include <Arduino.h>
#include "protocol.h"
#include "net.h"
#include "link.h"

#define LINES_PER_PAGE  PAGE_MAX_LINES    // 한 장의 줄 수 (common/protocol.h — Mega 와 같은 값)

enum EState : uint8_t { E_IDLE, E_SEND, E_WAIT, E_REPORT };

static EState   state = E_IDLE;
static BookJob  job;
static uint8_t  seq = 0, page = 0, pageCount = 0;
static bool     jobOk = true, localJob = false;
static char     failMsg[48] = "";
static const char* failCode = DONE_ERR_PRINT_FAILED;   // 서버에 보고할 오류 이름
static unsigned long tPoll = 0, tQuery = 0, tLastStatus = 0, tWarn = 0;
static uint8_t  busyRetries = 0, shownState = 0xFF, shownLine = 0xFF;
static uint8_t  lostCount = 0;     // "Mega 가 이 쪽을 잊은 것 같다"가 연속으로 보인 횟수

static const char* megaStateName(uint8_t s) {
  switch (s) {
    case ST_BOOT: return "시작"; case ST_HOMING: return "원점 잡는 중"; case ST_IDLE: return "대기";
    case ST_RECEIVING: return "쪽 받는 중"; case ST_WAIT_BUTTON: return "버튼 대기";
    case ST_PRINTING: return "타각 중"; case ST_PARKING: return "원점 복귀"; case ST_ERROR: return "오류";
  }
  return "?";
}

static void fail(const char* msg, const char* code = DONE_ERR_PRINT_FAILED) {
  jobOk = false;
  failCode = code;
  strncpy(failMsg, msg, sizeof(failMsg) - 1); failMsg[sizeof(failMsg) - 1] = 0;
  Serial.print("[실패] "); Serial.println(msg);
  state = E_REPORT;
}

static void startJob(bool local) {
  localJob = local;
  seq++;
  page = 0;
  pageCount = (uint8_t)((job.info.lineCount + LINES_PER_PAGE - 1) / LINES_PER_PAGE);
  jobOk = true; failMsg[0] = 0; busyRetries = 0;
  linkClearEvents();
  Serial.print("쪽 수: "); Serial.println(pageCount);
  if (!MEGA_CONNECTED) {                          // ESP32 만 시험: 내용을 보여 주고 끝
    for (int i = 0; i < job.info.lineCount; i++) {
      Serial.print(i + 1); Serial.print("줄:");
      for (uint8_t c = 0; c < job.len[i]; c++) { Serial.print(' '); Serial.print(job.cell[i][c]); }
      Serial.println();
    }
    state = E_REPORT;
    return;
  }
  state = E_SEND;
}

// 한 쪽을 Mega 로 보냄: P -> L x 줄 수 -> G.  성공하면 true
static bool sendPage(uint8_t* nakOut) {
  int first = page * LINES_PER_PAGE;
  int n = job.info.lineCount - first; if (n > LINES_PER_PAGE) n = LINES_PER_PAGE;
  uint8_t p[PROTO_MAX_PAYLOAD];
  p[0] = seq; p[1] = page; p[2] = pageCount; p[3] = (uint8_t)n;
  if (!linkRequest(FT_PAGE_BEGIN, p, 4, nakOut, page)) return false;
  for (int i = 0; i < n; i++) {
    uint8_t len = job.len[first + i];
    p[0] = (uint8_t)i; p[1] = len;
    memcpy(p + 2, job.cell[first + i], len);
    if (!linkRequest(FT_LINE, p, (uint8_t)(2 + len), nakOut, i)) return false;
  }
  p[0] = (uint8_t)n;
  if (!linkRequest(FT_GO, p, 1, nakOut)) return false;
  Serial.print("[보냄] 쪽 "); Serial.print(page + 1); Serial.print(" / "); Serial.print(pageCount);
  Serial.print("  ("); Serial.print(n); Serial.println("줄)");
  return true;
}

static void makeTestJob() {
  static const uint8_t l0[] = {1, 2, 4, 8, 16, 32, 63};
  job.info.lineCount = 3; job.info.hasId = false; job.info.id[0] = 0;
  job.len[0] = sizeof(l0); memcpy(job.cell[0], l0, sizeof(l0));
  job.len[1] = 10; job.len[2] = 10;
  for (uint8_t i = 0; i < 10; i++) { job.cell[1][i] = 63; job.cell[2][i] = (i & 1) ? 42 : 21; }
}

static void help() {
  Serial.println("--- Eye2Dot 펌웨어 2 (ESP32) 시험 명령 ---");
  Serial.println(" ?  도움말      w  Wi-Fi·서버 정보      h  서버 /health 확인");
  Serial.println(" s  Mega 상태 묻기      t  시험 쪽을 Mega 로 보내기 (서버 없이)      x  취소");
}

static void console() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    MegaStatus st;
    switch (c) {
      case '?': help(); break;
      case 'w': netPrintInfo(); break;
      case 'h': netHealth(); break;
      case 's':
        if (linkQuery(st)) {
          Serial.print("Mega: "); Serial.print(megaStateName(st.state));
          Serial.print(" | 쪽 "); Serial.print(st.page + 1); Serial.print(" 줄 "); Serial.print(st.line + 1);
          Serial.print(" | 오류 "); Serial.print(st.err);
          Serial.print(" | 깨진 프레임 "); Serial.println(linkBadFrames());
        } else Serial.println("Mega 가 대답하지 않습니다 - TX/RX 가 엇갈렸는지, GND, 분압 저항, Mega 전원을 확인");
        break;
      case 't':
        if (state == E_IDLE) { makeTestJob(); Serial.println("시험 쪽을 보냅니다"); startJob(true); }
        else Serial.println("지금은 다른 작업 중입니다");
        break;
      case 'x':
        linkRequest(FT_CANCEL, nullptr, 0);
        if (state != E_IDLE) fail("사용자 취소", DONE_ERR_CANCELLED);
        break;
      default: break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Eye2Dot 펌웨어 2 - ESP32 시작");
  linkBegin();
  netBegin();
  if (netIsUp()) netHealth();
  help();
}

void loop() {
  console();
  linkPump();

  switch (state) {
    case E_IDLE: {
      if (millis() - tPoll < POLL_INTERVAL_MS) break;
      tPoll = millis();
      if (!netEnsureWifi()) break;
      if (MEGA_CONNECTED) {                          // Mega 가 받을 준비가 됐을 때만 작업을 꺼내 옴
        MegaStatus st;
        if (!linkQuery(st)) {
          if (millis() - tWarn > 5000UL) { tWarn = millis(); Serial.println("Mega 응답 없음 - 연결될 때까지 작업을 가져오지 않습니다"); }
          break;
        }
        if (st.state != ST_IDLE) {
          if (millis() - tWarn > 5000UL) { tWarn = millis(); Serial.print("Mega 가 아직 준비 중: "); Serial.println(megaStateName(st.state)); }
          break;
        }
      }
      int r = netFetchJob(job);
      if (r == 1) startJob(false);
      else if (r == 2) { localJob = false; fail("찍을 수 없는 점자 (너무 김)", DONE_ERR_BAD_BRAILLE); }
      break;
    }

    case E_SEND: {
      uint8_t nak = 0;
      if (sendPage(&nak)) {
        tLastStatus = millis(); tQuery = millis(); shownState = 0xFF; busyRetries = 0; lostCount = 0;
        state = E_WAIT;
      } else if (nak == NAK_BUSY && busyRetries < 20) {
        busyRetries++; delay(500);                  // Mega 가 아직 원점 복귀 중 -> 잠깐 뒤 다시
      } else if (nak) {
        char m[40]; snprintf(m, sizeof(m), "Mega 가 거절 (이유 %u)", nak); fail(m);
      } else {
        fail("Mega 응답 없음");
      }
      break;
    }

    case E_WAIT: {
      uint8_t a, b;
      if (linkTakeError(a)) { char m[40]; snprintf(m, sizeof(m), "Mega 오류 %u", a); fail(m, a == ERR_CANCELLED ? DONE_ERR_CANCELLED : DONE_ERR_PRINT_FAILED); break; }
      bool done = linkTakeDone(a, b) && a == seq && b == page;
      if (!done && millis() - tQuery >= 2000UL) {    // 2초마다 진행 상황 확인 (D 가 사라졌을 때도 대비)
        tQuery = millis();
        MegaStatus st;
        if (linkQuery(st)) {
          tLastStatus = millis();
          if (st.state != shownState || st.line != shownLine) {
            shownState = st.state; shownLine = st.line;
            Serial.print("  Mega: "); Serial.print(megaStateName(st.state));
            if (st.state == ST_PRINTING) { Serial.print("  줄 "); Serial.print(st.line + 1); }
            if (st.state == ST_WAIT_BUTTON) Serial.print("  <- 종이를 바꾸고 [출력 계속] 버튼");
            Serial.println();
          }
          if (st.state == ST_ERROR) { char m[40]; snprintf(m, sizeof(m), "Mega 오류 %u", st.err); fail(m); break; }
          if (st.err == ERR_CANCELLED && st.state != ST_PRINTING && st.state != ST_WAIT_BUTTON && st.state != ST_RECEIVING) { fail("기기에서 취소", DONE_ERR_CANCELLED); break; }
          if (st.seq == seq && st.doneCount > page && st.state == ST_IDLE) done = true;
          // G 를 받은 Mega 는 원점 잡기·버튼 대기·타각 중 하나여야 한다. 그런데 "대기"이고 이 쪽을 끝낸 기록도 없으면
          // Mega 가 도중에 다시 시작된 것이다 (전원 흔들림, PC 에서 Mega 시리얼 모니터를 열 때의 리셋 등).
          // 그대로 두면 끝없이 기다리게 되므로 두 번 연속(약 4초) 보이면 실패로 끝낸다.
          else if (st.state == ST_IDLE || st.state == ST_BOOT) {
            if (++lostCount >= 2) { fail("Mega 가 도중에 다시 시작됨"); break; }
          } else lostCount = 0;
        } else if (millis() - tLastStatus > 30000UL) { fail("Mega 응답 끊김"); break; }
      }
      if (done) {
        Serial.print("[끝] 쪽 "); Serial.print(page + 1); Serial.print(" / "); Serial.println(pageCount);
        page++;
        state = (page < pageCount) ? E_SEND : E_REPORT;
      }
      break;
    }

    case E_REPORT:
      if (!localJob) {
        if (netEnsureWifi()) netReportDone(job.info.id, jobOk, failCode);
        else Serial.println("Wi-Fi 가 끊겨 완료 보고를 못 했습니다");
      }
      Serial.println(jobOk ? "==== 작업 완료 ====" : "==== 작업 실패 ====");
      tPoll = millis();
      state = E_IDLE;
      break;
  }
}
