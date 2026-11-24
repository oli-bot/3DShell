#!/usr/bin/env python3
"""
Multi-species Chibi Pet Mascots for DSSH:
  1. CRAB (🦀 螃蟹)       - Original front-facing chibi crab from Fishason/DSSH
  2. CRAB_XIETE (🦀 蟹特) - Coral Anthropic crab with big pincers & stalk eyes
  3. CHICK (🐣 坤坤)      - KUN KUN meme look: center-parted gray hair, yellow
                           face, red earring, black suit w/ white V chain,
                           black-pattern basketball held at the right side
  4. PHAGE (🔬 噬菌体)    - Blue T4 bacteriophage icon: outlined hexagonal
                           capsid, short tail & four bent tail-fiber legs
  5. ALIEN (👽 绿外星人)  - Glowing green alien with antenna & cosmic eyes
  6. NIULAI (🐂 牛来)      - 20×18 front-facing vivid-orange mascot with
                           dark-maroon stepped horns, white/black slit
                           eyes, and a pink snout/arms/feet
  7. DOG (🐕 刀盾狗)      - 20×18 orange-yellow Shiba with pointy ears and a
                           cream muzzle, holding a small round steel shield
                           in the left paw and a short blade in the right;
                           big head, tiny body, 5 flat colours

Outputs:
    docs/media/mascot_preview.png       All 7 pets side-by-side
    docs/media/mascot_preview.gif       All 7 pets animated walk loop

NOTE: the frame data here is a *duplicate* of source/mascot.c (kept so the
preview can be rendered without a C toolchain).  main() cross-checks every
species against mascot.c and fails loudly on drift — if you edit a sprite
in mascot.c, edit it here too (or the check will tell you exactly which
array diverged).
"""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:          # only needed to *render*; tools/check_mascot_sync.py
    Image = None             # imports this module to compare frame data

ROOT     = Path(__file__).parent.parent
OUT_DIR  = ROOT / "docs" / "media"
OUT_DIR.mkdir(parents=True, exist_ok=True)

BG = (24, 24, 37, 255)
W = 20

# --- 1. CRAB_ORIG (🦀 螃蟹 - Fishason/DSSH) ---
CRAB_ORIG_H = 12
CRAB_ORIG_PALETTE = {
    '@': (232, 155, 137, 255),
    'F': (216, 130, 113, 255),
    '#': (0, 0, 0, 255),
    '.': None,
}
crab_orig_w0 = [
    "....................",
    ".......@@@@@@.......",
    ".....@@@@@@@@@@.....",
    "...@@@@@@@@@@@@@@...",
    "..@@@@##@@@@##@@@@..",
    "..@@@@##@@@@##@@@@..",
    ".@@@@@@@@@@@@@@@@@@.",
    ".@@@@@@@@@@@@@@@@@@.",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    "....FFF......FFF....",
]
crab_orig_w1 = [
    "....................",
    "......@@@@@@........",
    "....@@@@@@@@@@......",
    "..@@@@@@@@@@@@@@....",
    ".@@@@##@@@@##@@@@...",
    ".@@@@##@@@@##@@@@...",
    ".@@@@@@@@@@@@@@@@@@.",
    ".@@@@@@@@@@@@@@@@@@.",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    "....FFF......FFF....",
]
crab_orig_w2 = [
    ".....@@@@@@.........",
    "...@@@@@@@@@@.......",
    ".@@@@@@@@@@@@@@.....",
    ".@@@@##@@@@##@@@@...",
    ".@@@@##@@@@##@@@@...",
    "@@@@@@@@@@@@@@@@@@..",
    "@@@@@@@@@@@@@@@@@@..",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    ".............FFF....",
    "....FFF.............",
]
crab_orig_w4 = [
    "....................",
    "........@@@@@@......",
    "......@@@@@@@@@@....",
    "....@@@@@@@@@@@@@@..",
    "...@@@@##@@@@##@@@@.",
    "...@@@@##@@@@##@@@@.",
    ".@@@@@@@@@@@@@@@@@@.",
    ".@@@@@@@@@@@@@@@@@@.",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    "....FFF......FFF....",
]
crab_orig_w5 = [
    ".........@@@@@@.....",
    ".......@@@@@@@@@@...",
    ".....@@@@@@@@@@@@@@.",
    "...@@@@##@@@@##@@@@.",
    "...@@@@##@@@@##@@@@.",
    "..@@@@@@@@@@@@@@@@@@",
    "..@@@@@@@@@@@@@@@@@@",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    "....FFF.............",
    ".............FFF....",
]
crab_orig_frames = [crab_orig_w0, crab_orig_w1, crab_orig_w2, crab_orig_w0, crab_orig_w4, crab_orig_w5]

