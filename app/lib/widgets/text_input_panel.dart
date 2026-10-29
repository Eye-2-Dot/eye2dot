import 'package:flutter/material.dart';

import '../constants.dart';
import '../services/api_service.dart';

/// 라벨 모드에서 사진 대신 글자를 직접 입력받는 칸.
///
/// 화면 전체가 아니라 한 덩어리(위젯)라서, 세연의 분기 화면 안에
/// 그대로 끼워 넣으면 된다. 파일이 따로 있으므로 충돌이 나지 않는다.
///
/// 쓰는 쪽:
///
/// TextInputPanel(
///   onResult: (result) {
///     // result['ok'] == true 이면 결과 화면으로 이동
///     // 사진 경로에서 받는 것과 형태가 완전히 같다
///   },
/// )
class TextInputPanel extends StatefulWidget {
  const TextInputPanel({
    super.key,
    required this.onResult,
    this.mode = kModeLabel,
    this.maxLength = 100,
  });

  /// 서버 응답을 받았을 때 불린다. 성공이든 실패든 항상 불린다.
  ///
  /// 화면 이동은 이 콜백을 받는 쪽에서 결정한다.
  /// 이 위젯은 입력과 전송까지만 책임진다.
  final void Function(Map<String, dynamic> result) onResult;

  /// 지금은 라벨 모드에서만 쓴다. 책 모드에 붙일 때 값만 바꾸면 된다.
  final String mode;

  /// 라벨은 사물 이름 한 개라 길 필요가 없다.
  final int maxLength;

  @override
  State<TextInputPanel> createState() => _TextInputPanelState();
}

class _TextInputPanelState extends State<TextInputPanel> {
  final TextEditingController _controller = TextEditingController();
  final FocusNode _focusNode = FocusNode();

  bool _sending = false;
  String? _errorMessage;

  @override
  void initState() {
    super.initState();
    // 글자가 바뀔 때마다 전송 버튼을 켜고 끄기 위해 다시 그린다.
    _controller.addListener(_onTextChanged);
  }

  @override
  void dispose() {
    // 컨트롤러를 안 버리면 화면을 오갈 때마다 메모리가 쌓인다.
    _controller.removeListener(_onTextChanged);
    _controller.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  void _onTextChanged() {
    setState(() {
      // 사용자가 고치기 시작하면 이전 오류 문구는 지운다.
      if (_errorMessage != null) _errorMessage = null;
    });
  }

  /// 공백만 있는 입력은 보내지 않는다.
  bool get _canSend => _controller.text.trim().isNotEmpty && !_sending;

  Future<void> _send() async {
    if (!_canSend) return;

    // 키보드를 내려서 전송 중 화면이 가려지지 않게 한다.
    _focusNode.unfocus();

    setState(() {
      _sending = true;
      _errorMessage = null;
    });

    final result = await ApiService.sendText(
      text: _controller.text,
      mode: widget.mode,
    );

    // 응답을 기다리는 동안 화면이 사라졌을 수 있다.
    if (!mounted) return;

    setState(() => _sending = false);

    if (result['ok'] != true) {
      // 실패는 이 칸 아래에 바로 보여준다.
      // 화면을 옮겨 버리면 사용자가 방금 친 글자를 잃어버린다.
      setState(() => _errorMessage = result['reason'] as String?);
    }

    widget.onResult(result);
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        TextField(
          controller: _controller,
          focusNode: _focusNode,
          enabled: !_sending,
          maxLength: widget.maxLength,
          textInputAction: TextInputAction.done,
          // 엔터를 눌러도 전송되게 한다. 버튼까지 안 가도 된다.
          onSubmitted: (_) => _send(),
          decoration: InputDecoration(
            labelText: '만들 라벨 내용',
            hintText: '예: 우유',
            border: const OutlineInputBorder(),
            errorText: _errorMessage,
          ),
        ),
        const SizedBox(height: 12),
        SizedBox(
          height: 48,
          child: FilledButton(
            onPressed: _canSend ? _send : null,
            child: _sending
                ? const SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Text('점자로 만들기'),
          ),
        ),
      ],
    );
  }
}
