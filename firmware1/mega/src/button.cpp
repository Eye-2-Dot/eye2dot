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
  { PIN_BTN_CAPTURE, false, false, 0, false },   // BUTTON_CAPTURE
  { PIN_BTN_PRINT,   false, false, 0, false },   // BUTTON_PRINT
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


// 눌림 표시를 지우고, 버튼의 "지금 상태"를 다시 읽어 맞춥니다.
// 타각처럼 buttonsUpdate() 가 오래 안 불린 뒤에 부르면, 그동안 누르고 있는 버튼이
// 곧바로 "새로 눌림"으로 잡히지 않습니다 (떼었다가 다시 눌러야 새 눌림).
void buttonsClearPresses() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    Button& b = buttons[i];
    bool raw    = (digitalRead(b.pin) == LOW);
    b.stable    = raw;
    b.lastRaw   = raw;
    b.changedMs = now;
    b.pressed   = false;
  }
}
