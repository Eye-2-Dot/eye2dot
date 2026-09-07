import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:http/http.dart' as http;

import '../constants.dart';

/// 서버 통신 담당 클래스
///
/// 앱과 서버 사이에서 데이터를 주고받는 코드. 화면은 만들지 않는다.
///
/// 주의: Chrome(웹)에서 실행하므로 dart:io 의 File 은 절대 쓰지 말 것.
/// 이미지는 항상 Uint8List(바이트 배열) 형태로 주고받는다.
///
/// ── 호출 규약 (세연·준석과 합의된 형태. 바꾸지 말 것) ──
///
/// final result = await ApiService.sendImage(
///   bytes: bytes,
///   mode: widget.mode,   // 'label' 또는 'book'
/// );
/// if (result['ok'] == true) { ... } else { ... }
///
/// ── 반환 형태 ──
///
/// 성공:
/// {
///   'ok': true,
///   'text': '우유',                 // 준석이 화면에 표시
///   'reason': '흰색 종이팩에 ...',   // 전용 기기가 음성으로 읽는 값
///   'braille': [13, 41],           // 기기 솔레노이드 구동용
///   'braille_preview': '⠍⠩',
///   'cell_count': 2,
/// }
///
/// 실패:
/// {
///   'ok': false,
///   'error': 'recognition_failed',  // 6종 중 하나. 준석이 이 값으로 문구 분기
///   'reason': '사용자에게 보여줄 실패 이유',
/// }
///
/// 이 함수는 예외를 던지지 않는다. 어떤 상황에서도 위 두 형태 중 하나를 돌려준다.
/// 호출하는 쪽에서 try/catch 를 감쌀 필요가 없다.
class ApiService {
  // ── 설정 ──

  /// 개발 중에는 true (목 서버). 실제 AI를 붙일 때 false 로만 바꾸면 된다.
  static const bool _useMock = true;

  /// 요청 하나가 이만큼 넘게 걸리면 포기한다.
  /// OCR + AI 호출이라 넉넉히 잡았다. 무한 로딩을 막는 안전장치.
  static const Duration _timeout = Duration(seconds: 30);

  /// 네트워크 오류 시 최대 시도 횟수 (첫 시도 포함).
  static const int _maxAttempts = 3;

  static String get _processPath => _useMock ? '/process-mock' : '/process-file';

  // ── 1. 서버 연결 확인 ──

