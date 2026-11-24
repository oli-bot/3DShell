#!/usr/bin/env python3
"""Outline audit for mascot sprites in source/mascot.c.

Some sprites are drawn with a 1-px dark outline, which means every opaque
pixel that is NOT the outline character must be sealed off from the
background by an outline pixel (or by the canvas edge).  Nothing enforced
that, so the 刀盾狗 bake shipped with bare steel on its grip, bare fur
between its ears, and bare fur at its chin corners — all of which are
invisible at 1× until you magnify the sprite.

Only the sprites that actually declare an outline are audited.  The others
are deliberately outline-less (the crab / phage / alien draw their "outline"
in the same colour as their fill; the chick and 牛来 use their dark colour
for eyes / horns / jacket rather than as a perimeter), so auditing them
would just produce noise.  Add an entry to OUTLINE_CHARS when a sprite
adopts the convention.

Usage:
    python3 tools/audit_mascot_outline.py                 # audit everything declared
    python3 tools/audit_mascot_outline.py --species dog
    python3 tools/audit_mascot_outline.py --grid          # dump bad frames

Exit code 0 = clean, 1 = gaps found.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASCOT_C = ROOT / 'source' / 'mascot.c'

# species prefix -> the character that draws its perimeter outline.
# None / absent = this sprite has no perimeter outline by design.
OUTLINE_CHARS = {
    'dog': 'o',
}


def parse(path=MASCOT_C):
    src = path.read_text()
    arrays = {}
    for m in re.finditer(r'static const char \*const (\w+)\[(\d+)\]\s*=\s*\{(.*?)\n\};',
                         src, re.S):
        arrays[m.group(1)] = re.findall(r'"([^"]*)"', m.group(3))
    return arrays


def audit(rows, outline):
    """Fill pixels (not '.', not `outline`) that have a background neighbour."""
    h = len(rows)
    bad = []
    for r in range(h):
        for c, ch in enumerate(rows[r]):
            if ch == '.' or ch == outline:
                continue
            for dr, dc in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                rr, cc = r + dr, c + dc
                if not (0 <= rr < h and 0 <= cc < len(rows[rr])):
                    continue                      # canvas edge seals it
                if rows[rr][cc] == '.':
                    bad.append((r, c, ch))
                    break
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--species', help='substring filter on the array name')
    ap.add_argument('--frame', help='exact array name')
    ap.add_argument('--grid', action='store_true',
                    help='dump offending frames with the bad pixels marked "!"')
    args = ap.parse_args()

    arrays = parse()
    dirty = []
    checked = 0
    for name in sorted(arrays):
        if args.frame and name != args.frame:
            continue
        if args.species and args.species not in name:
            continue
        prefix = name.split('_')[0]
        outline = OUTLINE_CHARS.get(prefix)
        if outline is None:
            continue
        rows = arrays[name]
        if not rows:
            continue
        checked += 1
        bad = audit(rows, outline)
        if bad:
            dirty.append((name, rows, bad))

    if not dirty:
        print(f'  ✓ outline audit clean — {checked} array(s) checked, '
              f'outline char(s) {sorted(set(OUTLINE_CHARS.values()))}')
        return 0

    total = sum(len(b) for _, _, b in dirty)
    print(f'\n!! {len(dirty)} frame(s) have fill pixels sitting on the '
          f'background ({total} pixel(s)):')
    for name, rows, bad in dirty:
        shown = ', '.join(f'({r},{c}){ch!r}' for r, c, ch in bad[:12])
        more = f' … +{len(bad) - 12}' if len(bad) > 12 else ''
        print(f'   {name}: {shown}{more}')
        if args.grid:
            marks = {(r, c) for r, c, _ in bad}
            for r, row in enumerate(rows):
                print(f'      {r:2d} '
                      + ''.join('!' if (r, c) in marks else ch
                                for c, ch in enumerate(row)))
            print()
    print('\n   Fix: give those pixels an outline neighbour, or turn them into\n'
          '   the outline character if the shape genuinely ends there.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
