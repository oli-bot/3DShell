#!/usr/bin/env bash
# Host-side tests for the SSH machine picker (source/machine_ui.c).
#
# machine_ui.c names no C2D_/C3D_ symbol — every rect and glyph goes
# through the renderer_draw_* wrappers — so the same tools/stub/ that
# stands in for the project page's 3DS surface is enough to compile and
# run the row state machine, the touch mapping and the per-theme colours
# natively.  See tools/stub/pui_host_stubs.c for what is stubbed.
#
# font_atlas.c / font_data.c are linked for real (both are citro2d-free)
# so the glyph boxes the ink check scans are the ones the device gets.
# kb_theme.c comes along because the bottom screen wears the live
# keyboard palette, and utf8_next.c because the row labels are clipped
# through source/text_fit.h, which is built on the one decoder.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 -Isource -Itools/stub \
    -o build/test_machine_ui \
    tools/test_machine_ui.c \
    tools/stub/pui_host_stubs.c \
    source/machine_ui.c \
    source/config.c \
    source/kb_theme.c \
    source/font_atlas.c \
    source/font_data.c \
    source/utf8_next.c \
    -lm
./build/test_machine_ui
