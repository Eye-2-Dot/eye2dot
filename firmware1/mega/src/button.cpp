/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * button.cpp — 조작 버튼 읽기
 *
 * 버튼은 누르는 순간 접점이 몇 ms 동안 붙었다 떨어졌다를 반복합니다(떨림, 바운스).
 * 같은 값이 DEBOUNCE_MS 동안 유지될 때만 "바뀌었다"고 봅니다.
 */

#include <Arduino.h>

#include "button.h"
#include "pins.h"

static const uint8_t DEBOUNCE_MS = 30;

struct Button {
  uint8_t  pin;
  bool     stable;      // 떨림이 가라앉은 뒤의 상태 (true = 눌림)
  bool     lastRaw;     // 마지막으로 읽은 그대로의 값
  uint32_t changedMs;   // lastRaw 가 마지막으로 바뀐 시각
  bool     pressed;     // 새로 눌렸음 (buttonTakePress 로 읽으면 지워짐)
};

// 줄 순서는 button.h 의 ButtonId 순서와 같아야 합니다
static Button buttons[] = {
  { PIN_BTN_OK,     false, false, 0, false },   // BUTTON_OK
  { PIN_BTN_CANCEL, false, false, 0, false },   // BUTTON_CANCEL
};
static_assert(sizeof(buttons) / sizeof(buttons[0]) == BUTTON_COUNT,
              "버튼 표의 줄 수가 button.h 의 ButtonId 개수와 다릅니다");


void buttonsInit() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    Button& b = buttons[i];
    pinMode(b.pin, INPUT_PULLUP);
    b.stable    = (digitalRead(b.pin) == LOW);
    b.lastRaw   = b.stable;
    b.changedMs = millis();
    b.pressed   = false;
  }
}


void buttonsUpdate(uint32_t now) {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    Button& b = buttons[i];
    bool raw = (digitalRead(b.pin) == LOW);   // INPUT_PULLUP: 누르면 LOW
    if (raw != b.lastRaw) {
      b.lastRaw   = raw;
      b.changedMs = now;
    } else if (raw != b.stable && now - b.changedMs >= DEBOUNCE_MS) {
      b.stable = raw;
      if (raw) {
        b.pressed = true;
      }
    }
  }
}


bool buttonTakePress(ButtonId id) {
  if (id >= BUTTON_COUNT) {
    return false;
  }
  bool p = buttons[id].pressed;
  buttons[id].pressed = false;
  return p;
}


void buttonsClearPresses() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    buttons[i].pressed = false;
  }
}
