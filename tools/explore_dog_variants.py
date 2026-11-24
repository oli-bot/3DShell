#!/usr/bin/env python3
"""Shield / blade variant explorer for the 刀盾狗 mascot.

The 20×18 bake reads acceptably at 1× but two shapes are weak, and one is
outright broken:

  * SHIELD — 5×7 rounded rect with a 1×2 dark bar in it.  Reads as a card /
    phone rather than a 小圆盾, and it is taller than it is wide.
  * BLADE  — 4 px wide the whole way with a flat 2 px cap, so it reads as a
    bar/棍 rather than a blade/刃.
  * GRIP   — BUG: rows 13-16 have no outline in column 19 at all, i.e. bare
    steel colour sitting directly on the background.  The brief asks for a
    dark outline, and every other edge of the sprite has one.

This script swaps one region at a time out of the shipping bake and renders
each candidate at 10× (so the construction is legible) plus a flat silhouette
(so we can tell whether it still reads as a shield / a blade when colour is
thrown away).

Output: design/mascot/dog_variants.png
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'design' / 'mascot'
OUT.mkdir(parents=True, exist_ok=True)

PAL = {
    'o': (0x2b, 0x1a, 0x10, 255),
    'f': (0xf2, 0xa0, 0x3c, 255),
    'w': (0xff, 0xf4, 0xdf, 255),
    'm': (0xc9, 0xd8, 0xe4, 255),
    'd': (0x7b, 0x8f, 0xa0, 255),
    '.': None,
}

# The shipping 20×18 sprite, straight out of source/mascot.c.
BASE = [
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offffffffo.....",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".ooo.ofwwoowwfo.ommo",
    "ommmoofwwwwwwfo.ommo",
    "omdmoofwwwwwwfo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffooooodd.",
    "ommmoffoffffoffwwdd.",
    ".ooo.ffoffffoffwwdd.",
    ".....oooffffooooodd.",
    ".......oo..oo....oo.",
]

SHIELD_R0, SHIELD_C0 = 9, 0      # 7 rows × 5 cols
BLADE_R0, BLADE_C0 = 2, 16       # 16 rows × 4 cols
PAW = {(14, 16): 'w', (15, 16): 'w'}   # the paw sits on top of the grip

# In the shipping sprite the shield's right outline (col 4) doubles as the
# LEFT outline of the arm at rows 14-15.  Any shield that stops above row 14
# therefore leaves the arm's fur sitting bare on the background, so the arm
# gets its own outline back.
ARM_OUTLINE = {(14, 4): 'o', (15, 4): 'o'}


def patch(grid, r0, c0, block):
    """Replace a rectangular region; block rows may be shorter than the region."""
    rows = [list(r) for r in grid]
    for i, line in enumerate(block):
        for j, ch in enumerate(line):
            rows[r0 + i][c0 + j] = ch
    return [''.join(r) for r in rows]


# ── shield candidates: 7 rows × 5 cols (rows 9-15, cols 0-4) ────────────────
SHIELDS = {
    'S1  现状 · 5×7 · 1×2 竖条': [
        ".ooo.",
        "ommmo",
        "omdmo",
        "omdmo",
        "ommmo",
        "ommmo",
        ".ooo.",
    ],
    'S2  5×7 · 中心单点 boss': [
        ".ooo.",
        "ommmo",
        "ommmo",
        "omdmo",
        "ommmo",
        "ommmo",
        ".ooo.",
    ],
    'S3  5×5 圆盾 · 单点 boss': [
        ".ooo.",
        "ommmo",
        "omdmo",
        "ommmo",
        ".ooo.",
        ".....",
        ".....",
    ],
    'S4  5×7 尖底 heater · 单点 boss': [
        ".ooo.",
        "ommmo",
        "omdmo",
        "ommmo",
        "ommmo",
        ".omo.",
        "..o..",
    ],
    'S5  5×6 · 单点 boss（折中）': [
        ".ooo.",
        "ommmo",
        "omdmo",
        "omdmo",
        "ommmo",
        ".ooo.",
        ".....",
    ],
    'S6  5×5 圆盾下移一行（贴臂）': [
        ".....",
        ".ooo.",
        "ommmo",
        "omdmo",
        "ommmo",
        ".ooo.",
        ".....",
    ],
}

# ── blade candidates: 16 rows × 4 cols (rows 2-17, cols 16-19) ──────────────
_GUARD = "oooo"          # row 12
_GRIP_FIXED = [          # rows 13-17, with the missing col-19 outline restored
    "oddo",
    ".ddo",
    ".ddo",
    "oddo",
    ".oo.",
]
_GRIP_BROKEN = [         # rows 13-17 exactly as currently shipped
    "odd.",
    ".dd.",
    ".dd.",
    "odd.",
    ".oo.",
]

BLADES = {
    'B1  现状 · 平头 + 缺描边': ["....", ".oo."] + ["ommo"] * 8 + [_GUARD] + _GRIP_BROKEN,
    'B2  补握柄描边':           ["....", ".oo."] + ["ommo"] * 8 + [_GUARD] + _GRIP_FIXED,
    'B3  描边 + 刀尖三段收窄':   ["..o.", ".oo."] + ["ommo"] * 8 + [_GUARD] + _GRIP_FIXED,
    'B4  描边 + 收窄 + 加宽护手': ["..o.", ".oo."] + ["ommo"] * 8 + ["ooooo"[:4]] + _GRIP_FIXED,
    'B5  描边 + 长收窄 + 短刃':   ["..o.", ".oo.", ".oo."] + ["ommo"] * 6 + [_GUARD] + _GRIP_FIXED,
}
# B4's guard is 5 wide, so it has to be patched one column further left
BLADE_GUARD_WIDE = 'B4  描边 + 收窄 + 加宽护手'

# ── rendering ───────────────────────────────────────────────────────────────
BG_DARK = (0x2a, 0x2a, 0x33, 255)


def render(grid, scale, bg=BG_DARK, flat=None):
    im = Image.new('RGBA', (20 * scale, 18 * scale), bg)
    for r, row in enumerate(grid):
        for c, ch in enumerate(row):
            col = flat if (flat and ch != '.') else PAL.get(ch)
            if col is None:
                continue
            for dy in range(scale):
                for dx in range(scale):
                    im.putpixel((c * scale + dx, r * scale + dy), col)
    return im


def build(shield=None, blade=None):
    g = BASE
    if shield is not None:
        g = patch(g, SHIELD_R0, SHIELD_C0, shield)
        for (r, c), ch in ARM_OUTLINE.items():
            row = list(g[r]); row[c] = ch; g[r] = ''.join(row)
    if blade is not None:
        g = patch(g, BLADE_R0, BLADE_C0, blade)
        for (r, c), ch in PAW.items():
            row = list(g[r]); row[c] = ch; g[r] = ''.join(row)
    return g


def variants(kind):
    """Yield (label, grid) for the given kind, using the other part's baseline."""
    if kind == 'shield':
        for label, block in SHIELDS.items():
            yield label, build(shield=block)
    else:
        for label, block in BLADES.items():
            g = build(blade=block)
            if label == BLADE_GUARD_WIDE:      # widen the guard one col left
                row = list(g[12]); row[15] = 'o'; g[12] = ''.join(row)
            yield label, g


