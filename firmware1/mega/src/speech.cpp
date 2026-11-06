/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * speech.cpp — 오디오 출력 구현
 *
 * 타이머는 Arduino 함수(analogWrite, tone) 대신 레지스터로 직접 다룹니다.
 * analogWrite는 주파수를 고를 수 없고, tone은 Timer2를 통째로 가져가서
 * 우리가 쓸 8kHz 샘플 인터럽트를 만들 수 없기 때문입니다.
 */

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>

#include "pins.h"
#include "protocol.h"
#include "speech.h"

// 서버·ESP32 와 약속한 음성 형식(protocol.h)과 재생 설정이 어긋나면 컴파일을 멈춥니다.
static_assert(SPEECH_SAMPLE_RATE == AUDIO_SAMPLE_RATE, "speech.h 와 protocol.h 의 샘플레이트가 다릅니다");
static_assert(SPEECH_SILENCE == AUDIO_SILENCE, "speech.h 와 protocol.h 의 무음 값이 다릅니다");
static_assert((SPEECH_STREAM_BUFFER & (SPEECH_STREAM_BUFFER - 1)) == 0, "링 버퍼 크기는 2의 거듭제곱이어야 합니다");
static_assert(SPEECH_STREAM_BUFFER / 2 >= AUDIO_CHUNK_MAX, "링 버퍼가 음성 조각보다 너무 작습니다");

// ─────────────────────────────────────────────
// 소리 신호 표 — 높이·길이·횟수를 바꾸려면 여기만 고치면 됩니다
//
//   freq  : 삐 소리 높이 (Hz, 1 ~ SPEECH_MAX_FREQ)
//   onMs  : 삐 한 번의 길이
//   offMs : 삐와 삐 사이 쉬는 길이 (1번만 울리면 쓰이지 않음)
//   count : 삐 횟수 (1 이상)
//
// 줄 순서는 speech.h 의 enum SpeechBeep 순서와 같아야 합니다.
// ─────────────────────────────────────────────
struct BeepPattern {
  uint16_t freq;
  uint16_t onMs;
  uint16_t offMs;
  uint8_t  count;
};

static const BeepPattern BEEP_PATTERNS[] = {
  // freq  onMs  offMs  count
  { 1000,  100,     0,     1 },   // BEEP_READY       : 준비·촬영 — 짧은 삐
  {  400,   80,    80,     2 },   // BEEP_WRONG       : 잘못 누름 — 낮은 삐삐
  {  300,  600,     0,     1 },   // BEEP_FAIL        : 서버·카메라 등 실패 — 낮고 긴 삐
  { 1500,   80,    80,     2 },   // BEEP_DONE        : 타각 완료 — 높은 삐삐
  {  400,  150,   150,     3 },   // BEEP_LOW_BATTERY : 배터리 부족 — 낮은 삐 3번
};
static_assert(sizeof(BEEP_PATTERNS) / sizeof(BEEP_PATTERNS[0]) == BEEP_COUNT,
              "소리 신호 표의 줄 수가 speech.h 의 SpeechBeep 개수와 다릅니다");

// ESP32 가 보내는 알림음 프레임(protocol.h 의 ALERT_NETWORK·CAMERA·GENERIC)을 낼 소리.
// 프레임의 바이트 값은 그대로 두고, ATmega 쪽에서는 종류와 상관없이 모두 실패 소리로 냅니다.
static const SpeechBeep ALERT_FRAME_BEEP = BEEP_FAIL;

