#!/usr/bin/env bash
# Compile + run the host-side AI-request JSON writer test.
#
# Built with AddressSanitizer on purpose: the writer takes user speech and
# server-controlled text and emits it into a caller-sized buffer, so the
# interesting failures are one-past-the-cap writes that plain -O2 runs
# straight through and reports as a pass.  Probe once so a toolchain
# without the runtime says so instead of quietly losing the coverage.
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
    -o build/test_json_build \
    tools/test_json_build.c source/json_build.c
./build/test_json_build
