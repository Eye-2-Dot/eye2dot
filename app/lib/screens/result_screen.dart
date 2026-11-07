import 'dart:async';

import 'package:flutter/material.dart';

import '../constants.dart';
import '../services/api_service.dart';

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
  /// 펌웨어로 보낼 데이터다. 출력하기를 누르면 이 값을 **그대로** 서버(POST /print)로 보낸다.
  /// 기기용 거울 모양으로 바꾸는 일은 서버가 한다.
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

  @override
  Widget build(BuildContext context) {
    final isBook = mode == kModeBook;
    final title = isBook ? '책 점역 결과' : '점자 라벨 결과';

    return Scaffold(
      appBar: AppBar(title: Text(title)),
      body: SafeArea(
        // 내용이 길면 이 영역만 세로로 스크롤된다
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: isBook ? _buildBookContent(context) : _buildLabelContent(context),
        ),
      ),
      // 버튼은 스크롤과 상관없이 항상 화면 아래에 고정.
      // bottomNavigationBar 자리에 두어야 출력 결과 알림(SnackBar)이 버튼을 가리지 않고 그 위에 뜬다.
      // (body 안에 두면 알림이 떠 있는 몇 초 동안 버튼을 누를 수 없다)
      bottomNavigationBar: SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
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
                  // 출력할 데이터가 없으면 비활성화 (회색으로 눌리지 않는다)
                  child: _PrintButton(
                    enabled: _canPrint,
                    mode: mode,
                    text: text,
                    reason: reason,
                    braille: braille,
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

// ─────────────────────────────────────────────
// 출력 버튼 — 서버에 출력 요청(POST /print)을 보낸다
//
// 결과 화면 전체는 상태 없는 위젯으로 두고, 상태가 필요한 이 버튼만 따로 뺐다.
//   보내는 동안 : 잠그고 진행 표시 (두 번 눌러 작업이 두 개 생기지 않게)
//   성공하면    : _kSentLockDuration 동안 "보냈어요"(체크)로 잠갔다가 다시 연다
//   실패하면    : 바로 다시 누를 수 있다
// 결과는 화면 아래 SnackBar(잠깐 뜨는 알림)로 알린다.
// ─────────────────────────────────────────────

/// 성공한 뒤 버튼을 "보냈어요"로 잠가 두는 시간
const Duration _kSentLockDuration = Duration(seconds: 3);

/// 서버에 아예 닿지 못했을 때(연결 실패·시간 초과)의 안내
const String _kPrintNetworkMessage = '서버에 연결하지 못했어요. 서버가 켜져 있는지, 같은 네트워크에 있는지 확인해 주세요';

/// 그 밖에 이유를 알 수 없는 실패 (카메라 화면과 같은 문구)
const String _kPrintDefaultMessage = '오류가 발생했습니다. 다시 시도해 주세요';

enum _PrintStatus { ready, sending, sent }

class _PrintButton extends StatefulWidget {
  const _PrintButton({
    required this.enabled,
    required this.mode,
    required this.text,
    required this.reason,
    required this.braille,
  });

  /// false 면 출력할 데이터가 없어 처음부터 누를 수 없다
  final bool enabled;
  final String mode;
  final String text;
  final String? reason;
  final dynamic braille;

  @override
  State<_PrintButton> createState() => _PrintButtonState();
}

class _PrintButtonState extends State<_PrintButton> {
  _PrintStatus _status = _PrintStatus.ready;

  /// "보냈어요" 잠금을 푸는 타이머. 화면을 떠나면 취소한다.
  Timer? _unlockTimer;

  @override
  void dispose() {
    _unlockTimer?.cancel();
    super.dispose();
  }

  Future<void> _send() async {
    // 보내는 중이거나 보낸 직후에는 무시 (버튼도 잠겨 있지만 한 번 더 막는다)
    if (_status != _PrintStatus.ready) return;
    setState(() => _status = _PrintStatus.sending);

    Map<String, dynamic> response;
    try {
      // braille 은 서버에서 받은 값 그대로 보낸다 (거울 변환은 서버가)
      response = await ApiService.sendPrint(
        mode: widget.mode,
        text: widget.text,
        reason: widget.reason,
        braille: widget.braille as Object,
      );
    } catch (e) {
      // ApiService 는 실패를 예외 대신 ok:false 로 돌려주므로 여기는 예상하지 못한 오류뿐이다
      debugPrint('[ResultScreen] 출력 요청 중 예상하지 못한 오류: $e');
      response = {'ok': false, 'error': 'unexpected'};
    }
    if (!mounted) return; // 기다리는 동안 화면을 떠났으면 끝

    final ok = response['ok'] == true;
    _showMessage(
      ok ? _successMessage(response) : _failureMessage(response),
      duration: Duration(seconds: ok ? 6 : 4),
    );

    if (!ok) {
      setState(() => _status = _PrintStatus.ready); // 실패는 바로 다시 누를 수 있게
      return;
    }
    setState(() => _status = _PrintStatus.sent);
    _unlockTimer = Timer(_kSentLockDuration, () {
      if (mounted) setState(() => _status = _PrintStatus.ready);
    });
  }

  /// 성공 문구. 서버가 알려 준 기기(device)로 고르고, 없으면 mode 로 고른다.
  String _successMessage(Map<String, dynamic> response) {
    final device = response['device'];
    final isBook = device == 'fw2' || (device == null && widget.mode == kModeBook);
    final message = isBook
        ? '책 타각기로 보냈어요'
        : '라벨 출력기로 보냈어요. 기기에서 안내를 듣고 출력 버튼을 눌러 주세요';
    final waiting = response['waiting'];
    return waiting is int ? '$message (대기 중인 작업 $waiting개)' : message;
  }

  /// 실패 문구. 서버의 reason 을 그대로 보여 주고, 서버에 닿지 못했으면 따로 안내한다.
  String _failureMessage(Map<String, dynamic> response) {
    final error = response['error'];
    final reason = response['reason'];
    debugPrint('[ResultScreen] 출력 실패 error=$error reason=$reason');

    // /print 는 network_error 를 내지 않으므로, 이 값은 앱이 만든 연결 실패·시간 초과다
    if (error == 'network_error') return _kPrintNetworkMessage;
    if (error != 'parse_failed' && reason is String && reason.trim().isNotEmpty) return reason;
    return _kPrintDefaultMessage;
  }

  void _showMessage(String message, {required Duration duration}) {
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(message), duration: duration));
  }

  @override
  Widget build(BuildContext context) {
    final Widget icon;
    final String label;
    switch (_status) {
      case _PrintStatus.sending:
        icon = const SizedBox(
          width: 18,
          height: 18,
          child: CircularProgressIndicator(strokeWidth: 2),
        );
        label = '보내는 중…';
      case _PrintStatus.sent:
        icon = const Icon(Icons.check);
        label = '보냈어요';
      case _PrintStatus.ready:
        icon = const Icon(Icons.print);
        label = '출력하기';
    }

    final canPress = widget.enabled && _status == _PrintStatus.ready;
    return FilledButton.icon(
      // onPressed 가 null 이면 회색으로 눌리지 않는다
      onPressed: canPress ? _send : null,
      icon: icon,
      label: Text(label, style: const TextStyle(fontSize: 16)),
    );
  }
}
