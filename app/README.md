# app — Eye 2 Dot 앱 (Flutter)

사진을 찍거나 글자를 입력해 점자로 바꾸고, 기기로 출력을 보내는 앱이다.
앱을 쓰는 사람은 보조자(비시각장애인)라 음성 안내 없이 큰 버튼으로만 안내한다.

## 실행 (PC 에서 개발할 때)

서버(`server/`)를 먼저 켠다 (루트 [`README.md`](../README.md) "빠른 시작"). 그다음 같은 PC 에서:

```bash
cd app
flutter pub get
flutter run -d chrome
```

## 서버 주소

앱이 요청을 보낼 주소는 [`lib/services/server_config.dart`](lib/services/server_config.dart) 가 아래 순서로 정한다. 앞의 것이 있으면 뒤는 보지 않는다.

| 순서 | 조건 | 주소 |
|---|---|---|
| 1 | 빌드할 때 `--dart-define=SERVER_URL=...` 을 넣었다 | 그 값 (`192.168.43.10` 처럼 적어도 `http://`·`:8000` 을 붙인다) |
| 2 | 브라우저에서 열었고, 페이지가 `localhost`·`127.0.0.1` 이면서 포트가 8000 이 아니다 (`flutter run -d chrome`) | `http://localhost:8000` |
| 2 | 브라우저에서 열었고, 그 밖 (서버가 띄운 앱 화면, 아래 "태블릿·휴대폰 브라우저로 쓰기") | 페이지를 띄운 서버 그대로 |
| 3 | 그 밖 (휴대폰에 설치한 앱) | [`lib/constants.dart`](lib/constants.dart) 의 `kServerBaseUrl` |

같은 파일(`constants.dart`)의 `kUseMock` 을 `true` 로 바꾸면 서버 없이 앱 안의 가짜 결과로 화면만 볼 수 있다.

## 태블릿·휴대폰 브라우저로 쓰기

앱을 설치하지 않고, 노트북 서버가 띄워 주는 앱 화면을 Chrome 으로 연다.
서버는 `app/build/web` 이 있으면 `http://<노트북 IP>:8000/` 에서 앱 화면을 보여 주고,
앱은 자기를 띄운 서버로 요청하므로 서버 주소를 따로 정할 필요가 없다.

### 시연 준비 순서

시연 기기는 갤럭시탭(Chrome), 네트워크는 팀원 스마트폰 핫스팟이다. 노트북·갤럭시탭·ESP32 2개가 모두 이 핫스팟에 붙는다.

1. **스마트폰 핫스팟 켜기.** 대역은 **2.4GHz** (ESP32 는 5GHz 에 붙지 못한다).
   갤럭시는 핫스팟 설정의 "대역", 아이폰은 "호환성 최대화"를 켠다. 데이터는 넉넉히, 충전기를 꽂아 둔다.
2. **노트북을 핫스팟에 연결.** 핫스팟 **하나에만** 연결한다 (유선 랜·VPN 은 끈다. 서버가 엉뚱한 IP 를 출력할 수 있다).
   처음 서버를 켤 때 Windows 방화벽 창이 뜨면 Python 을 허용한다.
   Windows 가 핫스팟을 "공용 네트워크"로 잡으면 **공용**에도 체크해야 갤럭시탭·ESP32 가 접속할 수 있다.
3. **앱 화면 만들기** (앱을 고쳤을 때만. `app/build/web` 이 이미 있으면 건너뛴다)
   ```bash
   cd app
   flutter build web --no-web-resources-cdn
   ```
   `--no-web-resources-cdn`: 화면을 그리는 엔진 파일을 인터넷(CDN) 대신 노트북 서버에서 받게 한다.
4. **서버 켜기** (`server` 폴더에서 `uvicorn main:app --host 0.0.0.0 --port 8000`). 로그 맨 위의 주소를 확인한다.
   ```
   갤럭시탭에서 접속: http://192.168.x.x:8000/
   펌웨어 secrets.h 의 서버 IP(SERVER_IP_VALUE)도 이 주소여야 합니다: 192.168.x.x
   ```
   "앱 화면 없음" 이 나오면 3번을 먼저 한다.
