import 'package:flutter/material.dart';

import '../constants.dart';

/// 결과 화면 (아직 껍데기)
///
/// 지금은 전달받은 text 만 화면 중앙에 보여준다.
/// TODO(C): 점자 미리보기(braille_preview), 판단 근거(reason),
///          다시 촬영 / 처음으로 돌아가기 버튼 등을 여기에 채울 것.
class ResultScreen extends StatelessWidget {
  const ResultScreen({super.key, required this.mode, required this.text});

  /// 어떤 모드로 만든 결과인지. kModeLabel 또는 kModeBook.
  final String mode;

  /// 서버가 인식한 텍스트 (사물 이름 또는 책 본문).
  final String text;

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
