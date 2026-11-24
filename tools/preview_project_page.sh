#!/usr/bin/env bash
# Renders the project page's screens to PNGs so a layout change can be
# looked at without flashing the console.
#
# Same host stub the project-page tests use, with text painting switched
# on — see tools/preview_project_page.c.  Output goes to
# design/project-page/ (the committed preview area, alongside
# design/mascot/), and is regenerated wholesale each run.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build design/project-page

gcc -O2 -Wall -Wextra -std=c11 -Isource -Itools/stub \
    -o build/preview_project_page \
    tools/preview_project_page.c \
    tools/stub/pui_host_stubs.c \
    source/project_ui.c \
    source/config.c \
    source/project_presets.c \
    source/kb_theme.c \
    source/font_atlas.c \
    source/font_data.c \
    source/utf8_next.c \
    -lm

rm -f design/project-page/*.png design/project-page/*.bmp
PREVIEW_DIR="$PROJECT_ROOT/design/project-page" ./build/preview_project_page

for bmp in design/project-page/*.bmp; do
    sips -s format png "$bmp" --out "${bmp%.bmp}.png" >/dev/null
    rm -f "$bmp"
done
ls -1 design/project-page
