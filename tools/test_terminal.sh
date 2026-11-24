#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build

# The scrollback-stride regressions are heap overruns, which plain -O2
# happily runs through: without a sanitizer the suite reports a pass while
# reading and writing past the end of the ring.  Probe once so a toolchain
# without the runtime says so instead of quietly losing the coverage.
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
printf 'int main(void){return 0;}\n' > build/.san-probe.c
if ! gcc $SAN build/.san-probe.c -o build/.san-probe 2>/dev/null; then
    SAN="-fsanitize=undefined"
    echo "note: AddressSanitizer unavailable — bounds coverage degraded" >&2
fi
rm -f build/.san-probe build/.san-probe.c

gcc -O2 -Wall -Wextra -std=c11 $SAN \
    -o build/test_terminal \
    tools/test_terminal.c source/terminal.c
./build/test_terminal
