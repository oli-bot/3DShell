#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 -Itools/stub -Iinclude -Isource \
    -o build/test_mascot_e2e \
    tools/test_mascot_e2e.c
./build/test_mascot_e2e
