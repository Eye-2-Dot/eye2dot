import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;

import '../constants.dart';

/// 사진 인식은 서버가 Gemini를 거치므로 보통 2~5초 걸린다. 넉넉히 잡는다.
const Duration _kImageTimeout = Duration(seconds: 30);

/// 텍스트 점역은 AI를 부르지 않아 빠르다.
const Duration _kTextTimeout = Duration(seconds: 10);

/// 네트워크 오류 시 재시도 횟수. 첫 시도 1번 + 재시도 최대 3번 = 최대 4번 보낸다.
const int _kMaxRetries = 3;

/// 서버 통신 담당 클래스
///
/// 화면 쪽은 성공이든 실패든 항상 Map 하나를 받는다.
/// 서버 오류와 네트워크 오류는 예외를 던지지 않고 {ok: false, error, reason?} 으로 돌려준다.
///
/// 성공 여부는 HTTP 상태 코드가 아니라 반드시 ok 필드로 판단할 것.
/// (recognition_failed 는 HTTP 200 으로 온다)
///
/// 주의: Chrome(웹)에서 실행하므로 dart:io 의 File 은 절대 쓰지 말 것.
/// 이미지는 항상 Uint8List(바이트 배열) 형태로 주고받는다.
class ApiService {
  // ── 테스트에서 바꿔 끼우는 통로. 앱 코드에서는 건드리지 말 것 ──

  /// 더미 모드 여부. 평소에는 constants.dart 의 kUseMock 을 따른다.
  @visibleForTesting
  static bool useMock = kUseMock;

  /// 요청을 보낼 연결(client)을 만드는 함수. 테스트에서는 가짜 서버(MockClient)로 바꾼다.
  @visibleForTesting
  static http.Client Function() clientFactory = http.Client.new;

  /// 재시도 전에 쉬는 시간의 단위. n번째 재시도 전에 n배만큼 쉰다 (1초, 2초, 3초).
  @visibleForTesting
  static Duration retryDelayUnit = const Duration(seconds: 1);

  /// 이미지를 서버로 보내고 인식 + 점역 결과를 받아온다.
  ///
  /// [bytes] 촬영한 이미지의 원본 바이트
  /// [mode]  kModeLabel(사물 라벨) 또는 kModeBook(책 점역)
  ///
  /// ── 실제 서버 응답 형태 (D 참고용) ──
  /// 자세한 내용은 docs/api-spec.md 참고.
  ///
  /// 점자 셀 하나는 6비트 정수(mask)다. 점 번호 n(1~6)이 비트 (n-1)에 대응하고,
  /// 0은 빈 칸(공백)을 뜻한다.
  ///
  /// 주의: braille 의 타입이 모드에 따라 다르다.
  ///   - label: 사물 이름 한 개라 항상 한 줄  → 1차원 `List<int>`
  ///   - book : 본문이 길어 여러 줄이 됨      → 2차원 `List<List<int>>`
  ///            (펌웨어가 줄바꿈 위치를 알아야 하므로 줄 단위로 나뉜다.
  ///             한 줄 최대 30셀, 낱말 중간은 잘리지 않는다.)
  ///
  /// label 성공 시:
  /// {
  ///   "ok": true,
  ///   "text": "우유",                        // 인식된 텍스트
  ///   "reason": "흰색 종이팩에 우유 표기가 있습니다.",
  ///   "braille": [13, 41],                   // 1차원 — 솔레노이드 구동용
  ///   "braille_preview": "⠍⠩",               // 사람이 눈으로 확인하는 용도
  ///   "cell_count": 2
  /// }
  ///
  /// book 성공 시:
  /// {
  ///   "ok": true,
  ///   "text": "점자는 손끝으로...",            // 원문의 줄바꿈(\n)이 그대로 남아 있다
  ///   "reason": "OCR로 텍스트를 인식했습니다.",
  ///   "braille": [                           // 2차원 — 바깥 배열의 원소 하나가 한 줄
  ///     [40, 14, 34, 40, 35, 9, 42, 18, 0, 32],
  ///     [17, 13, 18, 40, 35, 21, 10, 35, 0, 49]
  ///   ],
  ///   "braille_preview": "⠨⠎⠢...\n⠑⠍⠒...",   // 줄바꿈으로 이어 붙인 문자열
  ///   "cell_count": 448                      // 모든 줄의 셀 수 합
  /// }
  ///
  /// 실패 시:
  /// {
  ///   "ok": false,
  ///   "error": "recognition_failed",   // invalid_mode / invalid_image /
  ///                                    // recognition_failed / rate_limit /
  ///                                    // network_error / parse_failed
  ///   "reason": "사용자에게 보여줄 실패 이유"
  /// }
  ///
  /// recognition_failed 는 HTTP 200으로 내려온다.
  /// 따라서 상태 코드가 아니라 ok 필드로 성공 여부를 판단할 것.
  ///
  /// 앱이 직접 만드는 실패 응답에는 reason 이 없다. 화면이 error 코드로 문구를 고른다.
  ///   network_error : 연결 실패(재시도 후에도) 또는 시간 초과
  ///   parse_failed  : 서버 응답이 JSON 이 아니거나 ok 필드가 없음 (예: FastAPI 자동 422 오류)
  ///
  /// 보낼 곳: POST {kServerBaseUrl}/process-file?mode={mode}
  ///         multipart/form-data, 필드 image(파일)
  ///
  /// kUseMock 이 true 면 서버에 보내지 않고 1초 뒤 더미 결과를 돌려준다.
  static Future<Map<String, dynamic>> sendImage({
    required Uint8List bytes,
    required String mode,
  }) async {
    if (useMock) return _mockResult(mode);

    final uri = Uri.parse('$kServerBaseUrl/process-file')
        .replace(queryParameters: {'mode': mode});

    return _send(
      timeout: _kImageTimeout,
      // multipart 요청은 한 번만 보낼 수 있으므로 시도할 때마다 새로 만든다
      buildRequest: () => http.MultipartRequest('POST', uri)
        ..files.add(http.MultipartFile.fromBytes(
          'image',
          bytes,
          // 파일 이름이 없으면 서버(FastAPI)가 파일이 아니라 글자로 보고 422 오류를 낸다
          filename: 'photo.jpg',
        )),
    );
  }

