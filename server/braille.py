"""
한글·영어(로마자) → 점자 변환 모듈  (수정판, 2026-11-09)

공개 함수 (main.py가 이 이름으로 import함, 변경 금지):
    text_to_braille(text)  : 문자열 → 점자 셀 배열. 각 셀은 6비트 정수(mask).
                              예: [9, 22, 0, 3, ...] (0은 빈 칸/공백)
    text_to_braille_lines(text) : 문자열 → 줄 단위로 나눈 2차원 셀 배열.
                              예: [[13, 41, 0, 22], [9, 3, 36]]
    cells_to_unicode(cells): 정수 셀 배열 → 화면 확인용 유니코드 점자 문자열
    mask_to_dots(mask)     : 정수 하나 → 사람이 읽는 점 번호 튜플, 예: (1, 4)

기준 자료
    국립국어원 「한국 점자 규정」(2017년 개정) 원문 PDF
    https://www.korean.go.kr/front/etcData/etcDataView.do?etc_seq=542&mn_id=46
    규정 원문은 점형을 "ASCII 점자 표기"(예: ㅖ = '/')로 적는다.
    아래 표들은 사람이 읽기 쉽게 점 번호로 적었고, 맨 아래 _verify_tables()가
    규정 원문의 ASCII 표기와 하나하나 대조해서 틀린 게 없는지 확인한다.

이번 수정에서 바뀐 것
    1. ㅖ  : (1,3,4,5,6) → (3,4)                   [제8항]  ※ 예전 값은 ㅚ와 같은 모양이었음
    2. ㅒ  : (3,4,5,6)   → (3,4,5) + (1,2,3,5) 2칸  [제8항]  ※ 예전 값은 수표와 같은 모양이었음
    3. 로마자 종료표 : (5,6) → (2,5,6)              [제30항]
    4. 약자·약어 추가 (가~하, 것, 억~인, 까·싸·껏, 성·정·청, 그래서 등 7개) [제12~18항]
    5. 숫자 뒤에 ㄴ·ㄷ·ㅁ·ㅋ·ㅌ·ㅍ·ㅎ 또는 '운'이 오면 한 칸 띄움   [제38항]
    + 모음 뒤 '예', 'ㅑ·ㅘ·ㅜ·ㅝ' 뒤 '애'에 붙임표(3-6) 넣기     [제10·11항]
      (초성 ㅇ을 생략하기 때문에 이게 없으면 두 모음이 한 글자처럼 읽힘)

점 번호 배치:
    1 · · 4
    2 · · 5
    3 · · 6

점형은 처음부터 끝까지 "점 번호가 켜진 6비트 정수"(mask)로만 다룬다.
n번 점(1~6)은 비트 (n-1)에 대응한다.
  → 펌웨어1에서는 bit0 = 점1 = PA0 = D22 … bit5 = 점6 = PA5 = D27 이라
    이 숫자를 그대로 PORTA 에 쓰면 된다.
"""

import itertools

# ─────────────────────────────────────────────
# 점형 ↔ 비트마스크 변환
# ─────────────────────────────────────────────

def _mask(*dot_numbers):
    """점 번호들(1~6)을 받아 6비트 정수 하나로 합친다. 테이블 정의 전용."""
    m = 0
    for n in dot_numbers:
        m |= 1 << (n - 1)
    return m


def mask_to_dots(mask):
    """비트마스크 → 오름차순 점 번호 튜플. 예: mask_to_dots(9) → (1, 4)"""
    return tuple(n for n in range(1, 7) if mask & (1 << (n - 1)))


# ─────────────────────────────────────────────
# 자모 순서 (유니코드 한글 음절 조합 공식이 요구하는 고정 순서)
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
# 기본 자모 점형  [제1~9항]
# ─────────────────────────────────────────────

# 첫소리(초성). ㅇ은 첫소리에서 적지 않는다 [제1항].
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

