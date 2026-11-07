/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * punch.h — 점자 타각 (라벨 한 장 찍기)
 *
 * ── 구조 ──
 *   punch    = 라벨 한 장 찍는 순서
 *              헤드 원점 → 칸마다 이동 → 솔레노이드 → 원점 → 테이프 이송
 *   solenoid = 핀 6개 누르기          (세연, solenoid.h — 완료)
 *   stepper  = 헤드·테이프 이동        (준석, 아직 없음)
 *
 * punch 는 solenoid·stepper 를 불러 순서만 짜고, 핀을 직접 만지지 않는 것이 목표입니다.
 * 지금은 stepper 가 없어서 헤드를 옮기지 않고, 모든 칸을 같은 자리에서 솔레노이드로 찍습니다.
 *
 * ── 찍는 방향 (팀 약속 — stepper 를 만들 때 이대로) ──
 *   - 솔레노이드는 라벨을 위에서 아래로 찍습니다. 점은 아랫면으로 튀어나오므로
 *     사람은 라벨을 좌우로 뒤집어 읽습니다.
 *   - 줄의 첫 칸 job.cells[줄][0] 을 위에서 볼 때 **오른쪽 끝**에 찍고, 칸마다 **왼쪽으로** 진행합니다.
 *     줄은 위에서 아래로 (라벨은 한 줄).
 *     → 뒤집어 읽으면 첫 칸이 왼쪽 끝에 와서 순서가 맞습니다.
 *   - 솔레노이드 1번(D22) = 위에서 볼 때 왼쪽 위. 1·2·3 왼쪽 열, 4·5·6 오른쪽 열 (위→아래).
 *   - 칸 안의 좌우는 서버가 이미 바꿔서 보냅니다(거울 값). 펌웨어는 칸 값·칸 순서를 뒤집지 않습니다.
 *
 * 부르는 쪽: job — 타각할 때가 되면 punchBraille() 을 부르고,
 *            결과에 따라 ESP32 에 'D'(완료) 또는 'E'(실패)를 보냅니다.
 */

#ifndef PUNCH_H
#define PUNCH_H

#include <Arduino.h>

#include "link.h"   // BrailleJob

/*
 * setup() 맨 처음에 한 번 (main.cpp). 솔레노이드 핀을 출력으로 만들고 모두 끕니다.
 * stepper 가 생기면 그 Init 도 여기서 부릅니다.
 * Serial.begin() 보다 먼저 불리므로 이 안에서는 Serial 로그를 쓰지 마세요.
 */
void punchInit();

/*
 * 점자 한 장을 찍습니다.
 *
 *   넘겨받는 것 : job.lineCount       줄 수 (라벨은 1)
 *                 job.lineLen[줄]     그 줄의 칸 수
 *                 job.cells[줄][칸]   칸 값 = 서버가 거울로 바꾼 6비트 값 = 솔레노이드 비트
 *                                     bit0 = 솔레노이드 1번, bit1 = 2번, bit2 = 3번,
 *                                     bit3 = 4번, bit4 = 5번, bit5 = 6번
 *                                     (1번 = 위에서 볼 때 왼쪽 위, 위 "찍는 방향" 참고)
 *                 (구조체는 include/link.h, 크기 한도는 common/protocol.h 의 BRAILLE_MAX_*)
 *   솔레노이드  : pins.h 의 PIN_SOL[0..5] = 솔레노이드 1..6 (D22~D27)
 *   돌려줄 값   : 다 찍었으면 true  → ESP32 에 'D'
 *                 걸림·리밋 스위치 이상 등으로 실패하면 false → 'E'
 *   주의        : 이 함수가 끝날 때까지 loop() 가 멈춥니다. 오래 걸리면 중간중간
 *                 linkPoll() 을 불러 시리얼 수신 버퍼가 넘치지 않게 할 것.
 */
bool punchBraille(const BrailleJob& job);

#endif  // PUNCH_H