// ─────────────────────────────────────────────
// 사인 테이블
//
// 값 = round(127.5 + 127.5 * sin(2*pi*i/256))
//   → 범위 0~255, 중심 128. 8비트 unsigned PCM과 같은 형식입니다.
//
// PROGMEM을 붙이면 플래시(256KB)에만 올라가고 RAM(8KB)은 쓰지 않습니다.
// 붙이지 않으면 부팅할 때 RAM으로 복사되어 256바이트를 잡아먹습니다.
// 음성 링 버퍼(1KB)가 RAM 을 쓰므로 RAM은 아껴 둡니다.
//
// 읽을 때는 반드시 pgm_read_byte()를 써야 합니다.
// 그냥 배열처럼 접근하면 AVR은 같은 주소의 RAM을 읽어서 쓰레기 값이 나옵니다.
// ─────────────────────────────────────────────
static const uint8_t SINE_TABLE[256] PROGMEM = {
  128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 162, 165, 167, 170, 173,
  176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 203, 206, 208, 211, 213, 215,
  218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 238, 240, 241, 243, 244,
  245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255,
  255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 246,
  245, 244, 243, 241, 240, 238, 237, 235, 234, 232, 230, 228, 226, 224, 222, 220,
  218, 215, 213, 211, 208, 206, 203, 201, 198, 196, 193, 190, 188, 185, 182, 179,
  176, 173, 170, 167, 165, 162, 158, 155, 152, 149, 146, 143, 140, 137, 134, 131,
  128, 124, 121, 118, 115, 112, 109, 106, 103, 100,  97,  93,  90,  88,  85,  82,
   79,  76,  73,  70,  67,  65,  62,  59,  57,  54,  52,  49,  47,  44,  42,  40,
   37,  35,  33,  31,  29,  27,  25,  23,  21,  20,  18,  17,  15,  14,  12,  11,
   10,   9,   7,   6,   5,   5,   4,   3,   2,   2,   1,   1,   1,   0,   0,   0,
    0,   0,   0,   0,   1,   1,   1,   2,   2,   3,   4,   5,   5,   6,   7,   9,
   10,  11,  12,  14,  15,  17,  18,  20,  21,  23,  25,  27,  29,  31,  33,  35,
   37,  40,  42,  44,  47,  49,  52,  54,  57,  59,  62,  65,  67,  70,  73,  76,
   79,  82,  85,  88,  90,  93,  97, 100, 103, 106, 109, 112, 115, 118, 121, 124,
};

// ─────────────────────────────────────────────
// 재생 상태
// ─────────────────────────────────────────────
enum SpeechState : uint8_t {
  SPEECH_STOPPED = 0,   // 무음
  SPEECH_DC      = 1,   // 고정값 출력 (ISR은 아무것도 하지 않음)
  SPEECH_TONE    = 2,   // 사인파 재생 (ISR이 매 샘플 OCR1A를 갱신)
  SPEECH_STREAM  = 3,   // PCM 스트리밍 (ISR이 링 버퍼에서 한 바이트씩 꺼내 OCR1A를 갱신)
};

// ─────────────────────────────────────────────
// ISR과 메인이 함께 쓰는 변수들
//
// volatile이 반드시 필요합니다.
// 없으면 컴파일러는 "이 변수는 이 함수 안에서 아무도 안 바꾸네"라고 판단해
// 레지스터에 캐싱하거나 조건문을 통째로 지워버립니다.
// ISR이 언제 끼어들어 값을 바꾸는지 컴파일러는 알 수 없기 때문입니다.
// ─────────────────────────────────────────────

// 위상 누산기 (16비트 고정소수점)
//   상위 8비트 = 테이블 인덱스 (0~255)
//   하위 8비트 = 소수부. 이것 덕분에 임의의 주파수를 낼 수 있습니다.
static volatile uint16_t phase = 0;

// 한 샘플마다 위상에 더할 양. 이 값이 곧 주파수입니다.
static volatile uint16_t phaseInc = 0;

static volatile SpeechState speechState = SPEECH_STOPPED;

// ─────────────────────────────────────────────
// PCM 스트리밍 링 버퍼
//
//   ringHead : 다음에 쓸 위치. loop() 쪽(speechStreamWrite)만 바꿉니다
//   ringTail : 다음에 읽을 위치. ISR만 바꿉니다
//   head == tail 이면 비어 있음. 한 칸은 늘 비워 두어 "가득 참"과 구분합니다.
//
// head·tail 은 16비트라 AVR에서 두 번에 나눠 읽고 씁니다.
// 그래서 loop() 쪽에서 읽거나 쓸 때는 반드시 ATOMIC_BLOCK 안에서 합니다.
// (쓰는 도중에 ISR이 끼면 반쯤 바뀐 값을 읽어 엉뚱한 바이트를 재생합니다)
// ─────────────────────────────────────────────
static const uint16_t RING_MASK = SPEECH_STREAM_BUFFER - 1;

