// 스텝 모터 2축 (X: 헤드 좌우, Y: 종이판 앞뒤). delay() 없이 loop 마다 조금씩 움직입니다.
#pragma once
#include <Arduino.h>

enum AxisId : uint8_t { AX_X = 0, AX_Y = 1 };

void  stepperInit();
void  stepperUpdate();                         // loop 에서 계속 불러 줌
void  stepperEnable(bool on);                  // 모터 전류 켜기/끄기
bool  stepperEnabled();
void  stepperMoveToMm(AxisId a, float mm);     // 가감속 이동. 원점을 잡은 뒤에는 범위를 벗어나지 않게 자름
void  stepperJogMm(AxisId a, float mm, float speedMmS);  // 일정 속도 상대 이동 (원점 잡기용)
void  stepperStop(AxisId a);                   // 즉시 멈춤
bool  stepperBusy(AxisId a);
bool  stepperAnyBusy();
float stepperPosMm(AxisId a);
void  stepperSetPosMm(AxisId a, float mm);     // 지금 위치를 이 값으로 정함
void  stepperSetHomed(bool h);
bool  stepperHomed();
