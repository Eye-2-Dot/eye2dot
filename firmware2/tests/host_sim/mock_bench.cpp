#include <Arduino.h>
double simDropRate = 0; void simOnPrint(const char*) {}
void pinMode(uint8_t, uint8_t) {} void digitalWrite(uint8_t, uint8_t) {} int digitalRead(uint8_t) { return HIGH; }
int analogRead(uint8_t) { return 512; } void tone(uint8_t, unsigned, unsigned long) {} void noTone(uint8_t) {}
void setup(); void loop();
int main() { setup(); for (int i = 0; i < 100; i++) loop(); return 0; }