# --- 2. CRAB_XIETE (🦀 蟹特Crab) ---
CRAB_H = 12
CRAB_PALETTE = {
    '@': (232, 122, 92, 255),
    'C': (244, 140, 110, 255),
    'L': (198, 88, 64, 255),
    'W': (255, 255, 255, 255),
    '#': (24, 24, 37, 255),
    'K': (255, 168, 150, 255),
    '.': None,
}
crab_w0 = [
    "CC.C..........C.CC..",
    "CCCCC..W#...#W.CCCCC",
    ".CCC...##...##..CCC.",
    "..CC....@@@@....CC..",
    "...C..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    "L..L.@@@@@@@@@@.L..L",
    ".L..L.@@@@@@@@.L..L.",
    "..L..L........L..L..",
    "....................",
]
crab_w1 = [
    "C.CC..........C.CC..",   # pincer tip; matches source/mascot.c crab_w1
    "CCCCC..W#...#W.CCCCC",
    ".CCC...##...##..CCC.",
    "..CC....@@@@....CC..",
    "...C..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    "LL..@@@@@@@@@@@@.LL.",
    ".L.L.@@@@@@@@@@.L..L",
    "..L.L.@@@@@@@@.L..L.",
    ".....L........L..L..",
    "....................",
]
crab_w2 = [
    ".CC...........C.CC..",
    "CC.C...W#...#W.CCCCC",
    "CCCCC..##...##..CCC.",
    ".CCC....@@@@....CC..",
    "..CC..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    "L..L.@@@@@@@@@@L..L.",
    ".L..L.@@@@@@@@..L..L",
    "..L..L............L.",
    "....................",
]
crab_w4 = [
    "CC.C..........CC.C..",
    "CCCCC..W#...#W.CCCCC",
    ".CCC...##...##..CCC.",
    "..CC....@@@@....CC..",
    "...C..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@..LL",
    "L..L.@@@@@@@@@@.L.L.",
    ".L..L.@@@@@@@@.L.L..",
    "..L..L........L.....",
    "....................",
]
crab_w5 = [
    "CC.C...........CC...",
    "CCCCC..W#...#W.C.CC.",
    ".CCC...##...##.CCCCC",
    "..CC....@@@@....CCC.",
    "...C..@@@@@@@@..CC..",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    ".L.L.@@@@@@@@@@.L..L",
    "L..L..@@@@@@@@.L..L.",
    ".L............L..L..",
    "....................",
]
crab_frames = [crab_w0, crab_w1, crab_w2, crab_w0, crab_w4, crab_w5]

# --- 2. CHICK (KUN KUN 鸡你太美) ---
# v5 redesign — faithful to the reference pixel art:
#   • center-parted gray hair framing the face (rows 0-7)
#   • yellow face w/ gray brows, black eyes, orange beak, red earring
#   • black jacket w/ two gray-white overall straps running from the
#     shoulders down to gray-white overalls pants; black feet
#   • 9x9 basketball w/ black outline/seams, dribbled while walking
CHICK_H = 18
CHICK_PALETTE = {
    '@': (250, 204, 21, 255),  # yellow face skin / hand
    'H': (156, 163, 175, 255), # gray hair & brows
    'K': (13, 15, 24, 255),    # black jacket / feet
    'B': (249, 115, 22, 255),  # orange beak
    'R': (226, 133, 58, 255),  # basketball orange
    'E': (205, 32, 47, 255),   # red earring
    'S': (214, 217, 222, 255), # gray-white overalls & straps
    'W': (255, 255, 255, 255),
    '#': (10, 10, 16, 255),    # pupils / ball outline & seams
    '.': None,
}
_BALL = [
    "..#####..",
    ".#RR#RR#.",
    "#R#R#R#R#",
    "#R#R#R#R#",
    "#########",
    "#R#R#R#R#",
    "#R#R#R#R#",
    ".#RR#RR#.",
    "..#####..",
]
def _chick(feet, hand=(12, 13), ball_off=0, eyes_up=False):
    """Build an 18-row frame.  feet = 3 rows (legs S / feet K x2);
    hand = rows carrying the yellow left hand; ball_off shifts the
    basketball down (dribble); eyes_up swaps brows/eyes (lookup)."""
    rows = [list("." * 20) for _ in range(18)]
    top = [
        "...HHHH.HHHH........",
        "..HHHHHHHHHHHH......",
        ".HHHHHHHHHHHHH......",
        ".HHH@@@@@@HHHH......",
        "HHHH@@@@@@HHHH......",
        "HHHH@HH@HHHH........",
        "...E@##@##..........",
        "...E@BBBB@..........",
        "....@@BB@@..........",
        "....@@@@@@..........",
        "..KKSKKKSKK.........",
        "..KKSKKKSKK.........",
        "..KKSKKKSKK.........",
        "..SSSSSSSSS.........",
        "..SSSSSSSSS.........",
    ]
    if eyes_up:
        top[5] = "HHHH@##@##HHHH......"
        top[6] = "...E@@@@@@.........."
    for r, line in enumerate(top):
        for c, ch in enumerate(line):
            if ch != '.':
                rows[r][c] = ch
    for r in hand:
        rows[r][1] = '@'
    for i, row in enumerate(feet):
        rows[15 + i] = list(row)
    # Ball in the LEFT hand: mirror the character, nudge it one column
    # right so a hair fringe stays visible between ball and face, then
    # draw the ball on top (ball occludes the hair behind it).
    rows = [list("." + "".join(r)[::-1][:-1]) for r in rows]
    for i, brow in enumerate(_BALL):
        r = 4 + ball_off + i
        if r > 16:
            break
        for c, ch in enumerate(brow):
            if ch != '.':
                rows[r][c] = ch
    return ["".join(r) for r in rows]

