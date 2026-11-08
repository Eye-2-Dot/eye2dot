// 결과 화면 테스트 — '출력하기' 버튼이 서버(POST /print)로 출력 요청을 보내는지
//
// 진짜 서버 대신 가짜 서버(MockClient)를 끼워 넣는다.
// 확인하는 것: 요청 형식, 보내는 동안 잠금, 성공 뒤 3초 잠금, 성공·실패 문구,
//             직접 입력(/translate)으로 만든 결과 화면에서도 같은 요청이 나가는지.

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

/// 서버의 /print 성공 응답
http.Response printOk({String device = 'fw1', int waiting = 1}) =>
    jsonResponse({'ok': true, 'job_id': '7cabc156cc10', 'device': device, 'waiting': waiting});

/// 라벨 결과 화면을 띄운다 (서버 /process-file 응답을 받은 것처럼)
Future<void> showLabelResult(
  WidgetTester tester, {
  Object braille = const <int>[13, 41],
  String reason = '흰색 종이팩에 우유 표기가 있습니다.',
}) async {
  await tester.pumpWidget(MaterialApp(
    home: ResultScreen(
      mode: kModeLabel,
      text: '우유',
      reason: reason,
      braille: braille,
      braillePreview: '⠍⠩',
      cellCount: 2,
    ),
  ));
}

/// 출력 버튼 (FilledButton.icon 은 FilledButton 을 이어받은 위젯이라 이렇게 찾는다)
final Finder printButton = find.byWidgetPredicate((w) => w is FilledButton);

bool printButtonEnabled(WidgetTester tester) =>
    tester.widget<FilledButton>(printButton).onPressed != null;

