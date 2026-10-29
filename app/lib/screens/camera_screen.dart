import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';

import '../constants.dart';
import '../services/api_service.dart';
import 'result_screen.dart';

/// 서버가 reason 을 주지 않았을 때 오류 코드별로 보여 줄 문구
const Map<String, String> _kErrorMessages = {
  'invalid_image': '사진에 문제가 있습니다. 다시 촬영해 주세요',
  'recognition_failed': '인식하지 못했습니다. 다시 찍어 주세요',
  'rate_limit': '잠시 후 다시 시도해 주세요',
  'network_error': '네트워크 연결을 확인해 주세요',
  'parse_failed': '오류가 발생했습니다. 다시 시도해 주세요',
  'missing_text': '입력한 내용이 없습니다',
  'text_too_long': '너무 깁니다. 더 짧게 입력해 주세요',
  'invalid_mode': '잘못된 요청입니다',
};

/// 위 표에 없는 오류 코드일 때의 문구
const String _kDefaultErrorMessage = '오류가 발생했습니다. 다시 시도해 주세요';

/// 서버 reason 이 있어도 무시하고 항상 위 표의 고정 문구를 보여 줄 오류 코드.
///
/// 이 둘의 reason 에는 "Gemini API 오류: ..." 같은 기술 문구가 담겨 있는데,
/// 사용자가 할 수 있는 일은 네트워크 확인이나 재시도뿐이라 보여 줄 실익이 없다.
/// 원래 reason 은 디버깅용으로 콘솔에만 남긴다.
const Set<String> _kFixedMessageErrors = {'network_error', 'parse_failed'};

/// 실패 응답 → 화면에 보여 줄 문구.
/// reason 이 있으면 그것을, 없으면(또는 고정 문구 대상이면) 오류 코드별 문구를 쓴다.
String _messageFor(Map<String, dynamic> response) {
  final error = response['error'];
  final reason = response['reason'];
  debugPrint('[CameraScreen] 실패 error=$error reason=$reason');

  if (reason is String &&
      reason.trim().isNotEmpty &&
      !_kFixedMessageErrors.contains(error)) {
    return reason;
  }
  return _kErrorMessages[error] ?? _kDefaultErrorMessage;
}

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

  /// 마지막으로 보낸 요청. "다시 시도"는 이것을 그대로 다시 보낸다.
  /// (사진이면 같은 사진, 텍스트면 같은 글자. 다시 찍거나 다시 입력하지 않아도 된다)
  /// 보낸 적이 없거나 요청과 무관한 오류(권한 등)면 null 이고, 그때는 "다시 시도"를 숨긴다.
  Future<Map<String, dynamic>> Function()? _lastRequest;

  /// 마지막 요청이 텍스트였다면 그 글자, 사진이었다면 null.
  /// 로딩 문구와 실패 시 버튼("다시 입력하기" / "새 사진 고르기")을 고르는 데 쓴다.
  String? _lastText;

  bool _isLoading = false;
  String? _errorMessage;

  // 1. 카메라 또는 갤러리 이미지 선택 (취소 시 에러 없이 복귀)
  Future<void> _pickImage(ImageSource source) async {
    try {
      final XFile? pickedFile = await _picker.pickImage(source: source);

      // 사용자가 촬영/선택 창을 그냥 닫은 경우 에러 없이 리턴
      if (pickedFile == null) return;

      final bytes = await pickedFile.readAsBytes();

      // 이미지 선택 즉시 서버 전송
      _submit(() => ApiService.sendImage(bytes: bytes, mode: widget.mode));
    } catch (e) {
      setState(() {
        _lastRequest = null;
        _lastText = null;
        _errorMessage = '카메라 또는 앨범 접근 권한을 확인해 주세요.';
      });
    }
  }

  // 2. 직접 텍스트 입력 다이얼로그 (점자 라벨 모드 전용)
  //    [initialText] 가 있으면 입력칸에 미리 채워 둔다 ("다시 입력하기"용)
  Future<void> _showTextInputDialog({String? initialText}) async {
    final controller = TextEditingController(text: initialText);

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

    // 텍스트를 입력받으면 사진과 똑같이 서버로 보내 점역 결과를 받아온다
    if (enteredText != null && enteredText.isNotEmpty && mounted) {
      _submit(
        () => ApiService.sendText(text: enteredText, mode: widget.mode),
        text: enteredText,
      );
    }
  }

  // 3. 서버 전송 및 실패 UI 전담 처리 (사진·텍스트 공통)
  //    [text] 텍스트 요청이면 그 글자, 사진 요청이면 생략
  Future<void> _submit(
    Future<Map<String, dynamic>> Function() request, {
    String? text,
  }) async {
    setState(() {
      _lastRequest = request;
      _lastText = text;
      _isLoading = true;
      _errorMessage = null;
    });

    try {
      final Map<String, dynamic> response = await request();

      if (!mounted) return;

      if (response['ok'] == true) {
        // 성공 시 서버 응답 전체를 ResultScreen 으로 넘긴다 (점자 데이터는 나중에 펌웨어로 보낸다)
        Navigator.push(
          context,
          MaterialPageRoute(
            builder: (context) => ResultScreen(
              mode: widget.mode,
              // 서버가 앞뒤 공백을 떼므로 보낸 값이 아니라 응답의 text 를 쓴다
              text: (response['text'] as String?) ?? '',
              reason: response['reason'] as String?,
              braille: response['braille'],
              braillePreview: response['braille_preview'] as String?,
              cellCount: response['cell_count'] as int?,
            ),
          ),
        );
      } else {
        // 실패 시 camera_screen에서 에러 메시지 표시
        setState(() {
          _errorMessage = _messageFor(response);
        });
      }
    } catch (e) {
      // ApiService 는 서버·네트워크 오류를 예외 대신 ok:false 로 돌려주므로
      // 여기에는 예상하지 못한 오류(응답 값의 타입이 다른 경우 등)만 온다
      debugPrint('[CameraScreen] 예상하지 못한 오류: $e');
      if (!mounted) return;
      setState(() {
        _errorMessage = _kDefaultErrorMessage;
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
    final bool isTextRequest = _lastText != null;

    // 1) 서버 처리 중 화면
    if (_isLoading) {
      return Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const CircularProgressIndicator(),
          const SizedBox(height: 24),
          Text(
            isTextRequest ? '점역 중입니다...' : '인식 중입니다...',
            style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
          ),
          const SizedBox(height: 8),
          const Text(
            '잠시만 기다려 주세요.',
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
              // [중요 4] 재촬영·재입력이 아닌 마지막 요청 재전송 실행
              if (_lastRequest != null) ...[
                ElevatedButton.icon(
                  onPressed: () => _submit(_lastRequest!, text: _lastText),
                  icon: const Icon(Icons.refresh),
                  label: const Text('다시 시도'),
                ),
                const SizedBox(width: 12),
              ],
              // 텍스트 요청이 실패했으면 이전 글자를 채운 입력창을 다시 연다
              if (isTextRequest)
                OutlinedButton.icon(
                  onPressed: () => _showTextInputDialog(initialText: _lastText),
                  icon: const Icon(Icons.edit_note),
                  label: const Text('다시 입력하기'),
                )
              else
                OutlinedButton.icon(
                  onPressed: () {
                    setState(() {
                      _errorMessage = null;
                      _lastRequest = null;
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
