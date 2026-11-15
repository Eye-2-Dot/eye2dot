// 가짜 기계 — 펌웨어가 내보내는 핀 신호를 받아 "헤드·카드 랙·솔레노이드"가 실제로 어떻게 움직였을지 계산한다
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

enum LimitMode { LIMIT_NORMAL, LIMIT_NEVER, LIMIT_STUCK };

struct PunchEvent {
  uint8_t mask;       // 켜진 솔레노이드 (bit0 = 1번)
  double  headMm;     // 그때 헤드 위치 (원점 스위치가 눌리는 자리 = 0)
  double  cardMm;     // 그때 카드 랙 위치 (멈춤 턱 = 0)
  double  onMs;       // 켜져 있던 시간
};

struct Machine {
  // ── 시험이 정하는 것 ──
  double    headStartMm  = 37.3;   // 전원을 켰을 때 헤드가 있던 자리
  double    cardStartMm  = 12.0;   // 전원을 켰을 때 랙이 있던 자리
  LimitMode limitMode    = LIMIT_NORMAL;
  bool      cardSensorSees = true; // 카드 감지 센서(D35)가 카드를 보는지
  unsigned  cpuStepCostUs = 150;   // 스텝 한 번 계산하는 데 드는 시간 (ATmega 의 느린 소수 계산 흉내)
  // 출력 버튼(D32)을 시간에 따라 누르고 떼기 (0 = 쓰지 않음). 움직이는 도중에 멈추는 시험용
  uint64_t  heldFromUs = 0;        // 이때부터 누르고 있음
  uint64_t  releaseAtUs = 0;       // 이때 뗌
  uint64_t  pressAgainAtUs = 0;    // 이때 다시 누름 (멈춤 요청)
  uint64_t  usbKeyAtUs = 0;        // 이때 USB 시리얼로 글자 하나가 들어옴

  // ── 기계 상태 (스텝 단위) ──
  long   headSteps = 0, cardSteps = 0;
  int    level[70] = {0};
  int    mode[70]  = {0};

  // ── 기록 ──
  std::vector<PunchEvent> punches;
  long     headStepCount = 0, cardStepCount = 0;
  long     headStall = 0;              // 헤드가 끝에 부딪혀 헛돈 스텝
  long     cardStallAtZero = 0;        // 랙이 멈춤 턱에서 헛돈 스텝 (영점 맞출 때는 정상)
  long     cardCrash = 0;              // 랙이 최대 거리 밖으로 나가려 한 스텝 (있으면 안 됨)
  long     badCoilPattern = 0;         // 순서표에 없는 코일 조합 (있으면 안 됨)
  long     skippedPhase = 0;           // 순서표를 두 칸 이상 건너뜀 (있으면 안 됨)
  long     stepsWhileSolenoidOn = 0;   // 솔레노이드가 눌린 채로 움직인 스텝 (있으면 안 됨)
  double   minStepIntervalUs[2] = {1e12, 1e12};
  double   maxSolenoidOnMs = 0;
  double   cardMaxMm = 0;

  void   reset();
  double headMm() const;
  double cardMm() const;
};

extern Machine  machine;
extern uint64_t simNowUs;                 // 가짜 시계 (µs)
extern std::vector<std::string> simBeeps; // speechBeep 로 낸 소리 이름

void simPowerOn(uint64_t startUs);        // 펌웨어 전역 변수는 그대로 두고 기계·시계만 처음으로
