#!/usr/bin/env bash
# Host-side smoke test for softkb i18n table invariants.
# The actual settings_items[] table is in source/softkb.c and depends
# on citro2d, so we cannot run it on host.  This test guards the
# invariants that DO travel through public headers / source-level
# inspection: the LANG_* count, the field count, and the max nopts
# ceiling.  Anything more granular lives in the 3DS build.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
gcc -O2 -Wall -Wextra -std=c11 \
    -o build/test_softkb_i18n \
    tools/test_softkb_i18n.c
./build/test_softkb_i18n