# 모음(중성). 값은 "셀의 나열"(튜플). 1칸/2칸 상관없이 항상 extend 하면 된다.
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
    'ㅒ': (_mask(3, 4, 5), _mask(1, 2, 3, 5)),     # [수정] 2칸: ㅑ + ㅐ
    'ㅖ': (_mask(3, 4),),                           # [수정] 1칸
    'ㅘ': (_mask(1, 2, 3, 6),),
    'ㅙ': (_mask(1, 2, 3, 6), _mask(1, 2, 3, 5)),  # 2칸: ㅘ + ㅐ
    'ㅚ': (_mask(1, 3, 4, 5, 6),),
    'ㅝ': (_mask(1, 2, 3, 4),),
    'ㅞ': (_mask(1, 2, 3, 4), _mask(1, 2, 3, 5)),  # 2칸: ㅝ + ㅐ
    'ㅟ': (_mask(1, 3, 4), _mask(1, 2, 3, 5)),     # 2칸: ㅜ + ㅐ
    'ㅢ': (_mask(2, 4, 5, 6),),
}

# 받침(종성). 겹받침은 JONG_SPLIT으로 낱자 분해 후 이 표를 쓴다.
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
    'ㅆ': _mask(3, 4),  # 받침 ㅆ은 전용 점형 (ㅖ와 같은 모양이지만 자리가 달라 구분됨)
}

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

# 된소리 초성(ㄲㄸㅃㅆㅉ) → 된소리표(6점) + 예사소리  [제3항]
DOEN_MAP = {'ㄲ': 'ㄱ', 'ㄸ': 'ㄷ', 'ㅃ': 'ㅂ', 'ㅆ': 'ㅅ', 'ㅉ': 'ㅈ'}
DOEN_MARK_MASK = _mask(6)

# 모음 연쇄 붙임표(3-6)  [제10·11항]
VOWEL_HYPHEN_MASK = _mask(3, 6)


# ─────────────────────────────────────────────
# 약자 [제12~17항]  · 약어 [제18항]
# ─────────────────────────────────────────────

# 제12항: 가 나 다 마 바 사 자 카 타 파 하 것
#   - 가, 사 는 전용 점형 1칸
#   - 나 다 마 바 자 카 타 파 하 는 'ㅏ'를 빼고 첫소리만 적는다
ABBR_GA = _mask(1, 2, 4, 6)          # 가
ABBR_SA = _mask(1, 2, 3)             # 사
ABBR_GEOT = (_mask(4, 5, 6), _mask(2, 3, 4))   # 것 (2칸)
OMIT_A_CHO = {'ㄴ', 'ㄷ', 'ㅁ', 'ㅂ', 'ㅈ', 'ㅋ', 'ㅌ', 'ㅍ', 'ㅎ'}

# 제15항: 모음으로 시작하는 약자. (모음, 받침 첫 글자) → 1칸
#   글자 속에 들어 있어도 쓴다. 예: 꺾 = ㄲ + 억 + ㄱ, 넋 = ㄴ + 억 + ㅅ
VOWEL_ABBR = {
    ('ㅓ', 'ㄱ'): _mask(1, 4, 5, 6),      # 억
    ('ㅓ', 'ㄴ'): _mask(2, 3, 4, 5, 6),   # 언
    ('ㅓ', 'ㄹ'): _mask(2, 3, 4, 5),      # 얼
    ('ㅕ', 'ㄴ'): _mask(1, 6),            # 연
    ('ㅕ', 'ㄹ'): _mask(1, 2, 5, 6),      # 열
    ('ㅕ', 'ㅇ'): _mask(1, 2, 4, 5, 6),   # 영
    ('ㅗ', 'ㄱ'): _mask(1, 3, 4, 6),      # 옥
    ('ㅗ', 'ㄴ'): _mask(1, 2, 3, 5, 6),   # 온
    ('ㅗ', 'ㅇ'): _mask(1, 2, 3, 4, 5, 6),  # 옹
    ('ㅜ', 'ㄴ'): _mask(1, 2, 4, 5),      # 운
    ('ㅜ', 'ㄹ'): _mask(1, 2, 3, 4, 6),   # 울
    ('ㅡ', 'ㄴ'): _mask(1, 3, 5, 6),      # 은
    ('ㅡ', 'ㄹ'): _mask(2, 3, 4, 6),      # 을
    ('ㅣ', 'ㄴ'): _mask(1, 2, 3, 4, 5),   # 인
}