CJK = '/System/Library/Fonts/Supplemental/Arial Unicode.ttf'
f_h = ImageFont.truetype(CJK, 15)
f_l = ImageFont.truetype(CJK, 12)
f_s = ImageFont.truetype(CJK, 11)

S_BIG, S_SIL = 8, 6
CELL = 20 * S_BIG + 16          # 176
HDR = 46


def sheet(kind, labels_grids, title):
    n = len(labels_grids)
    W = 16 + n * CELL
    H = HDR + 18 * S_BIG + 34 + 18 * S_SIL + 34
    im = Image.new('RGBA', (W, H), (0xff, 0xff, 0xff, 255))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 34], fill=(0x1e, 0x1e, 0x26, 255))
    d.text((14, 9), title, font=f_h, fill=(0xff, 0xff, 0xff, 255))
    d.text((14, 40), '放大 8×', font=f_s, fill=(0x88, 0x88, 0x92, 255))

    for i, (label, g) in enumerate(labels_grids):
        x = 16 + i * CELL
        d.text((x, 40), label, font=f_l, fill=(0x30, 0x30, 0x3a, 255))
        im.alpha_composite(render(g, S_BIG), (x, HDR + 18))
        y2 = HDR + 18 * S_BIG + 34
        d.text((x, y2 - 16), '剪影', font=f_s, fill=(0x88, 0x88, 0x92, 255))
        im.alpha_composite(render(g, S_SIL, flat=(0x24, 0x24, 0x2c, 255)),
                           (x, y2 + 14))
    return im


sh = sheet('shield', list(variants('shield')),
           '盾牌变体（刀固定为现状）— 20×18 画布，盾占 cols 0-4 / rows 9-15')
bl = sheet('blade', list(variants('blade')),
           '刀与握柄变体（盾固定为现状）— 刀占 cols 16-19 / rows 2-17')

W = max(sh.width, bl.width)
H = sh.height + bl.height + 8
out = Image.new('RGBA', (W, H), (0xff, 0xff, 0xff, 255))
out.alpha_composite(sh, (0, 0))
out.alpha_composite(bl, (0, sh.height + 8))
out.convert('RGB').save(OUT / 'dog_variants.png')
print('wrote', OUT / 'dog_variants.png', out.size)
