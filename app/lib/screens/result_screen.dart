/* import 'package:flutter/material.dart';

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
*/

/* import 'package:flutter/material.dart';

class ResultScreen extends StatelessWidget {
  final String mode; 
  final String text;
  final bool isOk; // 서버 성공 여부 추가
  final String? errorCode; // 서버 에러 코드 추가 (성공일 경우 null)

  const ResultScreen({
    super.key, 
    required this.mode, 
    required this.text, 
    required this.isOk, 
    this.errorCode,
  });

  // 오류 코드에 따른 안내 문구 변환 함수
  String getErrorMessage(String? errorCode) {
    switch (errorCode) {
      case 'recognition_failed':
        return '인식하지 못했습니다.\n다시 찍어 주세요.';
      case 'invalid_image':
        return '사진에 문제가 있습니다.\n다시 촬영해 주세요.';
      case 'rate_limit':
        return '잠시 후 다시 시도해 주세요.';
      case 'network_error':
        return '네트워크 연결을 확인해 주세요.';
      case 'parse_failed':
      default:
        return '오류가 발생했습니다.\n다시 시도해 주세요.';
    }
  }

  @override
  Widget build(BuildContext context) {
    // 더미 데이터를 삭제하고 생성자로 전달받은 실제 변수(isOk, mode, text, errorCode)를 직접 사용합니다.
    final errorMessage = getErrorMessage(errorCode);

    return Scaffold(
      appBar: AppBar(
        title: const Text('인식 결과'), 
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // 1. 결과 텍스트 또는 오류 메시지 표시 영역
              Expanded(
                child: Center(
                  child: isOk 
                    // 성공했을 때의 화면 분기
                    ? (mode == 'label'
                        ? Text(
                            text,
                            style: const TextStyle(fontSize: 64, fontWeight: FontWeight.bold),
                            textAlign: TextAlign.center,
                          )
                        : SingleChildScrollView(
                            child: Text(
                              text,
                              style: const TextStyle(fontSize: 24, height: 1.5),
                            ),
                          ))
                    // 실패했을 때의 화면 분기 (오류 문구 출력)
                    : Text(
                        errorMessage,
                        style: const TextStyle(fontSize: 24, color: Colors.red, height: 1.5),
                        textAlign: TextAlign.center,
                      ),
                ),
              ),
              const SizedBox(height: 24),

              // 2. 하단 버튼 영역
              Row(
                children: [
                  Expanded(
                    child: ElevatedButton(
                      style: ElevatedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(vertical: 20),
                      ),
                      onPressed: () {
                        Navigator.pop(context); 
                      },
                      child: const Text('다시 찍기', style: TextStyle(fontSize: 20)),
                    ),
                  ),
                  // 성공(isOk == true) 상태일 때만 '출력하기' 버튼을 표시합니다.
                  if (isOk) ...[
                    const SizedBox(width: 16),
                    Expanded(
                      child: ElevatedButton(
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 20),
                          backgroundColor: Colors.blueAccent,
                          foregroundColor: Colors.white,
                        ),
                        onPressed: () {
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(
                              content: Text('출력 요청을 보냈습니다.', style: TextStyle(fontSize: 16)),
                              duration: Duration(seconds: 2),
                            ),
                          );
                        },
                        child: const Text('출력하기', style: TextStyle(fontSize: 20)),
                      ),
                    ),
                  ],
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}
*/

/* import 'package:flutter/material.dart';

class ResultScreen extends StatelessWidget {
  final String mode; 
  final String text;
  final bool isOk; 
  final String? errorCode; 
  final String? reason; 
  final dynamic braille; 

  const ResultScreen({
    super.key, 
    required this.mode, 
    required this.text, 
    required this.isOk, 
    this.errorCode,
    this.reason,
    this.braille,
  });

  // 네트워크 오류만 남기고 나머지 오류 문구는 통합 정리
  String getErrorMessage(String? errorCode) {
    if (errorCode == 'network_error') {
      return '네트워크 연결을 확인해 주세요.';
    }
    return '오류가 발생했습니다.';
  }

  @override
  Widget build(BuildContext context) {
    final errorMessage = getErrorMessage(errorCode);

    return Scaffold(
      appBar: AppBar(
        title: const Text('인식 결과'), 
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Expanded(
                child: Center(
                  child: isOk 
                    ? (mode == 'label'
                        ? Text(
                            text,
                            style: const TextStyle(fontSize: 64, fontWeight: FontWeight.bold),
                            textAlign: TextAlign.center,
                          )
                        : SingleChildScrollView(
                            child: Text(
                              text,
                              style: const TextStyle(fontSize: 24, height: 1.5),
                            ),
                          ))
                    : Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Text(
                            errorMessage,
                            style: const TextStyle(fontSize: 24, color: Colors.red, height: 1.5),
                            textAlign: TextAlign.center,
                          ),
                          if (reason != null && reason!.isNotEmpty) ...[
                            const SizedBox(height: 12),
                            Text(
                              reason!,
                              style: const TextStyle(fontSize: 14, color: Colors.grey),
                              textAlign: TextAlign.center,
                            ),
                          ],
                        ],
                      ),
                ),
              ),
              const SizedBox(height: 24),

              // 2. 하단 버튼 영역 분기
              Row(
                children: [
                  // 성공 시: 기존처럼 '다시 찍기', '출력하기' 버튼 제공
                  if (isOk) ...[
                    Expanded(
                      child: ElevatedButton(
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 20),
                        ),
                        onPressed: () {
                          Navigator.pop(context); // 카메라 화면으로 한 칸 뒤로 가기
                        },
                        child: const Text('다시 찍기', style: TextStyle(fontSize: 20)),
                      ),
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: ElevatedButton(
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 20),
                          backgroundColor: Colors.blueAccent,
                          foregroundColor: Colors.white,
                        ),
                        onPressed: () {
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(
                              content: Text('출력 요청을 보냈습니다.', style: TextStyle(fontSize: 16)),
                              duration: Duration(seconds: 2),
                            ),
                          );
                        },
                        child: const Text('출력하기', style: TextStyle(fontSize: 20)),
                      ),
                    ),
                  ] 
                  // 오류 발생 시: '처음으로' 버튼 하나만 제공
                  else ...[
                    Expanded(
                      child: ElevatedButton(
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 20),
                          backgroundColor: Colors.grey[800],
                          foregroundColor: Colors.white,
                        ),
                        onPressed: () {
                          // 쌓여있는 이전 화면들을 모두 닫고 제일 첫 화면(모드 선택)으로 돌아가는 함수
                          Navigator.popUntil(context, (route) => route.isFirst);
                        },
                        child: const Text('처음으로', style: TextStyle(fontSize: 20)),
                      ),
                    ),
                  ],
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
} */

