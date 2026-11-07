# Eye 2 Dot

사진 한 장을 찍으면 Gemini 가 무엇인지 판단하고, 그 결과를 **점자 라벨로 찍고 음성으로 읽어 주는** 시각장애인용 점자 라벨링 시스템.

- **라벨 모드**: 사물 사진 → 사물 이름 → 점자 라벨 + 음성 안내 (펌웨어 1, 라벨 출력기)
- **책 모드**: 책 페이지 사진 → 본문 글자 인식(OCR) → 점역 (서버·앱에서 지원, 책 타각기 펌웨어는 아직 없음)

## 폴더 지도

| 폴더 | 무엇 | 먼저 읽을 문서 |
|---|---|---|
| [`app/`](app/) | Flutter 앱 — 모드 선택, 촬영, 결과 화면 | [`app/README.md`](app/README.md) |
| [`server/`](server/) | FastAPI 서버 — Gemini 인식, 점역(`braille.py`), 음성(`speech.py`) | [`docs/api-spec.md`](docs/api-spec.md) (설치·실행 포함) |
| [`firmware1/`](firmware1/) | 펌웨어 1 라벨 출력기 — ESP32-CAM(`cam/`) + ATmega2560(`mega/`) | [`firmware1/README.md`](firmware1/README.md) |
| [`docs/`](docs/) | API·통신 규격 | 아래 "기준 문서" |

## 기준 문서

같은 내용을 여러 곳에 적지 않는다. 바꿀 때는 아래 기준 문서를 고치고, 다른 문서는 그곳을 가리키게 한다.

| 내용 | 기준 |
|---|---|
| 서버 HTTP 주소, 응답 형식, 음성 형식 | [`docs/api-spec.md`](docs/api-spec.md) |
| 폴링 제안(`/print`, `/jobs` — 아직 실제 서버에 없음), 두 칩(ESP32-CAM ↔ ATmega2560) 사이 프레임 | [`docs/POLLING_API.md`](docs/POLLING_API.md) |
| 시리얼 값 (통신 속도, 프레임 번호, 크기·시간 한도) | [`firmware1/common/protocol.h`](firmware1/common/protocol.h) |
| 펌웨어 1 사용법, 배선, 시험 방법 | [`firmware1/README.md`](firmware1/README.md) |

## 비밀값 — git 에 올리지 않는 파일

아래 파일은 `.gitignore` 에 들어 있어 커밋되지 않는다. 각자 컴퓨터에서 만든다.

| 파일 | 내용 | 만드는 법 |
|---|---|---|
| `server/.env` | Gemini API 키 | `server/` 에 `.env` 를 만들고 `GEMINI_API_KEY=발급받은_키` 한 줄. 없으면 서버가 켜지지 않는다 |
| `firmware1/cam/include/secrets.h` | Wi-Fi 이름·비밀번호, 서버(노트북) IP | 같은 폴더의 `secrets.example.h` 를 `secrets.h` 로 복사해 채운다. 없으면 cam 빌드가 멈춘다 |

- **견본 파일(`secrets.example.h`)에는 진짜 값을 넣지 말 것.** 견본은 git 에 올라간다.
- 비밀은 아니지만 컴퓨터마다 다른 `upload_ports.ini`(COM 포트), `*.code-workspace`(VS Code 작업공간)도 올리지 않는다.

## 라이선스

MIT — [`LICENSE`](LICENSE)
