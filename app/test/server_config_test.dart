// 서버 주소 정하기 테스트 (lib/services/server_config.dart)
//
// 1. normalizeServerUrl: 사람이 적은 주소를 http://호스트:포트 로 다듬는지
// 2. resolveServerUrl : 빌드값 → 웹(개발 Chrome / 서버가 띄운 페이지) → 앱 기본값 순서로 고르는지

import 'package:flutter_test/flutter_test.dart';

import 'package:app/constants.dart';
import 'package:app/services/server_config.dart';

void main() {
  group('normalizeServerUrl', () {
    const cases = {
      '192.168.43.10': 'http://192.168.43.10:8000', // http:// 와 :8000 을 붙인다
      '192.168.43.10:8000': 'http://192.168.43.10:8000',
      '192.168.43.10:9000': 'http://192.168.43.10:9000', // 적은 포트는 그대로
      'http://192.168.43.10': 'http://192.168.43.10:8000',
      'http://192.168.43.10:80': 'http://192.168.43.10:80', // :80 을 적었으면 지우지 않는다
      'https://example.com': 'https://example.com:8000',
      '  192.168.43.10:8000/  ': 'http://192.168.43.10:8000', // 앞뒤 공백, 끝의 / 는 뗀다
      'http://192.168.43.10:8000/health?x=1': 'http://192.168.43.10:8000', // 경로는 버린다
      'LAPTOP.local': 'http://laptop.local:8000',
    };
    cases.forEach((input, expected) {
      test('"$input" → $expected', () => expect(normalizeServerUrl(input), expected));
    });

    const invalid = [
      '',
      '   ',
      '192.168 .43.10', // 띄어쓰기
      'http://', // 호스트 없음
      '192.168.43.10:99999', // 포트 범위 밖
      '192.168.43.10:abc',
      'ftp://192.168.43.10',
    ];
    for (final input in invalid) {
      test('"$input" → null', () => expect(normalizeServerUrl(input), isNull));
    }
  });

  group('resolveServerUrl', () {
    String resolve({String defined = '', required bool isWeb, String page = 'file:///'}) =>
        resolveServerUrl(
          defined: defined,
          isWeb: isWeb,
          pageUri: Uri.parse(page),
          fallback: 'http://fallback:8000',
        );

    test('1) 빌드할 때 넣은 값이 가장 먼저다 (웹이어도)', () {
      expect(resolve(defined: '192.168.43.10', isWeb: false), 'http://192.168.43.10:8000');
      expect(resolve(defined: 'http://10.0.0.5:9000', isWeb: true, page: 'http://192.168.0.2:8000/'),
          'http://10.0.0.5:9000');
    });

    test('1) 빌드값의 형식이 틀리면 무시하고 다음 규칙으로', () {
      expect(resolve(defined: 'not a url', isWeb: false), 'http://fallback:8000');
    });

    test('2) 개발용 Chrome(localhost/127.0.0.1, 포트가 8000 아님) → http://localhost:8000', () {
      expect(resolve(isWeb: true, page: 'http://localhost:53211/'), 'http://localhost:8000');
      expect(resolve(isWeb: true, page: 'http://127.0.0.1:53211/#/'), 'http://localhost:8000');
    });

    test('2) 서버가 띄운 페이지 → 그 서버 주소 그대로', () {
      expect(resolve(isWeb: true, page: 'http://192.168.43.10:8000/'), 'http://192.168.43.10:8000');
      // 서버가 띄운 앱을 노트북에서 열 때
      expect(resolve(isWeb: true, page: 'http://localhost:8000/'), 'http://localhost:8000');
    });

    test('3) 휴대폰 앱 → 기본값', () {
      expect(resolve(isWeb: false), 'http://fallback:8000');
    });

    test('기본값을 주지 않으면 constants.dart 의 kServerBaseUrl', () {
      expect(resolveServerUrl(defined: '', isWeb: false, pageUri: Uri.parse('file:///')),
          kServerBaseUrl);
    });
  });

  test('테스트 환경(웹 아님, 빌드값 없음)의 실제 주소는 kServerBaseUrl', () {
    expect(serverBaseUrl, kServerBaseUrl);
  });
}
