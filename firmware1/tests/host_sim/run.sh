#!/usr/bin/env bash
# 펌웨어 1 (ATmega 쪽) PC 모의 시험 — 보드 없이 타각 순서를 확인한다
#   bash run.sh          (g++ 만 있으면 된다. 윈도우는 WSL 이나 Git Bash + MinGW)
set -e
cd "$(dirname "$0")"
OUT=build
mkdir -p "$OUT"

SRC=../../mega/src
FLAGS="-std=gnu++11 -O1 -w -fpermissive -I ../../common -I ."
FILES="$SRC/main.cpp $SRC/job.cpp $SRC/link.cpp $SRC/button.cpp $SRC/console.cpp
       $SRC/punch.cpp $SRC/solenoid.cpp $SRC/stepper.cpp sim_machine.cpp sim_main.cpp"

# build <이름> <motion.h 를 바꿔 치울 sed 식 (없으면 그대로)>
build() {
  local name=$1 edit=$2 inc="-I ../../mega/include"
  if [ -n "$edit" ]; then
    mkdir -p "$OUT/$name"
    sed -E "$edit" ../../mega/include/motion.h > "$OUT/$name/motion.h"
    inc="-I $OUT/$name -I ../../mega/include"
  fi
  # 가짜 Arduino.h 가 먼저 잡히도록 mock 을 맨 앞에
  g++ $FLAGS -I mock $inc $FILES -o "$OUT/sim_$name"
}

echo "컴파일 중…"
build default ""
build homeleft 's/(HEAD_HOME_AT_RIGHT *= *)true/\1false/'
build sensor   's/(CARD_SENSOR_INSTALLED *= *)false/\1true/'

fail=0
run() { "$OUT/sim_$1" "$2" || fail=1; }

run default label      # 평소: 작업 두 개
run default wrap       # 타각 도중 micros() 가 넘어가도 괜찮은지
run default worst      # 가장 오래 걸리는 경우의 시간
run default noswitch   # 원점 스위치 고장 ①
run default stuck      # 원점 스위치 고장 ②
run default badjob     # 카드에 안 들어가는 점자
run default console    # USB 시험 명령
run default stopbtn    # 움직이는 도중 버튼으로 멈추기
run default stopusb    # 움직이는 도중 USB 로 멈추기
run default stopcap    # 촬영 버튼으로 멈춘 뒤 그 버튼을 계속 누르고 있음
run default stopcon    # USB 명령으로 움직이다 촬영 버튼으로 멈춘 뒤 계속 누르고 있음
echo; echo "##### 원점 스위치가 왼쪽 끝일 때 (HEAD_HOME_AT_RIGHT = false) #####"
run homeleft label
echo; echo "##### 카드 감지 센서를 달았을 때 (CARD_SENSOR_INSTALLED = true) #####"
run sensor label
run sensor nocard

echo
if [ $fail -eq 0 ]; then echo "전체 결과: 모두 통과"; else echo "전체 결과: 실패 있음"; fi
exit $fail