_FEET_PLANT = (
    "...SS...SS..........",
    "...KK...KK..........",
    "..KKK...KKK.........",
)
_FEET_TUCK = (
    "...SS...SS..........",
    "...KK...KK..........",
    "...KK...KK..........",
)
# walk cycle: feet step while the ball traces a dribble bounce
_DRIB = (0, 2, 4, 4, 2, 0)
chick_w0 = _chick(_FEET_PLANT, ball_off=_DRIB[0])
chick_w1 = _chick((
    "...SS...SS..........",
    "...KK...KK..........",
    "..KKK....KK.........",
), ball_off=_DRIB[1])
chick_w2 = _chick((
    "..SS....SS..........",
    "..KK....KK..........",
    ".KKK....KKK.........",
), ball_off=_DRIB[2])
chick_w3 = _chick((
    "...SS...SS..........",
    "...KK...KK..........",
    "...KK...KKK.........",
), ball_off=_DRIB[3])
chick_w4 = _chick((
    "...SS...SS..........",
    "...KK...KK..........",
    "..KK.....KK.........",
), ball_off=_DRIB[4])
chick_w5 = _chick((
    "...SS...SS..........",
    "...KK...KK..........",
    "..KK....KKK.........",
), ball_off=_DRIB[5])
chick_frames = [chick_w0, chick_w1, chick_w2, chick_w3, chick_w4, chick_w5]
chick_type_f0 = _chick(_FEET_PLANT, hand=(11, 12))
chick_type_f1 = _chick(_FEET_PLANT, hand=(13,))
chick_happy_f = _chick(_FEET_TUCK, hand=(10, 11))
chick_lookup_f = _chick(_FEET_PLANT, eyes_up=True)
chick_play_frames = [
    _chick(_FEET_PLANT, ball_off=_DRIB[0]),
    _chick(_FEET_PLANT, ball_off=_DRIB[1]),
    _chick(_FEET_PLANT, ball_off=_DRIB[2]),
    _chick(_FEET_PLANT, ball_off=_DRIB[3]),
    _chick(_FEET_PLANT, ball_off=_DRIB[4]),
    _chick(_FEET_PLANT, ball_off=_DRIB[5]),
]

# --- 3. PHAGE ---
PHAGE_H = 12
PHAGE_PALETTE = {
    '@': (184, 226, 248, 255),
    'D': (95, 159, 214, 255),
    'L': (184, 226, 248, 255),
    '.': None,
}
phage_w0 = [
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....L.L......L.L....",
    "....L.L......L.L....",
    "....................",
]
phage_w1 = [
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    ".............L.L....",
    ".............L.L....",
    "....................",
]
phage_w2 = [
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....L.L.............",
    "....L.L.............",
    "....................",
]
phage_w4 = [
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    ".............L.L....",
    ".............L.L....",
    "....................",
]
phage_w5 = [
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....L.L......L.L....",
    "....L.L......L.L....",
    "....................",
]
phage_frames = [phage_w0, phage_w1, phage_w2, phage_w0, phage_w4, phage_w5]