static uint8_t           ring[SPEECH_STREAM_BUFFER];
static volatile uint16_t ringHead      = 0;
static volatile uint16_t ringTail      = 0;
static volatile bool     streamPlaying = false;   // 미리 채우기가 끝나 재생 중
static volatile bool     streamEnded   = false;   // 더 올 데이터 없음

// ─────────────────────────────────────────────
// 소리 신호 진행 상태 — 삐(켜짐) / 쉼(꺼짐)을 count 번 (표는 파일 맨 위)
// 인터럽트가 아니라 loop() 의 speechUpdate() 가 millis() 로 진행합니다.
// ─────────────────────────────────────────────
static bool               alertActive = false;
static bool               alertToneOn = false;
static uint8_t            alertLeft   = 0;     // 남은 삐 횟수
static uint32_t           alertNextMs = 0;     // 삐↔쉼이 바뀔 시각
static const BeepPattern* alertPat    = nullptr;


// ─────────────────────────────────────────────
// speechInit() — 타이머 두 개를 설정합니다
// ─────────────────────────────────────────────
void speechInit() {
  // ── 출력 핀을 출력 모드로 ──
  // COM1A1만 켜고 pinMode(OUTPUT)를 빼먹으면 파형이 핀 밖으로 나가지 않습니다.
  // 소리가 아예 안 날 때 가장 먼저 의심할 부분입니다.
  //   핀 11 = PB5 이므로 DDRB의 5번 비트
  pinMode(PIN_AUDIO, OUTPUT);
  digitalWrite(PIN_AUDIO, LOW);

  // 설정하는 동안 인터럽트를 막아둡니다.
  // 레지스터를 반쯤 고친 상태에서 ISR이 돌면 엉뚱한 출력이 나옵니다.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {

    // ─────────────────────────────────────────
    // Timer1 — PWM 캐리어 (62.5kHz)
    //
    // 파형 모드: Fast PWM 8비트 → WGM13:10 = 0101
    //   WGM10 은 TCCR1A, WGM12 는 TCCR1B 에 있습니다.
    //   TOP = 255 로 고정되므로 샘플 값(0~255)을 그대로 OCR1A에 넣으면 됩니다.
    //
    // 출력 모드: COM1A1 = 1, COM1A0 = 0 → 비반전
    //   OCR1A 값이 클수록 HIGH 구간이 길어집니다 = 평균 전압이 높아집니다.
    //
    // 분주: CS12:10 = 001 → 분주 없음
    //
    // 캐리어 주파수 = F_CPU / (TOP + 1) = 16,000,000 / 256 = 62,500 Hz
    //   사람이 들을 수 있는 한계(약 20kHz)보다 훨씬 높아서
    //   캐리어 자체는 들리지 않고, RC 필터로 쉽게 깎아낼 수 있습니다.
    // ─────────────────────────────────────────
    // 핀 방향(출력 모드)은 위 pinMode 에서 설정함
    TCCR1A = (1 << COM1A1) | (1 << WGM10);
    TCCR1B = (1 << WGM12)  | (1 << CS10);
    TCNT1  = 0;
    OCR1A  = SPEECH_SILENCE;   // 듀티비 50% = 무음

    // ─────────────────────────────────────────
    // Timer2 — 샘플 인터럽트 (8kHz)
    //
    // 파형 모드: CTC → WGM22:20 = 010 (WGM21만 1)
    //   TCNT2가 OCR2A에 닿으면 0으로 돌아가면서 인터럽트가 걸립니다.
    //
    // 분주: CS22:20 = 010 → 분주 8
    //
    // 인터럽트 주파수 = F_CPU / (분주 * (OCR2A + 1))
    //                = 16,000,000 / (8 * 250)
    //                = 8,000 Hz          ← 서버 샘플레이트와 정확히 일치
    //   나누어떨어져서 오차가 0입니다. 재생 속도가 밀리지 않습니다.
    //
    // 한 인터럽트 사이의 간격 = 125us = 2000 사이클.
    // ISR은 40사이클 남짓이라 CPU의 2% 정도만 씁니다.
    // ─────────────────────────────────────────
    TCCR2A = (1 << WGM21);
    TCCR2B = (1 << CS21);
    TCNT2  = 0;
    OCR2A  = 249;                // 250분주 (0부터 세므로 250-1)
    TIMSK2 = (1 << OCIE2A);      // OCR2A 일치 인터럽트 허용

    phase       = 0;
    phaseInc    = 0;
    speechState = SPEECH_STOPPED;
  }

  // Timer0 은 건드리지 않습니다.
  // millis(), micros(), delay() 가 Timer0 을 쓰기 때문에
  // 손대면 시간 관련 함수가 전부 망가집니다.

  sei();   // 전역 인터럽트 허용 (Arduino 프레임워크가 이미 켜두지만 확실히)
}


