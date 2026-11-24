#!/usr/bin/env python3
"""Bake the 32×32 刀盾狗 master down to the mascot canvas (20×18) and
compare the result against the current MASCOT_DOG frames in mascot.c.

The in-app mascot is 20 px wide × 18 px tall (OX_W/OX_H) sitting at y=221
on the 320×240 bottom screen, so the 32×32 master has to be resampled.

Outputs (design/mascot/):
  dog_mascot_bake.png   old vs baked variants, 12× + 1:1 inset
  dog_mascot_bake.txt   baked char grids, ready to paste into mascot.c

Run:  python3 tools/bake_mascot_dog20.py
"""
import importlib.util
import re
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "design" / "mascot"
OUT.mkdir(parents=True, exist_ok=True)

spec = importlib.util.spec_from_file_location(
    "g", ROOT / "tools" / "gen_mascot_dog32.py")
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

PALETTE = g.PALETTE
W_M, H_M = 20, 18          # mascot canvas
DARK_BG = (24, 24, 37, 255)


# Pillow's built-in bitmap font has no CJK glyphs, so every Chinese label in
# the comparison sheets came out as tofu boxes.  Prefer a real system font
# and only fall back to the built-in one.
_FONT_CANDIDATES = (
    '/System/Library/Fonts/Supplemental/Arial Unicode.ttf',
    '/System/Library/Fonts/PingFang.ttc',
    '/System/Library/Fonts/Hiragino Sans GB.ttc',
    '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
)


def font(sz):
    for path in _FONT_CANDIDATES:
        if Path(path).exists():
            try:
                return ImageFont.truetype(path, sz)
            except OSError:
                continue
    try:
        return ImageFont.load_default(size=sz)
    except TypeError:
        return ImageFont.load_default()


def nearest_char(px):
    if px[3] == 0:
        return '.'
    best, bd = '.', 1 << 30
    for k, c in PALETTE.items():
        d = (c[0] - px[0]) ** 2 + (c[1] - px[1]) ** 2 + (c[2] - px[2]) ** 2
        if d < bd:
            bd, best = d, k
    return best


def img_to_grid(img):
    return [''.join(nearest_char(img.getpixel((x, y)))
                    for x in range(img.width))
            for y in range(img.height)]


def grid_to_img(grid, scale=1):
    h, w = len(grid), len(grid[0])
    img = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    px = img.load()
    for r in range(h):
        for c in range(w):
            ch = grid[r][c]
            if ch == '.':
                continue
            for dy in range(scale):
                for dx in range(scale):
                    px[c * scale + dx, r * scale + dy] = PALETTE[ch]
    return img


# ── current in-app dog (parsed out of mascot.c for a fair comparison) ────
src = (ROOT / "source" / "mascot.c").read_text(encoding="utf-8")
m = re.search(r"static const char \*const dog_w0\[18\] = \{(.*?)\};",
              src, re.S)
OLD_ROWS = re.findall(r'"([^"]*)"', m.group(1))
OLD = [r for r in OLD_ROWS]
OLD_PAL = {                      # the old dog's own legend -> our 5 colours
    '@': PALETTE['f'], 'D': PALETTE['o'], 'C': PALETTE['w'],
    'W': PALETTE['w'], '#': PALETTE['o'], 'P': PALETTE['w'],
    'S': PALETTE['m'], 's': PALETTE['m'], 'M': PALETTE['m'],
    'K': PALETTE['d'], 'G': PALETTE['o'], '.': None,
}


def old_to_img(grid, scale=1):
    h, w = len(grid), len(grid[0])
    img = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    px = img.load()
    for r in range(h):
        for c in range(w):
            col = OLD_PAL.get(grid[r][c])
            if not col:
                continue
            for dy in range(scale):
                for dx in range(scale):
                    px[c * scale + dx, r * scale + dy] = col
    return img


# ── bake variants ───────────────────────────────────────────────────────
master = g.render()                                   # 32×32 RGBA

BAKE_A = img_to_grid(master.resize((W_M, H_M), Image.NEAREST))

# uniform 18×18 then centre in the 20-wide canvas (keeps the aspect ratio)
sq = img_to_grid(master.resize((H_M, H_M), Image.NEAREST))
BAKE_B = [('.' + r + '.') for r in sq]

