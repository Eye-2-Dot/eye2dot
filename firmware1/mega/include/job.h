/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * job.h — 작업 진행 (상태 기계: 지금 상태에 따라 할 일을 나누는 구조)
 *
 * ESP32 가 보낸 작업 하나를 처음부터 끝까지 진행합니다.
 *   점자 프레임 도착 → 음성 재생 → 버튼 → 타각(punch) → ESP32 에 'D'(완료) 또는 'E'(실패·취소)
 * 그 밖에 맡는 것: ESP32 알림음 프레임 → 소리 신호, 작업 시간 초과, 상태 LED
 *
 * ── link · speech · job 의 경계 ──
 *   link   = 음성 파이프. 프레임이 도착한 "그 순간"에 해야 하는 일을 합니다.
 *              음성 시작 → speechStreamBegin, 조각 → speechStreamWrite, 끝 → speechStreamEnd
 *              링 버퍼에 자리가 있을 때만 'R' 로 다음 조각 요청 (흐름 제어)
 *              새 점자 프레임이 오면 이전 작업의 음성 정지
 *            이 일들은 다음 프레임이 같은 버퍼를 덮어쓰기 전에 끝나야 하므로 link 안에 둡니다.
 *            (job 이 나중에 하면, 같은 linkPoll 안에서 이미 시작된 새 음성까지 멈출 수 있음)
 *   speech = 소리 내기 (타이머, 링 버퍼, 소리 신호)
 *   job    = 정책. 언제 음성을 멈추고(speechStop + linkAudioAbort),
 *            언제 버튼을 받고, 언제 'D'/'E' 를 보낼지 정합니다.
 *            음성 수신 과정에는 끼어들지 않고 결과(linkAudioState, speechStreamFinished)만 봅니다.
 */

#ifndef JOB_H
#define JOB_H

#include <Arduino.h>

/* setup() 에서 한 번. 상태 LED 핀을 준비합니다. */
void jobInit();

/*
 * loop() 에서 매번 부릅니다 (delay() 없음). buttonsUpdate() 다음에 부르세요.
 *   ESP32 알림음 → 소리 신호, 새 작업 시작, 버튼·재생 끝·시간 초과 처리, LED
 */
void jobUpdate(uint32_t now);

/* 작업이 진행 중인지 (console 이 소리 시험 명령을 막을 때 씀) */
bool jobIsActive();

/* 지금 상태 이름 (console 의 상태 출력용) */
const __FlashStringHelper* jobStateName();

#endif  // JOB_H