# 제16항: 성 썽 정 쩡 청 은 ㅅ ㅆ ㅈ ㅉ ㅊ 다음에 '영' 약자를 써서 적는다.
#   (ㅅ ㅈ ㅊ 뒤의 ㅕ는 ㅓ처럼 소리 나기 때문)
#   그래서 이 다섯 첫소리 뒤에 '영'이 오는 글자(셩, 졍, 쳥 …)는 거꾸로
#   '영' 약자를 쓰지 않고 풀어 쓴다. (규정 문장에서 이끌어 낸 처리)
SEONG_CHO = {'ㅅ', 'ㅆ', 'ㅈ', 'ㅉ', 'ㅊ'}

# 제18항: 약어 7개. 낱말 맨 앞에 올 때만 쓴다(앞에 다른 음절이 붙으면 안 씀).
#   뒤에 다른 음절이 붙는 건 괜찮다. 예: 그래서인지 = [그래서] + 인지
WORD_ABBR = {
    '그래서':   (_mask(1), _mask(2, 3, 4)),
    '그러나':   (_mask(1), _mask(1, 4)),
    '그러면':   (_mask(1), _mask(2, 5)),
    '그러므로': (_mask(1), _mask(2, 6)),
    '그런데':   (_mask(1), _mask(1, 3, 4, 5)),
    '그리고':   (_mask(1), _mask(1, 3, 6)),
    '그리하여': (_mask(1), _mask(1, 5, 6)),
}
# 긴 것부터 비교해야 '그러면'이 '그러므로'를 가로채지 않는다
_WORD_ABBR_ORDER = sorted(WORD_ABBR, key=len, reverse=True)


# ─────────────────────────────────────────────
# 숫자 · 로마자  [제28~40항]
# ─────────────────────────────────────────────

NUM_MARK_MASK = _mask(3, 4, 5, 6)    # 수표
NUM_DOTS = {
    '1': _mask(1), '2': _mask(1, 2), '3': _mask(1, 4),
    '4': _mask(1, 4, 5), '5': _mask(1, 5), '6': _mask(1, 2, 4),
    '7': _mask(1, 2, 4, 5), '8': _mask(1, 2, 5), '9': _mask(2, 4),
    '0': _mask(2, 4, 5),
}

# 제38항: 숫자 바로 뒤에 이 첫소리(또는 '운' 약자)가 오면 숫자와 헷갈리므로 한 칸 띄운다.
#   ㄴ=3, ㄷ=9, ㅁ=5, ㅋ=6, ㅌ=8, ㅍ=4, ㅎ=0, 운=7 과 모양이 같다.
DIGIT_LIKE_CHO = {'ㄴ', 'ㄷ', 'ㅁ', 'ㅋ', 'ㅌ', 'ㅍ', 'ㅎ'}

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

ROMAN_MARK_MASK = _mask(3, 5, 6)     # 로마자표          [제30항]
ROMAN_END_MASK = _mask(2, 5, 6)      # 로마자 종료표 [수정] [제30항]
CAPITAL_MARK_MASK = _mask(6)         # 대문자표. 2글자 이상 대문자는 2번(이중 대문자표)


# ─────────────────────────────────────────────
# 음절 분해
# ─────────────────────────────────────────────

def _split_syllable(ch):
    """완성형 한글 음절 → (초성, 중성, 종성). 한글 음절이 아니면 None."""
    offset = ord(ch) - 0xAC00
    if not (0 <= offset < len(_CHOSEONG_ORDER) * _JUNG_COUNT * _JONG_COUNT):
        return None
    offset, jong_i = divmod(offset, _JONG_COUNT)
    cho_i, jung_i = divmod(offset, _JUNG_COUNT)
    return _CHOSEONG_ORDER[cho_i], _JUNGSEONG_ORDER[jung_i], _JONGSEONG_ORDER[jong_i]


def _starts_with_vowel(ch):
    """다음 글자가 모음으로 시작하는(첫소리 ㅇ) 한글 음절인지."""
    parts = _split_syllable(ch) if ch else None
    return parts is not None and parts[0] == 'ㅇ'


# ─────────────────────────────────────────────
# 음절 하나 → 점자 셀
# ─────────────────────────────────────────────

