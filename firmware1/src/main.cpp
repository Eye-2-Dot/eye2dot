/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * 3단계: Timer PWM 오디오 출력 확인
 *
 * 확인하는 것
 *   - Timer1 PWM이 핀 11로 나오는가
 *   - Timer2 8kHz 인터럽트가 제때 도는가
 *   - RC 필터 → LM386 → 스피커 경로가 살아있는가
 *
 * 배선
 *   핀 11 (PB5, OC1A) → RC 필터 → LM386 앰프 → 스피커
 *
 * 사용법
 *   시리얼 모니터(115200)를 열고 0~5 중 한 글자를 보내면 동작이 바뀝니다.
 *   단계별로 확인하면 소리가 안 날 때 원인을 좁힐 수 있습니다.
 */

#include <Arduino.h>

#include "pins.h"
#include "speech.h"

// 함수 선언 (정의는 파일 하단에)
int  freeRam();
void printHelp();
void printStatus();
void handleCommand(char cmd);

// 시리얼 속도. platformio.ini 의 monitor_speed 와 같아야 합니다.
const uint32_t BAUD_RATE = 115200;

// 상태를 시리얼에 출력하는 간격 (밀리초)
const uint16_t STATUS_INTERVAL = 2000;

// ─────────────────────────────────────────────
// 현재 상태 (표시용)
//
// speech 모듈은 "재생 중인가"만 알려주므로,
// 무엇을 재생 중인지는 여기서 따로 기록해 둡니다.
// ─────────────────────────────────────────────
char     currentCmd  = '0';   // 마지막으로 받은 명령
uint16_t currentFreq = 0;     // 사인파 재생 중이면 그 주파수, 아니면 0
uint32_t lastStatus  = 0;     // 마지막으로 상태를 출력한 시각


// ─────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(BAUD_RATE);
  delay(500);                 // 업로드 직후 첫 출력이 잘리는 것 방지

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // 타이머 설정. 이 시점부터 핀 11에 62.5kHz PWM이 나갑니다.
  // 듀티비는 50%(무음)으로 고정되어 있어 소리는 나지 않습니다.
  speechInit();

  printHelp();
}


// ─────────────────────────────────────────────
// loop()
//
// 소리는 인터럽트가 알아서 내보내므로
// loop()는 명령 처리와 상태 출력만 하면 됩니다.
// ─────────────────────────────────────────────
void loop() {
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
  // 시리얼 명령을 받아야 하기 때문입니다.
  uint32_t now = millis();
  if (now - lastStatus >= STATUS_INTERVAL) {
    lastStatus = now;
    printStatus();
  }

  // 재생 중이면 LED 점등 (소리가 안 들릴 때 코드가 도는지 눈으로 확인)
  digitalWrite(PIN_LED, speechIsPlaying() ? HIGH : LOW);
}


// ─────────────────────────────────────────────
// 명령 처리
//
// 아래 순서대로 확인하면 문제 지점을 좁힐 수 있습니다.
//   '2'에서 "툭" 소리 → 타이머부터 스피커까지 경로는 살아있음
//   '3'이 맑게 들림   → RC 필터가 62.5kHz 캐리어를 잘 깎고 있음
//   '3'이 웅웅거림    → 필터 상수 확인 (컷오프 3~5kHz 권장)
// ─────────────────────────────────────────────
void handleCommand(char cmd) {
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
void printHelp() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  Eye 2 Dot - Firmware 1"));
  Serial.println(F("  3단계: Timer PWM 오디오 출력"));
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
  Serial.println();
}


// ─────────────────────────────────────────────
// 현재 상태 + 남은 RAM
// ─────────────────────────────────────────────
void printStatus() {
  Serial.print(F("["));
  Serial.print(millis() / 1000);
  Serial.print(F("초] 상태: "));

  if (!speechIsPlaying()) {
    Serial.print(F("정지"));
  } else if (currentFreq > 0) {
    Serial.print(currentFreq);
    Serial.print(F("Hz 사인파"));
  } else {
    Serial.print(F("DC 출력"));
  }

  Serial.print(F(" (명령 '"));
  Serial.print(currentCmd);
  Serial.print(F("') | 남은 RAM: "));
  Serial.print(freeRam());
  Serial.println(F(" bytes"));
}


// ─────────────────────────────────────────────
// 남은 RAM 확인
//
// ATmega2560의 RAM은 8KB뿐입니다.
// 4단계에서 서버가 보낸 PCM을 담을 버퍼를 잡아야 하므로
// 여유가 얼마나 있는지 계속 지켜봅니다.
//
// 사인 테이블은 PROGMEM에 있어서 여기 숫자에 영향을 주지 않습니다.
// ─────────────────────────────────────────────
int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
