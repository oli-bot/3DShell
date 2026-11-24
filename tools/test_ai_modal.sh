#!/usr/bin/env bash
# Host-side tests for the AI-ask modal (source/ai_modal.c).
#
# ai_modal.c names no C2D_/C3D_ symbol — every rect and glyph goes through
# the renderer_draw_* wrappers — so the same tools/stub/ that stands in
# for the project page's 3DS surface is enough to compile and run the
# fade machine, the markdown transform and the wrap/ellipsis logic
# natively.  See tools/stub/pui_host_stubs.c for what is stubbed.
#
# font_atlas.c / font_data.c are linked for real (both are citro2d-free)
# so the glyph metrics the wrap and the question ellipsis use are the
# same ones the device gets.  kb_theme.c comes along because the modal
# now wears the live keyboard palette (ai_modal_draw takes a kb_pal_t*)
# instead of its own seven Tokyo Night constants.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 -Isource -Itools/stub \
    -o build/test_ai_modal \
    tools/test_ai_modal.c \
    tools/stub/pui_host_stubs.c \
    source/ai_modal.c \
    source/kb_theme.c \
    source/font_atlas.c \
    source/font_data.c \
    source/utf8_next.c \
    -lm
./build/test_ai_modal
