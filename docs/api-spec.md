# Eye 2 Dot API 명세

서버: `server/main.py` · 기본 주소: `http://localhost:8000`

설치: `cd server && pip install -r requirements.txt`

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

브라우저에서 띄운 앱도 `/speech`의 `X-Audio-Duration` 헤더를 읽을 수 있도록 노출해 두었다 (`expose_headers`).

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

---

## `GET /speech`

**펌웨어 1 음성 안내용.** 텍스트를 음성으로 바꿔 **오디오 바이트 그대로** 돌려준다 (JSON이 아니다).
음성 생성과 변환은 서버가 전부 하고, 기기는 받은 바이트를 재생만 한다.

### 요청

| 필드 | 위치 | 필수 | 설명 |
|---|---|---|---|
| `text` | 쿼리 | **예** | 읽어줄 텍스트. 앞뒤 공백을 뗀 뒤 **200자 이하**. **UTF-8로 퍼센트 인코딩**할 것 |

```
GET /speech?text=%EC%9A%B0%EC%9C%A0
```

- `%EC%9A%B0%EC%9C%A0`은 "우유"다. 한글 한 글자는 UTF-8로 3바이트라서 인코딩하면 `%XX` 3개, 9글자가 된다. 200자면 URL이 약 1.8KB.
- 200자는 **글자 수** 기준이다 (UTF-8 바이트 수가 아니다). 한글 한 글자도 1자.
- 서버가 Microsoft 음성 서버(edge-tts)를 거쳐 음성을 만들므로 **서버에 인터넷이 연결되어 있어야 한다.**
- 한 번 만든 텍스트는 서버가 기억해 두었다가(최근 64개) 다음부터 바로 돌려준다.
- 처음 요청하는 텍스트는 음성을 만드느라 수 초가 걸릴 수 있다. 서버는 음성 생성에 최대 15초, 변환에 최대 10초까지 기다린다. **기기의 HTTP 타임아웃은 30초 정도로 넉넉히** 잡을 것.

### 성공 응답 (200)

```
HTTP/1.1 200 OK
content-type: application/octet-stream
content-length: 12000
x-audio-duration: 1.500

<raw PCM 12000바이트>
```

(숫자는 1.5초짜리 음성일 때의 예시)

| 헤더 | 설명 |
|---|---|
| `Content-Length` | 오디오 바이트 수. 수신·버퍼 관리는 **이 값을 기준으로** 한다 |
| `X-Audio-Duration` | 재생 시간(초). `Content-Length ÷ 8000`을 소수점 셋째 자리까지 반올림한 문자열 |

> 서버는 헤더 이름을 소문자(`x-audio-duration`)로 보낸다. HTTP 헤더 이름은 대소문자를 구분하지 않으므로, 헤더를 찾을 때 대소문자를 무시하고 비교할 것.

### 오디오 형식

| 항목 | 값 |
|---|---|
| 샘플레이트 | **8000 Hz** |
| 샘플 크기 | **8비트 unsigned** (0~255) |
| 채널 | **모노** |
| 헤더 | **없음.** WAV 헤더 없이 본문 첫 바이트부터 바로 샘플이다 |
| 무음 | **128** (0이 아니다) |
| 크기 | 1바이트 = 샘플 1개 = 1/8000초 → **1초 = 8000바이트** |

- 받은 바이트를 순서대로, 1/8000초마다 한 개씩 8비트 PWM 듀티 값으로 그대로 쓰면 된다. 부호 변환이나 디코딩은 필요 없다.
- 1.5초 음성이면 12,000바이트다. ATmega2560의 RAM(8KB)에는 **1초 분량도 다 들어가지 않으므로** 전부 받아두고 재생할 수 없다. 아래 재생 가이드처럼 받으면서 조금씩 재생해야 한다.

### 실패 응답

> ⚠️ **`/process` 계열과 달리 성공 여부는 HTTP 상태 코드로 판단한다.**
> **상태 코드가 200일 때만 본문이 오디오다.** 그 외에는 본문을 재생하지 말 것 (JSON 글자가 잡음으로 나온다).

실패 본문은 `/process`와 같은 형식의 JSON이다.