  /// 텍스트를 서버로 보내 점역 결과를 받아온다. AI 는 거치지 않는다.
  ///
  /// [text] 점역할 글자 (서버가 앞뒤 공백을 떼므로 응답의 text 가 보낸 값과 다를 수 있다)
  /// [mode] kModeLabel 또는 kModeBook
  ///
  /// 응답 형식은 sendImage 와 같다. reason 은 "직접 입력한 문구입니다."
  ///
  /// 보낼 곳: POST {kServerBaseUrl}/translate
  ///         JSON {"text": ..., "mode": ...}
  ///
  /// kUseMock 이 true 면 입력과 상관없이 "우유" 더미 결과를 돌려준다.
  /// 앱에서는 점역을 할 수 없는데, 입력 글자에 엉뚱한 점자를 붙이면
  /// 나중에 펌웨어 테스트 때 헷갈리므로 text 도 더미 그대로 둔다.
  static Future<Map<String, dynamic>> sendText({
    required String text,
    required String mode,
  }) async {
    if (useMock) {
      final mock = await _mockResult(mode);
      return {...mock, 'reason': '직접 입력한 문구입니다.'};
    }

    final uri = Uri.parse('$kServerBaseUrl/translate');

    return _send(
      timeout: _kTextTimeout,
      // 헤더를 먼저 정해야 body 를 넣을 때 charset=utf-8 이 자동으로 붙는다
      buildRequest: () => http.Request('POST', uri)
        ..headers['Content-Type'] = 'application/json'
        ..body = jsonEncode({'text': text, 'mode': mode}),
    );
  }

  /// 서버 응답을 기다리는 느낌을 내도록 1초 쉬었다가 더미 결과를 준다 (로딩 화면 확인용).
  static Future<Map<String, dynamic>> _mockResult(String mode) async {
    await Future.delayed(const Duration(seconds: 1));
    return mode == kModeBook ? _kMockBookResult : _kMockLabelResult;
  }

