#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 -Isource \
    -o build/test_keyrepeat \
    tools/test_keyrepeat.c
./build/test_keyrepeat
