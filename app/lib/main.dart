/* import 'package:app/screens/result_screen.dart';*/
import 'package:flutter/material.dart';

import 'screens/mode_screen.dart';

void main() {
  runApp(const Eye2DotApp());
}

/// 앱의 루트 위젯
///
/// 첫 화면은 모드를 고르는 ModeScreen 이다.
class Eye2DotApp extends StatelessWidget {
  const Eye2DotApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Eye 2 Dot',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.indigo),
        useMaterial3: true,
      ),
      home: const ModeScreen(),
    );
  }
}
