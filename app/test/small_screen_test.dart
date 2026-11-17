// 화면 크기 테스트 — 휴대폰과 갤럭시탭 크기에서 세 화면(모드·촬영·결과)이 넘치지 않는지
//
// 화면을 360×640(휴대폰 세로), 800×1280(갤럭시탭 세로), 1280×800(갤럭시탭 가로)으로 바꿔 띄운다.
// 크기 단위는 화면 점(dp, 논리 픽셀)이다.
//
// 내용이 화면을 넘치면(노란·검은 줄무늬) Flutter 가 오류를 내고 테스트가 실패하므로,
// 넘침이 없는지는 화면을 띄우는 것만으로 확인된다.
// 태블릿 크기에서는 본문이 가운데 kContentMaxWidth(600) 폭 안에 있는지도 확인한다.

import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

import 'package:app/constants.dart';
import 'package:app/main.dart';
import 'package:app/screens/camera_screen.dart';
import 'package:app/screens/result_screen.dart';
import 'package:app/services/api_service.dart';

const Map<String, Size> kScreens = {
  '휴대폰 세로 360×640': Size(360, 640),
  '갤럭시탭 세로 800×1280': Size(800, 1280),
  '갤럭시탭 가로 1280×800': Size(1280, 800),
};

/// 화면 크기를 바꾼다 (테스트가 끝나면 원래대로).
void setScreen(WidgetTester tester, Size size) {
  tester.view.devicePixelRatio = 1.0;
  tester.view.physicalSize = size;
  addTearDown(tester.view.reset);
}

bool isTablet(Size size) => size.width > kContentMaxWidth;

/// 글자가 들어 있는 버튼
Finder buttonWithText(String text) => find
    .ancestor(of: find.text(text), matching: find.byWidgetPredicate((w) => w is ButtonStyleButton))
    .first;

/// [finder] 가 화면 가운데 kContentMaxWidth 폭 안에 있는지
void expectCenteredWithinMaxWidth(WidgetTester tester, Finder finder, Size screen) {
  final rect = tester.getRect(finder);
  expect(rect.width, lessThanOrEqualTo(kContentMaxWidth));
  expect(rect.center.dx, closeTo(screen.width / 2, 1));
}

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

/// 책 결과 화면: 한 줄 30칸 × 18줄, 본문도 길게
Widget bookResult() {
  final lines = List.generate(18, (_) => List<int>.filled(30, 63));
  return MaterialApp(
    home: ResultScreen(
      mode: kModeBook,
      text: List.filled(12, '점자는 손끝으로 읽는 문자이다. 여섯 개의 점을 조합하여 글자를 나타낸다.').join('\n'),
      reason: 'OCR로 텍스트를 인식했습니다.',
      braille: lines,
      braillePreview: List.filled(18, '⠿' * 30).join('\n'),
      cellCount: 540,
    ),
  );
}

