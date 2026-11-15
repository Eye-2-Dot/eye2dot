// 원점 잡기: X 를 스위치에 댄 뒤 Y 를 댑니다. 빠르게 한 번, 물러났다가 천천히 한 번.
#pragma once
#include <Arduino.h>
#include "stepper.h"

void    homingInit();
void    homingStart();
void    homingUpdate();          // loop 마다
void    homingAbort();           // 하던 원점 잡기를 그만두고 두 축을 멈춤
bool    homingBusy();
bool    homingDone();            // 성공하고 끝남
bool    homingFailed();
uint8_t homingError();           // ERR_* (protocol.h)
bool    limitPressed(AxisId a);  // 스위치가 눌렸는지 (떨림 걸러 냄)
bool    limitRaw(AxisId a);
