#!/usr/bin/env python3
"""
gen_mascot_speech.py — generate baked speech WAVs for the five mascot
voices that don't yet have one (CRAB / PHAGE / ALIEN / NIULAI / DOG).

For each entry in MASCOT_WORDS this script:
  1. Runs macOS `say -v 'Grandpa (中文（中国大陆）)'` to render the word
     to a temp AIFF (no header padding, no leading beep — `say` adds
     200 ms of silence around the word; we trim that in step 3).
  2. Converts to 22050 Hz mono PCM16 via `afconvert`.
  3. Reads the samples, trims leading/trailing silence, applies a
     half-cosine fade-out for the last 5 ms, and peaks-normalises to
     TARGET_PEAK.
  4. Writes `<out>/<key>.wav` as 22050 Hz mono PCM16 (so the
     subsequent C-header bake step can read it as raw PCM).

The processing chain matches `source/key_sound_ji.h`'s comment:
  stereo->mono mix -> 2:1 downsample with [0.25 0.5 0.25] anti-alias
  -> trim silence -> peak norm to 0.62 -> 5 ms raised-cosine fade-out.
We start from mono AIFF so the stereo->mono step is moot, and the
downsample step is also moot since `say` lets us ask for 22050 Hz
directly (see RATE below).

The C-side baked slots need the same conditions:
  - 22050 Hz, mono PCM16 (matches CLICK_RATE — ndsp ch0 is configured
    for this rate in audio.c:189)
  - same quiet envelope so the existing "drop overlapping taps" gate
    in audio_play_key() feels consistent.

Output files:
  build/mascot_speech/<key>.wav     one per mascot (CI artifact only)

This script is intentionally idempotent: re-running overwrites.

NOTE: requires macOS (uses `say` + `afconvert`).
"""

import os
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

# --- Config ----------------------------------------------------------------

# (output_key, chinese_text).  Output key becomes both the .wav basename
# and (in phase 2) the C identifier suffix in key_sound_<key>.h.
MASCOT_WORDS = [
    ("xie",    "蟹"),       # CRAB
    ("jun",    "菌"),       # PHAGE
    ("biubiu", "哔哔"),     # ALIEN
    ("mou",    "哞"),       # NIULAI
    ("wang",   "汪"),       # DOG
]

# Match the existing chicken (key_sound_ji.h) chain so the per-mascot
# baked slot sounds consistent: 22050 Hz, mono, peak 0.62.
RATE       = 22050
TARGET_PEAK = 0.62
FADE_MS    = 5

VOICE = "Grandpa (中文（中国大陆）)"   # only Mandarin male on stock macOS

# --- say / afconvert helpers -----------------------------------------------

def render_with_say(text: str, out_aiff: Path) -> None:
    """Render `text` to an AIFF file using the configured voice.
    Don't pass --data-format: stock `say` rejects LEI16@22050 with
    'Opening output file failed: fmt?' on recent macOS — the default
    AIFF-C output is what afconvert expects anyway.
    """
    cmd = ["say", "-v", VOICE, "-o", str(out_aiff), text]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"say failed: {r.stderr.strip()}")

def aiff_to_wav(aiff: Path, wav: Path) -> None:
    """afconvert AIFF -> WAV (PCM16, mono, 22050 Hz)."""
    cmd = [
        "afconvert",
        "-f", "WAVE",        # output format
        "-d", "LEI16",       # 16-bit little-endian PCM
        "-c", "1",       # 1 channel = mono
        "-r", str(RATE),     # sample rate
        str(aiff),
        str(wav),
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"afconvert failed: {r.stderr.strip()}")

# --- WAV I/O (stdlib only) -------------------------------------------------

def read_wav_mono_pcm16(path: Path) -> list[int]:
    """Read a 16-bit mono PCM WAV and return the raw int16 samples."""
    with wave.open(str(path), "rb") as w:
        assert w.getsampwidth() == 2, "expected 16-bit"
        assert w.getnchannels() == 1, "expected mono"
        assert w.getframerate() == RATE, f"expected {RATE} Hz, got {w.getframerate()}"
        n = w.getnframes()
        raw = w.readframes(n)
    return list(struct.unpack(f"<{n}h", raw))

def write_wav_mono_pcm16(path: Path, samples: list[int]) -> None:
    """Write 16-bit mono PCM WAV."""
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))

# --- DSP -------------------------------------------------------------------

def trim_silence(samples: list[int], threshold: int = 200) -> tuple[int, int]:
    """Return (start, end) sample indices such that |samples[i]| >= threshold
    in [start, end) and the rest is silence.  `say` outputs a flat-zero
    pad at both ends — threshold 200 (~-44 dBFS for int16) avoids eating
    the soft attack of real speech.
    """
    n = len(samples)
    start = 0
    while start < n and abs(samples[start]) < threshold:
        start += 1
    end = n
    while end > start and abs(samples[end - 1]) < threshold:
        end -= 1
    return start, end

def peak_normalise(samples: list[int], target: float) -> list[int]:
    """Multiply by (target / max|sample|).  Clamps to int16 range."""
    if not samples:
        return samples
    peak = max(abs(s) for s in samples)
    if peak == 0:
        return samples
    gain = (target * 32767.0) / peak
    out = []
    for s in samples:
        v = s * gain
        if v >  32767: v =  32767
        if v < -32768: v = -32768
        out.append(int(v))
    return out

def raised_cosine_fade_out(samples: list[int], fade_ms: int) -> list[int]:
    """Apply a half-cosine fade-out across the last `fade_ms` ms — prevents
    a hard click on voices that end on a non-zero sample.  Symmetric:
    `amp(t) = cos(pi/2 * (fade_n - t) / fade_n)` so the very last sample
    is exactly zero and the curve is smooth at t=0 (start of fade).
    """
    n_fade = int(RATE * fade_ms / 1000)
    if n_fade <= 0 or n_fade >= len(samples):
        return samples
    import math
    out = list(samples)
    for i, s in enumerate(out[-n_fade:]):
        t = (n_fade - i) / n_fade          # 1 at fade start, 0 at end
        amp = math.cos(math.pi / 2 * (1.0 - t))  # 0 at t=1, 1 at t=0
        out[-n_fade + i] = int(s * amp)
    out[-1] = 0                           # belt + suspenders
    return out

def process(samples: list[int]) -> list[int]:
    s, e = trim_silence(samples)
    trimmed = samples[s:e]
    if not trimmed:
        raise RuntimeError("trim_silence ate the whole signal — say output was silent")
    trimmed = peak_normalise(trimmed, TARGET_PEAK)
    trimmed = raised_cosine_fade_out(trimmed, FADE_MS)
    return trimmed

# --- Driver ----------------------------------------------------------------

def main() -> int:
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/mascot_speech")
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for key, text in MASCOT_WORDS:
            aiff = tmp / f"{key}.aiff"
            wav  = tmp / f"{key}.wav"
            out  = out_dir / f"{key}.wav"

            print(f"[{key}] say {text!r}")
            render_with_say(text, aiff)
            aiff_to_wav(aiff, wav)

            samples = read_wav_mono_pcm16(wav)
            processed = process(samples)
            write_wav_mono_pcm16(out, processed)

            ms = len(processed) * 1000 / RATE
            print(f"  -> {out}  {len(processed)} samples  {ms:.1f} ms")
    return 0

if __name__ == "__main__":
    sys.exit(main())