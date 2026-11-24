#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build

# Same sanitizer the scrollback tests insist on: this suite checks that a
# rejected value leaves its colour alone, and an out-of-bounds field offset in
# theme_ini.c's key table would otherwise just corrupt a neighbour quietly.
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
printf 'int main(void){return 0;}\n' > build/.san-probe.c
if ! gcc $SAN build/.san-probe.c -o build/.san-probe 2>/dev/null; then
    SAN="-fsanitize=undefined"
    echo "note: AddressSanitizer unavailable — bounds coverage degraded" >&2
fi
rm -f build/.san-probe build/.san-probe.c

gcc -O2 -Wall -Wextra -std=c11 $SAN \
    -o build/test_theme_ini \
    tools/test_theme_ini.c source/theme_ini.c source/kb_theme.c source/terminal.c
./build/test_theme_ini
