/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * console.cpp — USB 시리얼 시험 콘솔
 */

#include <Arduino.h>

#include "console.h"
#include "job.h"
#include "link.h"
#include "pins.h"
#include "speech.h"

// 시리얼 속도. platformio.ini 의 monitor_speed 와 같아야 합니다.
static const uint32_t BAUD_RATE = 115200;

// 상태를 시리얼에 출력하는 간격 (밀리초)
static const uint16_t STATUS_INTERVAL = 2000;

// ─────────────────────────────────────────────
// 현재 상태 (표시용)
//
// speech 모듈은 "재생 중인가"만 알려주므로,
// 무엇을 재생 중인지는 여기서 따로 기록해 둡니다.
// ─────────────────────────────────────────────
static char     currentCmd  = '0';   // 마지막으로 받은 명령
static uint16_t currentFreq = 0;     // 시험 명령으로 사인파를 켰으면 그 주파수, 아니면 0
static uint32_t lastStatus  = 0;     // 마지막으로 상태를 출력한 시각

// 함수 선언
static void handleCommand(char cmd);
static void printHelp();
static void printStatus();
static int  freeRam();


void consoleInit() {
  Serial.begin(BAUD_RATE);
  delay(500);                 // 업로드 직후 첫 출력이 잘리는 것 방지
  printHelp();
}


void consoleUpdate(uint32_t now) {
  // ── 시리얼 명령 처리 ──
  while (Serial.available() > 0) {
    char c = Serial.read();

    // 시리얼 모니터가 같이 보내는 줄바꿈은 무시합니다.
    if (c == '\n' || c == '\r') {
      continue;
    }
    handleCommand(c);
  }

  // ── 주기적 상태 출력 ──
  // delay() 대신 millis()를 쓰는 이유는 기다리는 동안에도
  // 시리얼 명령과 ESP32 프레임을 받아야 하기 때문입니다.
  if (now - lastStatus >= STATUS_INTERVAL) {
    lastStatus = now;
    printStatus();
  }
}


// ─────────────────────────────────────────────
// 명령 처리
//
// 아래 순서대로 확인하면 문제 지점을 좁힐 수 있습니다.
//   '2'에서 "툭" 소리 → 타이머부터 스피커까지 경로는 살아있음
//   '3'이 맑게 들림   → RC 필터가 62.5kHz 캐리어를 잘 깎고 있음
//   '3'이 웅웅거림    → 필터 상수 확인 (컷오프 3~5kHz 권장)
// ─────────────────────────────────────────────
static void handleCommand(char cmd) {
  // 작업 중이거나 소리 신호가 나는 중에는 소리 시험 명령을 실행하지 않습니다.
  // (작업 음성·소리 신호와 시험음이 서로 끼어들지 않게 — 도움말 '?' 는 언제나 됨)
  if (cmd >= '0' && cmd <= '5') {
    if (jobIsActive()) {
      Serial.println(F("작업 중 — 소리 시험 명령은 작업이 끝난 뒤에 보내세요"));
      return;
    }
    if (speechAlertActive()) {
      Serial.println(F("소리 신호 재생 중 — 끝난 뒤에 보내세요"));
      return;
    }
  }

  switch (cmd) {
    case '0':
      speechStop();
      currentFreq = 0;
      Serial.println(F("[0] 정지 — 무음 (OCR1A = 128)"));
      break;

    case '1':
      // 128은 듀티비 50%. 스피커 입장에서는 아무 변화가 없습니다.
      speechPlayDC(128);
      currentFreq = 0;
      Serial.println(F("[1] 고정값 128 — 무음이어야 정상입니다"));
      break;

    case '2':
      // 128 → 200으로 전압이 한 번 튑니다.
      // 이때 "툭" 소리가 나면 필터~앰프~스피커가 연결된 것입니다.
      speechPlayDC(200);
      currentFreq = 0;
      Serial.println(F("[2] 고정값 200 — \"툭\" 소리가 나면 경로 정상"));
      break;

    case '3':
      speechPlayTone(440);
      currentFreq = 440;
      Serial.println(F("[3] 440Hz 사인파 — 라(A4). 맑게 들리면 필터 정상"));
      break;

    case '4':
      speechPlayTone(1000);
      currentFreq = 1000;
      Serial.println(F("[4] 1000Hz 사인파 — 고음 확인"));
      break;

    case '5':
      speechPlayTone(220);
      currentFreq = 220;
      Serial.println(F("[5] 220Hz 사인파 — 저음 확인"));
      break;

    case '?':
    case 'h':
      printHelp();
      return;   // 상태는 바뀌지 않았으므로 currentCmd 갱신 안 함

    default:
      Serial.print(F("알 수 없는 명령: '"));
      Serial.print(cmd);
      Serial.println(F("'  (? 를 보내면 도움말)"));
      return;
  }

  currentCmd = cmd;
}


