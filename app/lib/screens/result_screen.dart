import 'package:flutter/material.dart';

import '../constants.dart';

/// 결과 화면 (아직 껍데기)
///
/// 지금은 전달받은 text 만 화면 중앙에 보여준다.
/// TODO(C): 점자 미리보기(braille_preview), 판단 근거(reason),
///          다시 촬영 / 처음으로 돌아가기 버튼 등을 여기에 채울 것.
class ResultScreen extends StatelessWidget {
  const ResultScreen({
    super.key,
    required this.mode,
    required this.text,
    this.reason,
    this.braille,
    this.braillePreview,
    this.cellCount,
  });

  /// 어떤 모드로 만든 결과인지. kModeLabel 또는 kModeBook.
  final String mode;

  /// 서버가 인식한 텍스트 (사물 이름 또는 책 본문).
  final String text;

  // ── 아래는 서버 응답을 그대로 담아 두는 값. 아직 화면에 표시하지 않는다 ──
  // 값이 없어도(null) 화면이 깨지지 않도록 모두 선택값이다.

  /// 판단 근거 1문장. 예: "흰색 종이팩에 우유 표기가 있습니다."
  final String? reason;

  /// 점자 셀 배열. 셀 하나는 6비트 정수(점 n번 = 비트 n-1, 0은 빈 칸).
  /// 펌웨어로 보낼 데이터다.
  ///   label: `List<int>`          예: [13, 41]
  ///   book : `List<List<int>>`    줄 단위, 한 줄 최대 30셀
  final dynamic braille;

  /// 사람이 눈으로 확인하는 유니코드 점자. 책 모드는 줄 사이에 \n 이 있다.
  final String? braillePreview;

  /// 전체 셀 수 (책 모드는 모든 줄의 합).
  final int? cellCount;

  @override
  Widget build(BuildContext context) {
    // 모드에 따라 제목만 다르게 보여 준다.
    final title = mode == kModeBook ? '책 점역 결과' : '점자 라벨 결과';

    return Scaffold(
      appBar: AppBar(title: Text(title)),
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Text(
            text,
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.headlineMedium,
          ),
        ),
      ),
    );
  }
}