// ─────────────────────────────────────────────
// speechPlayTone() — 사인파 재생 시작
// ─────────────────────────────────────────────
void speechPlayTone(uint16_t freq) {
  // 범위 제한. 상한은 3000Hz 입니다(SPEECH_MAX_FREQ, 이유는 speech.h 참고).
  // 나이퀴스트(4000Hz)에 가까울수록 파형이 찌그러지고, 정확히 4000Hz 면 무음,
  // 넘으면 앨리어싱으로 의도한 것과 전혀 다른 낮은 음이 들립니다.
  if (freq == 0) {
    speechStop();
    return;
  }
  if (freq > SPEECH_MAX_FREQ) {
    freq = SPEECH_MAX_FREQ;
  }

  // ── 위상 증가량 계산 ──
  //
  // 한 바퀴(360도)를 65536으로 봅니다.
  // 1초에 freq 바퀴를 돌아야 하고, 1초에 8000번 갱신되므로
  //
  //   increment = 65536 * freq / 8000
  //
  //   440Hz  → 3604  (실제 440.06Hz, 오차 0.06Hz)
  //   1000Hz → 8192  (정확)
  //   220Hz  → 1802
  //
  // 부동소수점을 쓰지 않는 이유:
  //   AVR에는 FPU가 없어서 float 곱셈 하나에 수백 사이클이 걸립니다.
  //   ISR 예산이 2000 사이클인데 여기서 다 써버립니다.
  //   여기서는 정수 나눗셈을 메인 쪽에서 딱 한 번만 하고,
  //   ISR에는 덧셈과 시프트만 남깁니다.
  //
  // uint32_t 로 올리는 이유:
  //   3000 * 65536 = 196,608,000. uint16_t(최대 65,535)로는 한참 넘칩니다.
  uint16_t inc = (uint16_t)(((uint32_t)freq * 65536UL) / SPEECH_SAMPLE_RATE);

  // ── 원자적으로 반영 ──
  //
  // phaseInc는 16비트라 AVR에서 두 번에 나눠 써집니다(하위 바이트, 상위 바이트).
  // 그 사이에 ISR이 끼어들면 "새 하위 바이트 + 옛 상위 바이트"라는
  // 존재하지 않는 값으로 샘플이 계산됩니다.
  // volatile은 최적화만 막아줄 뿐 이 문제는 막아주지 못합니다.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    phase       = 0;            // 항상 중심값(128)에서 시작 → 시작할 때 "툭" 소리 없음
    phaseInc    = inc;
    speechState = SPEECH_TONE;  // 이 줄부터 ISR이 OCR1A를 갱신하기 시작합니다
  }
}


// ─────────────────────────────────────────────
// speechPlayDC() — 고정값 출력
// ─────────────────────────────────────────────
void speechPlayDC(uint8_t value) {
  // 순서가 중요합니다.
  // OCR1A를 먼저 쓰면, 상태를 바꾸기 전에 ISR이 끼어들어
  // 방금 쓴 값을 사인파 샘플로 덮어씁니다.
  // 그래서 "ISR을 멈춘다 → 값을 쓴다" 순서로 갑니다.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    speechState = SPEECH_DC;
    OCR1A       = value;
  }
}


