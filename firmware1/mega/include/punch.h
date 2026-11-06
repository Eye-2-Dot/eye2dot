/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * punch.h — 점자 타각 (라벨 한 장 찍기)
 *
 * ── 앞으로의 구조 ──
 *   punch    = 라벨 한 장 찍는 순서
 *              헤드 원점 → 칸마다 이동 → 솔레노이드 → 원점 → 테이프 이송
 *   solenoid = 핀 6개 누르기          (세연이 만들 예정)
 *   stepper  = 헤드·테이프 이동        (준석이 만들 예정)
 *
 * punch 는 solenoid·stepper 를 불러 순서만 짜고, 핀을 직접 만지지 않는 것이 목표입니다.
 * 지금은 solenoid·stepper 가 아직 없어서 칸 값을 USB 시리얼에 출력만 합니다.
 *
 * 부르는 쪽: job — 타각할 때가 되면 punchBraille() 을 부르고,
 *            결과에 따라 ESP32 에 'D'(완료) 또는 'E'(실패)를 보냅니다.
 */

#ifndef PUNCH_H
#define PUNCH_H

#include <Arduino.h>

#include "link.h"   // BrailleJob

/*
 * setup() 에서 한 번. 지금은 비어 있습니다.
 * solenoid·stepper 가 생기면 그 Init 을 여기서 부릅니다.
 */
void punchInit();

/*
 * 점자 한 장을 찍습니다.
 *
 *   넘겨받는 것 : job.lineCount       줄 수 (라벨은 1)
 *                 job.lineLen[줄]     그 줄의 칸 수
 *                 job.cells[줄][칸]   칸 값 = 6비트 마스크
 *                                     bit0 = 점1, bit1 = 점2, bit2 = 점3,
 *                                     bit3 = 점4, bit4 = 점5, bit5 = 점6
 *                 (구조체는 include/link.h, 크기 한도는 common/protocol.h 의 BRAILLE_MAX_*)
 *   솔레노이드  : pins.h 의 PIN_SOL[0..5] = 점1..점6
 *   돌려줄 값   : 다 찍었으면 true  → ESP32 에 'D'
 *                 걸림·리밋 스위치 이상 등으로 실패하면 false → 'E'
 *   주의        : 이 함수가 끝날 때까지 loop() 가 멈춥니다. 오래 걸리면 중간중간
 *                 linkPoll() 을 불러 시리얼 수신 버퍼가 넘치지 않게 할 것.
 */
bool punchBraille(const BrailleJob& job);

#endif  // PUNCH_H
