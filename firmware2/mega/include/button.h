// "출력 계속" 버튼. 짧게 = 계속, 길게 = 취소.
#pragma once
#include <Arduino.h>

void buttonInit();
void buttonUpdate();          // loop 마다
bool buttonShortEvent();      // 짧게 눌렀다 뗀 순간 한 번만 true
bool buttonLongEvent();       // 길게 누른 순간 한 번만 true
bool buttonIsDown();