// ─────────────────────────────────────────────
// speechStop() — 정지
// ─────────────────────────────────────────────
void speechStop() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    speechState = SPEECH_STOPPED;
    phase       = 0;
    phaseInc    = 0;

    // 스트리밍 중이었다면 남은 데이터도 버립니다 (즉시 멈춤)
    ringHead      = 0;
    ringTail      = 0;
    streamPlaying = false;
    streamEnded   = false;

    // 무음은 0이 아니라 128입니다.
    // 0으로 떨어뜨리면 듀티비가 50%에서 0%로 급변하면서
    // 스피커에서 "툭" 하는 잡음이 납니다.
    OCR1A = SPEECH_SILENCE;
  }

  // 소리 신호 패턴도 멈춥니다 (패턴이 다음 삐를 다시 켜지 않도록)
  alertActive = false;

  // PWM(Timer1)과 샘플 인터럽트(Timer2)는 계속 돌려둡니다.
  // 매번 껐다 켜면 그 순간 핀 상태가 튀면서 잡음이 생깁니다.
}


// ─────────────────────────────────────────────
// speechIsPlaying()
// ─────────────────────────────────────────────
bool speechIsPlaying() {
  // 1바이트 읽기는 AVR에서 한 번에 처리되므로
  // 따로 인터럽트를 막을 필요가 없습니다.
  return speechState != SPEECH_STOPPED;
}


// ─────────────────────────────────────────────
// PCM 스트리밍
// ─────────────────────────────────────────────
void speechStreamBegin() {
  // 새 음성이 시작되면 소리 신호 패턴은 멈춥니다 (패턴이 사인파를 다시 켜지 않도록)
  alertActive = false;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    speechState   = SPEECH_STREAM;   // 이 줄부터 ISR이 링 버퍼를 봅니다 (지금은 비어 있어 무음)
    phase         = 0;
    phaseInc      = 0;
    ringHead      = 0;
    ringTail      = 0;
    streamPlaying = false;
    streamEnded   = false;
    OCR1A         = SPEECH_SILENCE;
  }
}


uint16_t speechStreamFree() {
  uint16_t used;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    used = (ringHead - ringTail) & RING_MASK;
  }
  return (SPEECH_STREAM_BUFFER - 1) - used;   // 한 칸은 비워 둡니다
}


bool speechStreamWrite(const uint8_t* data, uint16_t len) {
  if (speechState != SPEECH_STREAM || len > speechStreamFree()) {
    return false;
  }

  // head 는 loop() 쪽만 바꾸므로 여기서 읽는 것은 안전합니다.
  // 바이트를 먼저 다 써 두고, 마지막에 head 를 한 번에 옮깁니다.
  // ISR은 head 앞까지만 읽으므로 반쯤 쓴 데이터를 재생하는 일이 없습니다.
  uint16_t h = ringHead;
  for (uint16_t i = 0; i < len; i++) {
    ring[h] = data[i];
    h = (h + 1) & RING_MASK;
  }

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ringHead = h;
    // 미리 채우기: 절반 이상 모이면 재생 시작
    if (!streamPlaying && ((h - ringTail) & RING_MASK) >= SPEECH_STREAM_BUFFER / 2) {
      streamPlaying = true;
    }
  }
  return true;
}


void speechStreamEnd() {
  // 짧은 음성은 절반이 안 차도 끝 표시를 받으면 바로 재생합니다
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    streamEnded   = true;
    streamPlaying = true;
  }
}


bool speechStreamIsActive() {
  return speechState == SPEECH_STREAM;
}


bool speechStreamFinished() {
  if (speechState != SPEECH_STREAM || !streamEnded) {
    return false;
  }
  bool empty;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    empty = (ringHead == ringTail);
  }
  return empty;
}


