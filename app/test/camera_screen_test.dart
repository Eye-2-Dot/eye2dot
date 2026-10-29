// 카메라 화면 테스트 — 텍스트 직접 입력 경로로 서버 전송·오류 문구·버튼을 확인한다
//
// 사진 경로는 카메라 플러그인이 필요해 테스트에서 누를 수 없지만,
// 사진과 텍스트가 같은 전송 함수(_submit)를 쓰므로 여기서 함께 확인된다.
// 진짜 서버 대신 가짜 서버(MockClient)를 끼워 넣는다.

import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

import 'package:app/constants.dart';
import 'package:app/screens/camera_screen.dart';
import 'package:app/screens/result_screen.dart';
import 'package:app/services/api_service.dart';

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

http.Response jsonResponse(Object body, [int status = 200]) => http.Response.bytes(
      utf8.encode(jsonEncode(body)),
      status,
      headers: {'content-type': 'application/json'},
    );

/// 라벨 모드 카메라 화면을 띄우고 문구를 직접 입력해 보낸다.
Future<void> submitText(WidgetTester tester, String text) async {
  await tester.pumpWidget(const MaterialApp(home: CameraScreen(mode: kModeLabel)));
  await tester.tap(find.text('라벨 문구 직접 입력하기'));
  await tester.pumpAndSettle();
  await tester.enterText(find.byType(TextField), text);
  await tester.tap(find.text('확인'));
}

void main() {
  setUp(() {
    ApiService.useMock = false;
    ApiService.retryDelayUnit = Duration.zero;
  });
  tearDown(() {
    ApiService.useMock = kUseMock;
    ApiService.clientFactory = http.Client.new;
    ApiService.retryDelayUnit = const Duration(seconds: 1);
  });

  testWidgets('직접 입력도 서버를 거치고, 전송 중에는 "점역 중입니다..."가 보인다', (tester) async {
    final reply = Completer<http.Response>();
    final requests = useFakeServer((_) => reply.future);

    await submitText(tester, '우유');
    await tester.pump();

    expect(find.text('점역 중입니다...'), findsOneWidget);
    expect(find.text('잠시만 기다려 주세요.'), findsOneWidget);
    expect(requests.single.url.path, '/translate');

    reply.complete(jsonResponse({
      'ok': true,
      'text': '우유',
      'reason': '직접 입력한 문구입니다.',
      'braille': [13, 41],
      'braille_preview': '⠍⠩',
      'cell_count': 2,
    }));
    await tester.pumpAndSettle();

    // 서버 응답 전체가 결과 화면까지 전달된다
    final result = tester.widget<ResultScreen>(find.byType(ResultScreen));
    expect(result.mode, kModeLabel);
    expect(result.text, '우유');
    expect(result.reason, '직접 입력한 문구입니다.');
    expect(result.braille, [13, 41]);
    expect(result.braille, isA<List<int>>());
    expect(result.braillePreview, '⠍⠩');
    expect(result.cellCount, 2);
  });

  testWidgets('서버 reason 이 있으면 그대로 보여 주고, "다시 입력하기"는 이전 글자를 채워 연다',
      (tester) async {
    useFakeServer((_) => jsonResponse({
          'ok': false,
          'error': 'text_too_long',
          'reason': '라벨 문구는 50자 이하여야 합니다 (입력: 73자)',
        }, 400));

    await submitText(tester, '아주긴문구');
    await tester.pumpAndSettle();

    expect(find.text('라벨 문구는 50자 이하여야 합니다 (입력: 73자)'), findsOneWidget);
    expect(find.text('다시 시도'), findsOneWidget);
    expect(find.text('다시 입력하기'), findsOneWidget);
    expect(find.text('새 사진 고르기'), findsNothing);

    await tester.tap(find.text('다시 입력하기'));
    await tester.pumpAndSettle();

    final field = tester.widget<TextField>(find.byType(TextField));
    expect(field.controller!.text, '아주긴문구');
  });

  testWidgets('network_error 는 서버 reason 대신 고정 문구를 보여 준다', (tester) async {
    useFakeServer((_) => jsonResponse({
          'ok': false,
          'error': 'network_error',
          'reason': 'Gemini API 오류: 503 UNAVAILABLE',
        }, 502));

    await submitText(tester, '우유');
    await tester.pumpAndSettle();

    expect(find.text('네트워크 연결을 확인해 주세요'), findsOneWidget);
    expect(find.textContaining('Gemini'), findsNothing);
  });

  testWidgets('parse_failed 는 서버 reason 대신 고정 문구를 보여 준다', (tester) async {
    useFakeServer((_) => jsonResponse({
          'ok': false,
          'error': 'parse_failed',
          'reason': 'Gemini 응답에서 텍스트를 읽지 못했습니다.',
        }, 500));

    await submitText(tester, '우유');
    await tester.pumpAndSettle();

    expect(find.text('오류가 발생했습니다. 다시 시도해 주세요'), findsOneWidget);
    expect(find.textContaining('Gemini'), findsNothing);
  });

  testWidgets('reason 이 없으면 오류 코드별 문구, 모르는 코드면 기본 문구', (tester) async {
    useFakeServer((_) => jsonResponse({'ok': false, 'error': 'missing_text'}, 400));
    await submitText(tester, '!!!');
    await tester.pumpAndSettle();
    expect(find.text('입력한 내용이 없습니다'), findsOneWidget);

    // 서버의 예상 밖 오류는 error 에 예외 메시지가 그대로 들어온다
    useFakeServer((_) => jsonResponse({'ok': false, 'error': 'KeyError: name'}, 500));
    await tester.tap(find.text('다시 시도'));
    await tester.pumpAndSettle();
    expect(find.text('오류가 발생했습니다. 다시 시도해 주세요'), findsOneWidget);
    expect(find.textContaining('KeyError'), findsNothing);
  });

  testWidgets('"다시 시도"는 같은 글자를 다시 보낸다', (tester) async {
    final requests = useFakeServer((_) => jsonResponse(
        {'ok': false, 'error': 'rate_limit', 'reason': '잠시 후 다시 시도하세요.'}, 429));

    await submitText(tester, '샴푸');
    await tester.pumpAndSettle();
    await tester.tap(find.text('다시 시도'));
    await tester.pumpAndSettle();

    expect(requests, hasLength(2));
    expect(jsonDecode(requests[1].body), {'text': '샴푸', 'mode': 'label'});
  });

  testWidgets('서버에 연결하지 못하면 재시도 후 네트워크 문구를 보여 준다', (tester) async {
    final requests = useFakeServer((_) => throw http.ClientException('XMLHttpRequest error.'));

    await submitText(tester, '우유');
    await tester.pumpAndSettle();

    expect(requests, hasLength(4));
    expect(find.text('네트워크 연결을 확인해 주세요'), findsOneWidget);
  });
}
