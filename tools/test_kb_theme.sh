#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 -Isource \
    -o build/test_kb_theme \
    tools/test_kb_theme.c source/kb_theme.c -lm
./build/test_kb_theme
