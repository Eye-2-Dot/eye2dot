#!/usr/bin/env python3
"""가상 Mega 와 가상 ESP32 를 가상 UART 로 이어 한 작업을 끝까지 돌려 보는 시험.
   사용:  python3 run_sim.py            (정상 흐름)
          python3 run_sim.py drop        (바이트 1% 를 일부러 깨뜨림 -> 재전송 확인)
          python3 run_sim.py noswitch    (원점 스위치 고장 -> 오류 처리 확인)
          python3 run_sim.py megareset   (타각 도중 Mega 가 다시 시작됨 -> ESP32 가 끝없이 기다리지 않는지)
          python3 run_sim.py stop        (타각 도중 버튼 -> 그 자리에 멈추고 취소로 보고)"""
import os, socket, subprocess, sys
mode = sys.argv[1] if len(sys.argv) > 1 else "ok"
secs = {"ok": 44, "drop": 50, "noswitch": 26, "megareset": 22, "stop": 18}[mode]
STRIKES = 2          # solenoid.h 의 SOL_STRIKES (한 칸을 누르는 횟수)
a, b = socket.socketpair()
def spawn(exe, sock, log, extra):
    def pre():
        if sock.fileno() != 3:
            os.dup2(sock.fileno(), 3)
        os.set_inheritable(3, True)
    env = dict(os.environ, SIM_SECS=str(secs), PUNCH_LOG="punch.log", **extra)
    return subprocess.Popen([exe], stderr=open(log, "w"), preexec_fn=pre, env=env, close_fds=False)
extra = {"drop": {"DROP": "0.01"}, "noswitch": {"NO_SWITCH": "1"},
         "megareset": {"RESET_AT": "7"}, "stop": {"PRESS_AT": "7"}}.get(mode, {})
open("punch.log", "w").close()
pm = spawn("./sim_mega", a, "mega.log", extra)
pe = spawn("./sim_esp32", b, "esp32.log", extra)
pm.wait(); pe.wait()
mega = open("mega.log", encoding="utf-8").read(); esp = open("esp32.log", encoding="utf-8").read()
strikes = [tuple(float(v) for v in l.split()) for l in open("punch.log") if l.strip()]
# 같은 자리를 연달아 누른 것은 한 칸으로 묶는다 -> (X, Y, 점 값, 누른 횟수)
punch = []
for s in strikes:
    if punch and abs(punch[-1][0] - s[0]) < 0.01 and abs(punch[-1][1] - s[1]) < 0.01 and punch[-1][2] == s[2]:
        punch[-1] = punch[-1][:3] + (punch[-1][3] + 1,)
    else:
        punch.append(s + (1,))
print("===== ESP32 화면 ====="); print(esp)
print("===== Mega 화면 (상태 줄만) ====="); print("\n".join(l for l in mega.splitlines() if l.startswith("[") and "초]" not in l or "버튼" in l))
print("===== 찍힌 자리 (X mm, Y mm, 점 값, 누른 횟수) ====="); [print("  ", p) for p in punch]
ok = True
def check(name, cond):
    global ok; ok &= bool(cond); print(("통과  " if cond else "실패  ") + name)
if mode in ("ok", "drop"):
    exp = [(8, 8, 1), (20, 8, 63), (8, 28, 5), (20, 248, 7), (8, 8, 9), (8, 18, 63), (14, 18, 63)]
    check("찍힌 칸 수 7", len(punch) == len(exp))
    check("찍힌 자리·점 값이 기대와 같음 (오차 0.05mm 이내)",
          len(punch) == len(exp) and all(abs(p[0]-e[0]) < 0.05 and abs(p[1]-e[1]) < 0.05 and int(p[2]) == e[2] for p, e in zip(punch, exp)))
    check("칸마다 %d번씩 누름 (팀 결정: 두 번 타각)" % STRIKES, all(p[3] == STRIKES for p in punch))
    check("점자 배열(braille)을 골랐고 글자 속 대괄호에 속지 않음", '점자 배열 이름: "braille"' in esp and "줄 수: 27" in esp)
    check("2쪽으로 나눔", "쪽 수: 2" in esp)
    check("둘째 장은 버튼을 기다림", "버튼을 누르세요" in mega)
    check("서버가 완료 보고를 받아들임 (본문에 device·ok·error)", "/jobs/7cabc156cc10/done" in esp and '"device":"fw2","ok":true,"error":""' in esp and "-> 200" in esp and "작업 완료" in esp)
elif mode == "noswitch":
    check("전원을 켜도 바로 움직이지 않음 (작업이 올 때까지 대기)", "[상태] 대기" in mega.split("새 작업")[0] if "새 작업" in mega else "[상태] 대기" in mega)
    check("원점 실패를 오류로 처리", "[오류] 코드 1" in mega)
    check("아무것도 찍지 않음", len(punch) == 0)
    check("서버에 실패로 보고", '"ok":false,"error":"print_failed"' in esp and "작업 실패" in esp)
elif mode == "megareset":
    check("Mega 가 다시 시작됨", "다시 시작됨" in mega)
    check("ESP32 가 알아채고 기다림을 끝냄", "Mega 가 도중에 다시 시작됨" in esp)
    check("서버에 실패로 보고", '"ok":false,"error":"print_failed"' in esp and "작업 실패" in esp)
elif mode == "stop":
    check("버튼으로 멈춤", "[멈춤] 버튼" in mega)
    check("끝까지 찍지 않음", len(punch) < 7)
    check("멈춘 뒤 대기로 돌아가고 다시 움직이지 않음", mega.rstrip().split("[멈춤] 버튼")[-1].count("[상태]") == 1 and "[상태] 대기" in mega.split("[멈춤] 버튼")[-1])
    check("서버에 취소로 보고", '"ok":false,"error":"cancelled"' in esp and "작업 실패" in esp)
print("\n결과:", "모두 통과" if ok else "실패 있음"); sys.exit(0 if ok else 1)