```json
{ "ok": false, "error": "tts_unavailable", "reason": "음성 서버 응답 시간이 초과되었습니다." }
```

| `error` | HTTP | 발생 조건 | 기기 대응 |
|---|---|---|---|
| `missing_text` | 400 | `text`가 없거나 비었음. 글자·숫자 없이 공백·기호뿐인 경우도 포함 | 요청을 만드는 코드 확인. 재시도해도 결과가 같다 |
| `text_too_long` | 400 | 앞뒤 공백을 뗀 `text`가 200자 초과 | 텍스트를 줄여서 다시 요청 |
| `tts_unavailable` | 503 | 서버의 인터넷 끊김, 음성 서버 응답 없음·시간 초과·거부, 서버에 edge-tts 미설치 | 음성 없이 진행. 잠시 후 재시도 가능 |
| `conversion_failed` | 500 | 서버의 오디오 변환(ffmpeg) 실패 | 음성 없이 진행 |

기기가 서버에 아예 접속하지 못하면(기기 쪽 네트워크 문제) HTTP 응답 자체가 오지 않는다. 이 경우는 기기의 연결 오류로 처리한다.

### 펌웨어 재생 가이드 (ATmega2560)

> 아래 코드는 형식을 설명하기 위한 **참고 구현이며, 실제 하드웨어에서 검증하지 않았다.** 핀·타이머·통신 방식은 회로에 맞게 조정할 것.

#### 전체 흐름

```
서버 ─HTTP─▶ 네트워크 칩(ESP32 등) ─UART─▶ ATmega2560 링 버퍼 ─8kHz 인터럽트─▶ PWM 핀 ─▶ RC 필터 ─▶ 앰프 ─▶ 스피커
```

1. 네트워크 칩이 `GET /speech`를 보낸다. 상태 코드가 200이 아니면 음성 없이 끝낸다.
2. 받은 바이트를 ATmega로 흘려보낸다. ATmega는 링 버퍼에 쌓는다.
3. 버퍼가 어느 정도(예: 512바이트 = 64ms) 차면 재생을 시작한다. 조금 모아두고 시작해야 전송이 잠깐 늦어져도 소리가 끊기지 않는다.
4. 타이머 인터럽트가 1/8000초마다 버퍼에서 한 바이트씩 꺼내 PWM 듀티로 쓴다.
5. `Content-Length`만큼 다 받고 버퍼도 비면 재생을 끝낸다.

#### 전송할 때 주의할 숫자

- UART로 넘긴다면 초당 8000바이트를 실시간으로 보내야 하므로 **115200bps 이상**이 필요하다 (8N1 기준 초당 약 11,520바이트).
- 전송 속도(초당 약 11,520바이트)가 재생 속도(초당 8000바이트)보다 빠르다. 그냥 쏟아부으면 ATmega 버퍼가 넘치므로 **흐름 제어가 필요하다.** 예) ATmega가 버퍼에 여유가 생길 때마다 네트워크 칩에 "N바이트 더 보내라"고 요청한다.
- Arduino `Serial`의 수신 버퍼는 64바이트뿐이라 115200bps로 받으면 약 5ms 만에 찬다. 재생 중에는 `loop()`에서 `delay()` 등으로 오래 멈추지 말고, 들어온 바이트를 계속 링 버퍼로 옮길 것.

#### 타이머와 인터럽트 (Arduino Mega, 16MHz)

- **Timer2**: 8비트 Fast PWM, 분주 없음 → 16MHz ÷ 256 = **62.5kHz** 반송파 (가청 주파수 밖). 출력은 **10번 핀(OC2A)**.
- **Timer1**: CTC 모드, 분주 없음, `OCR1A = 1999` → 16MHz ÷ 2000 = **정확히 8000Hz**마다 인터럽트.

