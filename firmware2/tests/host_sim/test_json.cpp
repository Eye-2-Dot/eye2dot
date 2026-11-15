// json_scan.h 단독 시험: 서버 응답 모양이 조금 달라도 읽는지
#include <stdio.h>
#include "json_scan.h"
static uint8_t cell[50][JS_MAX_CELLS], len[50]; static int fails = 0;
static void t(const char* name, const char* js, int lines, const char* id, const char* key, int l0len, int l0c0) {
  JsonJob j; int n = jsonParseJob(js, j, cell, len, 50);
  bool ok = n == lines && (!id || strcmp(j.id, id) == 0) && (!key || strcmp(j.linesKey, key) == 0)
            && (lines == 0 || (len[0] == l0len && (l0len == 0 || cell[0][0] == l0c0)));
  printf("%s  %s  (줄 %d, 번호 '%s', 이름 '%s')\n", ok ? "통과" : "실패", name, n, j.id, j.linesKey);
  if (!ok) fails++;
}
int main() {
  t("기본", "{\"job_id\":\"j1\",\"lines\":[[4,28],[1,60]]}", 2, "j1", "lines", 2, 4);
  t("작업 없음 null", "{\"job\": null}", 0, nullptr, nullptr, 0, 0);
  t("작업 없음 빈 객체", "{}", 0, nullptr, nullptr, 0, 0);
  t("숫자 id, 안쪽 객체", "{\"job\":{\"id\": 17, \"braille\": [ [ 9 ] , [ ] ]}}", 2, "17", "braille", 1, 9);
  t("거울 이름 우선", "{\"id\":\"a\",\"braille\":[[1]],\"braille_mirrored\":[[8]]}", 1, "a", "braille_mirrored", 1, 8);
  t("쪽>줄>칸 3차원", "{\"job_id\":\"p\",\"pages\":[[[1,2],[3]],[[4]]]}", 3, "p", "pages", 2, 1);
  t("1차원 배열은 무시", "{\"id\":\"x\",\"size\":[30,25],\"text\":\"[[1]]\"}", 0, nullptr, nullptr, 0, 0);
  t("job_id 가 id 보다 우선", "{\"id\":\"wrong\",\"job_id\":\"right\",\"lines\":[[63]]}", 1, "right", "lines", 1, 63);
  t("소수는 점자가 아님", "{\"id\":\"x\",\"box\":[[0.1,0.2]],\"lines\":[[7]]}", 1, "x", "lines", 1, 7);
  char big[400] = "{\"id\":\"b\",\"lines\":[["; for (int i = 0; i < 35; i++) strcat(big, i ? ",1" : "1"); strcat(big, "]]}");
  JsonJob j; jsonParseJob(big, j, cell, len, 50);
  printf("%s  35칸 줄은 30칸으로 자르고 알림 (길이 %d, 잘린 줄 %d)\n", (len[0] == 30 && j.truncated == 1) ? "통과" : "실패", len[0], j.truncated);
  if (!(len[0] == 30 && j.truncated == 1)) fails++;
  return fails;
}