  /// 요청을 보내고 응답을 Map 으로 바꾼다. 네트워크 오류면 재시도한다.
  ///
  /// 재시도함    : 연결 실패(서버 꺼짐, 와이파이 끊김), 서버의 network_error(Gemini 연결 실패)
  /// 재시도 안 함: 시간 초과, rate_limit, 그 밖에 서버가 답을 준 모든 실패
  static Future<Map<String, dynamic>> _send({
    required http.BaseRequest Function() buildRequest,
    required Duration timeout,
  }) async {
    for (var attempt = 0;; attempt++) {
      // 연결 실패는 순식간에 나므로 쉬지 않으면 재시도가 의미 없이 한꺼번에 끝난다
      if (attempt > 0) await Future.delayed(retryDelayUnit * attempt);

      final client = clientFactory();
      try {
        final response = await client
            .send(buildRequest())
            .then(http.Response.fromStream)
            .timeout(timeout);
        final result = _parse(response);

        if (result['error'] == 'network_error' && attempt < _kMaxRetries) {
          debugPrint('[ApiService] 서버 network_error, 재시도 ${attempt + 1}/$_kMaxRetries: ${result['reason']}');
          continue;
        }
        return result;
      } on TimeoutException {
        // 시간 초과는 재시도하지 않는다. 서버가 아직 Gemini 를 기다리는 중일 가능성이 높아서,
        // 다시 보내면 같은 요청을 두 번 처리해 API 비용과 한도를 낭비하고 사용자도 오래 기다린다.
        debugPrint('[ApiService] 시간 초과 (${timeout.inSeconds}초)');
        return _failure('network_error');
      } on http.ClientException catch (e) {
        debugPrint('[ApiService] 연결 실패 (${attempt + 1}번째 시도): $e');
        if (attempt < _kMaxRetries) continue;
        return _failure('network_error');
      } finally {
        // 연결을 닫아야 Chrome 에서 시간 초과된 요청도 실제로 취소된다
        client.close();
      }
    }
  }

  /// HTTP 응답 → Map. 서버 응답은 성공이든 실패든 상태 코드와 상관없이 그대로 돌려준다.
  static Map<String, dynamic> _parse(http.Response response) {
    // 서버 응답 헤더에 문자 인코딩이 없으면 response.body 는 한글이 깨질 수 있어 UTF-8 로 직접 푼다
    final body = utf8.decode(response.bodyBytes, allowMalformed: true);

    Object? decoded;
    try {
      decoded = jsonDecode(body);
    } on FormatException {
      decoded = null;
    }

    // 서버 응답에는 항상 ok 필드가 있다. 없으면 FastAPI 자동 오류(422)나 서버 밖의 오류 페이지 등이다.
    if (decoded is! Map<String, dynamic> || decoded['ok'] is! bool) {
      debugPrint('[ApiService] 알 수 없는 응답 (HTTP ${response.statusCode}): $body');
      return _failure('parse_failed');
    }

    if (decoded['ok'] == true) {
      decoded['braille'] = _normalizeBraille(decoded['braille']);
    }
    return decoded;
  }

  /// JSON 을 풀면 배열이 `List<dynamic>` 으로 나오므로 더미 데이터와 같은 타입으로 맞춘다.
  /// 라벨 → `List<int>`, 책 → `List<List<int>>`. 형태가 예상과 다르면 손대지 않는다.
  static Object? _normalizeBraille(Object? braille) {
    if (braille is! List) return braille;
    if (braille.every((cell) => cell is int)) return List<int>.from(braille);
    if (braille.every((line) => line is List && line.every((cell) => cell is int))) {
      return [for (final line in braille) List<int>.from(line as List)];
    }
    return braille;
  }

  static Map<String, dynamic> _failure(String error) => {'ok': false, 'error': error};
}

// ─────────────────────────────────────────────
// 목 데이터 (kUseMock 이 true 일 때만 쓰인다)
//
// 서버의 POST /process-mock 응답을 그대로 옮겨 놓은 값이다.
// 손으로 적은 것이 아니라 서버 응답에서 뽑아냈으므로,
// 더미 모드와 실서버를 오가도 화면 코드는 그대로 동작한다.
//
// 서버 쪽 원본: server/main.py 의 MOCK_RESULTS
// ─────────────────────────────────────────────