```cpp
#include <util/atomic.h>

#define SAMPLE_RATE 8000
#define SILENCE     128
#define PREBUFFER   512                 // 이만큼 모이면 재생 시작 (64ms)
#define BUF_SIZE    1024                // 반드시 2의 거듭제곱 (1024바이트 = 128ms)
#define BUF_MASK    (BUF_SIZE - 1)

static uint8_t buf[BUF_SIZE];
static volatile uint16_t head = 0;      // 다음에 쓸 위치. loop() 쪽만 바꾼다
static volatile uint16_t tail = 0;      // 다음에 읽을 위치. 인터럽트만 바꾼다
static volatile bool playing = false;

void audioBegin() {
  pinMode(10, OUTPUT);
  noInterrupts();

  // Timer2: 8비트 Fast PWM, 분주 없음 → 62.5kHz, 10번 핀(OC2A) 출력
  TCCR2A = _BV(COM2A1) | _BV(WGM21) | _BV(WGM20);
  TCCR2B = _BV(CS20);
  OCR2A  = SILENCE;

  // Timer1: CTC, 분주 없음 → 16MHz / (1999 + 1) = 8000Hz마다 인터럽트
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS10);
  OCR1A  = F_CPU / SAMPLE_RATE - 1;     // 1999
  TCNT1  = 0;
  TIMSK1 = _BV(OCIE1A);

  interrupts();
}

// 1/8000초마다: 샘플 한 바이트를 그대로 PWM 듀티로 쓴다
ISR(TIMER1_COMPA_vect) {
  if (playing && tail != head) {
    OCR2A = buf[tail];
    tail = (tail + 1) & BUF_MASK;
  } else {
    OCR2A = SILENCE;                    // 재생 전이거나 데이터가 늦으면 무음
  }
}

// 받은 샘플을 버퍼에 넣는다. 가득 차 있으면 false (흐름 제어로 이런 일이 없게 할 것)
bool audioPush(uint8_t sample) {
  bool ok = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {   // head·tail은 16비트라 인터럽트와 함께 쓸 때는 원자적으로
    uint16_t next = (head + 1) & BUF_MASK;
    if (next != tail) {
      buf[head] = sample;
      head = next;
      ok = true;
    }
  }
  return ok;
}

// 버퍼에 쌓인 바이트 수
uint16_t audioBuffered() {
  uint16_t n;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    n = (head - tail) & BUF_MASK;
  }
  return n;
}

// loop()에서 계속 호출. received = 지금까지 받은 바이트 수, total = Content-Length
void audioUpdate(uint32_t received, uint32_t total) {
  bool allReceived = (received >= total);
  if (!playing && (audioBuffered() >= PREBUFFER || allReceived)) {
    playing = true;                     // 충분히 모였거나, 짧은 음성을 다 받았으면 시작
  }
  if (playing && allReceived && audioBuffered() == 0) {
    playing = false;                    // 끝까지 재생함
  }
}
```

- **함께 쓸 수 없는 기능**: Timer2를 쓰므로 `tone()`과 9·10번 핀의 `analogWrite()`, Timer1을 쓰므로 11·12번 핀의 `analogWrite()`. 사용하는 라이브러리가 이 타이머를 쓰는지도 확인할 것.
- 재생하지 않을 때도 PWM을 끄지 않고 무음(128)으로 켜 둔다. 출력이 0V와 중간 전압 사이를 갑자기 오가면 "딱" 소리가 나기 때문이다.

#### 출력 회로

- PWM 핀으로 스피커를 직접 구동하지 말 것 (핀 허용 전류를 넘어 칩이 손상될 수 있다). **RC 저역통과 필터 → 앰프(PAM8403, LM386 등) → 스피커** 순서로 연결한다.
- RC 필터 예: **1kΩ + 47nF** → 차단 주파수 약 3.4kHz. 음성 대역(8kHz 샘플이 담을 수 있는 상한 4kHz)은 통과시키고 62.5kHz PWM 반송파는 걸러낸다.

#### URL 인코딩 (네트워크 칩 쪽, 예: ESP32 Arduino)

```cpp
// UTF-8 문자열 → 퍼센트 인코딩. 영문·숫자와 - _ . ~ 는 그대로, 나머지 바이트는 %XX
String urlEncode(const String& s) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    bool plain = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 c == '-' || c == '_' || c == '.' || c == '~';
    if (plain) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

// 사용 예 (서버 주소는 환경에 맞게)
String url = String("http://192.168.0.10:8000/speech?text=") + urlEncode(text);
```
