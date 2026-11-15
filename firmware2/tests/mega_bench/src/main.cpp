// Eye 2 Dot - 부품별 시험 프로그램 (ATmega2560)
// 부품을 하나 붙일 때마다 이 프로그램의 명령으로 그 부품만 확인합니다.
// 시리얼 모니터(115200)에서 명령을 입력하고 Enter. 모니터 설정은 "줄 끝 = 새 줄(LF)".
// 시험 전용이라 delay() 를 씁니다. 완성 펌웨어와 섞지 마세요.
#include <Arduino.h>

#ifndef BENCH_FW
#define BENCH_FW 2
#endif

// ---------------- 핀 (완성 펌웨어의 pins.h 와 같은 번호) ----------------
static const uint8_t PIN_SOL[6] = {22, 23, 24, 25, 26, 27};
// 모터 A: 펌웨어1 헤드 / 펌웨어2 X,   모터 B: 펌웨어1 카드 랙 / 펌웨어2 Y
#if BENCH_FW == 1
// 펌웨어 1: 28BYJ-48 + ULN2003 보드. IN1~IN4 에 선 4개
static const uint8_t A_IN[4] = {36, 37, 38, 39};
static const uint8_t B_IN[4] = {40, 41, 42, 43};
static const uint8_t HALF_STEP[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};   // 코일 켜는 순서 (비트 0 = IN1)
static uint8_t phaseA = 0, phaseB = 0;
static const float MAX_STEPS_PER_S = 750.0f;               // 이 모터의 한계
#else
// 펌웨어 2: 17HS3430(NEMA17) + A4988·DRV8825 계열 드라이버. STEP / DIR / EN
static const uint8_t A_STEP = 36, A_DIR = 37, A_EN = 38;
static const uint8_t B_STEP = 40, B_DIR = 41, B_EN = 42;
#endif
static const uint8_t SW_A = 34;                            // 펌웨어1 헤드 원점 / 펌웨어2 X 원점
static const uint8_t SW_B = 35;                            // 펌웨어1 카드 감지 / 펌웨어2 Y 원점
static const uint8_t BTN_2 = 32;                           // 펌웨어1 출력 / 펌웨어2 출력 계속
static const uint8_t PIN_LED = 13;
#if BENCH_FW == 1
static const uint8_t BTN_1 = 31;                           // 촬영
static const uint8_t PIN_AUDIO = 11;
static const float A_STEPS_PER_MM = 101.9f;                // GT2 20잇(40mm/바퀴), 하프 스텝 4076/바퀴
static const float B_STEPS_PER_MM = 64.87f;                // 20잇 피니언(62.83mm/바퀴)
static const char* NAME_A = "헤드"; static const char* NAME_B = "카드";
#else
static const float A_STEPS_PER_MM = 40.0f;                 // GT2 40잇, 1/16
static const float B_STEPS_PER_MM = 40.0f;
static const char* NAME_A = "X(헤드)"; static const char* NAME_B = "Y(종이판)";
#endif

static uint16_t onMs = 30;        // 솔레노이드 켜는 시간 (펌웨어 1 의 SOL_PULSE_MS 와 같은 값)
#if BENCH_FW == 1
static float speedMm = 5.0f;      // 모터 속도 mm/s (28BYJ-48 은 느리다: 헤드 최대 약 7, 카드 랙 약 11)
#else
static float speedMm = 20.0f;     // 모터 속도 mm/s
#endif
static bool watch = false;
static uint8_t lastIn = 0xFF;

