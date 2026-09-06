# Eye 2 Dot API 명세

서버: `server/main.py` · 기본 주소: `http://localhost:8000`

실행: `cd server && uvicorn main:app --reload`

---

## 공통 사항

### 모드

| 값 | 의미 |
|---|---|
| `label` | 사물 사진 → 사물 이름 → 점자 라벨 |
| `book` | 책 페이지 사진 → OCR 본문 → 점역 |

### 점자 셀 표현

점자 셀 하나는 **6비트 정수(mask)** 하나다. 점 번호 n(1~6)이 비트 (n-1)에 대응한다.

```
1 · · 4      예) {1, 4} → 0b001001 = 9
2 · · 5
3 · · 6
```

- `0`은 **빈 칸(공백)** 을 뜻한다.
- 유니코드 점자로 보려면 `chr(0x2800 + mask)`.

### CORS

개발 단계라 모든 출처를 허용한다 (`allow_origins=["*"]`). 외부 배포 시 특정 도메인만 허용할 것.

---

## `GET /health`

서버 생존 확인.

```json
{ "status": "ok", "model": "gemini-2.5-flash" }
```

---

## `POST /process`

ESP32-CAM 전용. 원본 JPEG 바이트를 그대로 본문에 넣는다.

```
POST /process?mode=label
Content-Type: image/jpeg

<JPEG 바이너리>
```

## `POST /process-file`

브라우저·앱 테스트용. `multipart/form-data`.

| 필드 | 위치 | 필수 | 설명 |
|---|---|---|---|
| `mode` | form | 아니오 (기본 `label`) | `label` 또는 `book` |
| `image` | form (파일) | **예** | 이미지 파일 |

## `POST /process-mock`

**앱 개발용 목(mock).** Gemini를 호출하지 않고 고정된 더미 결과를 즉시 반환한다.
`GEMINI_API_KEY` 없이도, 카메라 없이도 앱 화면을 개발할 수 있다.

| 필드 | 위치 | 필수 | 설명 |
|---|---|---|---|
| `mode` | **쿼리** | 아니오 (기본 `label`) | `label` 또는 `book` |
| `image` | form (파일) | 아니오 | 붙여도 되고 안 붙여도 된다. 붙여도 읽지 않는다 |

```
POST /process-mock?mode=book
```

응답 형식은 `/process-file`과 **완전히 동일**하다. 앱은 나중에 주소에서 `-mock`만 떼면 그대로 실서버로 전환된다.

---

## 성공 응답

세 엔드포인트(`/process`, `/process-file`, `/process-mock`)가 모두 같은 형식을 쓴다.

| 키 | 타입 | 설명 |
|---|---|---|
| `ok` | bool | 항상 `true` |
| `text` | 인식된 텍스트. 앱은 화면 표시, 펌웨어 1은 음성 안내에 사용. `book`은 개행(`\n`)이 그대로 남아 있다 |
| `reason` | 판단 근거 1문장. 펌웨어 1이 음성으로 읽어준다. 앱은 표시하지 않아도 된다 |
| `braille` | **모드에 따라 다름** | 아래 참고 |
| `braille_preview` | string | 사람이 눈으로 확인하는 유니코드 점자. `book`은 줄바꿈으로 이어 붙인다 |
| `cell_count` | int | 전체 점자 셀 수 (`book`은 모든 줄의 합) |

### ⚠️ `braille`의 타입은 모드에 따라 다르다

| 모드 | 타입 | 이유 |
|---|---|---|
| `label` | `int[]` (1차원) | 사물 이름 한 개라 항상 한 줄 |
| `book` | `int[][]` (2차원, 줄 단위) | 본문이 길어 여러 줄이 되므로, 펌웨어가 줄바꿈 위치를 알아야 한다 |

### `label` 응답 예시

```json
{
  "ok": true,
  "text": "우유",
  "reason": "흰색 종이팩에 우유 표기가 있습니다.",
  "braille": [13, 41],
  "braille_preview": "⠍⠩",
  "cell_count": 2
}
```

### `book` 응답 예시

`/process-mock?mode=book`의 실제 응답이다 (`braille`은 지면상 앞 2줄만, 실제로는 18줄 448셀).

```json
{
  "ok": true,
  "text": "점자는 손끝으로 읽는 문자이다. 여섯 개의 점을 일정한 규칙에 따라 조합하여 글자를 ...",
  "reason": "OCR로 텍스트를 인식했습니다.",
  "braille": [
    [40, 14, 34, 40, 35, 9, 42, 18, 0, 32, 37, 18, 32, 8, 42, 38, 42, 16, 37, 0, 21, 2, 1, 9, 42, 18],
    [17, 13, 18, 40, 35, 21, 10, 35, 0, 49, 32, 14, 4, 0, 8, 23, 58, 0, 40, 14, 34, 42, 2]
  ],
  "braille_preview": "⠨⠎⠢⠨⠣⠉⠪⠒⠀⠠⠥⠒⠠⠈⠪⠦⠪⠐⠥⠀⠕⠂⠁⠉⠪⠒\n⠑⠍⠒⠨⠣⠕⠊⠣⠀⠱⠠⠎⠄⠀⠈⠗⠺⠀⠨⠎⠢⠪⠂",
  "cell_count": 448
}
```

### `book`의 줄 나누기 규칙

`braille.py`의 `text_to_braille_lines()`가 담당한다.

- 한 줄 최대 셀 수는 `MAX_CELLS_PER_LINE = 30`. **하드웨어 실측 후 조정할 값이다.**
- 원문의 개행(`\n`) 위치에서는 무조건 줄을 바꾼다.
- 그 안에서 30셀을 넘으면 **낱말 경계(공백)** 에서 줄을 바꾼다. 낱말 중간은 자르지 않는다.
- 낱말 하나가 30셀보다 길면 담을 방법이 없으므로 그때만 30셀 단위로 끊는다.
- 낱말 사이 공백은 빈 칸(`0`) 한 개. 줄 맨 앞과 맨 끝에는 `0`이 오지 않는다.
- 점역 결과가 비는 줄(빈 문단 등)은 만들지 않는다. `braille`에 빈 배열은 들어가지 않는다.

---

## 실패 응답

| 키 | 타입 | 설명 |
|---|---|---|
| `ok` | bool | 항상 `false` |
| `error` | string | 오류 코드 (아래 표) |
| `reason` | string | 사용자에게 보여줄 실패 이유 |

| `error` | HTTP | 발생 조건 |
|---|---|---|
| `invalid_mode` | 400 | `mode`가 `label`/`book`이 아님 |
| `invalid_image` | 400 | 이미지로 열 수 없는 데이터 |
| `recognition_failed` | 200 | 사물·텍스트를 찾지 못함 |
| `rate_limit` | 429 | Gemini 요청 한도 초과 |
| `network_error` | 502 | Gemini 서버 연결 실패 |
| `parse_failed` | 500 | Gemini 응답 해석 실패 |

```json
{
  "ok": false,
  "error": "invalid_mode",
  "reason": "mode는 ['book', 'label'] 중 하나여야 합니다 (받은 값: 'banana')"
}
```

`recognition_failed`만 HTTP 200으로 나간다. 따라서 앱은 **상태 코드가 아니라 `ok` 필드로 성공 여부를 판단해야 한다.**
