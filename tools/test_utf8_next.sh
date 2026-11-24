#!/usr/bin/env bash
# Compile + run the host-side UTF-8 decoder test.
#
# source/utf8_next.c holds the decoder behind every label helper in
# renderer.c (IME candidate strip, window chip, key-popup bubble) and the
# pixel measurement those helpers centre against.  It is pure byte
# handling: no C2D_/C3D_ symbol, no 3DS service, and its only dependency
# is font_atlas.c + the generated font_data.c, which
# tools/test_font_atlas.sh already links on the host.  So the three
# families of ill-formed sequence it has to reject — overlongs, surrogate
# halves and codepoints past U+10FFFF — are checked instead of trusted.
#
# Built with AddressSanitizer on purpose: "the decoder never reads past
# the terminator" is exactly the kind of guarantee plain -O2 reports as a
# pass no matter how far past it walks.  Probe once so a toolchain without
# the runtime says so instead of quietly losing the coverage.
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
    -o build/test_utf8_next \
    tools/test_utf8_next.c \
    source/utf8_next.c \
    source/font_atlas.c \
    source/font_data.c
./build/test_utf8_next
