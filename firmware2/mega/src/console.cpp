#include <Arduino.h>
#include <stdlib.h>
#include "console.h"
#include "pins.h"
#include "config.h"
#include "stepper.h"
#include "homing.h"
#include "solenoid.h"
#include "job.h"
#include "link.h"
#include "button.h"
#include "protocol.h"

static unsigned long tKey = 0;
static char line[24];
static uint8_t n = 0;
static unsigned long tStatus = 0;

static void help() {
  Serial.println(F("--- Eye2Dot 펌웨어 2 (Mega) 시험 명령 ---"));
  Serial.println(F(" ?        이 도움말"));
  Serial.println(F(" p        상태 보기"));
  Serial.println(F(" l        스위치·버튼 지금 값   (처음에는 이것부터: 스위치를 손으로 눌러 가며 확인)"));
  Serial.println(F(" h        원점 잡기             (X 가 스위치 쪽으로 가야 합니다. 반대면 바로 아무 글자)"));
  Serial.println(F(" e / d    모터 전류 켜 두기 / 끄기   (끄면 원점을 잊습니다. x y 를 쓰려면 e 다음에 h)"));
  Serial.println(F(" x 30     X(헤드)를 원점에서 30mm 로      y 50  Y(종이판)를 50mm 로   (원점을 잡은 뒤에만)"));
  Serial.println(F(" s 63     솔레노이드 한 칸 찍기 (0~63, 점1=1 점2=2 점3=4 점4=8 점5=16 점6=32)"));
  Serial.println(F(" t        시험 쪽 찍기 (3줄)"));
  Serial.println(F(" c        취소"));
  Serial.println(F(" * 모터가 움직이는 중에는 아무 글자나 보내면 그 자리에 멈춥니다. 버튼을 눌러도 멈춥니다."));
  Serial.println(F(" * 솔레노이드 켜는 시간·횟수는 solenoid.h (SOL_PULSE_MS, SOL_STRIKES)에서 고칩니다."));
}

static void status() {
  Serial.print(F("[")); Serial.print(millis() / 1000); Serial.print(F("초] "));
  Serial.print(jobStateName());
  Serial.print(F(" | X ")); Serial.print(stepperPosMm(AX_X), 1);
  Serial.print(F(" Y "));   Serial.print(stepperPosMm(AX_Y), 1);
  Serial.print(stepperHomed() ? F(" (원점 잡음)") : F(" (원점 모름)"));
  Serial.print(F(" | 받은 프레임 ")); Serial.print(linkFramesRx());
  Serial.print(F(" 깨진 프레임 "));   Serial.print(linkBadFrames());
  Serial.print(F(" | 오류 "));        Serial.println(jobLastError());
}

// 대기(또는 오류) 상태에서만 되는 명령인지 확인. 아니면 이유를 찍고 false
static bool idleOnly() {
  if (jobState() == ST_IDLE || jobState() == ST_ERROR) return true;
  Serial.println(F("작업 중에는 쓸 수 없는 명령입니다 (c 로 취소한 뒤에)"));
  return false;
}

static void moveAxis(AxisId a, float mm) {
  if (!idleOnly()) return;
  if (!stepperHomed()) {
    Serial.println(F("원점을 모릅니다. e (모터 켜 두기) -> h (원점 잡기) 다음에 쓰세요"));
    return;
  }
  stepperMoveToMm(a, mm);          // 0 ~ 최대 거리로 잘라서 움직입니다
}

static void run() {
  char c = line[0];
  float v = (n > 1) ? atof(line + 1) : 0;
  switch (c) {
    case '?': help(); break;
    case 'p': status(); break;
    case 'h': if (idleOnly()) jobStartHoming(); break;
    case 'x': moveAxis(AX_X, v); break;
    case 'y': moveAxis(AX_Y, v); break;
    case 's':
      if (!idleOnly()) break;
      if (v < 0 || v > 63) { Serial.println(F("0~63 사이 숫자를 적으세요")); break; }
      Serial.print(F("찍음 ")); Serial.println((int)v);
      solenoidPunchCell((uint8_t)v);
      break;
    case 't': jobStartTestPage(); break;
    case 'c': jobOnCancel(); break;
    case 'e': if (idleOnly()) { jobKeepMotorsOn(true);  Serial.println(F("모터 켜 둠 (d 로 끔)")); } break;
    case 'd': if (idleOnly()) { jobKeepMotorsOn(false); Serial.println(F("모터 끔")); } break;
    case 'l':
      Serial.print(F("X 스위치 ")); Serial.print(limitRaw(AX_X) ? F("눌림") : F("떨어짐"));
      Serial.print(F(" | Y 스위치 ")); Serial.print(limitRaw(AX_Y) ? F("눌림") : F("떨어짐"));
      Serial.print(F(" | 버튼 ")); Serial.println(buttonIsDown() ? F("눌림") : F("떨어짐"));
      break;
    default: Serial.println(F("모르는 명령. ? 를 입력하세요")); break;
  }
}

void consoleInit() { help(); }

void consoleUpdate() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    // 모터가 움직이는 중에 온 글자는 명령이 아니라 "멈춤"입니다 (줄 끝 CR·LF 는 앞 명령에 딸려 온 것이라 제외)
    if (jobIsMoving() || stepperAnyBusy()) {
      if (c != '\r' && c != '\n') {
        if (jobIsMoving()) jobOnCancel();
        else { stepperStop(AX_X); stepperStop(AX_Y); Serial.println(F("[멈춤] USB 입력")); }   // x / y 로 움직이던 중
        n = 0;
      }
      continue;
    }
    tKey = millis();
    if (c == '\r' || c == '\n') { if (n) { line[n] = 0; Serial.print(F("> ")); Serial.println(line); run(); n = 0; } }
    else if (n < sizeof(line) - 1) line[n++] = c;
  }
  // Enter 가 전달되지 않는 모니터(줄 끝 = 없음)에서도 0.8초 뒤 실행
  if (n && millis() - tKey > 800UL) { line[n] = 0; Serial.print(F("> ")); Serial.println(line); run(); n = 0; }
  if (millis() - tStatus >= 5000UL) { tStatus = millis(); status(); }
}
