// ESP32 와 프레임 주고받기 (Serial1: D18 TX1, D19 RX1)
#pragma once
#include <Arduino.h>

void     linkInit();
void     linkUpdate();                       // loop 마다
void     linkSendAck(uint8_t type, uint8_t value);
void     linkSendNak(uint8_t type, uint8_t reason);
void     linkSendStatus();
void     linkSendPageDone(uint8_t jobSeq, uint8_t page);
void     linkSendError(uint8_t code);
uint32_t linkFramesRx();
uint32_t linkBadFrames();
