#!/usr/bin/env python3
"""Render a before/after sheet for the 刀盾狗 mascot redraw.

Left column  = the old MASCOT_DOG sprite as it shipped (pulled straight out
               of git history so the comparison is honest).
Right column = the new 20×18 frames baked by tools/bake_mascot_dog20.py.

Three rows: 6× on the 3DS bottom-screen grey, 1× on the same grey (the size
the player actually sees), and the flat silhouette of each.
"""
import re
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path('/Users/lianganbang/Workspace/DSSH')
OUT = ROOT / 'design' / 'mascot'
OUT.mkdir(parents=True, exist_ok=True)

# 3DS bottom screen is 320×240; the mascot sits on the same dark strip the
# other species do, so use that as the comparison backdrop.
BG_DARK = (0x2a, 0x2a, 0x33, 255)
BG_LIGHT = (0xf4, 0xf4, 0xf0, 255)

OLD_PAL = {
    '@': (0xd9, 0xa8, 0x4a, 255), 'D': (0x8b, 0x56, 0x2b, 255),
    'C': (0xf2, 0xe2, 0xb3, 255), 'W': (0xff, 0xff, 0xff, 255),
    '#': (0x14, 0x0e, 0x0a, 255), 'P': (0xe8, 0x78, 0x78, 255),
    'S': (0xe6, 0xe6, 0xe6, 255), 's': (0x96, 0x96, 0x96, 255),
    'M': (0xe6, 0xe6, 0xe6, 255), 'K': (0x5a, 0x50, 0x4b, 255),
    'G': (0x3c, 0x2d, 0x23, 255),
}
NEW_PAL = {
    'o': (0x2b, 0x1a, 0x10, 255), 'f': (0xf2, 0xa0, 0x3c, 255),
    'w': (0xff, 0xf4, 0xdf, 255), 'm': (0xc9, 0xd8, 0xe4, 255),
    'd': (0x7b, 0x8f, 0xa0, 255),
}


def old_frames():
    """The shipping dog_w0/dog_type_f1/dog_happy_f from git HEAD."""
    src = subprocess.run(['git', '-C', str(ROOT), 'show', 'HEAD:source/mascot.c'],
                         capture_output=True, text=True, check=True).stdout
    out = {}
    # the old data had the moved eye-glint in dog_type_f0 (f1 was a copy of w0)
    for n in ('dog_w0', 'dog_type_f0', 'dog_happy_f'):
        m = re.search(r'static const char \*const %s\[18\] = \{(.*?)\n\};' % n, src, re.S)
        out[n] = re.findall(r'"([^"]*)"', m.group(1))
    return out


def new_frames():
    src = (ROOT / 'source' / 'mascot.c').read_text()
    out = {}
    for n in ('dog_w0', 'dog_type_f1', 'dog_happy_f'):
        m = re.search(r'static const char \*const %s\[18\] = \{(.*?)\n\};' % n, src, re.S)
        out[n] = re.findall(r'"([^"]*)"', m.group(1))
    return out


def render(grid, pal, scale, bg=None):
    """Grid -> RGBA image at `scale`.  bg=None keeps transparency."""
    w, h = len(grid[0]), len(grid)
    img = Image.new('RGBA', (w * scale, h * scale), bg or (0, 0, 0, 0))
    for r, row in enumerate(grid):
        for c, ch in enumerate(row):
            col = pal.get(ch)
            if col is None:
                continue
            for dy in range(scale):
                for dx in range(scale):
                    img.putpixel((c * scale + dx, r * scale + dy), col)
    return img


def silhouette(grid, scale, color=(0x20, 0x20, 0x24, 255)):
    w, h = len(grid[0]), len(grid)
    img = Image.new('RGBA', (w * scale, h * scale), (0, 0, 0, 0))
    for r, row in enumerate(grid):
        for c, ch in enumerate(row):
            if ch == '.':
                continue
            for dy in range(scale):
                for dx in range(scale):
                    img.putpixel((c * scale + dx, r * scale + dy), color)
    return img


def label(draw, xy, text, font, fill):
    draw.text(xy, text, font=font, fill=fill)


old, new = old_frames(), new_frames()

S7, S5 = 7, 5                  # 7× blow-up, 5× for the native-size tiles
CJK = '/System/Library/Fonts/Supplemental/Arial Unicode.ttf'
f_title = ImageFont.truetype(CJK, 16)
f_head = ImageFont.truetype(CJK, 15)
f_body = ImageFont.truetype(CJK, 13)
f_tag = ImageFont.truetype(CJK, 12)

