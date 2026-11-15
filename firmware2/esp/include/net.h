// Wi-Fi 와 서버 요청
#pragma once
#include <Arduino.h>
#include "json_scan.h"

#define DEVICE_ID         "fw2"
#define POLL_INTERVAL_MS  1000     // 서버에 "작업 있어?" 물어보는 간격
#define HTTP_TIMEOUT_MS   8000
#define MAX_JOB_LINES     (PAGE_MAX_LINES * 16)   // 한 작업 최대 줄 수 (25줄 x 16쪽 = 400)

// 서버의 POST /jobs/{id}/done 이 받는 본문 (docs/POLLING_API.md 3장, server/main.py 의 DoneReport).
//   {"device":"fw2", "ok":true, "error":""}
//   실패면 ok=false 와 error = print_failed / bad_braille / cancelled 중 하나
#define SERVER_PORT     8000
#define DONE_BODY_OK    "{\"device\":\"" DEVICE_ID "\",\"ok\":true,\"error\":\"\"}"
#define DONE_BODY_FAIL  "{\"device\":\"" DEVICE_ID "\",\"ok\":false,\"error\":\"%s\"}"
#define DONE_ERR_PRINT_FAILED "print_failed"   // Mega 오류, 응답 없음
#define DONE_ERR_CANCELLED    "cancelled"      // 버튼을 길게 눌러 취소
#define DONE_ERR_BAD_BRAILLE  "bad_braille"    // 점자 데이터가 비었거나 형식이 다름

struct BookJob {
  JsonJob  info;
  uint8_t  len[MAX_JOB_LINES];
  uint8_t  cell[MAX_JOB_LINES][JS_MAX_CELLS];
};

void netBegin();
bool netIsUp();
bool netEnsureWifi();                 // 끊겼으면 다시 붙임. 붙어 있으면 true
bool netHealth();                     // GET /health
// 1 = 작업 받음, 0 = 없음, -1 = 통신 실패,
// 2 = 받았지만 찍을 수 없음 (줄이 MAX_JOB_LINES 를 넘거나 한 줄이 30칸을 넘음 — 잘라서 찍지 않고 실패로 보고)
int  netFetchJob(BookJob& job);
bool netReportDone(const char* id, bool ok, const char* errorCode);
void netPrintInfo();
