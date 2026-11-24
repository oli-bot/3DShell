#!/usr/bin/env python3
"""
bake_key_sound.py — turn a 22050 Hz mono PCM16 WAV into a C header
identical in shape to `source/key_sound_ji.h`.

Usage:
    python3 tools/bake_key_sound.py build/mascot_speech/xie.wav xie \
        > source/key_sound_xie.h

This is the second pass of the mascot-speech MVP.  Pass 1
(gen_mascot_speech.py) produces the WAVs; pass 2 bakes them into
`static const int16_t` arrays the existing audio.c g_voice[] table
can drop in next to KEY_SOUND_JI without further changes.

Layout (matches key_sound_ji.h verbatim so audio.c's
`{ KEY_SOUND_JI, KEY_SOUND_JI_LEN }` pattern is the only thing needed
on the call site):

    #pragma once
    #include <stdint.h>
    #define KEY_SOUND_<KEY>_LEN <N>
    static const int16_t KEY_SOUND_<KEY>[KEY_SOUND_<KEY>_LEN] = { ... };
"""

import struct
import sys
import wave
from pathlib import Path

def die(msg: str) -> "None":
    print(f"bake_key_sound: {msg}", file=sys.stderr)
    sys.exit(1)

def main() -> int:
    if len(sys.argv) != 3:
        die("usage: bake_key_sound.py <in.wav> <KEY>")
    wav_path = Path(sys.argv[1])
    key = sys.argv[2].upper()
    if not key.isidentifier():
        die(f"KEY must be a C identifier, got {key!r}")

    if not wav_path.is_file():
        die(f"no such file: {wav_path}")

    with wave.open(str(wav_path), "rb") as w:
        nch   = w.getnchannels()
        sw    = w.getsampwidth()
        rate  = w.getframerate()
        n     = w.getnframes()
        raw   = w.readframes(n)

    if nch != 1:
        die(f"expected mono, got {nch} channels")
    if sw != 2:
        die(f"expected 16-bit, got {sw * 8}-bit")
    if rate != 22050:
        die(f"expected 22050 Hz, got {rate}")

    samples = struct.unpack(f"<{n}h", raw)

    lines = []
    for i in range(0, n, 12):
        row = samples[i : i + 12]
        lines.append("    " + ", ".join(f"{s:6d}" for s in row) + ",")
    # Final row may have < 12 samples; still a valid C array literal.

    print(f"""#pragma once
#include <stdint.h>

/*
 * Auto-baked from {wav_path.name} (22050 Hz mono PCM16).
 * Pipeline: 22050 mono PCM16 WAV -> bake_key_sound.py.
 * (The upstream pipeline that produced this WAV — say TTS, a
 *  user-supplied recording via import_*.py, or anything else — is
 *  documented in tools/.)
 *
 * Total {n} samples = {n * 1000 // 22050}.{((n * 1000) % 22050) * 10 // 22050} ms @ 22050 Hz.
 * Drop into `g_voice[]` exactly like KEY_SOUND_JI.
 */

#define KEY_SOUND_{key}_LEN {n}u

static const int16_t KEY_SOUND_{key}[KEY_SOUND_{key}_LEN] = {{
{chr(10).join(lines)}
}};
""")
    return 0

if __name__ == "__main__":
    sys.exit(main())