/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * console.h — USB 시리얼 시험 콘솔
 *
 * 시리얼 모니터(115200)를 열고 한 글자를 보내면 동작이 바뀝니다.
 *   0~5 : 소리 시험 (무음, 고정값, 사인파). 단계별로 확인하면 소리가 안 날 때 원인을 좁힐 수 있습니다
 *   ?   : 도움말
 * 2초마다 상태(재생, 작업, 프레임 수, 남은 RAM)를 출력합니다.
 *
 * 작업 중이거나 소리 신호가 나는 중에는 0~5 를 실행하지 않습니다 (작업 음성과 섞이지 않게).
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <Arduino.h>

/*
 * setup() 맨 처음에 한 번. USB 시리얼을 열고 도움말을 출력합니다.
 * (업로드 직후 첫 출력이 잘리지 않도록 500ms 기다립니다 — setup 안이라 괜찮음)
 */
void consoleInit();

/* loop() 에서 매번 부릅니다 (delay() 없음). 명령 처리, 2초마다 상태 출력 */
void consoleUpdate(uint32_t now);

#endif  // CONSOLE_H