// ─────────────────────────────────────────────
// 소리 신호
//
// 기존 사인파(speechPlayTone)로 삐 소리를 내고,
// 쉬는 구간은 고정값 128(speechPlayDC = 무음)으로 둡니다.
// delay() 없이 millis() 로 진행해서 그동안에도 시리얼을 계속 받습니다.
// 패턴이 끝나면 speechStop() 으로 무음(128)에 돌아갑니다.
// ─────────────────────────────────────────────
void speechBeep(SpeechBeep kind) {
  if (kind >= BEEP_COUNT) {
    kind = BEEP_FAIL;   // 표에 없는 번호는 실패 소리로
  }
  alertPat    = &BEEP_PATTERNS[kind];
  alertLeft   = alertPat->count;
  alertToneOn = true;
  alertNextMs = millis() + alertPat->onMs;

  speechPlayTone(alertPat->freq);   // 시험음이나 음성이 나던 중이어도 소리 신호가 우선입니다
  alertActive = true;
}


void speechAlertPlay(uint8_t kind) {
  (void)kind;   // 종류는 지금 소리에 쓰지 않습니다 (맨 위 ALERT_FRAME_BEEP 참고)
  speechBeep(ALERT_FRAME_BEEP);
}


bool speechAlertActive() {
  return alertActive;
}


void speechUpdate() {
  if (!alertActive || (int32_t)(millis() - alertNextMs) < 0) {
    return;
  }

  if (alertToneOn) {
    // 삐 하나가 끝남
    alertLeft--;
    if (alertLeft == 0) {
      speechStop();                    // 패턴 끝 (alertActive 도 꺼집니다)
      return;
    }
    speechPlayDC(SPEECH_SILENCE);      // 쉼
    alertToneOn  = false;
    alertNextMs += alertPat->offMs;
  } else {
    speechPlayTone(alertPat->freq);    // 다음 삐
    alertToneOn  = true;
    alertNextMs += alertPat->onMs;
  }
}


// ─────────────────────────────────────────────
// 샘플 인터럽트 — 125us마다 실행됩니다
//
// 여기에서 절대 하면 안 되는 것들:
//   Serial.print()  — 한 글자에 수백 사이클. 다음 인터럽트를 놓칩니다
//   delay()         — millis()가 인터럽트로 도는데 여기서 기다리면 멈춥니다
//   부동소수점 연산  — FPU가 없어서 수백 사이클
//   긴 반복문       — 125us 안에 못 끝내면 소리가 깨집니다
//
// 지금은 테이블 조회·링 버퍼 읽기와 대입뿐입니다.
// ─────────────────────────────────────────────
ISR(TIMER2_COMPA_vect) {
  // ── PCM 스트리밍 ──
  // 링 버퍼에서 한 바이트를 꺼내 그대로 듀티비로 씁니다.
  // 미리 채우기 전이거나 데이터가 늦게 와서 비어 있으면 무음(128)을 냅니다.
  // (끊김은 생겨도 엉뚱한 값이 나와 잡음이 나지는 않습니다)
  // ISR 안에서는 인터럽트가 이미 막혀 있으므로 16비트 head 를 그냥 읽어도 됩니다.
  if (speechState == SPEECH_STREAM) {
    if (streamPlaying && ringTail != ringHead) {
      OCR1A    = ring[ringTail];
      ringTail = (ringTail + 1) & RING_MASK;
    } else {
      OCR1A = SPEECH_SILENCE;
    }
    return;
  }

  // 사인파 재생 중이 아니면 즉시 빠져나옵니다.
  // DC/정지 상태에서는 OCR1A를 건드리지 않아야
  // 메인이 써둔 값이 그대로 유지됩니다.
  if (speechState != SPEECH_TONE) {
    return;
  }

  phase += phaseInc;   // 16비트라 65536을 넘으면 알아서 0으로 돌아옵니다
                       // (사인파가 한 바퀴 돈 것과 정확히 같은 동작)

  // 상위 8비트만 꺼내 테이블 인덱스로 씁니다(0~255).
  // 하위 8비트는 소수부라 버립니다.
  OCR1A = pgm_read_byte(&SINE_TABLE[phase >> 8]);
}