static void help() {
  Serial.println(F("=== Eye2Dot 부품별 시험 ==="));
#if BENCH_FW == 1
  Serial.println(F("회로: 펌웨어 1 (라벨 출력기)"));
#else
  Serial.println(F("회로: 펌웨어 2 (책 타각기)"));
#endif
  Serial.println(F(" ?        도움말"));
  Serial.println(F(" b        버튼·스위치 감시 켜기/끄기 (바뀔 때마다 한 줄)"));
  Serial.println(F(" 1 ~ 6    솔레노이드 하나만 (D22 ~ D27)"));
  Serial.println(F(" a        솔레노이드 6개 동시"));
  Serial.println(F(" k        1번부터 6번까지 1초 간격으로 차례로 (어느 선이 몇 번인지 확인)"));
  Serial.println(F(" m 21     점 값으로 찍기 (0~63)"));
  Serial.println(F(" o 40     솔레노이드 켜는 시간 ms (최대 50 - 더 길면 코일이 뜨거워짐)"));
  Serial.println(F(" A 10     모터 A 를 +10mm   (A -10 은 반대로)"));
  Serial.println(F(" B 10     모터 B 를 +10mm"));
#if BENCH_FW == 1
  Serial.println(F(" s 5      모터 속도 mm/s (헤드 최대 약 7, 카드 랙 약 11)"));
  Serial.println(F(" H        모터 A(헤드)를 스위치 쪽으로 보내 원점 찾기"));
  Serial.println(F(" d        모터 코일 끄기   (움직이는 중에 아무 글자나 보내면 멈춤)"));
#else
  Serial.println(F(" s 20     모터 속도 mm/s"));
  Serial.println(F(" H / J    모터 A / B 를 스위치 쪽으로 보내 원점 찾기"));
  Serial.println(F(" e / d    모터 전류 켜기 / 끄기   (움직이는 중에 아무 글자나 보내면 멈춤)"));
#endif
  Serial.println(F(" u        ESP32 연결 확인 (PING 을 보내고 PONG 을 기다림)"));
#if BENCH_FW == 1
  Serial.println(F(" t        스피커 시험음        v  배터리 전압 (A0)"));
#endif
}

static uint8_t readInputs() {
  uint8_t v = 0;
  if (digitalRead(SW_A) == LOW) v |= 1;
  if (digitalRead(SW_B) == LOW) v |= 2;
  if (digitalRead(BTN_2) == LOW) v |= 4;
#if BENCH_FW == 1
  if (digitalRead(BTN_1) == LOW) v |= 8;
#endif
  return v;
}

static void showInputs(uint8_t v) {
  Serial.print(F("D34 스위치A ")); Serial.print((v & 1) ? F("눌림") : F("--"));
  Serial.print(F(" | D35 스위치B ")); Serial.print((v & 2) ? F("눌림") : F("--"));
  Serial.print(F(" | D32 버튼 ")); Serial.print((v & 4) ? F("눌림") : F("--"));
#if BENCH_FW == 1
  Serial.print(F(" | D31 촬영 버튼 ")); Serial.print((v & 8) ? F("눌림") : F("--"));
#endif
  Serial.println();
}

static void fire(uint8_t mask) {
  mask &= 0x3F;
  for (uint8_t i = 0; i < 6; i++) if (mask & (1 << i)) digitalWrite(PIN_SOL[i], HIGH);
  delay(onMs);
  for (uint8_t i = 0; i < 6; i++) digitalWrite(PIN_SOL[i], LOW);
  delay(200);                              // 코일 식히기 (연달아 입력해도 과열되지 않게)
  Serial.print(F("찍음: 점 값 ")); Serial.print(mask);
  Serial.print(F("  켠 시간 ")); Serial.print(onMs); Serial.println(F(" ms"));
}

#if BENCH_FW == 1
static void coils(const uint8_t* pins, uint8_t bits) {
  for (uint8_t i = 0; i < 4; i++) digitalWrite(pins[i], (bits & (1 << i)) ? HIGH : LOW);
}
static void motors(bool on) {
  if (on) { coils(A_IN, HALF_STEP[phaseA]); coils(B_IN, HALF_STEP[phaseB]); }
  else    { coils(A_IN, 0); coils(B_IN, 0); }
}
// 한 스텝: 코일 켜는 순서를 한 칸 넘긴다
static void oneStep(bool isA, bool plus) {
  uint8_t& ph = isA ? phaseA : phaseB;
  ph = (uint8_t)((ph + (plus ? 1 : 7)) & 7);
  coils(isA ? A_IN : B_IN, HALF_STEP[ph]);
}
#else
static void motors(bool on) {
  digitalWrite(A_EN, on ? LOW : HIGH);     // A4988/DRV8825: LOW = 켜짐
  digitalWrite(B_EN, on ? LOW : HIGH);
}
static void oneStep(bool isA, bool plus) {
  digitalWrite(isA ? A_DIR : B_DIR, plus ? HIGH : LOW);
  delayMicroseconds(20);
  digitalWrite(isA ? A_STEP : B_STEP, HIGH); delayMicroseconds(5); digitalWrite(isA ? A_STEP : B_STEP, LOW);
}
#endif