W = 680
COLX = (22, 348)
TILE = 20 * S5                 # 100 px native tile
GAP = 8
COLW = 3 * TILE + 2 * GAP      # 316
R1, R2, R3 = 78, 248, 392      # row tops
H = R3 + 18 * S7 + 18

sheet = Image.new('RGBA', (W, H), (0xff, 0xff, 0xff, 255))
d = ImageDraw.Draw(sheet)

d.rectangle([0, 0, W, 44], fill=(0x1e, 0x1e, 0x26, 255))
d.text((16, 13), '刀盾狗 MASCOT_DOG — 改造前 / 改造后   (20×18 mascot canvas)',
       font=f_title, fill=(0xff, 0xff, 0xff, 255))

for i, t in enumerate(('放大 7×', '实际尺寸 · 三种表情', '轮廓剪影')):
    d.text((10, (R1, R2, R3)[i] + 4), t, font=f_tag, fill=(0x88, 0x88, 0x92, 255))

for col, (frames, pal, name, keys) in enumerate((
        (old, OLD_PAL, '改造前（旧：@ D C W # P S s M K G）',
         ('dog_w0', 'dog_type_f0', 'dog_happy_f')),
        (new, NEW_PAL, '改造后（新：o f w m d）',
         ('dog_w0', 'dog_type_f1', 'dog_happy_f')))):
    x = COLX[col]
    d.text((x, 52), name, font=f_head, fill=(0x40, 0x40, 0x48, 255))

    # --- 7× on the dark strip the mascot actually sits on
    big = render(frames['dog_w0'], pal, S7, BG_DARK)
    sheet.alpha_composite(big, (x, R1 + 26))
    d.rectangle([x - 1, R1 + 25, x + big.width, R1 + 27 + big.height],
                outline=(0xd0, 0xd0, 0xd8, 255))

    # --- native-size tiles: the two type frames + the happy face
    y = R2 + 26
    for j, key in enumerate(keys):
        tx = x + j * (TILE + GAP)
        small = render(frames[key], pal, S5, BG_LIGHT)
        sheet.alpha_composite(small, (tx, y))
        d.rectangle([tx - 1, y - 1, tx + small.width, y + small.height],
                    outline=(0xdd, 0xdd, 0xe2, 255))
        tag = ('walk', 'type', 'happy')[j]
        d.text((tx, y + small.height + 5), tag, font=f_tag, fill=(0x70, 0x70, 0x7a, 255))

    # --- flat silhouette: what survives when the sprite is a blob
    sil = silhouette(frames['dog_w0'], S7)
    sheet.alpha_composite(sil, (x, R3 + 26))

sheet.convert('RGB').save(OUT / 'dog_before_after.png')
print('wrote', OUT / 'dog_before_after.png')

# ---- a real-size strip: 8 copies each, with the walk bob applied ----------
strip = Image.new('RGBA', (W, 176), (0xff, 0xff, 0xff, 255))
d2 = ImageDraw.Draw(strip)
d2.rectangle([0, 0, W, 38], fill=(0x1e, 0x1e, 0x26, 255))
d2.text((16, 10), '底屏实际观感 · 走路时上下抖动（上排旧 / 下排新）',
        font=f_head, fill=(0xff, 0xff, 0xff, 255))

BANDW = 8 * 24 + 8
for row, (frames, pal) in enumerate(((old, OLD_PAL), (new, NEW_PAL))):
    y = 56 + row * 58
    band = Image.new('RGBA', (BANDW, 24), BG_DARK)
    for k in range(8):
        bob = 1 if (k & 1) else 0          # mascot_draw(): yi -= 1 on odd frames
        band.alpha_composite(render(frames['dog_w0'], pal, 1), (k * 24 + 4, bob))
    strip.alpha_composite(band, (20, y))
    d2.rectangle([19, y - 1, 20 + BANDW, y + 24], outline=(0xd0, 0xd0, 0xd8, 255))
    d2.text((30 + BANDW, y + 4), '旧' if row == 0 else '新', font=f_body,
            fill=(0x50, 0x50, 0x58, 255))

strip.convert('RGB').save(OUT / 'dog_bottom_screen_strip.png')
print('wrote', OUT / 'dog_bottom_screen_strip.png')