const String kLabelSent = '라벨 출력기로 보냈어요. 기기에서 안내를 듣고 출력 버튼을 눌러 주세요';

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

  testWidgets('출력하기 → /print 에 결과를 그대로 보내고, 라벨 성공 문구와 대기 수를 보여 준다', (tester) async {
    final requests = useFakeServer((_) => printOk(waiting: 1));
    await showLabelResult(tester);

    await tester.tap(find.text('출력하기'));
    await tester.pumpAndSettle();

    final req = requests.single;
    expect(req.url.path, '/print');
    expect(jsonDecode(req.body), {
      'mode': 'label',
      'text': '우유',
      'reason': '흰색 종이팩에 우유 표기가 있습니다.',
      'braille': [13, 41], // 앱은 거울로 바꾸지 않는다
    });
    expect(find.text('$kLabelSent (대기 중인 작업 1개)'), findsOneWidget);
    expect(find.text('보냈어요'), findsOneWidget);
  });

  testWidgets('보내는 동안에는 잠기고 진행 표시가 보이며, 두 번 눌러도 요청은 한 번', (tester) async {
    final reply = Completer<http.Response>();
    final requests = useFakeServer((_) => reply.future);
    await showLabelResult(tester);

    await tester.tap(find.text('출력하기'));
    await tester.pump();

    expect(find.text('보내는 중…'), findsOneWidget);
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    expect(printButtonEnabled(tester), isFalse);

    await tester.tap(find.text('보내는 중…'));
    await tester.pump();
    expect(requests, hasLength(1));

    reply.complete(printOk());
    await tester.pumpAndSettle();

    expect(requests, hasLength(1));
    expect(find.text('보냈어요'), findsOneWidget);
  });

  testWidgets('성공 직후 3초 안에는 눌러도 요청이 안 나가고, 3초 뒤에는 나간다', (tester) async {
    final requests = useFakeServer((_) => printOk());
    await showLabelResult(tester);

    await tester.tap(find.text('출력하기'));
    await tester.pumpAndSettle(); // 성공 + 알림이 뜨는 데 1초도 안 걸린다
    expect(requests, hasLength(1));

    // 3초 안: "보냈어요"(체크)로 잠겨 있다
    expect(find.text('보냈어요'), findsOneWidget);
    expect(find.byIcon(Icons.check), findsOneWidget);
    expect(printButtonEnabled(tester), isFalse);
    await tester.tap(find.text('보냈어요'));
    await tester.pump(const Duration(seconds: 2));
    await tester.tap(find.text('보냈어요'));
    await tester.pump();
    expect(requests, hasLength(1), reason: '3초 안에는 요청이 나가지 않는다');

    // 3초가 지나면 다시 "출력하기"
    await tester.pump(const Duration(seconds: 1));
    expect(find.text('출력하기'), findsOneWidget);
    expect(printButtonEnabled(tester), isTrue);
    await tester.tap(find.text('출력하기'));
    await tester.pumpAndSettle();
    expect(requests, hasLength(2), reason: '3초 뒤에는 요청이 나간다');
  });

  testWidgets('서버 실패는 reason 을 보여 주고, 바로 다시 누를 수 있다', (tester) async {
    final requests = useFakeServer((_) => jsonResponse({
          'ok': false,
          'error': 'invalid_braille',
          'reason': '칸 값은 0~63 정수여야 합니다 (받은 값: 64).',
        }, 400));
    await showLabelResult(tester);

    await tester.tap(find.text('출력하기'));
    await tester.pumpAndSettle();

    expect(find.text('칸 값은 0~63 정수여야 합니다 (받은 값: 64).'), findsOneWidget);
    expect(find.text('출력하기'), findsOneWidget);
    expect(printButtonEnabled(tester), isTrue);

    await tester.tap(find.text('출력하기'));
    await tester.pumpAndSettle();
    expect(requests, hasLength(2));
  });

  testWidgets('서버에 연결하지 못하면 따로 안내하고, 재시도 없이 요청은 한 번', (tester) async {
    final requests = useFakeServer((_) => throw http.ClientException('XMLHttpRequest error.'));
    await showLabelResult(tester);

    await tester.tap(find.text('출력하기'));
    await tester.pumpAndSettle();

    expect(requests, hasLength(1));
    expect(find.text('서버에 연결하지 못했어요. 서버가 켜져 있는지, 같은 네트워크에 있는지 확인해 주세요'),
        findsOneWidget);
    expect(printButtonEnabled(tester), isTrue);
  });

  testWidgets('책: 2차원 braille 을 그대로 보내고 "책 타각기로 보냈어요"', (tester) async {
    final requests = useFakeServer((_) => printOk(device: 'fw2', waiting: 2));
    await tester.pumpWidget(const MaterialApp(
      home: ResultScreen(
        mode: kModeBook,
        text: '점자 시험',
        reason: 'OCR로 텍스트를 인식했습니다.',
        braille: <List<int>>[
          [1, 2, 0, 8],
          [63, 0, 36],
        ],
        braillePreview: '⠁⠃⠀⠈\n⠿⠀⠤',
        cellCount: 7,
      ),
    ));

    await tester.tap(find.text('출력하기'));
    await tester.pumpAndSettle();

    expect(jsonDecode(requests.single.body)['braille'], [
      [1, 2, 0, 8],
      [63, 0, 36],
    ]);
    expect(find.text('책 타각기로 보냈어요 (대기 중인 작업 2개)'), findsOneWidget);
  });

  testWidgets('점자 데이터가 없으면 버튼이 꺼져 있고 요청도 없다 (기존 동작)', (tester) async {
    final requests = useFakeServer((_) => printOk());
    await showLabelResult(tester, braille: const <int>[]);

    expect(find.text('출력할 점자 데이터가 없습니다'), findsOneWidget);
    expect(printButtonEnabled(tester), isFalse);
    await tester.tap(find.text('출력하기'));
    await tester.pump();
    expect(requests, isEmpty);
  });

  testWidgets('직접 입력(/translate)으로 만든 결과 화면에서도 같은 출력 요청이 나간다', (tester) async {
    final requests = useFakeServer((request) {
      if (request.url.path == '/translate') {
        return jsonResponse({
          'ok': true,
          'text': '우유',
          'reason': '직접 입력한 문구입니다.',
          'braille': [13, 41],
          'braille_preview': '⠍⠩',
          'cell_count': 2,
        });
      }
      return printOk();
    });

    // 카메라 화면에서 문구를 직접 입력 → 결과 화면
    await tester.pumpWidget(const MaterialApp(home: CameraScreen(mode: kModeLabel)));
    await tester.tap(find.text('라벨 문구 직접 입력하기'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField), '우유');
    await tester.pump(); // 글자 수를 다시 세어 확인 버튼이 켜지도록 화면을 한 번 다시 그린다
    await tester.tap(find.text('확인'));
    await tester.pumpAndSettle();
    expect(find.byType(ResultScreen), findsOneWidget);

    // 결과 화면에서 출력하기
    await tester.tap(find.text('출력하기'));
    await tester.pumpAndSettle();

    expect(requests.map((r) => r.url.path), ['/translate', '/print']);
    expect(jsonDecode(requests[1].body), {
      'mode': 'label',
      'text': '우유',
      'reason': '직접 입력한 문구입니다.',
      'braille': [13, 41],
    });
    expect(find.text('$kLabelSent (대기 중인 작업 1개)'), findsOneWidget);
  });
}
