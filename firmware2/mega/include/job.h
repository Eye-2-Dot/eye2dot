// 지금 무엇을 하는 중인지(상태)를 정하는 모듈. 장·줄을 관리합니다.
#pragma once
#include <Arduino.h>

void    jobInit();
void    jobUpdate();                       // loop 마다
uint8_t jobState();                        // ST_* (protocol.h)
const char* jobStateName();

// link 가 프레임을 받으면 부름. 0 = 받음, 그 외 = NAK 이유
uint8_t jobOnPageBegin(uint8_t seq, uint8_t page, uint8_t pageCount, uint8_t lineCount);
uint8_t jobOnLine(uint8_t index, uint8_t n, const uint8_t* cells);
uint8_t jobOnGo(uint8_t lineCount);
void    jobOnCancel();                     // 취소 요청 (다음 jobUpdate 에서 그 자리에 멈춤)
bool    jobIsMoving();                     // 모터가 움직이는 상태인지 (원점 잡기·타각·복귀)
void    jobFillStatus(uint8_t out[6]);

// 시험용 (console 이 부름)
void    jobStartHoming();
void    jobStartTestPage();
uint8_t jobLastError();
void    jobKeepMotorsOn(bool on);          // 대기에서도 모터 전류를 켜 둘지 (USB 명령 e / d)
