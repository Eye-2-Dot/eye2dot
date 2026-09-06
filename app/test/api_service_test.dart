// ApiService 더미 응답이 서버 목(POST /process-mock)과 같은 형식인지 확인하는 테스트
//
// 실제 통신으로 교체된 뒤에도 이 형식은 그대로여야 하므로,
// D가 api_service.dart를 고친 후에도 이 테스트는 계속 통과해야 한다.

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:app/constants.dart';
import 'package:app/services/api_service.dart';

void main() {
  final emptyBytes = Uint8List(0);

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
}
