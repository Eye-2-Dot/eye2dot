// PC 에서 펌웨어를 컴파일·실행해 보기 위한 가짜 Arduino (시험 전용)
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
// F("...") : 보드에서는 글자를 플래시에 두는 매크로. 여기서는 형만 맞춘다
class __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))
#define SERIAL_8N1 0
typedef uint8_t byte;

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t v);
int  digitalRead(uint8_t pin);

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& c) : s(c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  const char* c_str() const { return s.c_str(); }
  unsigned length() const { return (unsigned)s.size(); }
  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + o); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  bool operator==(const char* o) const { return s == o; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator=(const char* c) { s = c; return *this; }
  int indexOf(const char* t) const { size_t i = s.find(t); return i == std::string::npos ? -1 : (int)i; }
};
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }

class HardwareSerial {
 public:
  int fd = -1;          // -1 = USB 시리얼 흉내 (stderr 로 출력)
  void begin(unsigned long, ...) {}
  int  available();
  int  read();
  size_t write(uint8_t b);
  size_t write(const uint8_t* b, size_t n);
  void out(const char* t);
  void print(const char* t) { out(t); }
  void print(const __FlashStringHelper* t) { out(reinterpret_cast<const char*>(t)); }
  void print(const String& t) { out(t.c_str()); }
  void print(char c) { char b[2] = {c, 0}; out(b); }
  void print(int v) { out(std::to_string(v).c_str()); }
  void print(unsigned v) { out(std::to_string(v).c_str()); }
  void print(long v) { out(std::to_string(v).c_str()); }
  void print(unsigned long v) { out(std::to_string(v).c_str()); }
  void print(double v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); out(b); }
  template <class T> void println(T v) { print(v); out("\n"); }
  void println(double v, int d) { print(v, d); out("\n"); }
  void println() { out("\n"); }
};
extern HardwareSerial Serial, Serial1, Serial2;

// AVR 레지스터 흉내 (solenoid.cpp 가 PORTA·DDRA 를 직접 쓴다). 값이 바뀌면 simOnPortA 가 불린다
void simOnPortA(uint8_t before, uint8_t after);
struct Reg8 {
  uint8_t value = 0;
  bool    hook  = false;
  operator uint8_t() const { return value; }
  Reg8& operator=(uint8_t v) { uint8_t b = value; value = v; if (hook) simOnPortA(b, v); return *this; }
  Reg8& operator&=(uint8_t v) { return *this = (uint8_t)(value & v); }
  Reg8& operator|=(uint8_t v) { return *this = (uint8_t)(value | v); }
};
extern Reg8 PORTA, DDRA;
#define A0 54
#define constrain(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
int analogRead(uint8_t pin); void tone(uint8_t pin, unsigned f, unsigned long d = 0); void noTone(uint8_t pin);
