// PC 에서 펌웨어를 컴파일·실행해 보기 위한 가짜 Arduino (시험 전용 — 보드에는 올라가지 않는다)
//   시간은 진짜로 흐르지 않고, micros()·delay() 를 부를 때마다 가짜 시계가 조금씩 간다.
//   핀을 쓰고 읽는 것은 sim_machine.cpp 의 "가짜 기계"로 이어진다.
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>
#include <string>
#include <vector>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

typedef uint8_t byte;

// F("...") : 보드에서는 글자를 플래시에 두는 매크로. 여기서는 형만 맞춘다
class __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int  digitalRead(uint8_t pin);

template <class T> T min(T a, T b) { return a < b ? a : b; }
template <class T> T max(T a, T b) { return a > b ? a : b; }

// ── AVR 레지스터 흉내 (solenoid.cpp 가 PORTA·DDRA 를 직접 쓴다) ──
struct Reg8 {
  uint8_t value = 0;
  void (*onWrite)(uint8_t before, uint8_t after) = nullptr;
  operator uint8_t() const { return value; }
  Reg8& operator=(uint8_t v) {
    uint8_t before = value;
    value = v;
    if (onWrite) onWrite(before, v);
    return *this;
  }
  Reg8& operator&=(uint8_t v) { return *this = (uint8_t)(value & v); }
  Reg8& operator|=(uint8_t v) { return *this = (uint8_t)(value | v); }
};
extern Reg8 PORTA, DDRA;

// ── String (시험 코드가 쓰는 만큼만) ──
class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& c) : s(c) {}
  unsigned length() const { return (unsigned)s.size(); }
  const char* c_str() const { return s.c_str(); }
  char operator[](unsigned i) const { return s[i]; }
  bool operator==(const char* o) const { return s == o; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator=(const char* c) { s = c; return *this; }
  String substring(unsigned from) const { return String(from < s.size() ? s.substr(from) : std::string()); }
  float toFloat() const { return (float)atof(s.c_str()); }
  void trim() {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
  }
};

// ── 시리얼 ──
//   rx : 보드가 "받을" 바이트 (시험이 넣어 준다)     tx : 보드가 "보낸" 바이트 (시험이 확인한다)
class HardwareSerial {
 public:
  std::deque<uint8_t>  rx;
  std::vector<uint8_t> tx;
  bool echo = false;            // true 면 보낸 글자를 화면에도 찍는다 (USB 시리얼 흉내)

  void begin(unsigned long) {}
  int  available() { return (int)rx.size(); }
  int  read() {
    if (rx.empty()) return -1;
    uint8_t b = rx.front();
    rx.pop_front();
    return b;
  }
  size_t write(uint8_t b) {
    tx.push_back(b);
    if (echo) fputc(b, stdout);
    return 1;
  }
  size_t write(int b) { return write((uint8_t)b); }
  void out(const char* t) { while (*t) write((uint8_t)*t++); }

  void print(const char* t) { out(t); }
  void print(const __FlashStringHelper* t) { out(reinterpret_cast<const char*>(t)); }
  void print(const String& t) { out(t.c_str()); }
  void print(char c) { write((uint8_t)c); }
  void print(unsigned char v) { out(std::to_string((unsigned)v).c_str()); }
  void print(int v) { out(std::to_string(v).c_str()); }
  void print(unsigned v) { out(std::to_string(v).c_str()); }
  void print(long v) { out(std::to_string(v).c_str()); }
  void print(unsigned long v) { out(std::to_string(v).c_str()); }
  void print(double v, int digits = 2) {
    char b[40];
    snprintf(b, sizeof b, "%.*f", digits, v);
    out(b);
  }
  template <class T> void println(T v) { print(v); out("\n"); }
  void println(double v, int digits) { print(v, digits); out("\n"); }
  void println() { out("\n"); }
};
extern HardwareSerial Serial, Serial1;
