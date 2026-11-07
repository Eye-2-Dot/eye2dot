"""
기기(펌웨어 1·2)로 보내는 점자를 거울 모양으로 바꾸는 모듈

── 팀 약속 (2026-11-07) ─────────────────────────────────────────
- 펌웨어 1·2 모두 솔레노이드가 종이·라벨을 **위에서 아래로** 찍는다.
  점은 아랫면으로 튀어나오므로, 사람은 종이·라벨을 **좌우로 뒤집어** 읽는다.
- 기계는 줄의 첫 칸(배열 0번)을 위에서 볼 때 **오른쪽 끝**에 찍고 왼쪽으로 진행한다.
  줄은 위에서 아래로 찍는다.
  → 뒤집어 읽으면 첫 칸이 왼쪽 끝에 오므로 칸 순서·줄 순서는 그대로 두면 된다.
- 솔레노이드 번호는 위에서 볼 때 **왼쪽 위가 1번**이다 (1·2·3 왼쪽 열, 4·5·6 오른쪽 열).
  → 한 칸 안에서는 좌우가 바뀌어 보이므로, 점 1↔4, 2↔5, 3↔6 을 바꿔서 보낸다.

그래서 서버는 **칸 안의 좌우 열만** 바꾼다.
    mirrored = ((m & 0b000111) << 3) | ((m >> 3) & 0b000111)
- 칸 순서·줄 순서는 바꾸지 않는다. 빈 칸(0)으로 채우지도 않는다.
- 앱으로 가는 응답(braille, braille_preview)은 원래 모양 그대로다. 기기로 가는 것만 거울이다.
- 펌웨어(ESP32·ATmega)는 받은 값을 뒤집지 않고 그대로 솔레노이드 비트로 쓴다
  (bit0 = 위에서 볼 때 왼쪽 위 솔레노이드 … bit5 = 오른쪽 아래).

점 번호 배치 (읽는 면에서 볼 때 = braille.py 와 같음):
    1 · · 4
    2 · · 5
    3 · · 6

공개 함수:
    mirror_cell(m)          : 칸 하나 (0~63) → 거울 칸
    mirror_cells(cells)     : 한 줄 (라벨) → 칸마다 거울, 순서는 그대로
    mirror_lines(lines)     : 여러 줄 (책) → 줄·칸 순서는 그대로, 칸마다 거울
    mirror_braille(braille) : 1차원이면 mirror_cells, 2차원이면 mirror_lines

자체 시험: python mirror.py
"""

LEFT_COLUMN = 0b000111    # 점 1·2·3
RIGHT_COLUMN = 0b111000   # 점 4·5·6


def mirror_cell(m):
    """점자 칸 하나의 좌우 열을 바꾼다 (점 1↔4, 2↔5, 3↔6).

    m 은 0~63 정수여야 한다. 아니면 ValueError (bool 도 받지 않는다).
    예: mirror_cell(35) → 28   (점 1·2·6 → 점 4·5·3)
    """
    # bool 은 int 의 하위 클래스라 따로 막는다 (True 가 1 로 찍히는 것 방지)
    if isinstance(m, bool) or not isinstance(m, int) or not 0 <= m <= 63:
        raise ValueError(f"점자 칸은 0~63 정수여야 합니다 (받은 값: {m!r})")
    return ((m & LEFT_COLUMN) << 3) | ((m & RIGHT_COLUMN) >> 3)


def mirror_cells(cells):
    """한 줄(1차원 칸 배열)을 칸마다 거울로. 칸 순서는 바꾸지 않는다."""
    return [mirror_cell(m) for m in cells]


def mirror_lines(lines):
    """여러 줄(2차원 배열)을 칸마다 거울로. 줄 순서·칸 순서는 바꾸지 않는다."""
    return [mirror_cells(line) for line in lines]


def mirror_braille(braille):
    """응답의 braille 값 그대로 받아 거울로. 라벨(1차원)·책(2차원) 모두 된다.

    빈 배열은 빈 배열 그대로. 칸 값이 잘못되면 ValueError.
    """
    if braille and isinstance(braille[0], list):
        return mirror_lines(braille)
    return mirror_cells(braille)


# ─────────────────────────────────────────────
# 자체 시험
# ─────────────────────────────────────────────
if __name__ == "__main__":
    import sys

    failures = []

    def check(name, got, expected):
        ok = got == expected
        print(f"[{'통과' if ok else '실패'}] {name}: {got}" + ("" if ok else f" (기대: {expected})"))
        if not ok:
            failures.append(name)

    # 팀이 정한 예시 ("사과" = ㅅ ㅏ ㄱ ㅘ)
    check("[32,35,8,39] → [4,28,1,60]", mirror_cells([32, 35, 8, 39]), [4, 28, 1, 60])

    # 경계값
    check("0 → 0 (빈 칸)", mirror_cell(0), 0)
    check("63 → 63 (6점 전부)", mirror_cell(63), 63)
    check("점1(1) → 점4(8)", mirror_cell(1), 8)
    check("점4(8) → 점1(1)", mirror_cell(8), 1)

    # 0~63 전부: 두 번 뒤집으면 원래대로, 한 번 뒤집은 값도 0~63
    twice_bad = [m for m in range(64) if mirror_cell(mirror_cell(m)) != m]
    check("0~63 두 번 뒤집으면 원래대로 (어긋난 값)", twice_bad, [])
    range_bad = [m for m in range(64) if not 0 <= mirror_cell(m) <= 63]
    check("0~63 뒤집은 값의 범위 (벗어난 값)", range_bad, [])

    # 칸·줄 순서는 그대로
    check("책: 줄·칸 순서 유지", mirror_lines([[1, 2], [8], []]), [[8, 16], [1], []])
    check("mirror_braille 1차원", mirror_braille([1, 0, 8]), [8, 0, 1])
    check("mirror_braille 2차원", mirror_braille([[1], [8, 0]]), [[8], [1, 0]])
    check("mirror_braille 빈 배열", mirror_braille([]), [])

    # 잘못된 값은 거부
    for bad in (64, -1, True, "a", 1.0, None):
        try:
            mirror_cell(bad)
            check(f"{bad!r} 거부", "통과됨", "ValueError")
        except ValueError:
            check(f"{bad!r} 거부", "ValueError", "ValueError")

    print()
    if failures:
        print(f"[자체 시험 실패] {len(failures)}개: {failures}")
        sys.exit(1)
    print("[자체 시험 통과]")