def _encode_choseong(cho):
    """첫소리 → 셀 리스트. ㅇ은 적지 않음, 된소리는 된소리표 + 예사소리."""
    if cho == 'ㅇ':
        return []
    if cho in DOEN_MAP:
        return [DOEN_MARK_MASK, CHO_DOTS[DOEN_MAP[cho]]]
    return [CHO_DOTS[cho]]


def _encode_jong_letters(letters):
    return [JONG_DOTS[letter] for letter in letters]


def _encode_syllable(ch, next_ch=''):
    """한글 음절 한 글자 → 점자 셀 리스트 (약자 규칙 포함).

    next_ch : 같은 낱말에서 바로 뒤에 오는 글자(제17항 판단용). 없으면 ''.
    """
    parts = _split_syllable(ch)
    if parts is None:
        return []
    cho, jung, jong = parts
    jong_letters = JONG_SPLIT.get(jong, (jong,)) if jong else ()

    # 1) 것 · 껏  [제12·14항]
    if jung == 'ㅓ' and jong == 'ㅅ' and cho in ('ㄱ', 'ㄲ'):
        return ([DOEN_MARK_MASK] if cho == 'ㄲ' else []) + list(ABBR_GEOT)

    # 2) 가 나 다 … 하 (+ 받침)  [제12~14·17항]
    if jung == 'ㅏ':
        plain = DOEN_MAP.get(cho, cho)            # 까→가, 싸→사, 따→다 …
        doen = [DOEN_MARK_MASK] if cho in DOEN_MAP else []
        if plain == 'ㄱ':
            return doen + [ABBR_GA] + _encode_jong_letters(jong_letters)
        if plain == 'ㅅ':
            return doen + [ABBR_SA] + _encode_jong_letters(jong_letters)
        if plain in OMIT_A_CHO:
            # 제17항: 받침 없이 뒤에 모음이 이어지면 'ㅏ'를 빼지 않는다.
            #         '팠'도 'ㅏ'를 빼지 않는다.
            keep_a = (not jong and _starts_with_vowel(next_ch)) or (cho == 'ㅍ' and jong == 'ㅆ')
            if not keep_a:
                # 따·빠·짜는 규정에 예시가 없지만 제3항(된소리표)+제12항을 함께 적용
                return doen + [CHO_DOTS[plain]] + _encode_jong_letters(jong_letters)
        # 그 밖(ㄹ, ㅇ, ㅊ 등)은 아래 일반 규칙으로

    cells = _encode_choseong(cho)

    # 3) 성 썽 정 쩡 청  [제16항]
    if cho in SEONG_CHO and jung == 'ㅓ' and jong == 'ㅇ':
        return cells + [VOWEL_ABBR[('ㅕ', 'ㅇ')]]

    # 4) 억 언 얼 연 열 영 옥 온 옹 운 울 은 을 인  [제15항]
    if jong_letters and (jung, jong_letters[0]) in VOWEL_ABBR:
        blocked = cho in SEONG_CHO and jung == 'ㅕ' and jong_letters[0] == 'ㅇ'   # 셩·졍·쳥은 풀어 씀
        if not blocked:
            return cells + [VOWEL_ABBR[(jung, jong_letters[0])]] + _encode_jong_letters(jong_letters[1:])

    # 5) 일반: 첫소리 + 모음 + 받침
    return cells + list(JUNG_DOTS[jung]) + _encode_jong_letters(jong_letters)


def _needs_vowel_hyphen(prev_ch, ch):
    """제10·11항: 두 음절 사이에 붙임표(3-6)가 필요한지.

    - 받침 없는 음절(모음으로 끝남) 뒤에 '예'가 오면
    - 'ㅑ, ㅘ, ㅜ, ㅝ'로 끝나는 음절 뒤에 '애'가 오면
    """
    prev = _split_syllable(prev_ch) if prev_ch else None
    cur = _split_syllable(ch)
    if prev is None or cur is None or prev[2] or cur[0] != 'ㅇ':
        return False
    if cur[1] == 'ㅖ':
        return True
    return cur[1] == 'ㅐ' and prev[1] in ('ㅑ', 'ㅘ', 'ㅜ', 'ㅝ')


