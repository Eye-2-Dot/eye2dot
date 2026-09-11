// ─────────────────────────────────────────────
// 개발 확인용 임시 진입점 — 커밋하지 말 것
//
// api_service.dart 는 부품이라 혼자서는 실행되지 않는다.
// 세연의 카메라 화면이 완성되기 전에 서버 통신이 제대로 되는지
// 혼자 확인해 보려고 만든 임시 화면이다.
//
// 실행:
//   cd app
//   flutter run -d chrome -t lib/dev_api_check.dart
//
// 확인이 끝나면 이 파일은 지운다. (PR 에 포함시키지 않는다)
// ─────────────────────────────────────────────

import 'dart:typed_data';

import 'package:flutter/material.dart';

import 'constants.dart';
import 'services/api_service.dart';

void main() => runApp(const DevApiCheckApp());

class DevApiCheckApp extends StatelessWidget {
  const DevApiCheckApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'API 점검',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(useMaterial3: true),
      home: const DevApiCheckPage(),
    );
  }
}

class DevApiCheckPage extends StatefulWidget {
  const DevApiCheckPage({super.key});

  @override
  State<DevApiCheckPage> createState() => _DevApiCheckPageState();
}

class _DevApiCheckPageState extends State<DevApiCheckPage> {
  String _log = '버튼을 눌러 확인하세요.';
  bool _busy = false;

  /// 버튼 하나를 눌렀을 때의 공통 처리. 로딩 표시를 켜고 끈다.
  Future<void> _run(Future<String> Function() task) async {
    setState(() {
      _busy = true;
      _log = '요청 중...';
    });
    final text = await task();
    if (!mounted) return;
    setState(() {
      _busy = false;
      _log = text;
    });
  }

  // ── 1. 서버가 켜져 있는지 ──

  Future<String> _checkHealth() async {
    final ok = await ApiService.checkHealth();
    if (ok) {
      return '서버 연결 성공\n\n주소: $kServerBaseUrl';
    }
    return '서버에 연결할 수 없습니다.\n\n'
        '주소: $kServerBaseUrl\n\n'
        '확인할 것\n'
        '  · uvicorn 이 켜져 있는가\n'
        '  · 포트 번호가 맞는가\n'
        '  · 브라우저 개발자도구 콘솔에 CORS 메시지가 있는가';
  }

  // ── 2. 사진 전송 ──

  Future<String> _send(String mode) async {
    // 목 서버는 사진 내용을 보지 않으므로 아무 바이트나 보내도 된다.
    // 실서버로 바꾼 뒤에는 이 버튼 대신 진짜 사진으로 확인해야 한다.
    final dummy = Uint8List.fromList(List<int>.filled(64, 7));

    final sw = Stopwatch()..start();
    final r = await ApiService.sendImage(bytes: dummy, mode: mode);
    sw.stop();

    final out = StringBuffer()
      ..writeln('모드: $mode')
      ..writeln('걸린 시간: ${sw.elapsedMilliseconds}ms')
      ..writeln('ok: ${r['ok']}')
      ..writeln('');

    if (r['ok'] != true) {
      out
        ..writeln('error : ${r['error']}')
        ..writeln('reason: ${r['reason']}');
      return out.toString();
    }

    final text = r['text'] as String;
    final lines = brailleLines(r);

    out
      ..writeln('text (${text.length}자)')
      ..writeln(text.length > 80 ? '${text.substring(0, 80)}...' : text)
      ..writeln('')
      ..writeln('cell_count : ${r['cell_count']}')
      ..writeln('braille 타입: ${r['braille'].runtimeType}')
      ..writeln('braille 줄 수: ${lines.length}')
      ..writeln('셀 수 합계  : ${lines.fold<int>(0, (a, l) => a + l.length)}')
      ..writeln('');

    // 직접 계산한 점자 문자와 서버가 준 미리보기를 비교한다.
    // 두 줄이 같으면 인코딩이 서로 맞다는 뜻이다.
    if (lines.isNotEmpty) {
      final mine = brailleToUnicode(lines.first);
      final fromServer = (r['braille_preview'] as String).split('\n').first;
      out
        ..writeln('첫 줄 검증')
        ..writeln('  내 계산: $mine')
        ..writeln('  서버 값: $fromServer')
        ..writeln('  일치 여부: ${mine == fromServer ? "일치" : "불일치 — 확인 필요"}');
    }

    return out.toString();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('API 점검 (임시)')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                FilledButton(
                  onPressed: _busy ? null : () => _run(_checkHealth),
                  child: const Text('1. 서버 연결 확인'),
                ),
                FilledButton.tonal(
                  onPressed: _busy ? null : () => _run(() => _send(kModeLabel)),
                  child: const Text('2. 라벨 모드 전송'),
                ),
                FilledButton.tonal(
                  onPressed: _busy ? null : () => _run(() => _send(kModeBook)),
                  child: const Text('3. 책 모드 전송'),
                ),
                OutlinedButton(
                  onPressed: _busy
                      ? null
                      : () => _run(() => _send('wrong_mode')),
                  child: const Text('4. 잘못된 모드 (실패 확인)'),
                ),
              ],
            ),
            const SizedBox(height: 16),
            if (_busy) const LinearProgressIndicator(),
            const SizedBox(height: 16),
            Expanded(
              child: Container(
                width: double.infinity,
                padding: const EdgeInsets.all(14),
                decoration: BoxDecoration(
                  color: const Color(0xFFF5F4F1),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: SingleChildScrollView(
                  child: SelectableText(
                    _log,
                    style: const TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 13,
                      height: 1.5,
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