/// 라벨 모드 더미 결과. braille 은 1차원.
const Map<String, dynamic> _kMockLabelResult = {
  'ok': true,
  'text': '우유',
  'reason': '흰색 종이팩에 우유 표기가 있습니다.',
  'braille': <int>[13, 41],
  'braille_preview': '⠍⠩',
  'cell_count': 2,
};

/// 책 모드 더미 결과. braille 은 줄 단위 2차원 (18줄, 한 줄 최대 30셀).
const Map<String, dynamic> _kMockBookResult = {
  'ok': true,
  'text': '점자는 손끝으로 읽는 문자이다. 여섯 개의 점을 일정한 규칙에 따라 조합하여 글자를 나타내며, '
      '가로 두 칸 세로 세 칸으로 이루어진 직사각형 안에 점을 찍어 표현한다.\n'
      '한글 점자는 초성과 중성과 종성을 각각 다른 점형으로 적기 때문에 한 글자가 여러 칸을 차지하는 경우가 많다.\n'
      '시각장애인은 이 점자를 통해 책을 읽고 글을 쓴다. 점자를 익히면 스스로 정보를 얻을 수 있고, '
      '학습과 일상생활에서 다른 사람의 도움에 의존하지 않아도 된다.',
  'reason': 'OCR로 텍스트를 인식했습니다.',
  'braille': <List<int>>[
    [40, 14, 34, 40, 35, 9, 42, 18, 0, 32, 37, 18, 32, 8, 42, 38, 42, 16, 37, 0,
        21, 2, 1, 9, 42, 18],
    [17, 13, 18, 40, 35, 21, 10, 35, 0, 49, 32, 14, 4, 0, 8, 23, 58, 0, 40, 14,
        34, 42, 2],
    [21, 2, 40, 14, 54, 26, 35, 18, 0, 8, 41, 48, 21, 1, 29, 0, 32, 10, 35, 16,
        35, 0, 40, 37, 26, 35, 3, 26, 35, 49],
    [8, 42, 2, 40, 35, 16, 42, 2, 0, 9, 35, 19, 35, 9, 23, 17, 49, 0, 8, 35, 16,
        37, 0, 10, 13, 0, 11, 35, 18],
    [32, 29, 16, 37, 0, 32, 29, 0, 11, 35, 18, 42, 16, 37, 0, 21, 16, 13, 14,
        40, 21, 18],
    [40, 21, 1, 32, 35, 8, 35, 1, 26, 49, 54, 0, 35, 18, 29, 0, 40, 14, 34, 42,
        2, 0, 32, 40, 21, 1, 14],
    [25, 44, 26, 49, 18, 26, 35, 18, 10, 35],
    [26, 35, 18, 8, 42, 2, 0, 40, 14, 34, 40, 35, 9, 42, 18, 0, 48, 37, 32, 14,
        54, 8, 39],
    [40, 13, 54, 32, 14, 54, 8, 39, 0, 40, 37, 54, 32, 14, 54, 42, 2, 0, 8, 35,
        1, 8, 35, 1, 0, 10, 35, 16, 42, 18],
    [40, 14, 34, 26, 49, 54, 42, 16, 37, 0, 40, 14, 1, 8, 21, 0, 32, 10, 23, 17,
        13, 18, 29, 0, 26, 35, 18],
    [8, 42, 2, 40, 35, 8, 35, 0, 49, 16, 14, 0, 11, 35, 18, 42, 2, 0, 48, 35,
        40, 21, 26, 35, 9, 42, 18],
    [8, 49, 54, 13, 8, 35, 0, 17, 35, 18, 52, 10, 35],
    [32, 21, 8, 35, 1, 40, 35, 54, 23, 21, 18, 42, 18, 0, 21, 0, 40, 14, 34, 40,
        35, 16, 42, 2, 0, 19, 37, 54, 26, 23],
    [48, 23, 1, 42, 2, 0, 21, 2, 1, 8, 37, 0, 8, 42, 2, 42, 2, 0, 32, 32, 42,
        18, 10, 35],
    [40, 14, 34, 40, 35, 16, 42, 2, 0, 21, 1, 26, 21, 17, 49, 18, 0, 32, 42, 32,
        42, 16, 37],
    [40, 14, 54, 24, 37, 16, 42, 2, 0, 14, 20, 42, 2, 0, 32, 13, 0, 21, 12, 8,
        37, 0, 26, 35, 1, 32, 42, 3, 8, 39],
    [21, 2, 32, 35, 54, 32, 23, 54, 26, 39, 2, 29, 32, 14, 0, 10, 35, 16, 42,
        18, 0, 32, 35, 16, 35, 34, 58],
    [10, 37, 13, 34, 29, 0, 58, 40, 37, 18, 26, 35, 40, 21, 0, 35, 18, 52, 35,
        10, 37, 0, 10, 61, 18, 10, 35],
  ],
  'braille_preview': '⠨⠎⠢⠨⠣⠉⠪⠒⠀⠠⠥⠒⠠⠈⠪⠦⠪⠐⠥⠀⠕⠂⠁⠉⠪⠒\n'
      '⠑⠍⠒⠨⠣⠕⠊⠣⠀⠱⠠⠎⠄⠀⠈⠗⠺⠀⠨⠎⠢⠪⠂\n'
      '⠕⠂⠨⠎⠶⠚⠣⠒⠀⠈⠩⠰⠕⠁⠝⠀⠠⠊⠣⠐⠣⠀⠨⠥⠚⠣⠃⠚⠣⠱\n'
      '⠈⠪⠂⠨⠣⠐⠪⠂⠀⠉⠣⠓⠣⠉⠗⠑⠱⠀⠈⠣⠐⠥⠀⠊⠍⠀⠋⠣⠒\n'
      '⠠⠝⠐⠥⠀⠠⠝⠀⠋⠣⠒⠪⠐⠥⠀⠕⠐⠍⠎⠨⠕⠒\n'
      '⠨⠕⠁⠠⠣⠈⠣⠁⠚⠱⠶⠀⠣⠒⠝⠀⠨⠎⠢⠪⠂⠀⠠⠨⠕⠁⠎\n'
      '⠙⠬⠚⠱⠒⠚⠣⠒⠊⠣\n'
      '⠚⠣⠒⠈⠪⠂⠀⠨⠎⠢⠨⠣⠉⠪⠒⠀⠰⠥⠠⠎⠶⠈⠧\n'
      '⠨⠍⠶⠠⠎⠶⠈⠧⠀⠨⠥⠶⠠⠎⠶⠪⠂⠀⠈⠣⠁⠈⠣⠁⠀⠊⠣⠐⠪⠒\n'
      '⠨⠎⠢⠚⠱⠶⠪⠐⠥⠀⠨⠎⠁⠈⠕⠀⠠⠊⠗⠑⠍⠒⠝⠀⠚⠣⠒\n'
      '⠈⠪⠂⠨⠣⠈⠣⠀⠱⠐⠎⠀⠋⠣⠒⠪⠂⠀⠰⠣⠨⠕⠚⠣⠉⠪⠒\n'
      '⠈⠱⠶⠍⠈⠣⠀⠑⠣⠒⠴⠊⠣\n'
      '⠠⠕⠈⠣⠁⠨⠣⠶⠗⠕⠒⠪⠒⠀⠕⠀⠨⠎⠢⠨⠣⠐⠪⠂⠀⠓⠥⠶⠚⠗\n'
      '⠰⠗⠁⠪⠂⠀⠕⠂⠁⠈⠥⠀⠈⠪⠂⠪⠂⠀⠠⠠⠪⠒⠊⠣\n'
      '⠨⠎⠢⠨⠣⠐⠪⠂⠀⠕⠁⠚⠕⠑⠱⠒⠀⠠⠪⠠⠪⠐⠥\n'
      '⠨⠎⠶⠘⠥⠐⠪⠂⠀⠎⠔⠪⠂⠀⠠⠍⠀⠕⠌⠈⠥⠀⠚⠣⠁⠠⠪⠃⠈⠧\n'
      '⠕⠂⠠⠣⠶⠠⠗⠶⠚⠧⠂⠝⠠⠎⠀⠊⠣⠐⠪⠒⠀⠠⠣⠐⠣⠢⠺\n'
      '⠊⠥⠍⠢⠝⠀⠺⠨⠥⠒⠚⠣⠨⠕⠀⠣⠒⠴⠣⠊⠥⠀⠊⠽⠒⠊⠣',
  'cell_count': 448,
};
