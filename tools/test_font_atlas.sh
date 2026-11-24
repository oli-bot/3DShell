#!/usr/bin/env bash
# Compile + run the host-side font atlas test.
#
# source/font_atlas.c decides how many character cells a codepoint occupies
# and which bitmap it resolves to.  It calls no C2D_/C3D_ symbol and no 3DS
# service — its only dependency is the generated table in source/font_data.c,
# which tools/test_project_ui.sh already links on the host — so the whole
# atlas runs natively.
#
# Built with AddressSanitizer on purpose: every lookup here is a binary
# search over a generated table, and the interesting failure mode is an
# index that walks off the end.  Probe once so a toolchain without the
# runtime says so instead of quietly losing the coverage.
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

gcc -O2 -Wall -Wextra -std=c11 -Isource $SAN \
    -o build/test_font_atlas \
    tools/test_font_atlas.c source/font_atlas.c source/font_data.c
./build/test_font_atlas
