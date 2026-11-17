// 서버 주소 정하기
//
// 앱이 요청을 보낼 서버 주소를 아래 순서로 정한다 (app/README.md "서버 주소").
//   1) 빌드할 때 넣은 값: --dart-define=SERVER_URL=http://...
//   2) 웹(브라우저)에서 열렸을 때
//      - 개발용 Chrome(flutter run -d chrome): 페이지가 localhost/127.0.0.1 이고 포트가 8000 이 아님
//        → http://localhost:8000
//      - 그 밖: 페이지를 띄운 서버 그대로. 서버가 앱 화면도 띄워 준 경우다 (server/main.py 맨 끝)
//   3) 그 밖(휴대폰 앱): constants.dart 의 kServerBaseUrl

import 'package:flutter/foundation.dart';

import '../constants.dart';

/// 빌드할 때 넣은 서버 주소. 넣지 않았으면 빈 문자열.
const String _kDefinedServerUrl = String.fromEnvironment('SERVER_URL');

/// 앱이 실제로 쓰는 서버 주소. 앱이 켜져 있는 동안 바뀌지 않으므로 처음 쓸 때 한 번만 정한다.
final String serverBaseUrl = _resolveAppServerUrl();

String _resolveAppServerUrl() {
  if (_kDefinedServerUrl.trim().isNotEmpty && normalizeServerUrl(_kDefinedServerUrl) == null) {
    debugPrint('[ServerConfig] SERVER_URL 형식이 맞지 않아 무시합니다: $_kDefinedServerUrl');
  }
  final url = resolveServerUrl(
    defined: _kDefinedServerUrl,
    isWeb: kIsWeb,
    pageUri: Uri.base,
  );
  debugPrint('[ServerConfig] 서버 주소: $url');
  return url;
}

/// 사람이 적은 서버 주소를 `http://호스트:포트` 모양으로 다듬는다. 주소로 읽을 수 없으면 null.
///
///   192.168.43.10                      → http://192.168.43.10:8000   (http:// 와 :8000 을 붙임)
///   192.168.43.10:9000                 → http://192.168.43.10:9000
///   http://192.168.43.10:8000/health   → http://192.168.43.10:8000   (뒤의 / 와 경로는 버림)
///   빈칸, 띄어쓰기, 잘못된 포트, http·https 가 아닌 주소 → null
String? normalizeServerUrl(String input) {
  var text = input.trim();
  if (text.isEmpty || RegExp(r'\s').hasMatch(text)) return null;
  if (!text.contains('://')) text = 'http://$text';

  final uri = Uri.tryParse(text);
  if (uri == null || (uri.scheme != 'http' && uri.scheme != 'https') || uri.host.isEmpty) {
    return null;
  }

  // Uri 는 http 의 :80, https 의 :443 을 지워 버리므로 포트는 적힌 글자에서 직접 읽는다
  final authority = text.substring(text.indexOf('://') + 3).split(RegExp(r'[/?#]')).first;
  final portText = RegExp(r':(\d+)$').firstMatch(authority)?.group(1);
  final port = portText == null ? kServerPort : int.tryParse(portText);
  if (port == null || port < 1 || port > 65535) return null;

  return '${uri.scheme}://${uri.host}:$port';
}

/// 위 1)~3) 순서로 서버 주소를 고른다.
///
/// [defined]  빌드할 때 넣은 값 (없으면 빈 문자열). 형식이 틀리면 무시하고 다음 규칙으로 넘어간다
/// [isWeb]    브라우저에서 돌고 있는지 (앱에서는 kIsWeb)
/// [pageUri]  브라우저 주소창의 주소 (앱에서는 Uri.base)
/// [fallback] 휴대폰 앱의 기본 주소
String resolveServerUrl({
  required String defined,
  required bool isWeb,
  required Uri pageUri,
  String fallback = kServerBaseUrl,
}) {
  final fromDefine = normalizeServerUrl(defined);
  if (fromDefine != null) return fromDefine;

  if (isWeb && (pageUri.scheme == 'http' || pageUri.scheme == 'https')) {
    final isLocalPage = pageUri.host == 'localhost' || pageUri.host == '127.0.0.1';
    if (isLocalPage && pageUri.port != kServerPort) return 'http://localhost:$kServerPort';
    return pageUri.origin;
  }

  return fallback;
}