/// 점자 미리보기의 가로 스크롤
final Finder brailleScroll = find.byWidgetPredicate(
    (w) => w is SingleChildScrollView && w.scrollDirection == Axis.horizontal);

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

  for (final MapEntry(key: name, value: size) in kScreens.entries) {
    group(name, () {
      testWidgets('모드 화면', (tester) async {
        setScreen(tester, size);
        await tester.pumpWidget(const Eye2DotApp());

        expect(find.text('점자 라벨 만들기'), findsOneWidget);
        if (isTablet(size)) {
          expectCenteredWithinMaxWidth(tester, buttonWithText('점자 라벨 만들기'), size);
        }
      });

      for (final mode in [kModeLabel, kModeBook]) {
        testWidgets('촬영 화면 처음 ($mode)', (tester) async {
          setScreen(tester, size);
          await tester.pumpWidget(MaterialApp(home: CameraScreen(mode: mode)));

          expect(find.text('카메라로 촬영하기'), findsOneWidget);
          if (isTablet(size)) {
            expectCenteredWithinMaxWidth(tester, buttonWithText('카메라로 촬영하기'), size);
          }
        });
      }

      testWidgets('촬영 화면 로딩·오류 (버튼 두 개)', (tester) async {
        setScreen(tester, size);
        final reply = Completer<http.Response>();
        useFakeServer((_) => reply.future);

        await tester.pumpWidget(const MaterialApp(home: CameraScreen(mode: kModeLabel)));
        await tester.tap(find.text('라벨 문구 직접 입력하기'));
        await tester.pumpAndSettle();
        await tester.enterText(find.byType(TextField), '우유');
        await tester.pump();
        await tester.tap(find.text('확인'));
        await tester.pump();
        expect(find.text('점역 중입니다...'), findsOneWidget);

        // 가장 긴 오류 문구 + "다시 시도"·"다시 입력하기" 두 버튼
        reply.complete(jsonResponse({
          'ok': false,
          'error': 'network_error',
          'reason': 'Gemini 서버에 연결하지 못했습니다',
        }, 502));
        await tester.pumpAndSettle();
        expect(find.text('다시 시도'), findsOneWidget);
        expect(find.text('다시 입력하기'), findsOneWidget);
      });

      testWidgets('직접 입력 대화상자 + 키보드', (tester) async {
        setScreen(tester, size);
        await tester.pumpWidget(const MaterialApp(home: CameraScreen(mode: kModeLabel)));
        await tester.tap(find.text('라벨 문구 직접 입력하기'));
        await tester.pumpAndSettle();

        // 입력칸에 커서가 가면 키보드가 올라와 화면 아래 45% 를 가린다
        tester.view.viewInsets = FakeViewPadding(bottom: size.height * 0.45);
        await tester.pumpAndSettle();

        // 6글자: 안내 문구(빨간 글씨)까지 나와 대화상자가 가장 길어진다
        await tester.enterText(find.byType(TextField), '아이스크림콘');
        await tester.pump();
        expect(find.text('라벨 이름은 공백을 빼고 5글자까지입니다'), findsOneWidget);
      });

      testWidgets('결과 화면 (라벨)', (tester) async {
        setScreen(tester, size);
        await tester.pumpWidget(const MaterialApp(
          home: ResultScreen(
            mode: kModeLabel,
            text: '우유',
            reason: '흰색 종이팩에 우유 표기가 있습니다.',
            braille: <int>[13, 41],
            braillePreview: '⠍⠩',
            cellCount: 2,
          ),
        ));

        expect(find.text('출력하기'), findsOneWidget);
        if (isTablet(size)) {
          expectCenteredWithinMaxWidth(tester, brailleScroll, size);
          // 아래 버튼 줄도 본문과 같은 폭
          final left = tester.getRect(buttonWithText('다시 찍기')).left;
          final right = tester.getRect(buttonWithText('출력하기')).right;
          expect(right - left, lessThanOrEqualTo(kContentMaxWidth));
          expect((left + right) / 2, closeTo(size.width / 2, 1));
        }
      });

      testWidgets('결과 화면 (책, 긴 본문)', (tester) async {
        setScreen(tester, size);
        await tester.pumpWidget(bookResult());

        expect(find.text('전체 540칸 · 18줄'), findsOneWidget);
        if (isTablet(size)) {
          expectCenteredWithinMaxWidth(tester, brailleScroll, size);
        }
      });

      testWidgets('점자 미리보기는 손가락으로 끌어 가로로 스크롤된다', (tester) async {
        setScreen(tester, size);
        await tester.pumpWidget(bookResult());
        // 본문이 길어 점자 상자가 화면 아래에 있으므로 먼저 세로로 내려 보이게 한다
        await tester.ensureVisible(brailleScroll);
        await tester.pumpAndSettle();

        final scrollable = find.descendant(of: brailleScroll, matching: find.byType(Scrollable));
        final position = tester.state<ScrollableState>(scrollable).position;
        expect(position.maxScrollExtent, greaterThan(0), reason: '테스트 글꼴로는 30칸이 상자보다 넓다');
        expect(position.pixels, 0);

        // tester.drag 는 손가락(터치)으로 끈다
        await tester.drag(brailleScroll, const Offset(-200, 0));
        await tester.pumpAndSettle();
        expect(position.pixels, greaterThan(0));
      });
    });
  }
}
