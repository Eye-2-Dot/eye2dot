import 'package:flutter/material.dart';

/// 카메라 화면 (아직 껍데기)
///
/// 지금은 전달받은 mode 값만 화면에 보여준다.
/// TODO(B): 카메라로 사진 촬영 → 바이트 확보 →
///          ApiService.sendImage(bytes: ..., mode: mode) 호출 →
///          결과를 들고 ResultScreen 으로 이동하는 흐름을 여기에 채울 것.
///
/// 주의: Chrome(웹)에서 실행하므로 dart:io 의 File 은 쓰지 말고
///       image_picker 의 XFile.readAsBytes() 로 Uint8List 를 받을 것.
class CameraScreen extends StatelessWidget {
  const CameraScreen({super.key, required this.mode});

  /// 이전 화면(ModeScreen)에서 고른 모드. kModeLabel 또는 kModeBook.
  final String mode;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('촬영')),
      body: Center(
        child: Text(
          '카메라 화면 (mode: $mode)',
          style: Theme.of(context).textTheme.titleLarge,
        ),
      ),
    );
  }
}
