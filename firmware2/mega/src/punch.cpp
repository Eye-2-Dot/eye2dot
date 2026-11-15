#include "punch.h"
#include "stepper.h"
#include "solenoid.h"

enum PState : uint8_t { PU_IDLE, PU_FIND, PU_PRE, PU_MOVE, PU_SETTLE };

static PState st = PU_IDLE;
static const PageBuf* page = nullptr;
static uint8_t  line = 0, col = 0;
static uint16_t count = 0;
static float    tx = 0, ty = 0;
static unsigned long t0 = 0;

void punchStart(const PageBuf* pg) {
  page = pg; line = 0; col = 0; count = 0;
  st = PU_FIND;
}

void punchAbort() {
  st = PU_IDLE;
  solenoidAllOff();
  stepperStop(AX_X); stepperStop(AX_Y);
}

bool     punchBusy()  { return st != PU_IDLE; }
uint8_t  punchLine()  { return line; }
uint16_t punchCount() { return count; }

void punchUpdate() {
  switch (st) {
    case PU_FIND: {
      // 점이 하나라도 있는 다음 칸을 찾음 (빈 칸은 건너뜀)
      while (line < page->lineCount) {
        while (col < page->len[line] && page->cell[line][col] == 0) col++;
        if (col < page->len[line]) break;
        line++; col = 0;
      }
      if (line >= page->lineCount) { st = PU_IDLE; break; }      // 다 찍음
      // 첫 칸은 원점(오른쪽 끝) 가까이, 칸 번호가 커질수록 원점에서 멀어짐
      tx = X_FIRST_CELL_MM + col  * CELL_PITCH_MM;
      ty = Y_FIRST_LINE_MM + line * LINE_PITCH_MM;
      stepperMoveToMm(AX_Y, ty);
      if (X_BACKLASH_MM > 0.0f && tx < stepperPosMm(AX_X) - 0.01f) {
        float pre = tx - X_BACKLASH_MM; if (pre < 0) pre = 0;
        stepperMoveToMm(AX_X, pre);                               // 조금 더 갔다가
        st = PU_PRE;
      } else {
        stepperMoveToMm(AX_X, tx);
        st = PU_MOVE;
      }
      break;
    }
    case PU_PRE:
      if (!stepperAnyBusy()) { stepperMoveToMm(AX_X, tx); st = PU_MOVE; }   // 늘 같은 방향에서 다가감
      break;
    case PU_MOVE:
      if (!stepperAnyBusy()) { t0 = millis(); st = PU_SETTLE; }
      break;
    case PU_SETTLE:
      if (millis() - t0 >= MOVE_SETTLE_MS) {
        // 한 칸 찍기 (두 번 누르고 쉬는 시간까지 약 0.3초). 끝날 때까지 돌아오지 않지만
        // 그동안 main.cpp 의 solenoidIdle() 이 불려 ESP32 통신은 계속 됩니다.
        solenoidPunchCell(page->cell[line][col]);
        if (st != PU_SETTLE) break;        // 찍는 사이에 취소됨 (punchAbort)
        count++;
        col++;
        st = PU_FIND;
      }
      break;
    default: break;
  }
}
