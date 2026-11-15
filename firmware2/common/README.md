# common

펌웨어 2 의 ESP32 · Mega 공통 약속. [`protocol.h`](protocol.h) 한 파일을 두 칩이 같이 쓴다 (프레임 모양·종류, 상태·오류 코드, 한 장 25줄 × 30칸).
고치면 `mega` 와 `esp` 를 모두 다시 올린다. 펌웨어 1 의 `firmware1/common/protocol.h` 와는 다른 파일이다.