// steps 만큼 움직임. stopPin 이 255 가 아니면 그 스위치가 눌리는 순간 멈춤. 돌려주는 값 = 실제로 간 스텝
static long stepMove(bool isA, long steps, float stepsPerMm, uint8_t stopPin) {
  motors(true);
  delay(5);
  bool plus = steps >= 0;
  long n = plus ? steps : -steps;
  float target = speedMm * stepsPerMm, v = 3.0f * stepsPerMm;
#if BENCH_FW == 1
  if (target > MAX_STEPS_PER_S) target = MAX_STEPS_PER_S;
#endif
  if (v > target) v = target;
  long done = 0;
  for (; done < n; done++) {
    if (stopPin != 255 && digitalRead(stopPin) == LOW) break;
    if (Serial.available()) { while (Serial.available()) Serial.read(); Serial.println(F("(입력이 들어와 멈춤)")); break; }
    oneStep(isA, plus);
    unsigned long us = (unsigned long)(1000000.0f / v);
    if (us > 16000) delay(us / 1000); else delayMicroseconds((unsigned int)us);
    if (v < target) { v += 100.0f * stepsPerMm * (us * 0.000001f); if (v > target) v = target; }   // 부드럽게 출발
  }
#if BENCH_FW == 1
  motors(false);                           // 28BYJ-48 은 기어 때문에 꺼도 그 자리에 있다. 발열을 막으려고 끈다
#endif
  return done;
}

static void moveMm(bool isA, float mm) {
  float spm = isA ? A_STEPS_PER_MM : B_STEPS_PER_MM;
  Serial.print(F("모터 ")); Serial.print(isA ? NAME_A : NAME_B);
  Serial.print(F(" ")); Serial.print(mm, 1); Serial.println(F(" mm 이동"));
  long d = stepMove(isA, (long)(mm * spm), spm, 255);
  Serial.print(F("끝. 보낸 스텝 ")); Serial.print(d);
  Serial.println(F("  -> 자로 실제 이동 거리를 재서 명령한 값과 같은지 확인"));
}

static void home(bool isA) {
  uint8_t sw = isA ? SW_A : SW_B;
  float spm = isA ? A_STEPS_PER_MM : B_STEPS_PER_MM;
  Serial.print(F("원점 찾기: 모터 ")); Serial.println(isA ? NAME_A : NAME_B);
  if (digitalRead(sw) == LOW) { Serial.println(F("스위치가 이미 눌려 있어 3mm 물러납니다")); stepMove(isA, (long)(3 * spm), spm, 255); }
  float keep = speedMm;
#if BENCH_FW == 1
  speedMm = 6.0f;
  long lim = (long)(140.0f * spm);         // 헤드가 갈 수 있는 거리보다 조금 길게
#else
  speedMm = 15.0f;
  long lim = (long)(300.0f * spm);
#endif
  long d = stepMove(isA, -lim, spm, sw);
  speedMm = keep;
  if (digitalRead(sw) == LOW) { Serial.print(F("스위치에 닿음. 간 거리 ")); Serial.print(d / spm, 1); Serial.println(F(" mm")); }
#if BENCH_FW == 1
  else Serial.println(F("스위치를 못 찾음 -> 스위치 반대쪽으로 갔다면 motion.h 의 HEAD_DIR_INVERT 를 true 로 (완성 펌웨어에서)"));
#else
  else Serial.println(F("스위치를 못 찾음 -> 스위치 반대쪽으로 갔다면 모터 선 한 쌍을 바꾸거나 config.h 의 DIR_INVERT 를 true 로"));
#endif
}

static void linkTest() {
  Serial.println(F("ESP32 로 PING 을 보냅니다 (ESP32 에는 esp32_net_check 가 올라가 있어야 함)"));
  while (Serial1.available()) Serial1.read();
  Serial1.print("PING\n");
  String got; unsigned long t0 = millis();
  while (millis() - t0 < 2000) { while (Serial1.available()) got += (char)Serial1.read(); if (got.indexOf("PONG") >= 0) break; }
  if (got.indexOf("PONG") >= 0) Serial.println(F("성공: PONG 을 받았습니다. 두 칩의 선 3개가 올바릅니다"));
  else {
    Serial.print(F("실패: 받은 글자 수 ")); Serial.println(got.length());
    Serial.println(F("  0 글자   -> ESP32 TX -> Mega D19 선, GND, ESP32 전원을 확인"));
    Serial.println(F("  깨진 글자 -> 속도(57600)가 다르거나 GND 가 떠 있음"));
    Serial.println(F("  ESP32 화면에 PING 이 안 보이면 -> Mega D18 -> 분압 -> ESP32 RX 선을 확인"));
  }
}

