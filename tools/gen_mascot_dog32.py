#!/usr/bin/env python3
"""
刀盾狗 (Blade & Shield Dog) — 32×32 pixel sprite.  DSSH mascot rework.

Design brief:
  • 32×32 low-res pixel sprite, front-facing standing Shiba
  • big head / small body, round face, upright ears, dopey-cute face
  • orange-yellow fur + white muzzle
  • one paw holds a small round shield, the other a short blade
  • blade & shield clearly stick out past both sides of the body
  • dark pixel outline, minimal blocks, 5 colours, transparent background
  • must stay readable when shrunk to 20 px tall

Outputs (design/mascot/):
  dog_knife_shield_32.png           the sprite, 32×32, transparent
  dog_knife_shield_preview.png      8× zoom: on checkerboard + on 3DS-dark bg
  dog_knife_shield_readability.png  down-scale ladder + silhouette test
  dog_knife_shield.svg              crisp vector version
  dog_knife_shield_map.txt          char map + palette, for C integration

Run:  python3 tools/gen_mascot_dog32.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "design" / "mascot"
OUT.mkdir(parents=True, exist_ok=True)

W = H = 32

# ── Palette: exactly 5 opaque colours (+ transparent) ───────────────────
PALETTE = {
    'o': (0x2B, 0x1A, 0x10, 255),   # 深棕描边 (dark outline)
    'f': (0xF2, 0xA0, 0x3C, 255),   # 橙黄毛发 (orange-yellow fur)
    'w': (0xFF, 0xF4, 0xDF, 255),   # 白口鼻 / 胸 / 掌 (cream muzzle, chest, paw)
    'm': (0xC9, 0xD8, 0xE4, 255),   # 钢亮面 (steel light)
    'd': (0x7B, 0x8F, 0xA0, 255),   # 钢暗面 (steel dark)
}
LABELS = {
    'o': 'outline  #2B1A10',
    'f': 'fur      #F2A03C',
    'w': 'cream    #FFF4DF',
    'm': 'steel-L  #C9D8E4',
    'd': 'steel-D  #7B8FA0',
}

DARK_BG = (24, 24, 37, 255)          # 3DS bottom-screen backdrop


def mk(*segs):
    """Build one 32-char row from (start_col, text) segments."""
    a = ['.'] * W
    for c, t in segs:
        assert c >= 0 and c + len(t) <= W, f"segment out of range: col={c} {t!r}"
        for i, ch in enumerate(t):
            a[c + i] = ch
    return ''.join(a)


def f(n):
    return 'f' * n


# ── The sprite ──────────────────────────────────────────────────────────
#  col budget:  0-7 shield │ 8-23 head/body │ 24-25 gap │ 26-30 blade
GRID = [
    # --- ears (rows 0-4) ---
    mk((10, "oo"), (20, "oo")),                                  # 0  ear tips
    mk((9, "offo"), (19, "offo")),                               # 1
    mk((9, "ofwfo"), (18, "ofwfo")),                             # 2
    mk((8, "ofwwfo"), (18, "ofwwfo")),                           # 3
    mk((8, "ofwwfo"), (18, "ofwwfo")),                           # 4

    # --- head (rows 5-20) ---
    #  blade = 4 px wide (o,m,m,o) and 16 rows tall: a 4:1 ratio is what
    #  keeps it reading as a blade rather than a hammer once it is scaled
    #  down to 20 px.  A 5 px blade with a 7 px guard read as a mallet.
    mk((9, "o" + f(12) + "o"), (28, "oo")),                      # 5  crown + blade tip
    mk((8, "o" + f(14) + "o"), (27, "ommo")),                    # 6
    mk((8, "o" + f(14) + "o"), (27, "ommo")),                    # 7
    mk((8, "o" + f(14) + "o"), (27, "ommo")),                    # 8
    mk((8, "o" + f(14) + "o"), (27, "ommo")),                    # 9
    mk((8, "o" + f(14) + "o"), (27, "ommo")),                    # 10
    mk((8, "o" + "ff" + "oo" + f(6) + "oo" + "ff" + "o"),
       (27, "ommo")),                                            # 11 eyes
    mk((8, "o" + "ff" + "oo" + f(6) + "oo" + "ff" + "o"),
       (27, "ommo")),                                            # 12 eyes
    mk((8, "o" + "fffff" + "wwww" + "fffff" + "o"),
       (27, "ommo")),                                            # 13 muzzle top
    mk((8, "o" + "ffff" + "wwooww" + "ffff" + "o"),
       (27, "ommo")),                                            # 14 nose
    mk((8, "o" + "ffff" + "wwooww" + "ffff" + "o"),
       (27, "ommo")),                                            # 15 nose
    mk((8, "o" + "ffff" + "wwwwww" + "ffff" + "o"),
       (27, "ommo")),                                            # 16
    # row 17 has no mouth line: at 2 px wide a mouth under the nose read
    # as a "1" instead of a snout, so the nose carries the expression.
    mk((8, "o" + "ffff" + "wwwwww" + "ffff" + "o"),
       (27, "ommo")),                                            # 17
    mk((2, "oooo"),
       (8, "o" + "ffff" + "wwwwww" + "ffff" + "o"),
       (27, "ommo")),                                            # 18 shield top
    mk((1, "ommmmo"),
       (9, "o" + "ffff" + "wwww" + "ffff" + "o"),
       (27, "ommo")),                                            # 19
    mk((0, "ommmmmmo"),
       (10, "o" + "fff" + "wwww" + "fff" + "o"),
       (27, "oooo")),                                            # 20 chin + blade base

    # --- body + arms + weapons (rows 21-27) ---
    #  both arms sit on rows 22-25.  A 1-px gap at col 11 / col 20 keeps
    #  them reading as limbs instead of one solid dark slab, and the
    #  cream paw overlaps the grip by 1 px so it looks like it grips it.
    mk((0, "ommmmmmo"),
       (12, "o" + f(6) + "o"),
       (26, "oddddo")),                                          # 21 body top + crossguard
    mk((0, "ommddmmo"), (8, "ooo"),
       (12, "o" + f(6) + "o"),
       (21, "oooooo"), (27, "ddd")),                             # 22 arms top + grip
    mk((0, "ommddmmo"), (8, "fff"),
       (12, "o" + f(6) + "o"),
       (21, "fff"), (24, "wwww"), (28, "dd")),                   # 23 arms + paw gripping
    mk((0, "ommmmmmo"), (8, "fff"),
       (12, "o" + f(6) + "o"),
       (21, "fff"), (24, "wwww"), (28, "dd")),                   # 24 arms + paw gripping
    mk((1, "ommmmo"), (8, "ooo"),
       (12, "o" + f(6) + "o"),
       (21, "oooooo"), (27, "ddd")),                             # 25 arms bottom + grip
    mk((2, "oooo"),
       (12, "o" + f(6) + "o"),
       (27, "ooo")),                                             # 26 shield bottom + pommel
    mk((12, "o" + f(6) + "o")),                                  # 27 belly

    # --- legs (rows 28-30) ---
    mk((12, "ofo"), (17, "ofo")),                                # 28
    mk((12, "owo"), (17, "owo")),                                # 29 cream paws
    mk((12, "ooo"), (17, "ooo")),                                # 30 sole

    mk(),                                                        # 31
]

# ── Validate ────────────────────────────────────────────────────────────
assert len(GRID) == H, f"need {H} rows, got {len(GRID)}"
for _i, _r in enumerate(GRID):
    assert len(_r) == W, f"row {_i}: {len(_r)} chars (want {W})"
    for _ch in _r:
        assert _ch in PALETTE or _ch == '.', f"row {_i}: bad char {_ch!r}"
_used = {c for r in GRID for c in r if c != '.'}
assert _used <= set(PALETTE), _used
print(f"grid OK: {W}x{H}, {len(_used)} colours used -> {sorted(_used)}")


# ── Render helpers ──────────────────────────────────────────────────────
def render(grid=GRID, w=W, h=H, scale=1, flat=None):
    """Rasterise the char grid.  flat=colour -> solid silhouette."""
    img = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    px = img.load()
    for r in range(h):
        for c in range(w):
            ch = grid[r][c]
            if ch == '.':
                continue
            col = flat if flat else PALETTE[ch]
            for dy in range(scale):
                for dx in range(scale):
                    px[c * scale + dx, r * scale + dy] = col
    return img


def checker(w, h, sq=8, a=(255, 255, 255, 255), b=(228, 228, 232, 255)):
    img = Image.new("RGBA", (w, h), a)
    d = ImageDraw.Draw(img)
    for y in range(0, h, sq):
        for x in range(0, w, sq):
            if (x // sq + y // sq) % 2:
                d.rectangle([x, y, x + sq - 1, y + sq - 1], fill=b)
    return img


def font(sz):
    try:
        return ImageFont.load_default(size=sz)
    except TypeError:                      # Pillow < 10.1
        return ImageFont.load_default()


# ── 1. the sprite ───────────────────────────────────────────────────────
sprite = render()
sprite.save(OUT / "dog_knife_shield_32.png")
print("wrote", OUT / "dog_knife_shield_32.png")

# ── 2. preview sheet: checkerboard + dark 3DS bg, both at 8× ────────────
Z = 8
big = render(scale=Z)
SZ = W * Z

pad, head = 24, 44
sheet_w = pad * 3 + SZ * 2
sheet_h = head + pad * 2 + SZ
sheet = Image.new("RGBA", (sheet_w, sheet_h), (250, 250, 252, 255))
d = ImageDraw.Draw(sheet)
d.text((pad, 14), "刀盾狗 · 32×32 sprite   (5 colours, transparent bg)",
       font=font(20), fill=(30, 30, 40, 255))

cb = checker(SZ, SZ, sq=Z * 2)
cb.alpha_composite(big)
sheet.alpha_composite(cb, (pad, head))
d.text((pad, head + SZ + 6), "8× on checkerboard → background is transparent",
       font=font(15), fill=(90, 90, 100, 255))

dark = Image.new("RGBA", (SZ, SZ), DARK_BG)
dark.alpha_composite(big)
sheet.alpha_composite(dark, (pad * 2 + SZ, head))
d.text((pad * 2 + SZ, head + SZ + 6), "8× on 3DS bottom-screen bg",
       font=font(15), fill=(90, 90, 100, 255))

sheet.convert("RGB").save(OUT / "dog_knife_shield_preview.png")
print("wrote", OUT / "dog_knife_shield_preview.png")

# ── 3. the 20 px-tall rendering (the size the mascot row actually uses) ─
small20 = sprite.resize((20, 20), Image.NEAREST)
small20.save(OUT / "dog_knife_shield_20.png")
sil20 = render(flat=(0x2B, 0x1A, 0x10, 255)).resize((20, 20), Image.NEAREST)
sil20.save(OUT / "dog_knife_shield_20_silhouette.png")
print("wrote", OUT / "dog_knife_shield_20.png", "+ silhouette")

# ── 4. readability ladder + silhouette test ─────────────────────────────
sizes = [32, 28, 24, 20, 16]
Z2 = 8
row_h = 32 * Z2
gutter = 26
ladder_w = pad * 2 + sum(s * Z2 for s in sizes) + gutter * (len(sizes) - 1)
sheet2_h = 40 + (row_h + 46) * 2 + pad * 2

sheet2 = Image.new("RGBA", (max(ladder_w, 560), sheet2_h), (250, 250, 252, 255))
d2 = ImageDraw.Draw(sheet2)
d2.text((pad, 12), "Down-scale ladder — 每个尺寸放大 8× 显示，检验 20px 高度是否仍可辨认",
        font=font(19), fill=(30, 30, 40, 255))

x = pad
y0 = 40
for s in sizes:
    small = sprite.resize((s, s), Image.NEAREST)
    shown = small.resize((s * Z2, s * Z2), Image.NEAREST)
    yy = y0 + (row_h - s * Z2) // 2
    bg = checker(s * Z2, s * Z2, sq=Z2 * 2)
    bg.alpha_composite(shown)
    sheet2.alpha_composite(bg, (x, yy))
    lab = f"{s}px" + ("  ← 目标" if s == 20 else "")
    d2.text((x + 2, y0 + row_h + 6), lab, font=font(17),
            fill=(200, 60, 60, 255) if s == 20 else (90, 90, 100, 255))
    x += s * Z2 + gutter

# silhouette row
y1 = y0 + row_h + 46
d2.text((pad, y1 - 26), "Silhouette test — 纯色剪影是否还能读出「拿刀和盾的狗」",
        font=font(19), fill=(30, 30, 40, 255))
x = pad
for s in sizes:
    small = sprite.resize((s, s), Image.NEAREST)
    sil = render(flat=(0x2B, 0x1A, 0x10, 255)).resize((s, s), Image.NEAREST)
    shown = sil.resize((s * Z2, s * Z2), Image.NEAREST)
    yy = y1 + (row_h - s * Z2) // 2
    bg = Image.new("RGBA", (s * Z2, s * Z2), (255, 236, 214, 255))
    bg.alpha_composite(shown)
    sheet2.alpha_composite(bg, (x, yy))
    d2.text((x + 2, y1 + row_h + 6), f"{s}px", font=font(17), fill=(90, 90, 100, 255))
    x += s * Z2 + gutter

sheet2.convert("RGB").save(OUT / "dog_knife_shield_readability.png")
print("wrote", OUT / "dog_knife_shield_readability.png")

# ── 5. SVG (crisp at any zoom) ──────────────────────────────────────────
parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="320" height="320" '
         'viewBox="0 0 32 32" shape-rendering="crispEdges">']
for r in range(H):
    c = 0
    while c < W:
        ch = GRID[r][c]
        if ch == '.':
            c += 1
            continue
        run = 1
        while c + run < W and GRID[r][c + run] == ch:
            run += 1
        col = PALETTE[ch]
        parts.append(
            f'<rect x="{c}" y="{r}" width="{run}" height="1" '
            f'fill="#{col[0]:02X}{col[1]:02X}{col[2]:02X}"/>')
        c += run
parts.append('</svg>')
(OUT / "dog_knife_shield.svg").write_text("\n".join(parts), encoding="utf-8")
print("wrote", OUT / "dog_knife_shield.svg")

# ── 6. char map + palette (drop-in for mascot.c) ────────────────────────
lines = [
    "/* 刀盾狗 — 32×32 pixel sprite, front-facing Shiba with a round",
    " * shield (left paw) and a short blade (right paw).",
    " * 5 colours: o outline, f orange-yellow fur, w cream muzzle/paw,",
    " *            m steel light, d steel dark.   '.' = transparent.",
    " * Generated by tools/gen_mascot_dog32.py — edit there, not here. */",
    "",
    "static const char *const dog32_frame[32] = {",
]
for r in GRID:
    lines.append(f'    "{r}",')
lines += ["};", "", "/* palette */"]
for k in ['o', 'f', 'w', 'm', 'd']:
    col = PALETTE[k]
    lines.append(f'/*   {k}  #{col[0]:02X}{col[1]:02X}{col[2]:02X}   {LABELS[k]} */')
lines.append("")
lines.append("/* empty-row check: row 31 is fully transparent (breathing room) */")
(OUT / "dog_knife_shield_map.txt").write_text("\n".join(lines), encoding="utf-8")
print("wrote", OUT / "dog_knife_shield_map.txt")

# ── terminal readout ────────────────────────────────────────────────────
print()
for i, r in enumerate(GRID):
    print(f"{i:2d} {r}")
