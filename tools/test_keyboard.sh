#!/usr/bin/env bash
# Compile + run the host-side physical-keyboard tests.
#
# source/keyboard.c is the one input layer that decides what actually
# reaches the SSH pipe — SELECT/A/B/D-pad and the Y/X modifier state
# machine — and it is pure logic: no C2D_/C3D_ symbol, and its only 3DS
# dependency is the `u32` typedef plus the KEY_* bits in keyboard.h.
# tools/stub/3ds.h supplies both, so the whole binding table runs
# natively.
#
# terminal.c (+ font_atlas/font_data, which it calls into) are linked for
# real rather than stubbed: keyboard.c calls terminal_scroll_view() on the
# circle-pad and A-with-scrollback paths, and a hand-written stub would
# let those call sites rot without anyone noticing.  The tests pass NULL
# for the terminal, so no scrollback actually runs.
#
# Built with AddressSanitizer on purpose: the emissions it pins are
# memcpy'd into a fixed 64-byte out_buf (the A-key "dump the pinyin
# buffer" path writes up to IME_BUFFER_MAX bytes into it), and an
# over-long write is silent under plain -O2.  Probe once so a toolchain
# without the runtime says so instead of quietly losing the coverage.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build

SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
printf 'int main(void){return 0;}\n' > build/.san-probe.c
if ! gcc $SAN build/.san-probe.c -o build/.san-probe 2>/dev/null; then
    SAN="-fsanitize=undefined"
    echo "note: AddressSanitizer unavailable - bounds coverage degraded" >&2
fi
rm -f build/.san-probe build/.san-probe.c

gcc -O2 -Wall -Wextra -std=c11 -Isource -Itools/stub \
    -o build/test_keyboard \
    tools/test_keyboard.c \
    source/keyboard.c \
    source/ime_pinyin.c \
    source/terminal.c \
    source/font_atlas.c \
    source/font_data.c \
    -lm
./build/test_keyboard
