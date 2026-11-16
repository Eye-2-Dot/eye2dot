# app — Eye 2 Dot 앱 (Flutter)

사진을 찍거나 글자를 입력해 점자로 바꾸고, 기기로 출력을 보내는 앱이다.
앱을 쓰는 사람은 보조자(비시각장애인)라 음성 안내 없이 큰 버튼으로만 안내한다.

## 실행

서버(`server/`)를 먼저 켠다 (루트 [`README.md`](../README.md) "빠른 시작"). 그다음 같은 PC 에서:

```bash
cd app
flutter pub get
flutter run -d chrome
```

## 서버 주소

[`lib/constants.dart`](lib/constants.dart) 의 `kServerBaseUrl` (지금 `http://localhost:8000`).
같은 파일의 `kUseMock` 을 `true` 로 바꾸면 서버 없이 앱 안의 가짜 결과로 화면만 볼 수 있다.
휴대폰에서 돌리려면 남은 일이 있다 (루트 README "현재 상태 → 남은 과제").

## 화면 흐름

1. 모드 선택 ([`lib/screens/mode_screen.dart`](lib/screens/mode_screen.dart)) — "점자 라벨 만들기" / "책 점역하기"
2. 촬영 ([`lib/screens/camera_screen.dart`](lib/screens/camera_screen.dart)) — 카메라로 촬영하기, 앨범에서 사진 선택하기, 라벨 모드에서는 문구 직접 입력 → 서버(`/process-file`, `/translate`)
3. 결과 ([`lib/screens/result_screen.dart`](lib/screens/result_screen.dart)) — 점자 미리보기 확인 → "출력하기"(`POST /print`)로 서버 출력 대기열에 넣기

서버와 주고받는 코드는 [`lib/services/api_service.dart`](lib/services/api_service.dart), 주소 규격은 [`docs/api-spec.md`](../docs/api-spec.md).
