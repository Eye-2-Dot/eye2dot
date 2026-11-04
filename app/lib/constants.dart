// 앱 전역 상수 모음
//
// 서버 주소와 모드 값 문자열을 여기 한 곳에서 관리한다.
// 화면(screens)과 통신(services) 쪽에서 문자열을 직접 쓰지 말고
// 반드시 이 파일의 상수를 import 해서 쓸 것.

/// 백엔드 서버 주소
/// 지금은 로컬 개발용. 실제 배포 시 이 값만 바꾸면 된다.
const String kServerBaseUrl = 'http://localhost:8000';

/// 더미 모드 스위치
///
///   false (기본): 실서버(kServerBaseUrl)로 요청을 보낸다.
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
