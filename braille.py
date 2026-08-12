"""
한글 → 점자 변환 모듈

공개 함수 (main.py가 이 이름으로 import함, 변경 금지):
    text_to_braille(text)  : 문자열 → 점자 셀 배열 [[1,4], [2,3,5], ...]
    cells_to_unicode(cells): 점자 셀 배열 → 화면 확인용 유니코드 점자 문자열


점 번호 배치:
    1 · · 4
    2 · · 5
    3 · · 6

내부적으로는 점형을 "점 번호가 켜진 6비트 정수"(mask)로 다루고,
공개 API 경계에서만 [1,4] 같은 점 번호 리스트로 변환한다.
"""

import itertools

# ─────────────────────────────────────────────
# 점형 ↔ 비트마스크 변환
#
# 점 번호 n(1~6) → 비트 (n-1). 예: {1,4} → 0b001001 = 9
# 규칙(된소리표 유무, 종성 존재 여부 등) 판단이 정수 비교/OR로
# 끝나고, 유니코드 점자 변환도 offset 덧셈 한 줄로 끝나는 게 장점.
# ─────────────────────────────────────────────

def _mask(*dot_numbers):
    """점 번호들(1~6)을 받아 6비트 정수 하나로 합친다."""
    m = 0
    for n in dot_numbers:
        m |= 1 << (n - 1)
    return m


def _mask_to_dots(mask):
    """비트마스크를 오름차순 점 번호 리스트로 되돌린다. 출력 형식의 최종 변환 지점."""
    return [n for n in range(1, 7) if mask & (1 << (n - 1))]


# ─────────────────────────────────────────────
# 자모 순서 (유니코드 한글 음절 조합 공식이 요구하는 고정 순서)
# 이름은 새로 지었지만 순서 자체는 유니코드 표준이라 바꿀 수 없다.
# ─────────────────────────────────────────────

_CHOSEONG_ORDER = (
    'ㄱ', 'ㄲ', 'ㄴ', 'ㄷ', 'ㄸ', 'ㄹ', 'ㅁ', 'ㅂ', 'ㅃ', 'ㅅ',
    'ㅆ', 'ㅇ', 'ㅈ', 'ㅉ', 'ㅊ', 'ㅋ', 'ㅌ', 'ㅍ', 'ㅎ',
)

_JUNGSEONG_ORDER = (
    'ㅏ', 'ㅐ', 'ㅑ', 'ㅒ', 'ㅓ', 'ㅔ', 'ㅕ', 'ㅖ', 'ㅗ', 'ㅘ',
    'ㅙ', 'ㅚ', 'ㅛ', 'ㅜ', 'ㅝ', 'ㅞ', 'ㅟ', 'ㅠ', 'ㅡ', 'ㅢ', 'ㅣ',
)

_JONGSEONG_ORDER = (
    '', 'ㄱ', 'ㄲ', 'ㄳ', 'ㄴ', 'ㄵ', 'ㄶ', 'ㄷ', 'ㄹ', 'ㄺ', 'ㄻ',
    'ㄼ', 'ㄽ', 'ㄾ', 'ㄿ', 'ㅀ', 'ㅁ', 'ㅂ', 'ㅄ', 'ㅅ', 'ㅆ',
    'ㅇ', 'ㅈ', 'ㅊ', 'ㅋ', 'ㅌ', 'ㅍ', 'ㅎ',
)

_JUNG_COUNT = len(_JUNGSEONG_ORDER)  # 21
_JONG_COUNT = len(_JONGSEONG_ORDER)  # 28 (빈 종성 포함)


# ─────────────────────────────────────────────
# 초성 점형 (ㅇ은 어두에서 생략되므로 표에 없어도 정상 — _encode_choseong 참고)
# ─────────────────────────────────────────────
CHO_DOTS = {
    'ㄱ': _mask(4),
    'ㄴ': _mask(1, 4),
    'ㄷ': _mask(2, 4),
    'ㄹ': _mask(5),
    'ㅁ': _mask(1, 5),
    'ㅂ': _mask(4, 5),
    'ㅅ': _mask(6),
    'ㅈ': _mask(4, 6),
    'ㅊ': _mask(5, 6),
    'ㅋ': _mask(1, 2, 4),
    'ㅌ': _mask(1, 2, 5),
    'ㅍ': _mask(1, 4, 5),
    'ㅎ': _mask(2, 4, 5),
}

