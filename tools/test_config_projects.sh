#!/usr/bin/env bash
# Host-side smoke test for the project-bookmarks settings section.
# Reuses source/config.c; no 3DS toolchain needed.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 \
    -o build/test_config_projects \
    tools/test_config_projects.c source/config.c
./build/test_config_projects