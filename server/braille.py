"""
한글·영어(로마자) → 점자 변환 모듈

공개 함수 (main.py가 이 이름으로 import함, 변경 금지):
    text_to_braille(text)  : 문자열 → 점자 셀 배열. 각 셀은 6비트 정수(mask).
                              예: [9, 22, 0, 3, ...] (0은 빈 칸/공백)
    text_to_braille_lines(text) : 문자열 → 줄 단위로 나눈 2차원 셀 배열.
                              예: [[13, 41, 0, 22], [9, 3, 36]]
    cells_to_unicode(cells): 정수 셀 배열 → 화면 확인용 유니코드 점자 문자열
    mask_to_dots(mask)     : 정수 하나 → 사람이 읽는 점 번호 튜플, 예: (1, 4)

영어는 Grade 1(축약 없는 낱자 점역)만 지원한다. 로마자표(ROMAN_MARK_MASK)와
로마자 종료표(ROMAN_END_MASK)는 「한국 점자 규정」 로마자 장(章)과 대조 확인된 값이다.

점 번호 배치:
    1 · · 4
    2 · · 5
    3 · · 6

점형은 처음부터 끝까지 "점 번호가 켜진 6비트 정수"(mask)로만 다룬다.
n번 점(1~6)은 비트 (n-1)에 대응하므로, 유니코드 점자 변환은
chr(0x2800 + mask) 한 줄로 끝난다. 점 번호 리스트가 필요한 곳(화면 표시,
디버깅)에서만 mask_to_dots()로 그때그때 변환한다.
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
    """점 번호들(1~6)을 받아 6비트 정수 하나로 합친다. 테이블 정의 전용."""
    m = 0
    for n in dot_numbers:
        m |= 1 << (n - 1)
    return m


def mask_to_dots(mask):
    """비트마스크 → 오름차순 점 번호 튜플. 화면 표시·디버깅용 공개 보조 함수.

    예: mask_to_dots(9) → (1, 4)
    """
    return tuple(n for n in range(1, 7) if mask & (1 << (n - 1)))


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
    # 복합 모음 점자 매핑 (국립국어원 「한국 점자 규정」 준수)
    'ㅘ': (_mask(1, 2, 3, 6),),                   # 1칸 고유 점형
    'ㅙ': (_mask(1, 2, 3, 6), _mask(1, 2, 3, 5)), # 2칸: ㅘ + ㅐ
    'ㅚ': (_mask(1, 3, 4, 5, 6),),                # 1칸 고유 점형
    'ㅝ': (_mask(1, 2, 3, 4),),                   # 1칸 고유 점형
    'ㅞ': (_mask(1, 2, 3, 4), _mask(1, 2, 3, 5)), # 2칸: ㅝ + ㅐ
    'ㅟ': (_mask(1, 3, 4), _mask(1, 2, 3, 5)),    # 2칸: ㅜ + ㅐ
    'ㅢ': (_mask(2, 4, 5, 6),),                   # 1칸 고유 점형
    
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

# 영어(로마자) 낱자 점형 — 세계 공통 6점 점자 알파벳(Grade 1, 축약 없음).
# a~j는 NUM_DOTS의 1~0과 점형이 완전히 같다(국제 점자에서 숫자가 a~j
# 점형을 재사용하는 표준 방식) — 두 표를 나란히 두면 서로 교차 검증이 된다.
ENG_DOTS = {
    'a': _mask(1),             'b': _mask(1, 2),          'c': _mask(1, 4),
    'd': _mask(1, 4, 5),       'e': _mask(1, 5),          'f': _mask(1, 2, 4),
    'g': _mask(1, 2, 4, 5),    'h': _mask(1, 2, 5),       'i': _mask(2, 4),
    'j': _mask(2, 4, 5),       'k': _mask(1, 3),          'l': _mask(1, 2, 3),
    'm': _mask(1, 3, 4),       'n': _mask(1, 3, 4, 5),    'o': _mask(1, 3, 5),
    'p': _mask(1, 2, 3, 4),    'q': _mask(1, 2, 3, 4, 5), 'r': _mask(1, 2, 3, 5),
    's': _mask(2, 3, 4),       't': _mask(2, 3, 4, 5),    'u': _mask(1, 3, 6),
    'v': _mask(1, 2, 3, 6),    'w': _mask(2, 4, 5, 6),    'x': _mask(1, 3, 4, 6),
    'y': _mask(1, 3, 4, 5, 6), 'z': _mask(1, 3, 5, 6),
}

# 로마자표: 한글(또는 문자열 시작)에서 로마자 구간으로 들어갈 때 앞에 1회 삽입.
# 시작 전용 기호이며, 종료 시에는 ROMAN_MARK_MASK를 재사용하지 않고
# 아래의 별도 기호 ROMAN_END_MASK를 쓴다(둘은 서로 다른 점형이어야 한다).
ROMAN_MARK_MASK = _mask(3, 5, 6)

# 로마자 종료표: 로마자 구간 뒤에 공백 없이 바로 한글이 이어질 때 끝에 1회 삽입 (5-6점)
ROMAN_END_MASK = _mask(5, 6)

# 대문자표: 대문자 한 글자 앞에 1회. 대문자가 2자 이상 연이어 나오면
# (예: USB) 매 글자 앞이 아니라 구간 맨 앞에 이중대문자표(대문자표 2회)만
CAPITAL_MARK_MASK = _mask(6)


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
    """초성 자모 하나 → 점자 셀(mask) 리스트. 어두 ㅇ은 점을 찍지 않으므로 빈 리스트."""
    if cho == 'ㅇ':
        return []
    if cho in DOEN_MAP:
        return [DOEN_MARK_MASK, CHO_DOTS[DOEN_MAP[cho]]]
    return [CHO_DOTS[cho]]


def _encode_jungseong(jung):
    """중성 자모 하나 → 점자 셀(mask) 리스트 (단모음 1칸 / 이중모음 2칸)."""
    return list(JUNG_DOTS[jung])


def _encode_jongseong(jong):
    """종성 자모 하나(없으면 빈 문자열) → 점자 셀(mask) 리스트. 겹받침은 낱자로 분해."""
    if not jong:
        return []
    letters = JONG_SPLIT.get(jong, (jong,))
    return [JONG_DOTS[letter] for letter in letters]


def _encode_syllable(ch):
    """한글 음절 한 글자 → 점자 셀 리스트. 한글이 아니면 빈 리스트."""
    parts = _split_syllable(ch)
    if parts is None:
        return []
    cho, jung, jong = parts
    return _encode_choseong(cho) + _encode_jungseong(jung) + _encode_jongseong(jong)


# ─────────────────────────────────────────────
# 문자열 단위 처리
# 같은 종류(공백/숫자/한글/영어/기타)의 연속 구간을 묶어서 처리한다.
# 수표·로마자표는 그 종류가 끊기지 않고 이어지는 구간마다 한 번만
# 붙으면 되므로, 이렇게 구간을 미리 묶어두면 별도의 상태 플래그 없이
# 처리할 수 있다.
# ─────────────────────────────────────────────

def _char_kind(ch):
    """문자 하나를 space / digit / hangul / english / other 중 하나로 분류."""
    if ch == ' ':
        return 'space'
    if ch.isdigit():
        return 'digit'
    if '가' <= ch <= '힣':
        return 'hangul'
    if ch.isascii() and ch.isalpha():
        return 'english'
    return 'other'


def _encode_digit_run(run):
    """연속된 숫자 구간 → [수표] + 숫자 점형(mask)들."""
    cells = [NUM_MARK_MASK]
    cells.extend(NUM_DOTS[d] for d in run)
    return cells


def _encode_capital_span(letters):
    """대문자만으로 이루어진 구간 → 대문자표 점형(mask) 리스트.

    한 글자면 대문자표 1회, 두 글자 이상이면 이중대문자표(2회)만 앞에 찍는다.
    """
    if len(letters) == 1:
        return [CAPITAL_MARK_MASK]
    return [CAPITAL_MARK_MASK, CAPITAL_MARK_MASK]


def _encode_english_run(run, needs_end_mark):
    """연속된 로마자 구간 → [로마자표] + (대문자표 + 낱자)... [+ 로마자 종료표].

    대문자/소문자가 섞여 있으면 대문자 부분마다 _encode_capital_span으로
    표시하고, 점형 자체는 대소문자 구분 없이 ENG_DOTS 하나만 쓴다
    (점자는 모양이 아니라 앞에 붙는 표시 기호로만 대문자를 구분한다).
    needs_end_mark가 True면 구간 끝에 로마자 종료표(ROMAN_END_MASK, 로마자표와
    다른 점형)를 찍어 바로 이어지는 한글과 경계를 표시한다.
    """
    cells = [ROMAN_MARK_MASK]
    for is_upper, letters in itertools.groupby(run, key=str.isupper):
        letters = ''.join(letters)
        if is_upper:
            cells.extend(_encode_capital_span(letters))
        cells.extend(ENG_DOTS[c.lower()] for c in letters)
    if needs_end_mark:
        cells.append(ROMAN_END_MASK)
    return cells


def text_to_braille(text):
    """문자열 전체 → 점자 셀 배열. 각 셀은 6비트 정수(mask).

    반환 예: [9, 22, 0, 3]
    0은 공백(빈 칸)을 의미한다.
    한글·숫자·영어(Grade 1)를 지원하며, 그 외 문장부호 등은 건너뛴다.
    """
    runs = [(kind, ''.join(group)) for kind, group in itertools.groupby(text, key=_char_kind)]

    cells = []
    for i, (kind, run) in enumerate(runs):
        if kind == 'space':
            cells.extend(0 for _ in run)
        elif kind == 'digit':
            cells.extend(_encode_digit_run(run))
        elif kind == 'hangul':
            for ch in run:
                cells.extend(_encode_syllable(ch))
        elif kind == 'english':
            # 바로 다음 구간이 공백 없이 한글로 이어지는지 보고 종료표 필요 여부 결정
            next_kind = runs[i + 1][0] if i + 1 < len(runs) else None
            cells.extend(_encode_english_run(run, needs_end_mark=(next_kind == 'hangul')))
        # kind == 'other'(문장부호 등)는 현재 미지원이라 건너뜀
    return cells


# ─────────────────────────────────────────────
# 줄 나누기
#
# text_to_braille()은 개행(\n)을 'other'로 보고 건너뛰기 때문에 결과가
# 한 줄로 쭉 이어진다. 그러면 펌웨어가 어디서 줄을 바꿔야 할지 알 수 없다.
# 그래서 줄바꿈 위치를 판단하는 책임을 이 함수가 따로 맡는다.
# ─────────────────────────────────────────────

# 한 줄에 들어가는 최대 점자 셀 수.
# 실제 점자 출력 장치의 한 줄 폭에 맞춰야 하는 값이다.
# 일단 30으로 두고, 하드웨어 실측 후 조정할 것.
MAX_CELLS_PER_LINE = 30


def text_to_braille_lines(text, max_cells=MAX_CELLS_PER_LINE):
    """문자열 → 줄 단위로 나눈 2차원 점자 셀 배열.

    반환 예: [[13, 41, 0, 22], [9, 3, 36, 11]]

    줄을 나누는 기준은 두 가지다.
      1. 원문의 개행(\\n) 위치에서 무조건 줄을 바꾼다.
      2. 그 안에서 한 줄이 max_cells를 넘으면 낱말 경계(공백)에서 줄을 바꾼다.

    낱말 하나를 통째로 점역해서 줄에 담기 때문에 낱말 중간이 잘리지 않는다.
    다만 낱말 하나가 max_cells보다 긴 경우에는 담을 방법이 없으므로
    그때만 예외적으로 max_cells 단위로 끊는다.

    낱말 사이의 공백은 실제 점자와 같이 빈 칸(0) 한 개로 넣는다.
    줄 끝에는 공백을 남기지 않으며, 점역 결과가 비는 줄은 만들지 않는다.
    """
    lines = []

    # 1. 원문 개행으로 먼저 자른다.
    for paragraph in text.split('\n'):
        current = []

        # 2. 낱말 단위로 점역해서 줄에 채워 넣는다.
        for word in paragraph.split():
            cells = text_to_braille(word)
            if not cells:
                continue  # 문장부호만 있는 낱말 등 점역 결과가 없으면 건너뛴다

            # 지금 줄에 (빈 칸 + 낱말)을 더 넣을 수 없으면 여기서 줄을 끊는다
            if current and len(current) + 1 + len(cells) > max_cells:
                lines.append(current)
                current = []

            if current:
                current.append(0)  # 낱말 사이 빈 칸

            # 낱말 하나가 한 줄보다 긴 경우에만 어쩔 수 없이 중간에서 끊는다
            while len(current) + len(cells) > max_cells:
                room = max_cells - len(current)
                current.extend(cells[:room])
                lines.append(current)
                current = []
                cells = cells[room:]

            current.extend(cells)

        if current:
            lines.append(current)

    return lines


def cells_to_unicode(cells):
    """점자 셀 배열(mask 정수 리스트) → 유니코드 점자 문자열 (화면 확인용).

    예: [9] → '⠉'  (유니코드 점자 블록은 U+2800 + 비트마스크)
    """
    return ''.join(chr(0x2800 + mask) for mask in cells)


# ─────────────────────────────────────────────
# 자체 점검: 초성 19 / 중성 21 / 종성 27 / 로마자 26 전수 커버 여부 확인
# 어두 ㅇ은 의도적으로 무점(생략)이므로 CHO_DOTS 누락 검사에서 제외한다.
# ─────────────────────────────────────────────

def _verify_jamo_tables():
    """자모·로마자 테이블 누락 여부를 점검해 문제 메시지 리스트를 돌려준다. 문제 없으면 빈 리스트."""
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

    missing_eng = [c for c in "abcdefghijklmnopqrstuvwxyz" if c not in ENG_DOTS]
    if missing_eng:
        problems.append(f"로마자 누락: {missing_eng}")

    if ROMAN_MARK_MASK == ROMAN_END_MASK:
        problems.append("로마자표(ROMAN_MARK_MASK)와 로마자 종료표(ROMAN_END_MASK)가 같은 점형임")

    return problems


if __name__ == "__main__":
    problems = _verify_jamo_tables()
    if problems:
        print("[자모 점검 실패]")
        for p in problems:
            print(" -", p)
    else:
        print("[자모 점검 통과] 초성 19 / 중성 21 / 종성 27 / 로마자 26 전수 확인 완료")
    print()

    def _describe_cell(mask):
        """디버그 출력용: mask 정수 하나를 'mask(1,4)=9 ⠉' 형태 문자열로."""
        dots = ",".join(str(d) for d in mask_to_dots(mask))
        return f"mask({dots})={mask} {chr(0x2800 + mask)}"

    tests = ["컵", "물병", "안녕", "책 3권", "돼지 회의 위스키",
             "USB 케이블", "abc가나", "A동", "iPhone 신제품", "우유 500ml"]
    for t in tests:
        cells = text_to_braille(t)
        print(f"{t} →")
        print("  " + " / ".join(_describe_cell(m) for m in cells))
        print()