def f(n):
    return 'f' * n


def mk20(*segs):
    """Hand-authored 20-wide row."""
    a = ['.'] * W_M
    for c, t in segs:
        assert 0 <= c and c + len(t) <= W_M, f"col={c} {t!r}"
        for i, ch in enumerate(t):
            a[c + i] = ch
    return ''.join(a)


# Bake C: hand-redrawn at 20×18 from the 32×32 master.  1-px outlines
# cannot survive non-integer resampling, so the small size is authored
# fresh rather than sampled — this is the version worth shipping.
#
# 2026-09-14, second pass — three shape fixes found by looking at the first
# bake magnified 10× (see tools/explore_dog_variants.py for the candidates):
#   * shield  5×7 rounded rect read as a card/phone.  Now a 5×5 ROUND shield
#             with a single-pixel boss, sitting one row lower so it lines up
#             with the paw that holds it.
#   * blade   4 px wide the whole way with a flat 2 px cap read as a bar.
#             Now tapers 1 → 2 → 4 px at the tip so it reads as an edge.
#   * grip    BUG: rows 13-16 had no outline in column 19 — bare steel on
#             the background.  Column 19 is now 'o' all the way down.
#   * arm     the shield's right outline used to double as the arm's left
#             outline; moving the shield down meant the arm needed its own
#             (col 4, rows 14-15).
#   * outline audit then found three more places where a fill colour sat
#     directly on the background: the head top between the ears (row 2,
#     cols 9-10), the chin corners where the head overhangs the body
#     (row 11, cols 6 and 13), and the body's underside between the two
#     feet (row 16, cols 9-10).  The first two became 'o'.  The third could
#     not — turning row 16's fur into outline split the belly into two
#     isolated 1-px columns — so the feet row is sealed instead: row 17 is
#     one 6-px outline run and the feet read as the ends of the body's base.
#     Re-run tools/audit_mascot_outline.py after touching this grid.
BAKE_C = [
    mk20((6, "oo"), (12, "oo")),                                   # 0  ear tips
    mk20((5, "offo"), (11, "offo")),                               # 1  ears
    mk20((5, "o" + "fff" + "oo" + "fff" + "o"), (18, "o")),        # 2  head top + tip
    mk20((5, "o" + f(8) + "o"), (17, "oo")),                       # 3  blade tip
    mk20((5, "o" + f(8) + "o"), (16, "ommo")),                     # 4
    mk20((5, "o" + "f" + "oo" + "ff" + "oo" + "f" + "o"),
         (16, "ommo")),                                            # 5  eyes
    mk20((5, "o" + "f" + "oo" + "ff" + "oo" + "f" + "o"),
         (16, "ommo")),                                            # 6  eyes
    mk20((5, "o" + f(8) + "o"), (16, "ommo")),                     # 7
    mk20((5, "o" + "ff" + "wwww" + "ff" + "o"),
         (16, "ommo")),                                            # 8  muzzle top
    mk20((5, "o" + "f" + "ww" + "oo" + "ww" + "f" + "o"),
         (16, "ommo")),                                            # 9  nose
    mk20((1, "ooo"), (5, "o" + "f" + "wwwwww" + "f" + "o"),
         (16, "ommo")),                                            # 10 shield top
    mk20((0, "ommmo"), (5, "o" + "o" + "wwwwww" + "o" + "o"),
         (16, "ommo")),                                            # 11 chin
    mk20((0, "omdmo"), (7, "o" + f(4) + "o"), (16, "oooo")),       # 12 body top + guard
    mk20((0, "ommmo"), (5, "oo"), (7, "o" + f(4) + "o"),
         (13, "oooo"), (17, "ddo")),                               # 13 arms top
    mk20((1, "ooo"), (4, "o"), (5, "ff"), (7, "o" + f(4) + "o"),
         (13, "ffww"), (17, "ddo")),                               # 14 shield bottom + arm
    mk20((4, "o"), (5, "ff"), (7, "o" + f(4) + "o"),
         (13, "ffww"), (17, "ddo")),                               # 15 arm + paw
    mk20((5, "ooo"), (7, "o" + f(4) + "o"),
         (12, "ooooo"), (17, "ddo")),                              # 16 arms bottom
    mk20((7, "oooooo"), (17, "oo")),                               # 17 feet + grip cap
]