def _encode_hangul_run(run, at_word_start):
    """한글이 이어진 구간 → 셀 리스트."""
    cells = []
    i = 0
    # 약어(제18항)는 낱말 맨 앞일 때만
    if at_word_start:
        for word in _WORD_ABBR_ORDER:
            if run.startswith(word):
                cells.extend(WORD_ABBR[word])
                i = len(word)
                break
    while i < len(run):
        ch = run[i]
        next_ch = run[i + 1] if i + 1 < len(run) else ''
        if i > 0 and _needs_vowel_hyphen(run[i - 1], ch):
            cells.append(VOWEL_HYPHEN_MASK)
        cells.extend(_encode_syllable(ch, next_ch))
        i += 1
    return cells


def _hangul_looks_like_digit(run):
    """제38항: 이 한글 구간이 숫자 바로 뒤에 오면 띄어야 하는지."""
    parts = _split_syllable(run[0])
    if parts is None:
        return False
    cho, jung, jong = parts
    if cho in DIGIT_LIKE_CHO:
        return True
    first_jong = JONG_SPLIT.get(jong, (jong,))[0] if jong else ''
    return cho == 'ㅇ' and jung == 'ㅜ' and first_jong == 'ㄴ'   # '운' 약자


# ─────────────────────────────────────────────
# 문자열 단위 처리
# ─────────────────────────────────────────────

def _char_kind(ch):
    """문자 하나를 space / digit / hangul / english / other 중 하나로 분류."""
    if ch.isspace():
        return 'space'
    if '0' <= ch <= '9':
        return 'digit'
    if '가' <= ch <= '힣':
        return 'hangul'
    if ch.isascii() and ch.isalpha():
        return 'english'
    return 'other'


def _encode_capital_span(letters):
    """대문자 1글자면 대문자표 1번, 2글자 이상이면 2번(이중 대문자표)."""
    if len(letters) == 1:
        return [CAPITAL_MARK_MASK]
    return [CAPITAL_MARK_MASK, CAPITAL_MARK_MASK]


def _encode_english_letters(run):
    cells = []
    for is_upper, letters in itertools.groupby(run, key=str.isupper):
        letters = ''.join(letters)
        if is_upper:
            cells.extend(_encode_capital_span(letters))
        cells.extend(ENG_DOTS[c.lower()] for c in letters)
    return cells


def _encode_words(text):
    """문자열 → 낱말별 셀 리스트의 리스트. (공백 = 낱말 경계)

    로마자 규칙 [제30~35항]
      - 로마자 구간 맨 앞에 로마자표, 맨 끝에 로마자 종료표.
      - 띄어 쓴 로마자 낱말이 이어지면 한 구간으로 본다(제32항): 표는 처음·끝에 한 번씩.
      - 로마자 바로 뒤에 숫자가 붙으면 종료표를 적지 않는다(제35항). 예: MP3
      - 로마자 바로 뒤에 문장 부호가 붙으면 종료표를 적지 않는다(제33항).
    """
    runs = [(kind, ''.join(g)) for kind, g in itertools.groupby(text, key=_char_kind)]

    # 로마자 구간의 마지막 영어 run 위치를 미리 찾는다 (사이에 공백만 있으면 같은 구간)
    roman_start, roman_end = set(), set()
    i = 0
    while i < len(runs):
        if runs[i][0] != 'english':
            i += 1
            continue
        roman_start.add(i)
        last = i
        j = i + 1
        while j < len(runs):
            if runs[j][0] == 'english':
                last = j
            elif runs[j][0] != 'space':
                break
            j += 1
        roman_end.add(last)
        i = last + 1

    words = []
    current = []
    prev_kind = None
    for i, (kind, run) in enumerate(runs):
        if kind == 'space':
            if current:
                words.append(current)
                current = []
            prev_kind = 'space'
            continue

        if kind == 'digit':
            current.append(NUM_MARK_MASK)
            current.extend(NUM_DOTS[d] for d in run)
        elif kind == 'hangul':
            if prev_kind == 'digit' and _hangul_looks_like_digit(run):
                current.append(0)    # 제38항: 숫자와 띄어 쓴다 (낱말은 이어진 것으로 취급)
            current.extend(_encode_hangul_run(run, at_word_start=not current))
        elif kind == 'english':
            if i in roman_start:
                current.append(ROMAN_MARK_MASK)
            current.extend(_encode_english_letters(run))
            if i in roman_end:
                next_kind = runs[i + 1][0] if i + 1 < len(runs) else None
                if next_kind not in ('digit', 'other'):
                    current.append(ROMAN_END_MASK)
        # kind == 'other'(문장부호 등)는 아직 지원하지 않아 건너뜀
        prev_kind = kind

    if current:
        words.append(current)
    return words