static unsigned long tKey = 0;
static char line[24]; static uint8_t n = 0;

static void run() {
  char c = line[0];
  float v = (n > 1) ? atof(line + 1) : 0;
  switch (c) {
    case '?': help(); break;
    case 'b': watch = !watch; Serial.println(watch ? F("감시 켬: 버튼·스위치를 눌러 보세요") : F("감시 끔")); lastIn = 0xFF; break;
    case '1': case '2': case '3': case '4': case '5': case '6': fire(1 << (c - '1')); break;
    case 'a': fire(63); break;
    case 'k': for (uint8_t i = 0; i < 6; i++) { Serial.print(i + 1); Serial.print(F("번 (D")); Serial.print(PIN_SOL[i]); Serial.println(F(")")); fire(1 << i); delay(1000); } break;
    case 'm': fire((uint8_t)v); break;
    case 'o': onMs = (uint16_t)constrain((long)v, 5L, 50L); Serial.print(F("켜는 시간 ")); Serial.print(onMs); Serial.println(F(" ms")); break;
    case 'A': moveMm(true, v); break;
    case 'B': moveMm(false, v); break;
    case 's': speedMm = constrain(v, 1.0f, 80.0f); Serial.print(F("속도 ")); Serial.print(speedMm, 0); Serial.println(F(" mm/s")); break;
    case 'H': home(true); break;
#if BENCH_FW == 1
    case 'J': Serial.println(F("카드 랙에는 스위치가 없습니다 (완성 펌웨어의 Z 명령이 멈춤 턱에 밀어 붙여 영점을 맞춥니다)")); break;
#else
    case 'J': home(false); break;
    case 'e': motors(true);  Serial.println(F("모터 켬 (손으로 돌려 보면 버팀)")); break;
#endif
    case 'd': motors(false); Serial.println(F("모터 끔")); break;
    case 'u': linkTest(); break;
#if BENCH_FW == 1
    case 't': Serial.println(F("시험음 1kHz 0.5초")); tone(PIN_AUDIO, 1000, 500); delay(600); noTone(PIN_AUDIO); break;
    case 'v': { float a = analogRead(A0) * 5.0f / 1023.0f; Serial.print(F("A0 ")); Serial.print(a, 2);
                Serial.print(F(" V  -> 배터리 ")); Serial.print(a * (10.0f + 4.7f) / 4.7f, 2); Serial.println(F(" V (10k/4.7k 분압 기준)")); break; }
#endif
    default: Serial.println(F("모르는 명령. ? 를 입력하세요")); break;
  }
}

void setup() {
  for (uint8_t i = 0; i < 6; i++) { digitalWrite(PIN_SOL[i], LOW); pinMode(PIN_SOL[i], OUTPUT); }
#if BENCH_FW == 1
  for (uint8_t i = 0; i < 4; i++) { pinMode(A_IN[i], OUTPUT); pinMode(B_IN[i], OUTPUT); }
#else
  pinMode(A_STEP, OUTPUT); pinMode(A_DIR, OUTPUT); pinMode(A_EN, OUTPUT);
  pinMode(B_STEP, OUTPUT); pinMode(B_DIR, OUTPUT); pinMode(B_EN, OUTPUT);
#endif
  motors(false);
  pinMode(SW_A, INPUT_PULLUP); pinMode(SW_B, INPUT_PULLUP); pinMode(BTN_2, INPUT_PULLUP);
#if BENCH_FW == 1
  pinMode(BTN_1, INPUT_PULLUP);
#endif
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  Serial1.begin(57600);
  help();
}

void loop() {
  digitalWrite(PIN_LED, (millis() % 1000) < 100);       // 살아 있다는 표시
  while (Serial.available()) {
    char c = (char)Serial.read();
    tKey = millis();
    if (c == '\r' || c == '\n') { if (n) { line[n] = 0; Serial.print(F("> ")); Serial.println(line); run(); n = 0; } }
    else if (n < sizeof(line) - 1) line[n++] = c;
  }
  // Enter 가 전달되지 않는 모니터(줄 끝 = 없음)에서도 0.8초 뒤 실행
  if (n && millis() - tKey > 800UL) { line[n] = 0; Serial.print(F("> ")); Serial.println(line); run(); n = 0; }
  if (watch) {
    uint8_t v = readInputs();
    if (v != lastIn) { delay(20); v = readInputs(); if (v != lastIn) { lastIn = v; showInputs(v); } }
  }
}