# 중성 점형. 값은 "셀의 나열"(튜플)로 통일한다.
# 단모음/ㅑㅕㅛㅠ류는 1칸, ㅘㅝㅢ 같은 이중모음은 2칸(구성 모음을 그대로 이어 적음)이라
# 처리 코드가 칸 수를 신경 쓰지 않고 항상 extend 할 수 있다.
#
# [검증 필요] ㅒㅖㅘㅙㅚㅝㅞㅟㅢ 9개는 기존 코드에 아예 빠져 있던 항목이라
# 국립국어원 「한국 점자 규정」 원문과 대조 확인 후 실제 하드웨어에 반영할 것.
JUNG_DOTS = {
    'ㅏ': (_mask(1, 2, 6),),
    'ㅑ': (_mask(3, 4, 5),),
    'ㅓ': (_mask(2, 3, 4),),
    'ㅕ': (_mask(1, 5, 6),),
    'ㅗ': (_mask(1, 3, 6),),
    'ㅛ': (_mask(3, 4, 6),),
    'ㅜ': (_mask(1, 3, 4),),
    'ㅠ': (_mask(1, 4, 6),),
    'ㅡ': (_mask(2, 4, 6),),
    'ㅣ': (_mask(1, 3, 5),),
    'ㅐ': (_mask(1, 2, 3, 5),),
    'ㅔ': (_mask(1, 3, 4, 5),),
    # ㅒㅖ: 규정상 ㅐㅔ 점형에 6점을 더해 나타낸다 [검증 필요]
    'ㅒ': (_mask(3, 4, 5, 6),),
    'ㅖ': (_mask(1, 3, 4, 5, 6),),
    # ㅘㅙㅚㅝㅞㅟㅢ: 규정상 구성 모음 두 칸을 그대로 이어 적는다 [검증 필요]
    'ㅘ': (_mask(1, 3, 6), _mask(1, 2, 6)),   # ㅗ + ㅏ
    'ㅙ': (_mask(1, 3, 6), _mask(1, 2, 3, 5)),  # ㅗ + ㅐ
    'ㅚ': (_mask(1, 3, 6), _mask(1, 3, 5)),   # ㅗ + ㅣ
    'ㅝ': (_mask(1, 3, 4), _mask(2, 3, 4)),   # ㅜ + ㅓ
    'ㅞ': (_mask(1, 3, 4), _mask(1, 3, 4, 5)),  # ㅜ + ㅔ
    'ㅟ': (_mask(1, 3, 4), _mask(1, 3, 5)),   # ㅜ + ㅣ
    'ㅢ': (_mask(2, 4, 6), _mask(1, 3, 5)),   # ㅡ + ㅣ
}

# 종성(받침) 점형. 겹받침은 여기 없고 JONG_SPLIT으로 낱자 분해 후 재사용한다.
JONG_DOTS = {
    'ㄱ': _mask(1),
    'ㄴ': _mask(2, 5),
    'ㄷ': _mask(3, 5),
    'ㄹ': _mask(2),
    'ㅁ': _mask(2, 6),
    'ㅂ': _mask(1, 2),
    'ㅅ': _mask(3),
    'ㅇ': _mask(2, 3, 5, 6),
    'ㅈ': _mask(1, 3),
    'ㅊ': _mask(2, 3),
    'ㅋ': _mask(2, 3, 5),
    'ㅌ': _mask(2, 3, 6),
    'ㅍ': _mask(2, 5, 6),
    'ㅎ': _mask(3, 5, 6),
    'ㅆ': _mask(3, 4),  # 받침 ㅆ은 예사소리 ㅅ과 다른 전용 점형
}

# 겹받침(및 쌍자음 받침) → 낱자 분해. 분해된 각 글자는 JONG_DOTS를 그대로 참조한다.
JONG_SPLIT = {
    'ㄲ': ('ㄱ', 'ㄱ'),
    'ㄳ': ('ㄱ', 'ㅅ'),
    'ㄵ': ('ㄴ', 'ㅈ'),
    'ㄶ': ('ㄴ', 'ㅎ'),
    'ㄺ': ('ㄹ', 'ㄱ'),
    'ㄻ': ('ㄹ', 'ㅁ'),
    'ㄼ': ('ㄹ', 'ㅂ'),
    'ㄽ': ('ㄹ', 'ㅅ'),
    'ㄾ': ('ㄹ', 'ㅌ'),
    'ㄿ': ('ㄹ', 'ㅍ'),
    'ㅀ': ('ㄹ', 'ㅎ'),
    'ㅄ': ('ㅂ', 'ㅅ'),
}