def text_to_braille(text):
    """문자열 전체 → 점자 셀 배열. 각 셀은 6비트 정수(mask). 0은 빈 칸."""
    cells = []
    for word in _encode_words(text):
        if cells:
            cells.append(0)
        cells.extend(word)
    return cells


# ─────────────────────────────────────────────
# 줄 나누기
# ─────────────────────────────────────────────

# 한 줄에 들어가는 최대 점자 셀 수. 하드웨어 실측 후 조정할 것.
MAX_CELLS_PER_LINE = 30


def text_to_braille_lines(text, max_cells=MAX_CELLS_PER_LINE):
    """문자열 → 줄 단위로 나눈 2차원 점자 셀 배열.

      1. 원문의 개행(\\n) 위치에서 무조건 줄을 바꾼다.
      2. 그 안에서 한 줄이 max_cells를 넘으면 낱말 경계(공백)에서 줄을 바꾼다.
         낱말 하나가 max_cells보다 길 때만 예외적으로 낱말 중간에서 끊는다.
    낱말 사이 공백은 빈 칸(0) 한 개. 줄 끝에 공백을 남기지 않는다.
    """
    lines = []
    for paragraph in text.split('\n'):
        current = []
        for cells in _encode_words(paragraph):
            if current and len(current) + 1 + len(cells) > max_cells:
                lines.append(current)
                current = []
            if current:
                current.append(0)
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
    """점자 셀 배열 → 유니코드 점자 문자열 (화면 확인용). 예: [9] → '⠉'"""
    return ''.join(chr(0x2800 + mask) for mask in cells)


# ─────────────────────────────────────────────
# 자체 점검
#   (1) 표가 규정 원문의 ASCII 점자 표기와 같은지
#   (2) 규정에 나온 예시 낱말이 규정대로 점역되는지
# ─────────────────────────────────────────────

# 규정 원문이 쓰는 ASCII 점자 표기 → 점 번호 (북미 표준 ASCII 점자표)
_ASCII_BRAILLE = {
    'A': (1,), 'B': (1, 2), 'C': (1, 4), 'D': (1, 4, 5), 'E': (1, 5), 'F': (1, 2, 4),
    'G': (1, 2, 4, 5), 'H': (1, 2, 5), 'I': (2, 4), 'J': (2, 4, 5), 'K': (1, 3),
    'L': (1, 2, 3), 'M': (1, 3, 4), 'N': (1, 3, 4, 5), 'O': (1, 3, 5), 'P': (1, 2, 3, 4),
    'Q': (1, 2, 3, 4, 5), 'R': (1, 2, 3, 5), 'S': (2, 3, 4), 'T': (2, 3, 4, 5),
    'U': (1, 3, 6), 'V': (1, 2, 3, 6), 'W': (2, 4, 5, 6), 'X': (1, 3, 4, 6),
    'Y': (1, 3, 4, 5, 6), 'Z': (1, 3, 5, 6),
    '&': (1, 2, 3, 4, 6), '=': (1, 2, 3, 4, 5, 6), '(': (1, 2, 3, 5, 6), '!': (2, 3, 4, 6),
    ')': (2, 3, 4, 5, 6), '*': (1, 6), '<': (1, 2, 6), '%': (1, 4, 6), '?': (1, 4, 5, 6),
    ':': (1, 5, 6), '$': (1, 2, 4, 6), ']': (1, 2, 4, 5, 6), '\\': (1, 2, 5, 6),
    '[': (2, 4, 6), '1': (2,), '2': (2, 3), '3': (2, 5), '4': (2, 5, 6), '5': (2, 6),
    '6': (2, 3, 5), '7': (2, 3, 5, 6), '8': (2, 3, 6), '9': (3, 5), '0': (3, 5, 6),
    '/': (3, 4), '+': (3, 4, 6), '#': (3, 4, 5, 6), '>': (3, 4, 5), "'": (3,),
    '-': (3, 6), '@': (4,), '^': (4, 5), '_': (4, 5, 6), '"': (5,), '.': (4, 6),
    ';': (5, 6), ',': (6,), ' ': (),
}


