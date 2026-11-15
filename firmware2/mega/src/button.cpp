#include "button.h"
#include "pins.h"
#include "config.h"

static bool stable = false, lastRaw = false, longFired = false;
static bool evShort = false, evLong = false;
static unsigned long tChange = 0, tDown = 0;

void buttonInit() { pinMode(PIN_BTN_CONTINUE, INPUT_PULLUP); }

void buttonUpdate() {
  bool raw = (digitalRead(PIN_BTN_CONTINUE) == LOW);
  unsigned long now = millis();
  if (raw != lastRaw) { lastRaw = raw; tChange = now; }
  if (raw != stable && (now - tChange) >= BTN_DEBOUNCE_MS) {
    stable = raw;
    if (stable) { tDown = now; longFired = false; }
    else if (!longFired) evShort = true;
  }
  if (stable && !longFired && (now - tDown) >= BTN_LONG_MS) { longFired = true; evLong = true; }
}

bool buttonShortEvent() { bool e = evShort; evShort = false; return e; }
bool buttonLongEvent()  { bool e = evLong;  evLong  = false; return e; }
bool buttonIsDown()     { return stable; }