# --- 4. ALIEN ---
ALIEN_H = 12
ALIEN_PALETTE = {
    '@': (74, 222, 128, 255),
    'G': (34, 197, 94, 255),
    'A': (250, 204, 21, 255),
    'S': (56, 189, 248, 255),
    'W': (240, 253, 250, 255),
    '#': (15, 23, 42, 255),
    'P': (192, 132, 252, 255),
    '.': None,
}
alien_w0 = [
    ".........AA.........",
    ".........SS.........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    ".....GG@@@@@@GG.....",
    "......GG....GG......",
    ".......G....G.......",
]
alien_w1 = [
    "........AA..........",
    "........SS..........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    "....GG.@@@@@@.GG....",
    ".....GG......GG.....",
    "....................",
]
alien_w2 = [
    ".......AA...........",
    ".......SS...........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    ".....GG@@@@@@GG.....",
    "......GG....GG......",
    ".......G....G.......",
]
alien_w4 = [
    "..........AA........",
    "..........SS........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    "....GG.@@@@@@.GG....",
    ".....GG......GG.....",
    "....................",
]
alien_w5 = [
    "...........AA.......",
    "...........SS.......",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    ".....GG@@@@@@GG.....",
    "......GG....GG......",
    ".......G....G.......",
]
alien_frames = [alien_w0, alien_w1, alien_w2, alien_w0, alien_w4, alien_w5]

# ── 5. NIULAI (🐂 牛来) Frames ─────────────────────────────────────
NIULAI_H = 18
NIULAI_PALETTE = {
    'Y': (247, 147,  30, 255),  # vivid orange body
    'P': (227, 164, 158, 255),  # pink snout / arms / feet
    'D': (198, 134, 134, 255),  # darker pink shading
    'H': ( 93,  58,  58, 255),  # dark maroon horn / eye / mouth
    'W': (255, 255, 255, 255),  # white eye
    '.': None,
}

def s20(t):
    assert len(t) == 20, f'len {len(t)}: {t!r}'
    return t

_common_top = [
    s20("..HH............HH.."),
    s20("..HHHH........HHHH.."),
    s20("....HHYYYYYYYYHH...."),
    s20("...HHYYYYYYYYYYHH..."),
    s20("YYYYYYYYYYYYYYYYYYYY"),
    s20("YYYYYYYYYYYYYYYYYYYY"),
    s20(".YYYYWHWYYYYWHWYYYY."),
    s20("..YYYYYYYYYYYYYYYY.."),
    s20("...YYYYPPPPPPYYYY..."),
    s20("...YYYYPHPPHPYYYY..."),
    s20("...YYYYPPPPPPYYYY..."),
    s20("...YYYYPHHHHPYYYY..."),
    s20(".....YYPPPPPPYY....."),
    s20("...YYYYYYYYYYYYYY..."),
    s20("...YYYYYYYYYYYYYY..."),
    s20("...PPPYYYYYYYYPPP..."),
]
_plant = [
    s20(".....YYYY..YYYY....."),
    s20(".....PDP....PDP....."),
]
# Lifted hoof rises one row and reaches forward; the planted leg keeps
# the exact x-range it has in _plant so the foot never leaves the body.
_lift_left = [
    s20("....PDP....YYYY....."),
    s20("...........PDP......"),
]
_wide = [
    s20("....YYYY....YYYY...."),
    s20("....PDP......PDP...."),
]
_lift_right = [
    s20(".....YYYY....PDP...."),
    s20("......PDP..........."),
]

niulai_w0 = _common_top + _plant
niulai_w1 = _common_top + _lift_left
niulai_w2 = _common_top + _wide
niulai_w3 = _common_top + _plant
niulai_w4 = _common_top + _lift_right
niulai_w5 = _common_top + _wide

niulai_type_f0 = _common_top + _plant
niulai_type_f1 = _common_top + _lift_left

niulai_happy_f = _common_top[:6] + [
    s20(".YYYYYYYYYYYYYYYYYY."),  # closed eyes
    s20("..YYYYYYYYYYYYYYYY.."),
] + _common_top[8:] + [
    s20(".....YYYY..YYYY....."),
    s20(".....PDP....PDP....."),
]

niulai_lookup_f = _common_top[:6] + [
    s20(".YYYYYHYYYYYYHYYYYY."),  # eyes up
    s20("..YYYYYYYYYYYYYYYY.."),
] + _common_top[8:] + [
    s20(".....YYYY..YYYY....."),
    s20(".....PDP....PDP....."),
]