def _put(grid, r, c, ch):
    """Return a copy of the whole grid with one pixel changed."""
    rows = list(grid)
    row = list(rows[r])
    row[c] = ch
    rows[r] = ''.join(row)
    return rows


# Eye pixels in the 20×18 grid: 2×2 blocks at rows 5-6, cols 7-8 / 11-12.
EYE_COLS = (7, 8, 11, 12)


def make_frames():
    """Derive the 10 MASCOT_DOG frames from the base 20×18 sprite.

    Mirrors the conventions of the previous dog data:
      walk  ×6  identical — the stride bob is applied in mascot_draw(),
                so the six arrays were byte-identical before too
      type  ×2  eye highlight toggles corner (tap feedback)
      happy ×1  eyes squint to a single row
      lookup×1  eyes shift up one row

    At 20×18 the eyes are solid 2×2 dark blocks (rows 5-6, cols 7-8 /
    11-12) with no room for a highlight pixel, so the old dog's trick of
    sliding the white highlight to the top of the eye is not available —
    lookup moves the whole block up one row instead, which reads the same
    way (pupils sitting high in the face).
    """
    base = BAKE_C
    walk = [base] * 6

    t1 = base
    for c in (7, 11):                    # top-left pixel of each eye
        t1 = _put(t1, 5, c, 'w')

    happy = base
    for c in EYE_COLS:                   # drop the lower eye row -> squint
        happy = _put(happy, 6, c, 'f')

    lookup = base
    for c in EYE_COLS:                   # whole eye block moves up a row
        lookup = _put(lookup, 6, c, 'f')
        lookup = _put(lookup, 4, c, 'o')

    return {
        'dog_w0': walk[0], 'dog_w1': walk[1], 'dog_w2': walk[2],
        'dog_w3': walk[3], 'dog_w4': walk[4], 'dog_w5': walk[5],
        'dog_type_f0': base, 'dog_type_f1': t1,
        'dog_happy_f': happy, 'dog_lookup_f': lookup,
    }


FRAME_ORDER = ['dog_w0', 'dog_w1', 'dog_w2', 'dog_w3', 'dog_w4', 'dog_w5',
               'dog_type_f0', 'dog_type_f1', 'dog_happy_f', 'dog_lookup_f']

FRAME_NOTE = {
    'dog_w0': 'WALK 0', 'dog_w1': 'WALK 1', 'dog_w2': 'WALK 2',
    'dog_w3': 'WALK 3', 'dog_w4': 'WALK 4', 'dog_w5': 'WALK 5',
    'dog_type_f0': 'TYPE 0 (paw down)', 'dog_type_f1': 'TYPE 1 (paw up)',
    'dog_happy_f': 'HAPPY (eyes squint)', 'dog_lookup_f': 'LOOKUP (eyes up)',
}


def emit_c_block(frames):
    """The exact C source for the MASCOT_DOG frame arrays."""
    out = []
    for name in FRAME_ORDER:
        out.append(f'static const char *const {name}[18] = {{')
        for r in frames[name]:
            out.append(f'    "{r}",')
        out.append('};')
    return "\n".join(out)


VARIANTS = [
    ("旧版 MASCOT_DOG（现役 20×18）", OLD, 'old'),
    ("烘焙 A · 直接 32×32 → 20×18", BAKE_A, 'new'),
    ("烘焙 B · 等比 18×18 居中留边", BAKE_B, 'new'),
    ("烘焙 C · 按母版手工重绘 20×18", BAKE_C, 'new'),
]

Z = 12
pad, head = 22, 58
cw = W_M * Z + 20
sheet = Image.new("RGB", (pad * 2 + cw * len(VARIANTS),
                          head + H_M * Z + 30 + H_M * 4 + 40),
                  (250, 250, 252))
d = ImageDraw.Draw(sheet)
d.text((pad, 12), "刀盾狗接入预览 — 底屏吉祥物实际画布 20×18（12× 放大，深色底模拟 3DS）",
       font=font(17), fill=(30, 30, 40))

