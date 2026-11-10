# 펌웨어 2 — 책 점역기

펌웨어 1(라벨 출력기)과 같은 구조·같은 이름으로 만든다 (11/9 회의).

```
firmware2/
├── common/   ESP32 · Mega 가 같이 쓰는 약속 (통신 규칙 등) — 아직 없음
├── esp/      ESP32 쪽 코드 — 아직 없음
├── mega/     Arduino Mega 쪽 코드 (PlatformIO: VS Code 에서 이 mega 폴더를 연다)
│   ├── include/   .h  = 함수 "선언" (이름·쓰는 법만)
│   │   ├── pins.h        핀 번호 (초안 — 솔레노이드만 확정)
│   │   └── solenoid.h    솔레노이드 시간 값·함수 선언
│   └── src/       .cpp = 함수 "정의"(실제 내용)와 호출
│       ├── main.cpp      setup()/loop(). 지금은 솔레노이드 시리얼 시험
│       └── solenoid.cpp  6점 동시 · 두 번 타각 · 과열 방지
├── tests/    부품 하나씩 시험하는 PlatformIO 프로젝트 (펌웨어 1 tests 와 같은 방식)
│   ├── _shared/mega_test.h   시험 공용 (솔레노이드 끄기, 한 줄 입력 받기)
│   └── solenoid_test/        솔레노이드 6개 시험 (VS Code 에서 이 폴더를 연다)
└── tools/
    └── solenoid_mask_test/  Arduino IDE 로 바로 올리는 솔레노이드 시험 스케치
```

- 스텝 모터는 `mega/include/step.h` + `mega/src/step.cpp` 로 (스텝 모터 담당).
- 솔레노이드 배선·핀은 펌웨어 1 과 같다: D22~D27 → L298N #1 IN1~4, #2 IN1~2, 솔레노이드 다른 쪽은 GND.
- 칸 값은 서버가 거울로 바꿔 준 6비트 값을 **뒤집지 않고 그대로** 쓴다 (bit0 = 위에서 볼 때 왼쪽 위 = 1번).
- 업로드할 때는 배터리를 빼고 USB 만. 올린 뒤 배터리를 연결한다.
