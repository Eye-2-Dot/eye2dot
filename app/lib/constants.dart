// 앱 전역 상수 모음
//
// 서버 주소와 모드 값 문자열을 여기 한 곳에서 관리한다.
// 화면(screens)과 통신(services) 쪽에서 문자열을 직접 쓰지 말고
// 반드시 이 파일의 상수를 import 해서 쓸 것.
//
// 앱이 실제로 요청을 보낼 주소는 services/server_config.dart 가 정한다.

/// 휴대폰 앱(APK)으로 빌드할 때의 기본 주소.
/// 지금 시연은 브라우저 방식이라 쓰이지 않는다.
/// (브라우저에서는 앱 화면을 띄운 서버로 보낸다. 정하는 순서는 app/README.md "서버 주소")
const String kServerBaseUrl = 'http://localhost:8000';

/// 서버 포트. 주소에 포트를 적지 않았으면 이 값을 붙인다.
/// 서버 실행 명령(uvicorn ... --port 8000)과 같은 값.
const int kServerPort = 8000;

/// 넓은 화면(태블릿)에서 본문 폭의 최댓값.
/// 버튼과 글자가 화면 끝까지 늘어나지 않게 가운데에 이 폭까지만 쓴다.
const double kContentMaxWidth = 600;

/// 더미 모드 스위치
///
///   false (기본): 실서버로 요청을 보낸다.
///   true        : 서버에 아무것도 보내지 않고 앱 안의 더미 결과를 돌려준다.
///                 서버가 꺼져 있어도 화면 개발을 이어갈 수 있다.
///                 (사진이든 직접 입력이든 항상 같은 결과. 입력한 글자는 무시된다)
///
/// 바꾼 뒤에는 앱을 다시 실행할 것. 핫 리로드로는 const 값이 바뀌지 않을 수 있다.
const bool kUseMock = false;

/// 모드: 사물 이름을 점자 라벨로 뽑기
const String kModeLabel = 'label';

/// 모드: 책 페이지를 점역하기
const String kModeBook = 'book';
