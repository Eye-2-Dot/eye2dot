// ApiService 테스트
//
// 1. 더미 모드: 더미 응답이 서버 목(POST /process-mock)과 같은 형식인지
// 2. 실서버 모드: 가짜 서버(MockClient)를 끼워 넣어 요청 형식·응답 처리·재시도를 확인
//    (진짜 서버에는 요청을 보내지 않는다)

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

import 'package:app/constants.dart';
import 'package:app/services/api_service.dart';

/// 서버(FastAPI)처럼 charset 없이 application/json 으로 답하는 가짜 응답
http.Response jsonResponse(Object body, [int status = 200]) => http.Response.bytes(
      utf8.encode(jsonEncode(body)),
      status,
      headers: {'content-type': 'application/json'},
    );

/// 가짜 서버를 끼워 넣는다. 받은 요청은 돌려주는 리스트에 차례로 쌓인다.
List<http.Request> useFakeServer(
  FutureOr<http.Response> Function(http.Request request) handler,
) {
  final requests = <http.Request>[];
  ApiService.clientFactory = () => MockClient((request) async {
        requests.add(request);
        return handler(request);
      });
  return requests;
}

void main() {
  final emptyBytes = Uint8List(0);

  group('더미 모드', () {
    setUp(() => ApiService.useMock = true);
    tearDown(() => ApiService.useMock = kUseMock);

    test('라벨 모드는 braille이 1차원 정수 배열이다', () async {
      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeLabel);

      expect(r['ok'], isTrue);
      expect(r['text'], '우유');
      expect(r['braille'], isA<List<int>>());
      expect(r['braille'], [13, 41]);
      expect(r['cell_count'], (r['braille'] as List).length);
    });

    test('책 모드는 braille이 줄 단위 2차원 배열이다', () async {
      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeBook);
      final lines = r['braille'] as List<List<int>>;

      expect(r['ok'], isTrue);
      expect(lines, isNotEmpty);
      // 한 줄도 빠짐없이 서버의 MAX_CELLS_PER_LINE(30) 이하여야 한다
      expect(lines.every((l) => l.isNotEmpty && l.length <= 30), isTrue);
      // 줄 맨 앞과 맨 끝에는 빈 칸(0)이 오지 않는다
      expect(lines.every((l) => l.first != 0 && l.last != 0), isTrue);
      expect(r['cell_count'], lines.fold<int>(0, (sum, l) => sum + l.length));
    });

    test('두 모드 모두 응답 키가 서버와 같다', () async {
      const expected = {
        'ok',
        'text',
        'reason',
        'braille',
        'braille_preview',
        'cell_count',
      };

      for (final mode in [kModeLabel, kModeBook]) {
        final r = await ApiService.sendImage(bytes: emptyBytes, mode: mode);
        expect(r.keys.toSet(), expected, reason: 'mode=$mode');
      }
    });

    test('braille_preview의 줄 수가 braille의 줄 수와 같다', () async {
      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeBook);
      final lines = r['braille'] as List<List<int>>;
      final preview = (r['braille_preview'] as String).split('\n');

      expect(preview.length, lines.length);
      // 각 줄의 유니코드 점자 글자 수도 셀 수와 일치해야 한다
      for (var i = 0; i < lines.length; i++) {
        expect(preview[i].length, lines[i].length, reason: '$i번째 줄');
      }
    });

    test('텍스트 입력은 입력과 상관없이 "우유" 결과를 준다', () async {
      final r = await ApiService.sendText(text: '샴푸', mode: kModeLabel);

      expect(r['ok'], isTrue);
      expect(r['text'], '우유');
      expect(r['braille'], [13, 41]);
      expect(r['reason'], '직접 입력한 문구입니다.');
    });
  });

  group('실서버 모드', () {
    setUp(() {
      ApiService.useMock = false;
      ApiService.retryDelayUnit = Duration.zero; // 테스트가 기다리지 않도록
    });
    tearDown(() {
      ApiService.useMock = kUseMock;
      ApiService.clientFactory = http.Client.new;
      ApiService.retryDelayUnit = const Duration(seconds: 1);
    });

    test('sendImage: /process-file 에 쿼리 mode + multipart image(파일 이름 포함)로 보낸다',
        () async {
      final requests = useFakeServer((_) => jsonResponse({
            'ok': true,
            'text': '점자는',
            'reason': '3자를 인식했습니다.',
            'braille': [
              [40, 14, 34],
              [40, 35, 9],
            ],
            'braille_preview': '⠨⠎⠢\n⠨⠣⠉',
            'cell_count': 6,
          }));

      final r = await ApiService.sendImage(
        bytes: Uint8List.fromList([0xFF, 0xD8, 0xFF]),
        mode: kModeBook,
      );

      expect(requests, hasLength(1));
      final req = requests.single;
      expect(req.method, 'POST');
      expect(req.url.toString(), '$kServerBaseUrl/process-file?mode=book');
      expect(req.headers['content-type'], startsWith('multipart/form-data'));
      // 파일 이름이 있어야 서버가 글자가 아니라 파일로 받는다
      expect(latin1.decode(req.bodyBytes), contains('name="image"; filename="photo.jpg"'));

      expect(r['ok'], isTrue);
      expect(r['text'], '점자는', reason: '한글이 깨지지 않아야 한다');
      expect(r['braille'], isA<List<List<int>>>());
      expect(r['braille'], [
        [40, 14, 34],
        [40, 35, 9],
      ]);
    });

    test('sendText: /translate 에 JSON 으로 보내고 braille 은 List<int> 로 받는다', () async {
      final requests = useFakeServer((_) => jsonResponse({
            'ok': true,
            'text': '우유',
            'reason': '직접 입력한 문구입니다.',
            'braille': [13, 41],
            'braille_preview': '⠍⠩',
            'cell_count': 2,
          }));

      final r = await ApiService.sendText(text: '  우유  ', mode: kModeLabel);

      final req = requests.single;
      expect(req.method, 'POST');
      expect(req.url.toString(), '$kServerBaseUrl/translate');
      expect(req.headers['content-type'], startsWith('application/json'));
      expect(jsonDecode(req.body), {'text': '  우유  ', 'mode': 'label'});

      expect(r['ok'], isTrue);
      expect(r['text'], '우유');
      expect(r['braille'], isA<List<int>>());
      expect(r['braille_preview'], '⠍⠩');
    });

    test('400 실패 응답은 reason 까지 그대로 돌려준다', () async {
      const body = {
        'ok': false,
        'error': 'text_too_long',
        'reason': '라벨 문구는 50자 이하여야 합니다 (입력: 73자)',
      };
      final requests = useFakeServer((_) => jsonResponse(body, 400));

      final r = await ApiService.sendText(text: 'x', mode: kModeLabel);

      expect(r, body);
      expect(requests, hasLength(1), reason: '서버가 답한 실패는 재시도하지 않는다');
    });

    test('recognition_failed 는 HTTP 200 이어도 실패로 돌려준다', () async {
      useFakeServer((_) => jsonResponse(
          {'ok': false, 'error': 'recognition_failed', 'reason': '사물을 찾지 못했습니다.'}));

      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeLabel);

      expect(r['ok'], isFalse);
      expect(r['error'], 'recognition_failed');
    });

    test('JSON 이 아닌 응답은 parse_failed', () async {
      useFakeServer((_) => http.Response('Internal Server Error', 500));

      final r = await ApiService.sendText(text: 'x', mode: kModeLabel);

      expect(r, {'ok': false, 'error': 'parse_failed'});
    });

    test('ok 필드가 없는 JSON(FastAPI 422 등)은 parse_failed', () async {
      useFakeServer((_) => jsonResponse({
            'detail': [
              {'msg': 'Field required'},
            ],
          }, 422));

      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeLabel);

      expect(r, {'ok': false, 'error': 'parse_failed'});
    });

    test('연결 실패는 3번 재시도하고(총 4번) 그래도 안 되면 network_error', () async {
      final requests = useFakeServer((_) => throw http.ClientException('연결 거부'));

      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeLabel);

      expect(requests, hasLength(4));
      expect(r, {'ok': false, 'error': 'network_error'});
    });

    test('연결 실패 후 재시도에서 성공하면 그 결과를 돌려준다', () async {
      var calls = 0;
      final requests = useFakeServer((_) {
        if (++calls <= 2) throw http.ClientException('연결 거부');
        return jsonResponse({'ok': true, 'text': '우유', 'braille': [13, 41]});
      });

      final r = await ApiService.sendText(text: '우유', mode: kModeLabel);

      expect(requests, hasLength(3));
      expect(r['ok'], isTrue);
    });

    test('서버의 network_error(502)도 재시도한다', () async {
      final requests = useFakeServer((_) => jsonResponse(
          {'ok': false, 'error': 'network_error', 'reason': 'Gemini 서버에 연결하지 못했습니다'},
          502));

      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeLabel);

      expect(requests, hasLength(4));
      expect(r['error'], 'network_error');
    });

    test('rate_limit 은 재시도하지 않는다', () async {
      final requests = useFakeServer((_) => jsonResponse(
          {'ok': false, 'error': 'rate_limit', 'reason': '요청 한도를 초과했습니다.'}, 429));

      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeLabel);

      expect(requests, hasLength(1));
      expect(r['error'], 'rate_limit');
    });

    test('시간 초과는 재시도하지 않고 network_error', () async {
      final requests = useFakeServer((_) => throw TimeoutException('시간 초과'));

      final r = await ApiService.sendImage(bytes: emptyBytes, mode: kModeLabel);

      expect(requests, hasLength(1));
      expect(r, {'ok': false, 'error': 'network_error'});
    });
  });
}
