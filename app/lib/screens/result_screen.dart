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

import 'package:flutter/material.dart';

class ResultScreen extends StatelessWidget {
  final String mode; 
  final String text;

  const ResultScreen({super.key, required this.mode, required this.text}) : super(key: key);

  // 오류 코드에 따른 안내 문구 변환 함수
  String getErrorMessage(String errorCode) {
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
    // ----------------------------------------------------
    // [개발용 더미 데이터]
    const bool dummyOk = false; // true면 성공, false면 실패 화면
    const String dummyErrorCode = 'network_error'; // 테스트할 오류 코드
    const String dummyMode = 'label'; 
    const String dummyText = '우유';
    // ----------------------------------------------------
    
    final isOk = dummyOk;
    final currentMode = dummyMode; 
    final currentText = dummyText; 
    final errorMessage = getErrorMessage(dummyErrorCode);

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
                    ? (currentMode == 'label'
                        ? Text(
                            currentText,
                            style: const TextStyle(fontSize: 64, fontWeight: FontWeight.bold),
                            textAlign: TextAlign.center,
                          )
                        : SingleChildScrollView(
                            child: Text(
                              currentText,
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