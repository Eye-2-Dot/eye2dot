import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';

import '../constants.dart';
import '../services/api_service.dart';
import 'result_screen.dart';

class CameraScreen extends StatefulWidget {
  final String mode; // kModeLabel 또는 kModeBook

  const CameraScreen({
    super.key,
    required this.mode,
  });

  @override
  State<CameraScreen> createState() => _CameraScreenState();
}

class _CameraScreenState extends State<CameraScreen> {
  final ImagePicker _picker = ImagePicker();

  Uint8List? _imageBytes;
  bool _isLoading = false;
  String? _errorMessage;

  // 1. 카메라 또는 갤러리 이미지 선택 (취소 시 에러 없이 복귀)
  Future<void> _pickImage(ImageSource source) async {
    try {
      final XFile? pickedFile = await _picker.pickImage(source: source);

      // 사용자가 촬영/선택 창을 그냥 닫은 경우 에러 없이 리턴
      if (pickedFile == null) return;

      final bytes = await pickedFile.readAsBytes();
      setState(() {
        _imageBytes = bytes;
        _errorMessage = null;
      });

      // 이미지 선택 즉시 서버 전송
      _sendToServer();
    } catch (e) {
      setState(() {
        _errorMessage = '카메라 또는 앨범 접근 권한을 확인해 주세요.';
      });
    }
  }

  // 2. 직접 텍스트 입력 다이얼로그 (점자 라벨 모드 전용)
  Future<void> _showTextInputDialog() async {
    final controller = TextEditingController();

    final enteredText = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('점자 라벨 문구 직접 입력'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(
            hintText: '출력할 단어를 입력하세요 (예: 우유, 샴푸)',
            border: OutlineInputBorder(),
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('취소'),
          ),
          ElevatedButton(
            onPressed: () {
              final text = controller.text.trim();
              if (text.isNotEmpty) {
                Navigator.pop(context, text);
              }
            },
            child: const Text('확인'),
          ),
        ],
      ),
    );

    // 텍스트를 입력받으면 ResultScreen으로 이동 (피드백 보고서 합의: mode, text만 전달)
    if (enteredText != null && enteredText.isNotEmpty && mounted) {
      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (context) => ResultScreen(
            mode: widget.mode,
            text: enteredText,
          ),
        ),
      );
    }
  }

  // 3. 서버 전송 및 실패 UI 전담 처리
  Future<void> _sendToServer() async {
    if (_imageBytes == null) return;

    setState(() {
      _isLoading = true;
      _errorMessage = null;
    });

    try {
      // ApiService 정적 호출
      final Map<String, dynamic> response = await ApiService.sendImage(
        bytes: _imageBytes!,
        mode: widget.mode,
      );

      if (!mounted) return;

      if (response['ok'] == true) {
        // 성공 시 ResultScreen으로 모드와 인식된 텍스트만 전달
        Navigator.push(
          context,
          MaterialPageRoute(
            builder: (context) => ResultScreen(
              mode: widget.mode,
              text: (response['text'] as String?) ?? '',
            ),
          ),
        );
      } else {
        // 실패 시 camera_screen에서 에러 메시지 표시
        setState(() {
          _errorMessage = (response['error'] as String?) ?? '서버 처리에 실패했습니다.';
        });
      }
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _errorMessage = '네트워크 연결을 확인해 주세요.';
      });
    } finally {
      if (mounted) {
        setState(() {
          _isLoading = false;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    // 팀 공용 상수로 라벨 모드 확인
    final bool isLabelMode = widget.mode == kModeLabel;
    final String titleText = isLabelMode ? '점자 라벨 만들기' : '책 점역하기';

    return Scaffold(
      appBar: AppBar(
        title: Text(titleText),
      ),
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(24.0),
            child: _buildContent(isLabelMode),
          ),
        ),
      ),
    );
  }

  Widget _buildContent(bool isLabelMode) {
    // 1) 서버 처리 중 화면
    if (_isLoading) {
      return Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: const [
          CircularProgressIndicator(),
          SizedBox(height: 24),
          Text(
            '인식 중입니다...',
            style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
          ),
          SizedBox(height: 8),
          Text(
            '서버 연결 상태에 따라 최대 10초가 걸릴 수 있습니다.\n잠시만 기다려 주세요.',
            textAlign: TextAlign.center,
            style: TextStyle(fontSize: 13, color: Colors.grey),
          ),
        ],
      );
    }

    // 2) 서버 오류 발생 화면
    if (_errorMessage != null) {
      return Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(Icons.error_outline, color: Colors.red, size: 60),
          const SizedBox(height: 16),
          Text(
            _errorMessage!,
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 16, color: Colors.red),
          ),
          const SizedBox(height: 24),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              // [중요 4] 재촬영이 아닌 기존 사진 재전송 실행
              ElevatedButton.icon(
                onPressed: _sendToServer,
                icon: const Icon(Icons.refresh),
                label: const Text('다시 시도'),
              ),
              const SizedBox(width: 12),
              OutlinedButton.icon(
                onPressed: () {
                  setState(() {
                    _errorMessage = null;
                    _imageBytes = null;
                  });
                },
                icon: const Icon(Icons.photo_camera_back),
                label: const Text('새 사진 고르기'),
              ),
            ],
          ),
        ],
      );
    }

    // 3) 초기 입력 선택 화면 (모드별 분기)
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Icon(
          isLabelMode ? Icons.label_outline : Icons.menu_book,
          size: 70,
          color: Colors.blueAccent,
        ),
        const SizedBox(height: 20),
        Text(
          isLabelMode ? '라벨로 만들 대상을 선택해 주세요' : '점역할 책 페이지를 촬영해 주세요',
          textAlign: TextAlign.center,
          style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
        ),
        const SizedBox(height: 32),

        // 버튼 1: 카메라 촬영 (라벨, 책 공통)
        ElevatedButton.icon(
          onPressed: () => _pickImage(ImageSource.camera),
          icon: const Icon(Icons.camera_alt, size: 24),
          label: const Padding(
            padding: EdgeInsets.symmetric(vertical: 14),
            child: Text('카메라로 촬영하기', style: TextStyle(fontSize: 16)),
          ),
        ),
        const SizedBox(height: 14),

        // 버튼 2: 갤러리 선택 (라벨, 책 공통)
        ElevatedButton.icon(
          onPressed: () => _pickImage(ImageSource.gallery),
          icon: const Icon(Icons.photo_library, size: 24),
          style: ElevatedButton.styleFrom(
            backgroundColor: Colors.indigo.shade50,
            foregroundColor: Colors.indigo,
          ),
          label: const Padding(
            padding: EdgeInsets.symmetric(vertical: 14),
            child: Text('앨범에서 사진 선택하기', style: TextStyle(fontSize: 16)),
          ),
        ),

        // 버튼 3: 직접 텍스트 입력 (점자 라벨 모드일 때만 표시, 책 모드에서는 미표시)
        if (isLabelMode) ...[
          const SizedBox(height: 14),
          OutlinedButton.icon(
            onPressed: _showTextInputDialog,
            icon: const Icon(Icons.edit_note, size: 24),
            label: const Padding(
              padding: EdgeInsets.symmetric(vertical: 14),
              child: Text('라벨 문구 직접 입력하기', style: TextStyle(fontSize: 16)),
            ),
          ),
        ],
      ],
    );
  }
}