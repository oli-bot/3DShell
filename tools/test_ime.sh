#!/usr/bin/env bash
# Compile + run the host-side IME smoke test.  Uses system gcc, no
# 3DS toolchain needed — much faster iteration than `make` + 3dslink.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build

# The malformed-dict cases are out-of-bounds reads on the blob; plain -O2
# runs straight through them and reports a pass.  Probe once so a toolchain
# without the runtime says so instead of quietly losing the coverage.
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
printf 'int main(void){return 0;}\n' > build/.san-probe.c
if ! gcc $SAN build/.san-probe.c -o build/.san-probe 2>/dev/null; then
    SAN="-fsanitize=undefined"
    echo "note: AddressSanitizer unavailable - bounds coverage degraded" >&2
fi
rm -f build/.san-probe build/.san-probe.c

gcc -O2 -Wall -Wextra -std=c11 $SAN \
    -o build/test_ime \
    tools/test_ime.c source/ime_pinyin.c
./build/test_ime "$@"
