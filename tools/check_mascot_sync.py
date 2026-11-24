#!/usr/bin/env python3
"""Cross-check tools/preview_mascot.py's duplicated frame data against
source/mascot.c.  Run standalone, or via preview_mascot.py which calls it.

Exit code 0 = in sync, 1 = drift (prints which array and which rows).
"""
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASCOT_C = ROOT / 'source' / 'mascot.c'

# preview symbol -> the C walk-cycle table it must mirror.
# Resolving through `*_walk_frames[]` rather than assuming w0..w5 matters:
# crab/phage/alien only declare w0,w1,w2,w4,w5 and reuse w0 for the 4th
# frame, so a naive w0..w5 mapping reports phantom drift.
PAIRS = [
    ('CRAB_ORIG',  'crab_orig_frames',  'crab_orig_walk_frames'),
    ('CRAB',       'crab_frames',       'crab_walk_frames'),
    ('CHICK',      'chick_frames',      'chick_walk_frames'),
    ('PHAGE',      'phage_frames',      'phage_walk_frames'),
    ('ALIEN',      'alien_frames',      'alien_walk_frames'),
    ('NIULAI',     'niulai_walk_frames', 'niulai_walk_frames'),
    ('DOG',        'dog_frames',        'dog_walk_frames'),
]


def parse_mascot_c(path=MASCOT_C):
    """name -> list of rows, for every `static const char *const X[N] = {...}`."""
    src = path.read_text()
    out = {}
    for m in re.finditer(r'static const char \*const (\w+)\[(\d+)\]\s*=\s*\{(.*?)\n\};',
                         src, re.S):
        out[m.group(1)] = re.findall(r'"([^"]*)"', m.group(3))
    return out


def parse_walk_tables(path=MASCOT_C):
    """`X_walk_frames[6] = { a, b, ... }` -> {'X_walk_frames': ['a','b',...]}."""
    src = path.read_text()
    out = {}
    for m in re.finditer(r'static const char \*const \*const (\w+)\[(\d+)\]\s*=\s*\{(.*?)\n\};',
                         src, re.S):
        out[m.group(1)] = re.findall(r'\b(\w+)\b', m.group(3))
    return out


def load_preview():
    spec = importlib.util.spec_from_file_location(
        'preview_mascot', ROOT / 'tools' / 'preview_mascot.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)          # __name__ != "__main__" -> main() not run
    return mod


def check(verbose=True):
    c = parse_mascot_c()
    tables = parse_walk_tables()
    pv = load_preview()
    problems = []
    covered = set()

    for label, attr, table in PAIRS:
        frames = getattr(pv, attr, None)
        if frames is None:
            problems.append(f'{label}: preview has no `{attr}`')
            continue
        cnames = tables.get(table)
        if cnames is None:
            problems.append(f'{label}: source/mascot.c has no `{table}`')
            continue
        if len(frames) != len(cnames):
            problems.append(f'{label}: {len(frames)} preview frames vs '
                            f'{len(cnames)} entries in {table}')
            continue
        for i, (grid, cname) in enumerate(zip(frames, cnames)):
            crows = c.get(cname)
            if crows is None:
                problems.append(f'{label}: source/mascot.c has no `{cname}`')
                continue
            covered.add(cname)
            if len(grid) != len(crows):
                problems.append(f'{label}/{cname}: {len(grid)} rows vs {len(crows)}')
                continue
            for r, (a, b) in enumerate(zip(grid, crows)):
                if a != b:
                    problems.append(f'{label}/{cname} row {r}:\n'
                                    f'    preview: {a}\n'
                                    f'    mascot.c: {b}')

    # arrays the preview never renders are not an error, but silence is how
    # DOG went missing — always say something about them.
    extra = sorted(n for n in c
                   if re.match(r'^(crab_orig|crab|chick|phage|alien|niulai|dog)_', n)
                   and n not in covered)
    if extra and verbose:
        by_kind = {}
        for n in extra:
            kind = n.rsplit('_', 1)[0]          # e.g. crab_type, chick_play
            by_kind.setdefault(kind, []).append(n)
        kinds = ', '.join(f'{k}×{len(v)}' for k, v in sorted(by_kind.items()))
        print(f'  note: the preview renders walk cycles only — {len(extra)} '
              f'other state array(s) in mascot.c are not depicted ({kinds})')

    if problems:
        print(f'\n!! preview_mascot.py is OUT OF SYNC with source/mascot.c '
              f'({len(problems)} issue(s)):')
        for p in problems[:40]:
            print('   -', p)
        if len(problems) > 40:
            print(f'   ... and {len(problems) - 40} more')
        return False

    if verbose:
        total = sum(len(getattr(pv, a)) for _, a, _ in PAIRS)
        print(f'  ✓ preview_mascot.py matches source/mascot.c '
              f'({len(PAIRS)} species, {total} arrays)')
    return True

if __name__ == '__main__':
    sys.exit(0 if check() else 1)