for i, (label, grid, kind) in enumerate(VARIANTS):
    img = old_to_img(grid, Z) if kind == 'old' else grid_to_img(grid, Z)
    small = (old_to_img(grid, 1) if kind == 'old' else grid_to_img(grid, 1))
    x = pad + i * cw
    d.text((x, head - 22), label, font=font(14), fill=(40, 40, 60))

    bg = Image.new("RGBA", (W_M * Z, H_M * Z), DARK_BG)
    bg.alpha_composite(img)
    sheet.paste(bg.convert("RGB"), (x, head))

    # 1:1 strip under the big one
    y2 = head + H_M * Z + 26
    d.text((x, y2), "1:1 实际大小", font=font(14), fill=(90, 90, 100))
    bg2 = Image.new("RGBA", (W_M, H_M), DARK_BG)
    bg2.alpha_composite(small)
    sheet.paste(bg2.convert("RGB").resize((W_M * 4, H_M * 4), Image.NEAREST),
                (x, y2 + 20))

sheet.save(OUT / "dog_mascot_bake.png")
print("wrote", OUT / "dog_mascot_bake.png")

# ── the shipping candidate on its own: 1:1 + zoomed, light & dark ───────
ZZ = 10
one = grid_to_img(BAKE_C, 1)
big = grid_to_img(BAKE_C, ZZ)
sw, sh = W_M * ZZ, H_M * ZZ
card = Image.new("RGB", (22 * 3 + sw * 2, 46 + sh + 22 + H_M * 4 + 40),
                 (250, 250, 252))
dc = ImageDraw.Draw(card)
dc.text((22, 12), "烘焙 C — 手工重绘 20×18（底屏吉祥物实际尺寸）",
        font=font(17), fill=(30, 30, 40))
for i, (bgc, tag) in enumerate(((DARK_BG, "深色底（3DS 底屏）"),
                                ((255, 255, 255, 255), "浅色底"))):
    x = 22 + i * (sw + 22)
    b = Image.new("RGBA", (sw, sh), bgc)
    b.alpha_composite(big)
    card.paste(b.convert("RGB"), (x, 46))
    dc.text((x, 46 + sh + 6), tag, font=font(14), fill=(90, 90, 100))
    b2 = Image.new("RGBA", (W_M, H_M), bgc)
    b2.alpha_composite(one)
    card.paste(b2.convert("RGB").resize((W_M * 4, H_M * 4), Image.NEAREST),
               (x, 46 + sh + 26))
card.save(OUT / "dog_mascot_20x18.png")
print("wrote", OUT / "dog_mascot_20x18.png")

lines = ["/* 刀盾狗 — 20×18 mascot-canvas frames.",
         " * '.' transparent, o outline, f fur, w cream, m steel-L, d steel-D */", ""]
for name, grid, _ in VARIANTS[1:]:
    lines.append(f"/* {name} */")
    lines.append("static const char *const dog_bake[18] = {")
    for r in grid:
        lines.append(f'    "{r}",')
    lines += ["};", ""]

frames = make_frames()
lines += ["/* ── the 10 shipping MASCOT_DOG frames ─────────────────── */", ""]
lines.append(emit_c_block(frames))
(OUT / "dog_mascot_bake.txt").write_text("\n".join(lines), encoding="utf-8")
print("wrote", OUT / "dog_mascot_bake.txt")

# sanity: every frame is 20×18 and uses only the five palette chars
for nm, gr in frames.items():
    assert len(gr) == H_M, (nm, len(gr))
    for i, r in enumerate(gr):
        assert len(r) == W_M, (nm, i, len(r))
        assert set(r) <= set(PALETTE) | {'.'}, (nm, i, set(r))
print(f"frames OK: {len(frames)} × {W_M}×{H_M}")
for nm in FRAME_ORDER:
    assert nm in frames
print("frame diff vs base:",
      {nm: sum(1 for a, b in zip(frames[nm], frames['dog_w0']) if a != b)
       for nm in FRAME_ORDER if frames[nm] != frames['dog_w0']})

for title, grid in (("BAKE A (32×32 -> 20×18 nearest)", BAKE_A),
                    ("BAKE C (hand-redrawn 20×18)", BAKE_C),
                    ("OLD (current mascot.c dog_w0)", OLD)):
    print(f"\n--- {title} ---")
    for i, r in enumerate(grid):
        print(f"{i:2d} {r}")
