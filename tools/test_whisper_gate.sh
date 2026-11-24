#!/usr/bin/env bash
# Non-speech gate regression test.  Needs numpy (+ pywhispercpp, since it
# imports the daemon module), so prefer the whisper venv when present.
set -euo pipefail
cd "$(dirname "$0")/.."

VENV="$HOME/.local/share/dssh-whisper/venv/bin/python"
if [ -x "$VENV" ]; then
  PY="$VENV"
elif python3 -c 'import numpy, pywhispercpp' 2>/dev/null; then
  PY=python3
else
  echo "need numpy + pywhispercpp: run this from the dssh-whisper venv" >&2
  exit 1
fi

exec "$PY" tools/test_whisper_gate.py "$@"
