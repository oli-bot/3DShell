#!/usr/bin/env bash
# Host-side tests for the agent-CLI preset table (source/project_presets.c).
# No 3DS toolchain needed — the module is deliberately dependency-free.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 \
    -o build/test_project_presets \
    tools/test_project_presets.c source/project_presets.c
./build/test_project_presets