import 'dart:typed_data';
import 'package:flutter/material.dart';

class ResultScreen extends StatelessWidget {
  final String mode; 
  final String text;
  final bool isOk; 
  final String? errorCode; 
  final String? reason; 
  final dynamic braille; 
  final String? braillePreview; // 서버에서 전달받은 점자 유니코드 문자열
  final Uint8List? imageBytes; // 상단에 표시할 촬영/선택된 이미지 데이터

  const ResultScreen({
    super.key, 
    required this.mode, 
    required this.text, 
    required this.isOk, 
    this.errorCode,
    this.reason,
    this.braille,
    this.braillePreview,
    this.imageBytes,
  });

  // 네트워크 오류 시 앱 자체 문구 출력, 그 외에는 서버가 보낸 reason 값을 그대로 화면에 표시
  String getErrorMessage() {
    if (errorCode == 'network_error') {
      return '네트워크 연결을 확인해 주세요.';
    }
    return reason ?? '오류가 발생했습니다.';
  }

  // 타이핑 버튼 클릭 시 호출되는 팝업 함수
  void _showTypingDialog(BuildContext context) {
    final TextEditingController textController = TextEditingController();
    
    showDialog(
      context: context,
      builder: (context) {
        return AlertDialog(
          title: const Text('직접 텍스트 입력'),
          content: TextField(
            controller: textController,
            decoration: const InputDecoration(hintText: '점자로 변환할 텍스트를 입력하세요'),
            autofocus: true,
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('취소'),
            ),
            TextButton(
              onPressed: () {
                Navigator.pop(context); // 팝업 닫기
                // TODO: textController.text 값을 서버로 전송하여 점자 데이터를 다시 받아오는 로직 추가 필요
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(content: Text('${textController.text} 입력 완료 (서버 연동 필요)')),
                );
              },
              child: const Text('확인'),
            ),
          ],
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('인식 결과'), 
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // 1. 상단: 사진 표시 영역 (중앙 정렬)
              Expanded(
                flex: 2,
                child: Center(
                  child: imageBytes != null
                      ? Image.memory(imageBytes!, fit: BoxFit.contain)
                      : const Icon(Icons.image_not_supported, size: 64, color: Colors.grey),
                ),
              ),
              const SizedBox(height: 16),

              // 2. 중단: 인식된 텍스트 및 점자 표기 또는 오류 메시지
              Expanded(
                flex: 3,
                child: Center(
                  child: isOk 
                    ? SingleChildScrollView(
                        child: Column(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            Text(
                              text,
                              style: const TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
                              textAlign: TextAlign.center,
                            ),
                            if (braillePreview != null) ...[
                              const SizedBox(height: 16),
                              Text(
                                braillePreview!,
                                style: const TextStyle(fontSize: 32, letterSpacing: 2.0),
                                textAlign: TextAlign.center,
                              ),
                            ]
                          ],
                        ),
                      )
                    : Text(
                        getErrorMessage(),
                        style: const TextStyle(fontSize: 24, color: Colors.red, height: 1.5),
                        textAlign: TextAlign.center,
                      ),
                ),
              ),
              const SizedBox(height: 24),

              // 3. 하단: 다시 찍기 / 타이핑 / 출력하기 버튼 영역
              Row(
                children: [
                  Expanded(
                    child: ElevatedButton(
                      style: ElevatedButton.styleFrom(padding: const EdgeInsets.symmetric(vertical: 20)),
                      onPressed: () => Navigator.pop(context),
                      child: const Text('다시 찍기', style: TextStyle(fontSize: 16)),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: ElevatedButton(
                      style: ElevatedButton.styleFrom(padding: const EdgeInsets.symmetric(vertical: 20)),
                      onPressed: () => _showTypingDialog(context),
                      child: const Text('타이핑', style: TextStyle(fontSize: 16)),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: ElevatedButton(
                      style: ElevatedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(vertical: 20),
                        backgroundColor: Colors.blueAccent,
                        foregroundColor: Colors.white,
                      ),
                      onPressed: () {
                        ScaffoldMessenger.of(context).showSnackBar(
                          const SnackBar(
                            content: Text('출력 요청을 보냈습니다.', style: TextStyle(fontSize: 16)),
                            duration: Duration(seconds: 2),
                          ),
                        );
                      },
                      child: const Text('출력하기', style: TextStyle(fontSize: 16)),
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}