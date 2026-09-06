import 'dart:typed_data';

/// 서버 통신 담당 클래스
///
/// 지금은 껍데기 상태로, 서버에 연결하지 않고 고정된 더미 결과만 돌려준다.
/// 실제 통신 코드는 D가 나중에 채워 넣을 예정.
///
/// 주의: Chrome(웹)에서 실행하므로 dart:io 의 File 은 절대 쓰지 말 것.
/// 이미지는 항상 Uint8List(바이트 배열) 형태로 주고받는다.
class ApiService {
  /// 이미지를 서버로 보내고 인식 + 점역 결과를 받아온다.
  ///
  /// [bytes] 촬영한 이미지의 원본 바이트
  /// [mode]  kModeLabel(사물 라벨) 또는 kModeBook(책 점역)
  ///
  /// ── 실제 서버 응답 형태 (D 참고용, server/main.py 기준) ──
  ///
  /// 성공 시:
  /// {
  ///   "ok": true,
  ///   "text": "우유",                  // 인식된 텍스트
  ///   "reason": "판단 근거 1문장",
  ///   "braille": [[1,0,0,0,0,0], ...], // 솔레노이드 구동용 점자 셀 목록
  ///   "braille_preview": "⠕⠥",         // 사람이 눈으로 확인하는 용도
  ///   "cell_count": 2
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
  /// 보낼 곳: POST {kServerBaseUrl}/process?mode={mode}
  ///         Content-Type: image/jpeg, Body: 이미지 바이트 그대로
  static Future<Map<String, dynamic>> sendImage({
    required Uint8List bytes,
    required String mode,
  }) async {
    // TODO(D): 아래 더미 응답을 실제 서버 통신(http 패키지)으로 교체할 것.
    //          bytes 와 mode 는 그때 사용된다. 지금은 받기만 하고 쓰지 않는다.

    // 서버 응답을 기다리는 느낌을 내기 위한 임시 지연.
    // 화면 쪽에서 로딩 표시를 붙여 볼 수 있도록 남겨 둔다.
    await Future.delayed(const Duration(seconds: 1));

    return {'ok': true, 'text': '우유'};
  }
}
