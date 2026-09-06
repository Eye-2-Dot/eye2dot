import 'package:flutter/material.dart';

import '../constants.dart';
import 'camera_screen.dart';

/// 첫 화면 — 무엇을 만들지 모드를 고르는 화면
///
/// 앱 사용자는 비시각장애인(보조자)이므로 음성 안내 없이
/// 크고 명확한 버튼으로만 안내한다.
class ModeScreen extends StatelessWidget {
  const ModeScreen({super.key});

  /// 모드를 고르면 그 값을 들고 카메라 화면으로 이동한다.
  void _goToCamera(BuildContext context, String mode) {
    Navigator.push(
      context,
      MaterialPageRoute(builder: (_) => CameraScreen(mode: mode)),
    );
  }

  /// 크고 누르기 쉬운 모드 선택 버튼 하나를 만든다.
  /// 두 버튼의 생김새가 같으므로 여기서 한 번에 정의한다.
  Widget _buildModeButton(
    BuildContext context, {
    required IconData icon,
    required String label,
    required String mode,
  }) {
    return SizedBox(
      // 손이 큰 사람도 쉽게 누르도록 높이를 넉넉히 잡는다.
      height: 96,
      width: double.infinity,
      child: FilledButton.icon(
        onPressed: () => _goToCamera(context, mode),
        icon: Icon(icon, size: 36),
        label: Text(label, style: const TextStyle(fontSize: 22)),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Eye 2 Dot')),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(
              '모드 선택',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.headlineMedium,
            ),
            const SizedBox(height: 48),
            _buildModeButton(
              context,
              icon: Icons.label_outline,
              label: '점자 라벨 만들기',
              mode: kModeLabel,
            ),
            const SizedBox(height: 24),
            _buildModeButton(
              context,
              icon: Icons.menu_book_outlined,
              label: '책 점역하기',
              mode: kModeBook,
            ),
          ],
        ),
      ),
    );
  }
}
