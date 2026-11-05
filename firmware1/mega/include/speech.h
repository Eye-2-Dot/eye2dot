/*
 * Eye 2 Dot — 펌웨어 1 (라벨 출력기)
 * speech.h — 오디오 출력 모듈
 *
 * ATmega2560에는 DAC가 없습니다.
 * 그래서 소리를 이렇게 만듭니다.
 *
 *   Timer1  : 62.5kHz PWM 캐리어. 듀티비가 곧 출력 전압
 *   Timer2  : 8kHz 인터럽트. 125us마다 다음 샘플을 OCR1A에 넣음
 *   RC 필터 : 62.5kHz 캐리어를 깎아내고 오디오 성분만 남김
 *
 * 8kHz / 8비트 unsigned PCM 기준입니다.
 * (서버가 보내주는 음성 데이터의 형식과 동일)
 *
 * 3단계인 지금은 서버 없이 코드로 만든 사인파를 재생해서
 * 타이머 → 필터 → 앰프 → 스피커 경로만 검증합니다.
 */

#ifndef SPEECH_H
#define SPEECH_H

#include <Arduino.h>

// 샘플레이트. 서버가 보내는 PCM과 반드시 같아야 합니다.
constexpr uint16_t SPEECH_SAMPLE_RATE = 8000;

// 8비트 unsigned PCM의 무음 값.
// 0이 아니라 중심값 128입니다. 0을 출력하면 스피커 콘이
// 한쪽 끝까지 밀려서 "툭" 소리가 납니다.
constexpr uint8_t SPEECH_SILENCE = 128;

// 재생 가능한 최고 주파수 (나이퀴스트 한계 = 8000 / 2).
// 이보다 높은 값을 넣으면 앨리어싱으로 엉뚱한 음이 납니다.
constexpr uint16_t SPEECH_MAX_FREQ = SPEECH_SAMPLE_RATE / 2;

/*
 * 타이머를 설정하고 오디오 출력을 준비합니다.
 * setup()에서 한 번만 호출하세요.
 * 호출 직후 상태는 무음(OCR1A = 128)입니다.
 */
void speechInit();

/*
 * 사인파 재생을 시작합니다. 호출하면 바로 소리가 나고,
 * speechStop()을 부를 때까지 계속 납니다.
 *   freq : 1 ~ 4000 Hz (범위를 벗어나면 안쪽으로 잘립니다)
 */
void speechPlayTone(uint16_t freq);

/*
 * 고정값을 계속 출력합니다. 오디오 경로 점검용입니다.
 *   value = 128 → 무음
 *   value = 200 → DC. 바뀌는 순간 "툭" 소리가 나면 배선이 살아있는 것
 */
void speechPlayDC(uint8_t value);

/*
 * 출력을 멈추고 무음(OCR1A = 128)으로 되돌립니다.
 * PWM 자체는 계속 돌아갑니다 — 듀티비만 50%로 고정됩니다.
 *
 * 즉시 멈춤 함수이기도 합니다. 사인파·DC·PCM 스트리밍·알림음 모두 바로 멈춥니다.
 * (재생 중에 확인·취소 버튼을 누르면 이것을 부릅니다)
 */
void speechStop();

/*
 * 지금 무언가 출력 중인지 여부.
 * 사인파, DC, PCM 스트리밍 모두 "재생 중"으로 봅니다.
 */
bool speechIsPlaying();


// ─────────────────────────────────────────────
// PCM 스트리밍 — ESP32 가 조각으로 보내 주는 음성
//
// 링 버퍼(끝에 닿으면 처음으로 돌아가며 쓰는 저장 공간)에 받은 바이트를 쌓고,
// 8kHz 인터럽트가 한 바이트씩 꺼내 OCR1A 로 내보냅니다.
// 음성 전체(수 초 = 수만 바이트)는 RAM 8KB 에 다 안 들어가므로 받으면서 재생합니다.
// ─────────────────────────────────────────────

// 링 버퍼 크기. 반드시 2의 거듭제곱. 1024B = 0.128초 분량
constexpr uint16_t SPEECH_STREAM_BUFFER = 1024;

/*
 * 링 버퍼를 비우고 스트리밍 모드로 들어갑니다.
 * 버퍼가 절반 이상 차거나 speechStreamEnd() 를 부르면 재생을 시작합니다.
 * (조금 모아두고 시작해야 전송이 잠깐 늦어져도 끊기지 않습니다)
 * 사인파 시험음이나 알림음이 나던 중이면 멈추고 음성으로 바뀝니다.
 */
void speechStreamBegin();

/* 링 버퍼에 더 넣을 수 있는 바이트 수 */
uint16_t speechStreamFree();

/*
 * 받은 PCM 을 링 버퍼에 넣습니다.
 * 자리가 모자라거나 스트리밍 모드가 아니면 아무것도 넣지 않고 false.
 */
bool speechStreamWrite(const uint8_t* data, uint16_t len);

/* 더 올 데이터가 없다는 표시. 남은 것을 다 재생하면 끝납니다. */
void speechStreamEnd();

/* 스트리밍 모드인지 (재생 대기·재생 중 포함) */
bool speechStreamIsActive();

/* 끝 표시를 받은 뒤 버퍼까지 다 재생했는지. true 가 되면 speechStop() 으로 정리합니다. */
bool speechStreamFinished();


// ─────────────────────────────────────────────
// 알림음 — 서버에 닿지 않는 실패 등을 사인파 삐 소리로 알림
//   kind : protocol.h 의 ALERT_NETWORK(짧게 2번), ALERT_CAMERA(짧게 3번), ALERT_GENERIC(낮게 길게 1번)
// ─────────────────────────────────────────────
void speechAlertPlay(uint8_t kind);
bool speechAlertActive();

/* loop() 에서 매번 호출합니다. 알림음의 켜짐·쉼 패턴을 millis() 로 진행합니다. */
void speechUpdate();

#endif  // SPEECH_H
