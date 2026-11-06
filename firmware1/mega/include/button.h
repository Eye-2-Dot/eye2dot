/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * button.h — 조작 버튼 읽기 (떨림 방지, "새로 눌림" 꺼내기)
 *
 * 버튼은 한쪽을 핀, 다른 쪽을 GND 에 연결합니다 (INPUT_PULLUP, 누르면 LOW).
 * 이 모듈은 "어느 버튼이 새로 눌렸는가"만 압니다.
 * 그 버튼이 무슨 뜻인지(언제 무엇을 할지)는 job 이 정합니다.
 */

#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

// 버튼 번호. 핀은 button.cpp 의 표에서 pins.h 상수로 연결합니다.
enum ButtonId : uint8_t {
  BUTTON_OK,       // pins.h 의 PIN_BTN_OK
  BUTTON_CANCEL,   // pins.h 의 PIN_BTN_CANCEL
  BUTTON_COUNT     // 개수 (버튼 아님)
};

/* setup() 에서 한 번. 버튼 핀을 INPUT_PULLUP 으로 설정합니다. */
void buttonsInit();

/*
 * loop() 에서 매번 부릅니다 (delay() 없음).
 * 접점 떨림이 가라앉은 뒤에 눌림을 기록합니다.
 */
void buttonsUpdate(uint32_t now);

/* 새로 눌렸으면 한 번만 true (읽으면 지워짐) */
bool buttonTakePress(ButtonId id);

/* 쌓여 있던 눌림을 모두 버립니다 */
void buttonsClearPresses();

#endif  // BUTTON_H
