// 두 가상 칩이 함께 쓰는 부분: 실제 시계, 가상 UART(파일 디스크립터 3)
#include <Arduino.h>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <deque>

static auto T0 = std::chrono::steady_clock::now();
unsigned long micros() { return (unsigned long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - T0).count(); }
unsigned long millis() { return micros() / 1000UL; }
void delay(unsigned long ms) { usleep(ms * 1000); }
void delayMicroseconds(unsigned int) {}

HardwareSerial Serial, Serial1, Serial2;
static std::deque<uint8_t> rx;
extern void simOnPrint(const char* t);
extern double simDropRate;      // 보낸 바이트를 일부러 깨뜨리는 비율 (통신 오류 시험)

static void pump(int fd) {
  uint8_t b[256]; ssize_t n;
  while ((n = ::read(fd, b, sizeof b)) > 0) for (ssize_t i = 0; i < n; i++) rx.push_back(b[i]);
}
int HardwareSerial::available() { if (fd < 0) return 0; pump(fd); return (int)rx.size(); }
int HardwareSerial::read() { if (fd < 0 || rx.empty()) return -1; int v = rx.front(); rx.pop_front(); return v; }
size_t HardwareSerial::write(uint8_t b) { return write(&b, 1); }
size_t HardwareSerial::write(const uint8_t* b, size_t n) {
  if (fd < 0) return n;
  for (size_t i = 0; i < n; i++) {
    uint8_t v = b[i];
    if (simDropRate > 0 && (rand() / (double)RAND_MAX) < simDropRate) v ^= 0x55;
    if (::write(fd, &v, 1) < 0) {}
  }
  return n;
}
void HardwareSerial::out(const char* t) { fputs(t, stderr); fflush(stderr); simOnPrint(t); }

void simLinkInit(HardwareSerial& s) { s.fd = 3; fcntl(3, F_SETFL, fcntl(3, F_GETFL) | O_NONBLOCK); }