// ─────────────────────────────────────────────
// 도움말
// ─────────────────────────────────────────────
static void printHelp() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  Eye 2 Dot - Firmware 1 (ATmega2560)"));
  Serial.println(F("  USB 시험 콘솔: 소리 시험 + 상태 출력"));
  Serial.println(F("========================================"));
  Serial.print  (F("오디오 핀   : "));
  Serial.print  (PIN_AUDIO);
  Serial.println(F(" (PB5 / OC1A)"));
  Serial.println(F("PWM 캐리어  : 62500 Hz  (16MHz / 256)"));
  Serial.println(F("샘플레이트  : 8000 Hz   (16MHz / 8 / 250)"));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("명령 (시리얼로 한 글자 전송)"));
  Serial.println(F("  0 : 정지 (무음)"));
  Serial.println(F("  1 : 고정값 128   — 무음 확인"));
  Serial.println(F("  2 : 고정값 200   — DC. 툭 소리가 나면 경로 연결됨"));
  Serial.println(F("  3 : 440Hz 사인파 — 라 음. 맑게 들리면 필터 정상"));
  Serial.println(F("  4 : 1000Hz 사인파"));
  Serial.println(F("  5 : 220Hz 사인파 — 저음 확인"));
  Serial.println(F("  ? : 이 도움말"));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("순서대로 2 -> 3 으로 확인하는 것을 권합니다."));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("ESP32 연결: Serial1 (D19 RX1 / D18 TX1)"));
  Serial.println(F("버튼"));
  Serial.println(F("  D31 촬영 : 대기 중이면 촬영, 안내 재생·출력 대기 중이면 재촬영"));
  Serial.println(F("  D32 출력 : 안내 음성을 듣는 중이나 들은 뒤 누르면 타각"));
  Serial.println(F("대기가 아닐 때(인식 중·작업 중)는 0~5 시험 명령을 받지 않습니다."));
  Serial.println();
}


// ─────────────────────────────────────────────
// 현재 상태 + 남은 RAM
// ─────────────────────────────────────────────
static void printStatus() {
  Serial.print(F("["));
  Serial.print(millis() / 1000);
  Serial.print(F("초] 상태: "));

  if (!speechIsPlaying()) {
    Serial.print(F("정지"));
  } else if (speechAlertActive()) {
    Serial.print(F("소리 신호"));
  } else if (speechStreamIsActive()) {
    Serial.print(F("작업 음성"));
  } else if (currentFreq > 0) {
    Serial.print(currentFreq);
    Serial.print(F("Hz 사인파"));
  } else {
    Serial.print(F("DC 출력"));
  }

  Serial.print(F(" (명령 '"));
  Serial.print(currentCmd);
  Serial.print(F("') | 작업: "));
  Serial.print(jobStateName());
  Serial.print(F(" | 프레임 정상 "));
  Serial.print(linkFramesOk());
  Serial.print(F(" / 버림 "));
  Serial.print(linkFramesBad());
  Serial.print(F(" | 남은 RAM: "));
  Serial.print(freeRam());
  Serial.println(F(" bytes"));
}


// ─────────────────────────────────────────────
// 남은 RAM 확인
//
// ATmega2560의 RAM은 8KB뿐입니다.
// 음성 링 버퍼(1KB)와 시리얼 버퍼들이 RAM 을 차지하므로
// 여유가 얼마나 있는지 계속 지켜봅니다.
//
// 사인 테이블은 PROGMEM에 있어서 여기 숫자에 영향을 주지 않습니다.
// ─────────────────────────────────────────────
static int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
