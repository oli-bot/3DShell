#!/usr/bin/env bash
# Compile + run the host-side flat-JSON reader test.
#
# Built with AddressSanitizer on purpose: the case this module exists for
# (searching a reply buffer that is not NUL-terminated) is an out-of-bounds
# read that plain -O2 runs straight through and reports as a pass.  Probe
# once so a toolchain without the runtime says so instead of quietly losing
# the coverage.
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

gcc -O2 -Wall -Wextra -std=c11 $SAN \
    -o build/test_json_find \
    tools/test_json_find.c source/json_find.c
./build/test_json_find