# 된소리 초성(ㄲㄸㅃㅆㅉ) → 된소리표 + 예사소리 초성
DOEN_MAP = {'ㄲ': 'ㄱ', 'ㄸ': 'ㄷ', 'ㅃ': 'ㅂ', 'ㅆ': 'ㅅ', 'ㅉ': 'ㅈ'}
DOEN_MARK_MASK = _mask(6)

# 숫자 앞에 붙는 수표 + 숫자 0~9 점형 (점자 ㄱ~ㅈ행과 동일한 점형 재사용)
NUM_MARK_MASK = _mask(3, 4, 5, 6)
NUM_DOTS = {
    '1': _mask(1), '2': _mask(1, 2), '3': _mask(1, 4),
    '4': _mask(1, 4, 5), '5': _mask(1, 5), '6': _mask(1, 2, 4),
    '7': _mask(1, 2, 4, 5), '8': _mask(1, 2, 5), '9': _mask(2, 4),
    '0': _mask(2, 4, 5),
}


# ─────────────────────────────────────────────
# 음절 분해
# ─────────────────────────────────────────────

def _split_syllable(ch):
    """완성형 한글 음절 한 글자를 (초성, 중성, 종성) 자모로 분해한다.

    한글 음절이 아니면 None.
    """
    offset = ord(ch) - 0xAC00
    if not (0 <= offset < len(_CHOSEONG_ORDER) * _JUNG_COUNT * _JONG_COUNT):
        return None
    offset, jong_i = divmod(offset, _JONG_COUNT)
    cho_i, jung_i = divmod(offset, _JUNG_COUNT)
    return _CHOSEONG_ORDER[cho_i], _JUNGSEONG_ORDER[jung_i], _JONGSEONG_ORDER[jong_i]


# ─────────────────────────────────────────────
# 자모 하나 → 점자 셀(들) 인코딩
# 초성/중성/종성을 각각 독립 함수로 분리해 규칙(된소리, 겹받침)을
# 개별적으로 검증·수정할 수 있게 한다.
# ─────────────────────────────────────────────

def _encode_choseong(cho):
    """초성 자모 하나 → 점자 셀 리스트. 어두 ㅇ은 점을 찍지 않으므로 빈 리스트."""
    if cho == 'ㅇ':
        return []
    if cho in DOEN_MAP:
        return [_mask_to_dots(DOEN_MARK_MASK), _mask_to_dots(CHO_DOTS[DOEN_MAP[cho]])]
    return [_mask_to_dots(CHO_DOTS[cho])]


def _encode_jungseong(jung):
    """중성 자모 하나 → 점자 셀 리스트 (단모음 1칸 / 이중모음 2칸)."""
    return [_mask_to_dots(m) for m in JUNG_DOTS[jung]]


def _encode_jongseong(jong):
    """종성 자모 하나(없으면 빈 문자열) → 점자 셀 리스트. 겹받침은 낱자로 분해."""
    if not jong:
        return []
    letters = JONG_SPLIT.get(jong, (jong,))
    return [_mask_to_dots(JONG_DOTS[letter]) for letter in letters]


def _encode_syllable(ch):
    """한글 음절 한 글자 → 점자 셀 리스트. 한글이 아니면 빈 리스트."""
    parts = _split_syllable(ch)
    if parts is None:
        return []
    cho, jung, jong = parts
    return _encode_choseong(cho) + _encode_jungseong(jung) + _encode_jongseong(jong)


# ─────────────────────────────────────────────
# 문자열 단위 처리
# 같은 종류(공백/숫자/한글/기타)의 연속 구간을 묶어서 처리한다.
# 수표는 숫자가 끊기지 않고 이어지는 구간마다 한 번만 붙으면 되므로,
# 이렇게 구간을 미리 묶어두면 별도의 상태 플래그 없이 처리할 수 있다.
# ─────────────────────────────────────────────