niulai_walk_frames = [niulai_w0, niulai_w1, niulai_w2, niulai_w3, niulai_w4, niulai_w5]

# ── 6. DOG (🐕 刀盾狗) Frames ────────────────────────────────────────
DOG_H = 18
DOG_PALETTE = {
    'o': ( 43,  26,  16, 255),  # dark outline / eyes / nose
    'f': (242, 160,  60, 255),  # orange-yellow fur
    'w': (255, 244, 223, 255),  # cream muzzle / paws / eye glint
    'm': (201, 216, 228, 255),  # steel lit face (shield, blade)
    'd': (123, 143, 160, 255),  # steel shade (shield boss, grip)
    '.': None,
}

# Redrawn 2026-09-14 from the 32×32 master (tools/gen_mascot_dog32.py) and
# baked to the 20×18 mascot canvas by tools/bake_mascot_dog20.py.
_dog_base = [
    s20("......oo....oo......"),
    s20(".....offo..offo....."),
    s20(".....offfoofffo...o."),
    s20(".....offffffffo..oo."),
    s20(".....offffffffo.ommo"),
    s20(".....ofooffoofo.ommo"),
    s20(".....ofooffoofo.ommo"),
    s20(".....offffffffo.ommo"),
    s20(".....offwwwwffo.ommo"),
    s20(".....ofwwoowwfo.ommo"),
    s20(".ooo.ofwwwwwwfo.ommo"),
    s20("ommmooowwwwwwoo.ommo"),
    s20("omdmo..offffo...oooo"),
    s20("ommmooooffffoooooddo"),
    s20(".ooooffoffffoffwwddo"),
    s20("....offoffffoffwwddo"),
    s20(".....oooffffoooooddo"),
    s20(".......oooooo....oo."),
]

# All six walk frames are byte-identical: the stride bob lives in
# mascot_draw() (yi -= 1 on odd frames), not in the frame data.  The old
# dog data was the same way, so this is not a simplification.
dog_frames = [_dog_base] * 6

def render(lines, palette, scale=10, pad=4):
    h = len(lines)
    img = Image.new("RGBA", (W * scale + pad * 2, h * scale + pad * 2), BG)
    for r, row in enumerate(lines):
        for c, ch in enumerate(row):
            col = palette.get(ch)
            if col:
                for dy in range(scale):
                    for dx in range(scale):
                        img.putpixel((pad + c * scale + dx, pad + r * scale + dy), col)
    return img

def main():
    if Image is None:
        raise SystemExit('Pillow is required to render the preview '
                         '(pip install Pillow); the sync check does not need it')

    # Guard against the preview drifting away from the shipping sprite data.
    # The frame arrays above are a copy of source/mascot.c, so an edit to
    # one side silently desyncs the docs — check before rendering.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import check_mascot_sync
    if not check_mascot_sync.check():
        raise SystemExit('refusing to render a preview that does not match '
                         'source/mascot.c')

    species = [
        (crab_orig_frames,   CRAB_ORIG_PALETTE, CRAB_ORIG_H),
        (crab_frames,        CRAB_PALETTE,      CRAB_H),
        (chick_frames,       CHICK_PALETTE,     CHICK_H),
        (phage_frames,       PHAGE_PALETTE,     PHAGE_H),
        (alien_frames,       ALIEN_PALETTE,     ALIEN_H),
        (niulai_walk_frames, NIULAI_PALETTE,    NIULAI_H),
        (dog_frames,         DOG_PALETTE,       DOG_H),
    ]
    comp_frames = []
    for i in range(6):
        imgs = [render(frames[i], pal) for frames, pal, _ in species]
        max_h = max(im.height for im in imgs)
        w = sum(im.width for im in imgs) + 8 * (len(imgs) - 1)
        frame = Image.new("RGBA", (w, max_h), BG)
        x = 0
        for im in imgs:
            frame.paste(im, (x, max_h - im.height))  # bottom-align
            x += im.width + 8
        comp_frames.append(frame)

    comp_frames[0].save(OUT_DIR / "mascot_preview.png")
    print(f"  ✓ {OUT_DIR / 'mascot_preview.png'}")

    comp_frames[0].save(
        OUT_DIR / "mascot_preview.gif",
        save_all=True,
        append_images=comp_frames[1:],
        duration=130,
        loop=0,
        disposal=2,
    )
    print(f"  ✓ {OUT_DIR / 'mascot_preview.gif'}")

if __name__ == "__main__":
    main()
