// 앱 골격이 제대로 뜨고 화면 이동이 되는지 확인하는 기본 테스트

import 'package:flutter_test/flutter_test.dart';

import 'package:app/main.dart';

void main() {
  testWidgets('첫 화면에 모드 선택 버튼 두 개가 보인다', (WidgetTester tester) async {
    await tester.pumpWidget(const Eye2DotApp());

    expect(find.text('모드 선택'), findsOneWidget);
    expect(find.text('점자 라벨 만들기'), findsOneWidget);
    expect(find.text('책 점역하기'), findsOneWidget);
  });

  testWidgets('모드를 고르면 그 값을 들고 카메라 화면으로 이동한다', (WidgetTester tester) async {
    await tester.pumpWidget(const Eye2DotApp());

    await tester.tap(find.text('책 점역하기'));
    await tester.pumpAndSettle();

    expect(find.text('카메라 화면 (mode: book)'), findsOneWidget);
  });
}