def _char_kind(ch):
    """문자 하나를 space / digit / hangul / other 중 하나로 분류."""
    if ch == ' ':
        return 'space'
    if ch.isdigit():
        return 'digit'
    if '가' <= ch <= '힣':
        return 'hangul'
    return 'other'


def _encode_digit_run(run):
    """연속된 숫자 구간 → [수표] + 숫자 점형들."""
    cells = [_mask_to_dots(NUM_MARK_MASK)]
    cells.extend(_mask_to_dots(NUM_DOTS[d]) for d in run)
    return cells


def text_to_braille(text):
    """문자열 전체 → 점자 셀 배열.

    반환 예: [[1,2,4], [2,3,4], [1,2]]
    빈 리스트 []는 공백(빈 칸)을 의미한다.
    문장부호·영문 등 미지원 문자는 건너뛴다.
    """
    cells = []
    for kind, group in itertools.groupby(text, key=_char_kind):
        run = ''.join(group)
        if kind == 'space':
            cells.extend([] for _ in run)
        elif kind == 'digit':
            cells.extend(_encode_digit_run(run))
        elif kind == 'hangul':
            for ch in run:
                cells.extend(_encode_syllable(ch))
        # kind == 'other'는 현재 미지원이라 건너뜀
    return cells


def cells_to_unicode(cells):
    """점자 셀 배열 → 유니코드 점자 문자열 (화면 확인용).

    예: [[1,2,4]] → '⠋'  (유니코드 점자 블록은 U+2800 + 비트마스크)
    """
    return ''.join(chr(0x2800 + _mask(*cell)) for cell in cells)


# ─────────────────────────────────────────────
# 자체 점검: 초성 19 / 중성 21 / 종성 27 전수 커버 여부 확인
# 어두 ㅇ은 의도적으로 무점(생략)이므로 CHO_DOTS 누락 검사에서 제외한다.
# ─────────────────────────────────────────────

def _verify_jamo_tables():
    """자모 테이블 누락 여부를 점검해 문제 메시지 리스트를 돌려준다. 문제 없으면 빈 리스트."""
    problems = []

    # 'ㅇ'은 어두에서 무점 처리, 된소리 자모는 DOEN_MAP을 거쳐 예사소리로 인코딩되므로 둘 다 제외
    covered_cho = set(CHO_DOTS) | set(DOEN_MAP) | {'ㅇ'}
    missing_cho = [c for c in _CHOSEONG_ORDER if c not in covered_cho]
    if missing_cho:
        problems.append(f"초성 누락: {missing_cho}")

    missing_doen_target = [d for d, plain in DOEN_MAP.items() if plain not in CHO_DOTS]
    if missing_doen_target:
        problems.append(f"DOEN_MAP이 참조하는 예사소리가 CHO_DOTS에 없음: {missing_doen_target}")

    missing_jung = [j for j in _JUNGSEONG_ORDER if j not in JUNG_DOTS]
    if missing_jung:
        problems.append(f"중성 누락: {missing_jung}")

    covered_jong = set(JONG_DOTS) | set(JONG_SPLIT)
    missing_jong = [j for j in _JONGSEONG_ORDER if j and j not in covered_jong]
    if missing_jong:
        problems.append(f"종성 누락: {missing_jong}")

    # 겹받침 분해 결과 글자가 실제로 JONG_DOTS에 있는지도 확인
    for compound, letters in JONG_SPLIT.items():
        for letter in letters:
            if letter not in JONG_DOTS:
                problems.append(f"JONG_SPLIT['{compound}']이 참조하는 '{letter}'가 JONG_DOTS에 없음")

    return problems


if __name__ == "__main__":
    problems = _verify_jamo_tables()
    if problems:
        print("[자모 점검 실패]")
        for p in problems:
            print(" -", p)
    else:
        print("[자모 점검 통과] 초성 19 / 중성 21 / 종성 27 전수 확인 완료")
    print()

    tests = ["컵", "물병", "안녕", "책 3권", "돼지 회의 위스키"]
    for t in tests:
        cells = text_to_braille(t)
        print(f"{t:12} → {cells}")
        print(f"{'':12}   {cells_to_unicode(cells)}")
        print()