def _ascii_to_cells(s):
    return [_mask(*_ASCII_BRAILLE[c]) for c in s]


# 「한국 점자 규정」(2017) 원문의 ASCII 표기
_RULE_TABLE = {
    'CHO': {'ㄱ': '@', 'ㄴ': 'C', 'ㄷ': 'I', 'ㄹ': '"', 'ㅁ': 'E', 'ㅂ': '^', 'ㅅ': ',',
            'ㅈ': '.', 'ㅊ': ';', 'ㅋ': 'F', 'ㅌ': 'H', 'ㅍ': 'D', 'ㅎ': 'J'},
    'JONG': {'ㄱ': 'A', 'ㄴ': '3', 'ㄷ': '9', 'ㄹ': '1', 'ㅁ': '5', 'ㅂ': 'B', 'ㅅ': "'",
             'ㅇ': '7', 'ㅈ': 'K', 'ㅊ': '2', 'ㅋ': '6', 'ㅌ': '8', 'ㅍ': '4', 'ㅎ': '0',
             'ㅆ': '/'},
    'JUNG': {'ㅏ': '<', 'ㅑ': '>', 'ㅓ': 'S', 'ㅕ': ':', 'ㅗ': 'U', 'ㅛ': '+', 'ㅜ': 'M',
             'ㅠ': '%', 'ㅡ': '[', 'ㅣ': 'O', 'ㅐ': 'R', 'ㅒ': '>R', 'ㅔ': 'N', 'ㅖ': '/',
             'ㅘ': 'V', 'ㅙ': 'VR', 'ㅚ': 'Y', 'ㅝ': 'P', 'ㅞ': 'PR', 'ㅟ': 'MR', 'ㅢ': 'W'},
    'VOWEL_ABBR': {'억': '?', '언': ')', '얼': 'T', '연': '*', '열': '\\', '영': ']',
                   '옥': 'X', '온': '(', '옹': '=', '운': 'G', '울': '&', '은': 'Z',
                   '을': '!', '인': 'Q'},
    'WORD_ABBR': {'그래서': 'AS', '그러나': 'AC', '그러면': 'A3', '그러므로': 'A5',
                  '그런데': 'AN', '그리고': 'AU', '그리하여': 'A:'},
    'MARK': {'된소리표': ',', '수표': '#', '로마자표': '0', '로마자 종료표': '4',
             '대문자표': ',', '붙임표': '-'},
}

# 규정 본문에 실린 예시 낱말과 그 점자 (ASCII 표기)
_RULE_EXAMPLES = {
    '가': '$', '나': 'C', '다': 'I', '마': 'E', '바': '^', '사': 'L', '자': '.',
    '카': 'F', '타': 'H', '파': 'D', '하': 'J', '것': '_S',          # 제12항
    '강산': '$7L3', '난방': 'C3^7',                                   # 제13항
    '까': ',$', '싸': ',L', '껏': ',_S', '껐': ',@S/',               # 제14항
    '꺾다': ',@?AI', '넋': "C?'",                                     # 제15항
    '성': ',]', '썽': ',,]', '정': '.]', '쩡': ',.]', '청': ';]',
    '정성': '.],]', '청년': ';]C*',                                   # 제16항
    '나이': 'C<O',                                                    # 제17항
    '그래서인지': 'ASQ.O', '쭈그리고': ',.M@["O@U',                  # 제18항
    '아예': '<-/', '야애': '>-R', '구애': '@M-R',                     # 제10·11항
    'Canada로': '0,CANADA4"U', 'MP3': '0,,MP#C',                      # 제30·35항
    '1가': '#A$', '2권': '#B@P3', '1년': '#A C*',                     # 제38항
}


