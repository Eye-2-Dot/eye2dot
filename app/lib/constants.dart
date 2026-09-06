// 앱 전역 상수 모음
//
// 서버 주소와 모드 값 문자열을 여기 한 곳에서 관리한다.
// 화면(screens)과 통신(services) 쪽에서 문자열을 직접 쓰지 말고
// 반드시 이 파일의 상수를 import 해서 쓸 것.

/// 백엔드 서버 주소
/// 지금은 로컬 개발용. 실제 배포 시 이 값만 바꾸면 된다.
const String kServerBaseUrl = 'http://localhost:8000';

/// 모드: 사물 이름을 점자 라벨로 뽑기
const String kModeLabel = 'label';

/// 모드: 책 페이지를 점역하기
const String kModeBook = 'book';
