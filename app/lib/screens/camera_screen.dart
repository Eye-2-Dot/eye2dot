import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';
import '../services/api_service.dart';
import 'result_screen.dart';

class CameraScreen extends StatefulWidget {
  final String mode; // 'label' 또는 'book'

  const CameraScreen({super.key, required this.mode});

  @override
  State<CameraScreen> createState() => _CameraScreenState();
}

enum ViewState { empty, loaded, loading, error }

class _CameraScreenState extends State<CameraScreen> {
  ViewState _state = ViewState.empty;
  Uint8List? _imageBytes;
  String _errorMessage = '';

  // 사진 촬영 및 선택 (웹 호환 바이트 추출)
  Future<void> _pickImage() async {
    try {
      final ImagePicker picker = ImagePicker();
      final XFile? picked = await picker.pickImage(source: ImageSource.camera);

      if (picked == null) return;

      final Uint8List bytes = await picked.readAsBytes();
      setState(() {
        _imageBytes = bytes;
        _state = ViewState.loaded;
      });
    } catch (e) {
      setState(() {
        _errorMessage = '사진을 불러오는 중 오류가 발생했습니다.';
        _state = ViewState.error;
      });
    }
  }

  // 서버로 사진 전송
  Future<void> _sendToServer() async {
    if (_imageBytes == null) return;

    setState(() {
      _state = ViewState.loading;
    });

    try {
      final result = await ApiService.sendImage(
        bytes: _imageBytes!,
        mode: widget.mode,
      );

      if (result['ok'] == true) {
        if (!mounted) return;
        Navigator.push(
          context,
          MaterialPageRoute(
            builder: (context) => ResultScreen(
              mode: widget.mode,
              text: result['text'] ?? '',
            ),
          ),
        );
        setState(() {
          _state = ViewState.loaded;
        });
      } else {
        setState(() {
          _errorMessage = result['reason'] ?? '인식에 실패했습니다.';
          _state = ViewState.error;
        });
      }
    } catch (e) {
      setState(() {
        _errorMessage = '서버 연결 오류가 발생했습니다.';
        _state = ViewState.error;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final modeTitle = widget.mode == 'label' ? '점자 라벨 만들기' : '책 점역하기';

    return Scaffold(
      appBar: AppBar(title: Text(modeTitle), centerTitle: true),
      body: Padding(
        padding: const EdgeInsets.all(20.0),
        child: Center(
          child: switch (_state) {
            ViewState.empty => Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.camera_alt_outlined, size: 80, color: Colors.grey),
                  const SizedBox(height: 16),
                  const Text('사진을 촬영해 주세요', style: TextStyle(fontSize: 18)),
                  const SizedBox(height: 24),
                  ElevatedButton.icon(
                    onPressed: _pickImage,
                    icon: const Icon(Icons.photo_camera),
                    label: const Text('촬영하기'),
                    style: ElevatedButton.styleFrom(minimumSize: const Size(200, 50)),
                  ),
                ],
              ),
            ViewState.loaded => Column(
                children: [
                  Expanded(
                    child: ClipRRect(
                      borderRadius: BorderRadius.circular(12),
                      child: Image.memory(_imageBytes!, fit: BoxFit.contain),
                    ),
                  ),
                  const SizedBox(height: 20),
                  Row(
                    children: [
                      Expanded(
                        child: OutlinedButton(
                          onPressed: () => setState(() => _state = ViewState.empty),
                          style: OutlinedButton.styleFrom(minimumSize: const Size(0, 50)),
                          child: const Text('다시 찍기'),
                        ),
                      ),
                      const SizedBox(width: 16),
                      Expanded(
                        child: ElevatedButton(
                          onPressed: _sendToServer,
                          style: ElevatedButton.styleFrom(minimumSize: const Size(0, 50)),
                          child: const Text('전송'),
                        ),
                      ),
                    ],
                  ),
                ],
              ),
            ViewState.loading => const Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  CircularProgressIndicator(),
                  SizedBox(height: 20),
                  Text('인식 중입니다...', style: TextStyle(fontSize: 16)),
                ],
              ),
            ViewState.error => Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.error_outline, size: 60, color: Colors.red),
                  const SizedBox(height: 16),
                  Text(_errorMessage, textAlign: TextAlign.center, style: const TextStyle(fontSize: 16)),
                  const SizedBox(height: 24),
                  ElevatedButton(
                    onPressed: _pickImage,
                    child: const Text('다시 시도'),
                  ),
                ],
              ),
          },
        ),
      ),
    );
  }
}