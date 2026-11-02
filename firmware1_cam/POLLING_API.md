# 폴링 API 규격 (제안)

앱에서 "출력하기"를 누른 작업을 서버가 대기열에 넣어 두고,
펌웨어(ESP32)가 1초마다 "출력할 것 있나?" 물어 가져가는 방식.

```
앱 "출력하기"  ──POST /print──────────▶  서버 대기열
ESP32 (1초마다) ──GET /jobs/next────────▶  없음 / 작업 1개
ESP32 출력 끝   ──POST /jobs/{id}/done──▶  완료 기록
```

기준 구현: `tools/poll_test_server.py` (그대로 실행해 시험 가능).
서버 담당은 이 내용을 `server/main.py` 에 옮기면 된다.

---

## 기기 이름

| 모드 | 기기 |
|---|---|
| `label` | `fw1` (라벨 출력기) |
| `book`  | `fw2` (책 타각기) |

서버가 모드로 기기를 정한다. 앱은 기기 이름을 몰라도 된다.

---

## 1. `POST /print` — 앱이 호출

결과 화면에서 받은 값을 그대로 보낸다. 점역을 다시 하지 않는다.

```json
{"mode":"label", "text":"우유", "reason":"흰색 종이팩…", "braille":[13,41]}
```

성공: `{"ok":true, "job_id":"7cabc156cc10", "device":"fw1", "waiting":1}`

| error | HTTP | 조건 |
|---|---|---|
| `invalid_mode` | 400 | mode 가 label/book 이 아님 |
| `missing_braille` | 400 | braille 이 비었음 |

## 2. `GET /jobs/next?device=fw1` — ESP32 가 1초마다 호출

할 일 없음 (가장 흔함):
```json
{"ok":true, "job":null}
```

작업 있음:
```json
{"ok":true, "job":{"id":"7cabc156cc10", "mode":"label", "text":"우유",
                   "reason":"흰색 종이팩…", "braille":[13,41]}}
```

- 한 번에 **한 개만**, 들어온 순서대로 준다.
- 준 작업은 대기열에서 빼서 "진행 중"으로 옮긴다. **같은 작업을 두 번 주지 않는다.**
- `braille` 형식은 기존과 같다. 라벨은 1차원, 책은 2차원.
- `id` 는 영문·숫자만 (주소에 그대로 들어가므로).
- 기기 이름이 틀리면 400 `invalid_device`.

## 3. `POST /jobs/{id}/done` — ESP32 가 출력 후 호출

```json
{"device":"fw1", "ok":true, "error":""}
```

실패했으면 `"ok":false, "error":"print_failed"` 또는 `"bad_braille"`.

응답 `{"ok":true}`. 진행 중이 아닌 id 면 404 `unknown_job`.

## 4. `GET /jobs` — 확인용

대기 중·진행 중·최근 완료 작업을 보여 준다. 브라우저로 열어 확인.

---

## 정해 둔 것

- **완료 보고가 안 와도 자동으로 다시 주지 않는다.**
  책 한 쪽 타각은 몇 분 걸릴 수 있어서, 시간이 지났다고 다시 주면 같은 쪽이 두 번 찍힌다.
  필요하면 나중에 "다시 출력" 기능을 앱에 따로 둔다.
- 대기열은 메모리에만 둔다. 서버를 재시작하면 비워진다. 전시용으로는 충분하다.
- 방아쇠 → 촬영 경로(`/process`, `/process-file`)는 바뀌지 않는다.
  ESP32 가 먼저 묻고 바로 답을 받으므로 폴링이 필요 없다.

## 앱 쪽에서 할 일

결과 화면 "출력하기" 버튼의 TODO 자리에 `POST /print` 호출을 넣는다.
성공하면 "출력 대기열에 넣었습니다" 정도를 보여 주면 된다.