def _verify_tables():
    """표와 규정 예시를 점검해 문제 메시지 리스트를 돌려준다. 문제 없으면 빈 리스트."""
    problems = []

    def check(name, got, ascii_str):
        want = _ascii_to_cells(ascii_str)
        if list(got) != want:
            problems.append(f"{name}: 코드 {[mask_to_dots(m) for m in got]} ≠ 규정 "
                            f"{[mask_to_dots(m) for m in want]} ('{ascii_str}')")

    for k, a in _RULE_TABLE['CHO'].items():
        check(f"첫소리 {k}", [CHO_DOTS[k]], a)
    for k, a in _RULE_TABLE['JONG'].items():
        check(f"받침 {k}", [JONG_DOTS[k]], a)
    for k, a in _RULE_TABLE['JUNG'].items():
        check(f"모음 {k}", JUNG_DOTS[k], a)
    vowel_abbr_by_name = dict(zip(_RULE_TABLE['VOWEL_ABBR'], VOWEL_ABBR.values()))
    for k, a in _RULE_TABLE['VOWEL_ABBR'].items():
        check(f"약자 {k}", [vowel_abbr_by_name[k]], a)
    for k, a in _RULE_TABLE['WORD_ABBR'].items():
        check(f"약어 {k}", WORD_ABBR[k], a)
    marks = {'된소리표': DOEN_MARK_MASK, '수표': NUM_MARK_MASK, '로마자표': ROMAN_MARK_MASK,
             '로마자 종료표': ROMAN_END_MASK, '대문자표': CAPITAL_MARK_MASK,
             '붙임표': VOWEL_HYPHEN_MASK}
    for k, a in _RULE_TABLE['MARK'].items():
        check(k, [marks[k]], a)

    # 자모 누락
    covered_cho = set(CHO_DOTS) | set(DOEN_MAP) | {'ㅇ'}
    problems += [f"초성 누락: {c}" for c in _CHOSEONG_ORDER if c not in covered_cho]
    problems += [f"중성 누락: {j}" for j in _JUNGSEONG_ORDER if j not in JUNG_DOTS]
    covered_jong = set(JONG_DOTS) | set(JONG_SPLIT)
    problems += [f"종성 누락: {j}" for j in _JONGSEONG_ORDER if j and j not in covered_jong]

    # 한 칸짜리 모음끼리 모양이 겹치면 안 된다 (예전 ㅖ=ㅚ 같은 실수 방지)
    single = {}
    for k, v in JUNG_DOTS.items():
        if len(v) == 1:
            if v[0] in single:
                problems.append(f"모음 {single[v[0]]} 와 {k} 의 점형이 같음")
            single[v[0]] = k

    # 규정 예시 낱말
    for word, a in _RULE_EXAMPLES.items():
        check(f"예시 '{word}'", text_to_braille(word), a)

    # 모든 한글 음절(11,172자)이 오류 없이 6비트 셀로 바뀌는지
    for code in range(0xAC00, 0xD7A4):
        cells = text_to_braille(chr(code))
        if not cells or any(not (0 < m < 64) for m in cells):
            problems.append(f"음절 {chr(code)} 변환 이상: {cells}")
            break

    return problems


if __name__ == "__main__":
    problems = _verify_tables()
    if problems:
        print("[점검 실패]")
        for p in problems:
            print(" -", p)
    else:
        print(f"[점검 통과] 규정 표 대조 + 규정 예시 {len(_RULE_EXAMPLES)}개 + 한글 11,172자 변환 확인")
    print()

    def _describe_cell(mask):
        dots = ",".join(str(d) for d in mask_to_dots(mask)) or "빈칸"
        return f"({dots})={mask} {chr(0x2800 + mask)}"

    tests = ["컵", "물병", "안녕", "책 3권", "돼지 회의 위스키", "계란", "얘기",
             "USB 케이블", "abc가나", "A동", "iPhone 신제품", "우유 500ml", "3년", "그리고"]
    for t in tests:
        cells = text_to_braille(t)
        print(f"{t} → {cells_to_unicode(cells)}")
        print("  " + " / ".join(_describe_cell(m) for m in cells))
        print()
