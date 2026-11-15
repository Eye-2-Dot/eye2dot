// 아주 작은 JSON 읽기 도구 (외부 라이브러리 없이).
// 서버 응답에서 두 가지만 꺼냅니다:
//   1) 작업 번호  : "job_id" (없으면 "id") 의 값
//   2) 점자 줄들  : [[..],[..]] 모양의 2차원 숫자 배열
//      2차원 배열이 여러 개면 이름에 "mirror" 가 들어간 것을 고릅니다 (기기는 거울 점자를 찍어야 함).
// 필드 이름이 조금 달라도 동작하게 하려고 "모양"으로 찾습니다.
#pragma once
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "protocol.h"                 // LINE_MAX_CELLS (두 칩이 같이 쓰는 값)

#define JS_MAX_CELLS LINE_MAX_CELLS   // 한 줄의 칸 수

struct JsonJob {
  char id[48];
  bool hasId;
  int  lineCount;      // 0 이면 작업 없음
  int  totalLines;     // 서버가 보낸 전체 줄 수 (maxLines 를 넘으면 lineCount 보다 큼)
  char linesKey[32];   // 어떤 이름의 배열을 골랐는지 (확인용)
  int  truncated;      // 30칸을 넘어 잘린 줄 수
};

namespace jsonscan {

inline const char* ws(const char* p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; return p; }

// 따옴표로 시작하는 문자열을 읽고, 닫는 따옴표 다음 위치를 돌려줌
inline const char* readStr(const char* p, char* out, size_t cap) {
  size_t n = 0; p++;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) p++;
    if (out && n + 1 < cap) out[n++] = *p;
    p++;
  }
  if (out && cap) out[n] = 0;
  return *p == '"' ? p + 1 : p;
}

struct Sink { uint8_t (*cell)[JS_MAX_CELLS]; uint8_t* len; int maxLines; int count; int truncated; bool store; };

// '[' 에서 시작. 숫자만 든 배열이면 한 줄, 배열을 담은 배열이면 안으로 들어감.
inline const char* parseArr(const char* p, Sink& s, bool top) {
  const char* q = ws(p + 1);
  if (*q == '[') {                                   // 배열을 담은 배열
    while (true) {
      q = parseArr(q, s, false);
      if (!q) return nullptr;
      q = ws(q);
      if (*q == ',') { q = ws(q + 1); if (*q != '[') return nullptr; continue; }
      if (*q == ']') return q + 1;
      return nullptr;
    }
  }
  if (top) return nullptr;                           // 맨 바깥은 반드시 2차원 이상
  int n = 0, line = s.count;
  bool over = false;
  while (true) {                                     // 숫자 한 줄
    q = ws(q);
    if (*q == ']') { q++; break; }
    bool neg = false;
    if (*q == '-') { neg = true; q++; }
    if (!isdigit((unsigned char)*q)) return nullptr;
    long v = 0;
    while (isdigit((unsigned char)*q)) { v = v * 10 + (*q - '0'); if (v > 100000) v = 100000; q++; }
    if (*q == '.' || *q == 'e' || *q == 'E') return nullptr;   // 정수만
    if (neg) v = 0;
    if (n < JS_MAX_CELLS) { if (s.store && line < s.maxLines) s.cell[line][n] = (uint8_t)(v & 0x3F); n++; }
    else over = true;
    q = ws(q);
    if (*q == ',') { q++; continue; }
    if (*q == ']') { q++; break; }
    return nullptr;
  }
  if (s.store && line < s.maxLines) s.len[line] = (uint8_t)n;
  if (over) s.truncated++;
  s.count++;
  return q;
}

inline bool hasMirror(const char* k) {
  for (; *k; k++) if ((k[0] | 32) == 'm' && (k[1] | 32) == 'i' && (k[2] | 32) == 'r' && (k[3] | 32) == 'r') return true;
  return false;
}

} // namespace jsonscan

// 돌려주는 값 = 줄 수 (0 = 작업 없음). maxLines 를 넘는 줄은 버립니다.
inline int jsonParseJob(const char* s, JsonJob& out, uint8_t (*cell)[JS_MAX_CELLS], uint8_t* len, int maxLines) {
  using namespace jsonscan;
  out.id[0] = 0; out.hasId = false; out.lineCount = 0; out.totalLines = 0; out.linesKey[0] = 0; out.truncated = 0;
  char key[32] = ""; char tmp[48];
  const char* chosen = nullptr; char chosenKey[32] = "";
  bool idFromJobId = false;

  for (const char* p = s; *p; ) {
    if (*p == '"') {
      const char* e = readStr(p, tmp, sizeof(tmp));
      const char* c = ws(e);
      if (*c != ':') { p = e; continue; }            // 값으로 쓰인 문자열
      strncpy(key, tmp, sizeof(key) - 1); key[sizeof(key) - 1] = 0;
      p = ws(c + 1);
      bool isJobId = strcmp(key, "job_id") == 0;
      if (isJobId || (strcmp(key, "id") == 0 && !out.hasId)) {
        if (isJobId && idFromJobId) continue;
        if (*p == '"') { readStr(p, out.id, sizeof(out.id)); out.hasId = out.id[0] != 0; }
        else if (isdigit((unsigned char)*p)) {
          size_t n = 0; const char* d = p;
          while (isdigit((unsigned char)*d) && n + 1 < sizeof(out.id)) out.id[n++] = *d++;
          out.id[n] = 0; out.hasId = true;
        }
        if (isJobId && out.hasId) idFromJobId = true;
      }
      continue;
    }
    if (*p == '[') {
      Sink probe = {nullptr, nullptr, 0, 0, 0, false};
      const char* e = parseArr(p, probe, true);
      if (e && probe.count > 0) {
        if (!chosen || (hasMirror(key) && !hasMirror(chosenKey))) {
          chosen = p; strncpy(chosenKey, key, sizeof(chosenKey) - 1); chosenKey[sizeof(chosenKey) - 1] = 0;
        }
        p = e; continue;
      }
    }
    p++;
  }
  if (!chosen) return 0;
  Sink sink = {cell, len, maxLines, 0, 0, true};
  parseArr(chosen, sink, true);
  out.totalLines = sink.count;
  out.lineCount = sink.count > maxLines ? maxLines : sink.count;
  out.truncated = sink.truncated;
  strncpy(out.linesKey, chosenKey, sizeof(out.linesKey) - 1); out.linesKey[sizeof(out.linesKey) - 1] = 0;
  return out.lineCount;
}
