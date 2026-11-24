#!/usr/bin/env bash
# Renders the machine picker's screens to PNGs so a layout change — the
# two-line row, the ✎ strip, the top screen's move to the keyboard palette —
# can be looked at without flashing the console.
#
# Same host stub the machine-picker tests use, with text painting switched
# on — see tools/preview_machine_page.c.  Output goes to
# design/machine-page/ (the committed preview area, alongside
# design/project-page/ and design/mascot/), and is regenerated wholesale
# each run.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build design/machine-page

gcc -O2 -Wall -Wextra -std=c11 -Isource -Itools/stub \
    -o build/preview_machine_page \
    tools/preview_machine_page.c \
    tools/stub/pui_host_stubs.c \
    source/machine_ui.c \
    source/config.c \
    source/kb_theme.c \
    source/font_atlas.c \
    source/font_data.c \
    source/utf8_next.c \
    -lm

rm -f design/machine-page/*.png design/machine-page/*.bmp
PREVIEW_DIR="$PROJECT_ROOT/design/machine-page" ./build/preview_machine_page

for bmp in design/machine-page/*.bmp; do
    sips -s format png "$bmp" --out "${bmp%.bmp}.png" >/dev/null
    rm -f "$bmp"
done
ls -1 design/machine-page
