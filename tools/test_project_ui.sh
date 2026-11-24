#!/usr/bin/env bash
# Host-side tests for the project page's state machine.
#
# source/project_ui.c normally needs the 3DS toolchain, but only because
# <citro2d.h> defines C3D_RenderTarget and the KEY_* bits.  It calls no
# C2D_* symbol of its own — every draw goes through the renderer_draw_*
# wrappers — so tools/stub/ supplies those two things and the rest of
# the module compiles and runs natively.  See tools/stub/pui_host_stubs.c
# for what else is stubbed and why.
#
# font_atlas.c is linked for real (it is citro2d-free) so the text width
# the page uses to clip and centre is the same width the device gets.
# utf8_next.c comes along for the same reason since the clip/ellipsis
# helpers moved to source/text_fit.h, which is built on the one decoder.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 -Isource -Itools/stub \
    -o build/test_project_ui \
    tools/test_project_ui.c \
    tools/stub/pui_host_stubs.c \
    source/project_ui.c \
    source/config.c \
    source/project_presets.c \
    source/kb_theme.c \
    source/font_atlas.c \
    source/font_data.c \
    source/utf8_next.c \
    -lm
./build/test_project_ui