  /// 서버가 켜져 있는지 확인한다. 예외를 던지지 않고 true/false 만 돌려준다.
  static Future<bool> checkHealth() async {
    try {
      final res = await http
          .get(Uri.parse('$kServerBaseUrl/health'))
          .timeout(const Duration(seconds: 5));
      return res.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  // ── 2. 사진 전송 ──

  /// 이미지를 서버로 보내고 인식 + 점역 결과를 받아온다.
  ///
  /// [bytes] 촬영한 이미지의 원본 바이트
  /// [mode]  kModeLabel(사물 라벨) 또는 kModeBook(책 점역)
  static Future<Map<String, dynamic>> sendImage({
    required Uint8List bytes,
    required String mode,
  }) async {
    // 보내기 전에 걸러낼 수 있는 것은 서버까지 가지 않는다.
    if (mode != kModeLabel && mode != kModeBook) {
      return _fail('invalid_mode', '모드 값이 잘못되었습니다.');
    }
    if (bytes.isEmpty) {
      return _fail('invalid_image', '사진에 문제가 있습니다.\n다시 촬영해 주세요.');
    }

    for (var attempt = 1; attempt <= _maxAttempts; attempt++) {
      final bool isLastAttempt = attempt == _maxAttempts;

      try {
        final result = await _post(bytes: bytes, mode: mode);

        // 서버가 일시적인 연결 실패(502)를 보고한 경우에만 다시 시도한다.
        // rate_limit(429)은 다시 보내면 상황이 더 나빠지므로 바로 반환한다.
        final bool retryable =
            result['ok'] == false && result['error'] == 'network_error';
        if (retryable && !isLastAttempt) {
          await _backoff(attempt);
          continue;
        }

        return result;
      } on TimeoutException {
        if (isLastAttempt) {
          return _fail('network_error', '네트워크 연결을 확인해 주세요.');
        }
        await _backoff(attempt);
      } on http.ClientException {
        // 서버가 꺼져 있거나 CORS 로 막힌 경우 웹에서는 이 예외가 난다.
        if (isLastAttempt) {
          return _fail('network_error', '네트워크 연결을 확인해 주세요.');
        }
        await _backoff(attempt);
      }
    }

    // 위 반복문에서 항상 반환되지만, 컴파일러를 위해 남겨 둔다.
    return _fail('network_error', '네트워크 연결을 확인해 주세요.');
  }

  // ── 내부 구현 ──

  /// 실제 HTTP 요청 한 번. 네트워크 예외는 위로 그대로 던진다.
  static Future<Map<String, dynamic>> _post({
    required Uint8List bytes,
    required String mode,
  }) async {
    final uri = Uri.parse('$kServerBaseUrl$_processPath?mode=$mode');
    final request = http.MultipartRequest('POST', uri);

    // 필드 이름은 반드시 'image'. filename 도 꼭 붙여야 서버가 파일로 인식한다.
    request.files.add(
      http.MultipartFile.fromBytes('image', bytes, filename: 'photo.jpg'),
    );

    final streamed = await request.send().timeout(_timeout);
    final body = utf8.decode(await streamed.stream.toBytes());

    return _parse(body, mode);
  }

  /// 응답 본문을 해석해 화면에서 쓸 형태로 정규화한다.
  ///
  /// 상태 코드가 아니라 ok 필드로 성공 여부를 판단한다.
  /// recognition_failed 는 HTTP 200 으로 오기 때문이다.
  static Map<String, dynamic> _parse(String body, String mode) {
    late final Map<String, dynamic> data;
    try {
      data = jsonDecode(body) as Map<String, dynamic>;
    } catch (_) {
      return _fail('parse_failed', '오류가 발생했습니다.\n다시 시도해 주세요.');
    }

    // 실패 응답
    if (data['ok'] != true) {
      final error = data['error'] as String? ?? 'parse_failed';
      final reason = data['reason'] as String? ?? _defaultReason(error);
      return _fail(error, reason);
    }

    // 성공 응답
    try {
      return {
        'ok': true,
        'text': data['text'] as String? ?? '',
        'reason': data['reason'] as String? ?? '',
        'braille': _parseBraille(data['braille'], mode),
        'braille_preview': data['braille_preview'] as String? ?? '',
        'cell_count': data['cell_count'] as int? ?? 0,
      };
    } catch (_) {
      // 규격과 다른 모양이 왔다는 뜻. 서버 담당자와 확인할 것.
      return _fail('parse_failed', '오류가 발생했습니다.\n다시 시도해 주세요.');
    }
  }

  /// 점자 배열을 정확한 타입으로 바꾼다.
  ///
  /// jsonDecode 는 List<dynamic> 만 돌려주기 때문에, 이 변환을 거치지 않고
  /// List<int> 나 List<List<int>> 로 바로 캐스팅하면 실행 중에 오류가 난다.
  /// 여기서 한 번만 처리하고, 바깥에서는 규격대로의 타입을 쓴다.
  ///
  ///   label → List<int>        (한 줄)
  ///   book  → List<List<int>>  (여러 줄)
  static Object _parseBraille(dynamic raw, String mode) {
    final list = raw as List;

    if (mode == kModeBook) {
      final lines = list.map((row) => (row as List).cast<int>()).toList();
      assert(
        lines.every((line) => line.every((m) => m >= 0 && m <= 63)),
        '점자 mask 는 0~63 범위여야 합니다',
      );
      return lines;
    }

    final cells = list.cast<int>();
    assert(
      cells.every((m) => m >= 0 && m <= 63),
      '점자 mask 는 0~63 범위여야 합니다',
    );
    return cells;
  }

  /// 재시도 사이의 대기 시간. 시도할수록 조금씩 길게 기다린다.
  static Future<void> _backoff(int attempt) {
    return Future.delayed(Duration(milliseconds: 300 * attempt));
  }

  static Map<String, dynamic> _fail(String error, String reason) {
    return {'ok': false, 'error': error, 'reason': reason};
  }

  /// 서버가 reason 을 안 보냈을 때 쓸 기본 문구.
  static String _defaultReason(String error) {
    switch (error) {
      case 'recognition_failed':
        return '인식하지 못했습니다.\n다시 찍어 주세요.';
      case 'invalid_image':
        return '사진에 문제가 있습니다.\n다시 촬영해 주세요.';
      case 'rate_limit':
        return '잠시 후 다시 시도해 주세요.';
      case 'network_error':
        return '네트워크 연결을 확인해 주세요.';
      case 'invalid_mode':
        return '모드 값이 잘못되었습니다.';
      default:
        return '오류가 발생했습니다.\n다시 시도해 주세요.';
    }
  }
}

// ─────────────────────────────────────────────
// 나중에 기기 전송을 붙일 때 쓰는 도우미
//
// 지금은 아무도 호출하지 않는다. 점자 데이터를 줄 단위로 다뤄야 할 때
// 모드별 분기를 다시 쓰지 않도록 미리 만들어 둔 것이다.
// ─────────────────────────────────────────────

/// 성공 결과의 braille 값을 항상 2차원(줄 단위)으로 바꿔 준다.
/// 라벨 모드는 한 줄짜리 문서로 취급한다.
List<List<int>> brailleLines(Map<String, dynamic> result) {
  final raw = result['braille'];
  if (raw is List<List<int>>) return raw;
  if (raw is List<int>) return [raw];
  return const [];
}

/// 점자 mask 배열을 사람이 눈으로 확인할 수 있는 문자로 바꾼다.
/// 서버가 보낸 braille_preview 와 비교해 보면 인코딩이 맞는지 검증할 수 있다.
String brailleToUnicode(List<int> cells) {
  return String.fromCharCodes(cells.map((mask) => 0x2800 + mask));
}
