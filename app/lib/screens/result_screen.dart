import 'package:flutter/material.dart';

import '../constants.dart';

/// 결과 화면 — 인식·점역 결과를 확인하고 출력을 요청하는 화면
///
/// 라벨 모드: 이름을 크게, 그 아래 점자 미리보기와 칸 수
/// 책 모드  : 본문과 줄 단위 점자 미리보기, 전체 칸 수·줄 수 (화면 전체가 세로 스크롤)
///
/// 점자 미리보기는 두 모드 모두 화면 폭에 맞춰 줄을 바꾸지 않고 가로로 스크롤한다.
/// 화면의 한 줄이 기기가 출력하는 한 줄과 같아야 하기 때문이다.
///
/// 앱 사용자는 비시각장애인(보조자)이므로 음성 안내는 넣지 않는다.
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

  // ── 아래는 서버 응답을 그대로 담아 두는 값 ──
  // 값이 없어도(null) 화면이 깨지지 않도록 모두 선택값이고, 없으면 해당 영역을 숨긴다.

  /// 판단 근거 1문장. 예: "흰색 종이팩에 우유 표기가 있습니다."
  /// 화면에는 표시하지 않는다. 펌웨어 1이 음성으로 읽어 줄 값이라
  /// 출력 요청 때 함께 넘기기 위해 받아만 둔다.
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

  /// 출력할 점자 데이터가 있는지. 없으면 출력 버튼을 비활성화한다.
  bool get _canPrint => braille is List && (braille as List).isNotEmpty;

  /// 화면에 보여 줄 점자 미리보기. 없거나 비어 있으면 null.
  String? get _preview {
    final preview = braillePreview;
    return (preview == null || preview.isEmpty) ? null : preview;
  }

  /// 책 모드의 점자 줄 수. braille 이 줄 단위 2차원 배열일 때만 센다.
  int? get _lineCount {
    final data = braille;
    if (data is List && data.isNotEmpty && data.every((line) => line is List)) {
      return data.length;
    }
    return null;
  }

  /// 출력 요청. 지금은 안내만 표시한다.
  void _requestPrint(BuildContext context) {
    // TODO(D): 실제 기기로 출력 데이터를 보낼 것 (다음 단계)
    //   라벨 모드 → 펌웨어 1(라벨 출력기): 점자 배열 braille(List<int>) + 음성 데이터
    //              음성은 서버 GET /speech 가 만든다 (읽어 줄 내용: text, reason)
    //   책 모드   → A4 출력기: 점자 배열 braille(List<List<int>>)만
    //              바깥 배열의 원소 하나가 출력물의 한 줄이다
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(const SnackBar(content: Text('출력 요청을 보냈습니다')));
  }

  @override
  Widget build(BuildContext context) {
    final isBook = mode == kModeBook;
    final title = isBook ? '책 점역 결과' : '점자 라벨 결과';

    return Scaffold(
      appBar: AppBar(title: Text(title)),
      body: SafeArea(
        child: Column(
          children: [
            // 내용이 길면 이 영역만 세로로 스크롤된다
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.all(24),
                child: isBook ? _buildBookContent(context) : _buildLabelContent(context),
              ),
            ),
            // 버튼은 스크롤과 상관없이 항상 화면 아래에 고정
            const Divider(height: 1),
            _buildButtons(context),
          ],
        ),
      ),
    );
  }

  /// 라벨 모드: 이름(크게) → 점자 미리보기 → 칸 수
  Widget _buildLabelContent(BuildContext context) {
    final preview = _preview;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SizedBox(height: 24),
        Text(
          text,
          textAlign: TextAlign.center,
          style: Theme.of(context).textTheme.headlineMedium,
        ),
        if (preview != null) ...[
          const SizedBox(height: 32),
          _buildBraillePreview(
            context,
            preview,
            fontSize: 40,
            textAlign: TextAlign.center,
          ),
        ],
        if (cellCount != null) ...[
          const SizedBox(height: 12),
          Text(
            '점자 $cellCount칸',
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.bodyLarge,
          ),
        ],
        if (preview != null) ...[
          const SizedBox(height: 8),
          _buildFontNotice(TextAlign.center),
        ],
      ],
    );
  }

  /// 책 모드: 본문 → 점자 미리보기(줄 단위) → 전체 칸 수·줄 수
  Widget _buildBookContent(BuildContext context) {
    final theme = Theme.of(context);
    final preview = _preview;

    // "전체 448칸 · 18줄" — 둘 중 있는 것만 보여 준다
    final counts = [
      if (cellCount != null) '전체 $cellCount칸',
      if (_lineCount != null) '$_lineCount줄',
    ];

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('인식된 본문', style: theme.textTheme.titleMedium),
        const SizedBox(height: 8),
        Text(text, style: theme.textTheme.bodyLarge),
        if (preview != null) ...[
          const SizedBox(height: 32),
          Text('점자 미리보기', style: theme.textTheme.titleMedium),
          const SizedBox(height: 8),
          _buildBraillePreview(
            context,
            preview,
            fontSize: 20,
            textAlign: TextAlign.start,
          ),
        ],
        if (counts.isNotEmpty) ...[
          const SizedBox(height: 12),
          Text(counts.join(' · '), style: theme.textTheme.bodyLarge),
        ],
        if (preview != null) ...[
          const SizedBox(height: 8),
          _buildFontNotice(TextAlign.start),
        ],
      ],
    );
  }

  /// 점자 미리보기 상자. 줄을 자동으로 바꾸지 않고, 화면보다 길면 가로로 스크롤한다.
  /// (원문의 \n 줄바꿈만 그대로 반영된다)
  Widget _buildBraillePreview(
    BuildContext context,
    String preview, {
    required double fontSize,
    required TextAlign textAlign,
  }) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(12),
      ),
      child: LayoutBuilder(
        builder: (context, constraints) => SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          // 짧은 점자도 상자 폭을 다 쓰게 해야 가운데 정렬이 먹는다
          child: ConstrainedBox(
            constraints: BoxConstraints(minWidth: constraints.maxWidth),
            child: Text(
              preview,
              softWrap: false,
              textAlign: textAlign,
              // 등폭 글꼴로 칸 폭을 맞춘다
              style: TextStyle(fontFamily: 'monospace', fontSize: fontSize, height: 1.4),
            ),
          ),
        ),
      ),
    );
  }

  /// 점자 글자가 네모로 보일 때를 위한 안내. 눈에 띄지 않게 흐리고 작게.
  Widget _buildFontNotice(TextAlign textAlign) {
    return Text(
      '점자가 네모로 보이면 기기 글꼴 문제입니다. 실제 출력에는 영향이 없습니다.',
      textAlign: textAlign,
      style: const TextStyle(fontSize: 12, color: Colors.grey),
    );
  }

  /// 아래 고정 버튼: 다시 찍기 / 출력하기
  Widget _buildButtons(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(24, 12, 24, 16),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              Expanded(
                child: SizedBox(
                  height: 56,
                  child: OutlinedButton.icon(
                    onPressed: () => Navigator.pop(context),
                    icon: const Icon(Icons.camera_alt),
                    label: const Text('다시 찍기', style: TextStyle(fontSize: 16)),
                  ),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: SizedBox(
                  height: 56,
                  child: FilledButton.icon(
                    // 출력할 데이터가 없으면 비활성화 (onPressed 가 null 이면 회색으로 눌리지 않는다)
                    onPressed: _canPrint ? () => _requestPrint(context) : null,
                    icon: const Icon(Icons.print),
                    label: const Text('출력하기', style: TextStyle(fontSize: 16)),
                  ),
                ),
              ),
            ],
          ),
          if (!_canPrint) ...[
            const SizedBox(height: 8),
            const Text(
              '출력할 점자 데이터가 없습니다',
              style: TextStyle(fontSize: 13, color: Colors.grey),
            ),
          ],
        ],
      ),
    );
  }
}