5. **펌웨어 서버 IP 확인.** 로그의 IP 가 두 ESP32 의 `secrets.h` 에 적은 `SERVER_IP_VALUE` 와 다르면,
   두 ESP32(`firmware1/cam`, `firmware2/esp`)의 `secrets.h` 를 고쳐서 다시 올려야 한다.
   (견본 파일에 적힌 `192.168.137.1` 은 Windows 핫스팟의 값이라 스마트폰 핫스팟과 다르다. 로그에 나온 IP 를 쓴다)
6. **갤럭시탭을 핫스팟에 연결** → Chrome 으로 `http://<IP>:8000/health` 가 열리는지 먼저 보고 → `http://<IP>:8000/` 에 접속한다.

### 주의

- **갤럭시탭도 인터넷이 되어야 한글·점자 글자가 보인다.** 웹 앱은 한글·점자 글꼴을 처음 쓸 때 Google 글꼴 서버에서 받는다
  (`--no-web-resources-cdn` 은 엔진 파일만 노트북에서 받게 한다). 스마트폰 핫스팟은 데이터가 있으니 보통 문제없다.
- **앱을 다시 빌드했으면 갤럭시탭 Chrome 에서 새로고침**한다. 예전 화면이 남아 있을 수 있다.
- "카메라로 촬영하기"를 누르면 태블릿의 카메라 앱이 열린다. 찍은 사진은 긴 변 1600px 로 줄여서 보낸다.
- 연결이 안 될 때 화면 문구로 원인을 나눈다.
  - "서버에 연결하지 못했습니다…": 노트북 서버가 꺼져 있거나 갤럭시탭이 다른 네트워크에 있다.
  - "노트북 서버가 Gemini 에 연결하지 못했습니다…": 갤럭시탭 ↔ 노트북은 되지만 노트북이 인터넷(Gemini)에 못 나간다. 핫스팟 데이터를 확인한다.

### 나중에 필요하면: 앱 설치 (APK·아이폰)

지금 시연은 브라우저 방식이라 쓰지 않는다. 권한 설정(안드로이드 `AndroidManifest.xml`, 아이폰 `Info.plist`)은 넣어 두었다.

- 안드로이드 APK: Android Studio(Android SDK)가 있는 PC 에서
  `flutter build apk --release --dart-define=SERVER_URL=http://<노트북 IP>:8000` →
  `build/app/outputs/flutter-apk/app-release.apk` 를 휴대폰에 옮겨 설치 ("출처를 알 수 없는 앱" 허용).
  노트북 IP 가 바뀌면 다시 빌드해야 한다.
- 아이폰 앱: Mac 이 있어야 빌드할 수 있다.

## 화면 흐름

1. 모드 선택 ([`lib/screens/mode_screen.dart`](lib/screens/mode_screen.dart)) — "점자 라벨 만들기" / "책 점역하기"
2. 촬영 ([`lib/screens/camera_screen.dart`](lib/screens/camera_screen.dart)) — 카메라로 촬영하기, 앨범에서 사진 선택하기, 라벨 모드에서는 문구 직접 입력 → 서버(`/process-file`, `/translate`)
3. 결과 ([`lib/screens/result_screen.dart`](lib/screens/result_screen.dart)) — 점자 미리보기 확인 → "출력하기"(`POST /print`)로 서버 출력 대기열에 넣기

넓은 화면(태블릿)에서는 세 화면 모두 본문을 가운데 폭 600 까지만 쓴다 (`constants.dart` 의 `kContentMaxWidth`).

서버와 주고받는 코드는 [`lib/services/api_service.dart`](lib/services/api_service.dart), 주소 규격은 [`docs/api-spec.md`](../docs/api-spec.md).
