# PC 가상 시험

기기 없이 PC(리눅스·WSL·맥)에서 두 칩의 코드를 그대로 컴파일해 가상 UART 로 잇고, 한 작업을 끝까지 돌려 봅니다.
가짜 Arduino(`mock/`)가 모터·원점 스위치·솔레노이드·버튼·서버를 흉내 냅니다.

```
g++ -std=c++17 -O1 -Imock -I../../common -I../../mega/include  ../../mega/src/*.cpp  sim_common.cpp sim_mega.cpp  -o sim_mega
g++ -std=c++17 -O1 -Imock -I../../common -I../../esp/include ../../esp/src/*.cpp sim_common.cpp sim_esp32.cpp -o sim_esp32
g++ -std=c++17 -I../../common -I../../esp/include test_json.cpp -o test_json && ./test_json
python3 run_sim.py            # 정상 흐름: 27줄 -> 2쪽, 찍힌 자리 확인, 완료 보고
python3 run_sim.py drop       # 바이트 1% 손상 -> 재전송으로 끝까지 가는지
python3 run_sim.py noswitch   # 원점 스위치 고장 -> 오류로 멈추는지
```

코드를 고친 뒤 이 세 가지가 "모두 통과"로 나오는지 먼저 보고 기기에 올리면 됩니다.
