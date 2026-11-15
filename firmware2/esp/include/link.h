// Mega 와 프레임 주고받기 (Serial2)
#pragma once
#include <Arduino.h>

#define PIN_LINK_RX  16    // ESP32 RX2  <- (1k/2k 분압) <- Mega D18 (TX1)
#define PIN_LINK_TX  17    // ESP32 TX2  -> Mega D19 (RX1)

// Mega 없이 ESP32 만 시험할 때 false (받은 작업을 화면에만 보여 주고 완료 보고)
#define MEGA_CONNECTED true

struct MegaStatus { uint8_t state, seq, page, line, doneCount, err; };

void linkBegin();
void linkPump();                                    // 들어온 바이트 처리. 자주 불러 줌
// 프레임을 보내고 확인(ACK)을 기다림. NAK 이면 false 이고 *nak 에 이유.
//   expect : Mega 가 ACK 에 담아 돌려줄 값 (P = 쪽 번호, L = 줄 번호). 다른 값의 ACK 는 앞 프레임에 대한
//            늦은 응답이므로 무시한다. -1 이면 값을 보지 않는다.
bool linkRequest(uint8_t type, const uint8_t* p, uint8_t n, uint8_t* nak = nullptr, int expect = -1,
                 uint32_t timeoutMs = 400, uint8_t tries = 4);
bool linkQuery(MegaStatus& st, uint32_t timeoutMs = 300);
bool linkTakeDone(uint8_t& seq, uint8_t& page);     // "쪽 끝" 이 왔으면 한 번만 true
bool linkTakeError(uint8_t& code);                  // "오류" 가 왔으면 한 번만 true
void linkClearEvents();
uint32_t linkBadFrames();
