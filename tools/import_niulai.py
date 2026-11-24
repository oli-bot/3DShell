#!/usr/bin/env python3
"""
import_niulai.py — one-off: convert the user-supplied stereo 44.1 kHz
WAV at Downloads/niulai.wav into the same 22050 Hz mono PCM16 WAV
shape that gen_mascot_speech.py produces, so bake_key_sound.py can
turn it into a key_sound_niulai.h the same way as the other 5 voices.

This script is intentionally NOT a general importer — it hardcodes
the source path and the target name because the user asked for this
specific swap (NIULAI mascot voice from "哞" to a custom recording).
For any other source file, gen_mascot_speech.py is the right tool.

Pipeline (matches key_sound_ji.h comment):
  stereo L/R average -> 2:1 downsample with [0.25 0.5 0.25] anti-alias
  -> trim silence -> peak-norm to 0.62 -> 5 ms raised-cosine fade-out
  -> write 22050 Hz mono PCM16 WAV
"""

import math
import struct
import sys
import wave
from pathlib import Path

SRC = Path("/Users/lianganbang/Workspace/DSSH/build/mascot_speech/niulai_src.wav")
DST = Path("/Users/lianganbang/Workspace/DSSH/build/mascot_speech/niulai.wav")

TARGET_RATE   = 22050
TARGET_PEAK   = 0.62
FADE_MS       = 5
ANTI_ALIAS    = (0.25, 0.5, 0.25)   # 3-tap, sums to 1.0


def read_wav(path: Path):
    """Return (rate, nchannels, sampwidth, raw_bytes, n_frames)."""
    with wave.open(str(path), "rb") as w:
        return (w.getframerate(),
                w.getnchannels(),
                w.getsampwidth(),
                w.readframes(w.getnframes()),
                w.getnframes())


def stereo_to_mono_lr_avg(raw: bytes) -> list[int]:
    """Decode interleaved stereo int16 and average L+R into mono int16."""
    n = len(raw) // 4           # 2 bytes * 2 channels per frame
    samples = struct.unpack(f"<{2 * n}h", raw)
    out = []
    for i in range(n):
        l = samples[2 * i]
        r = samples[2 * i + 1]
        # Round-half-away-from-zero matches what most audio editors do.
        v = (l + r + (1 if (l + r) >= 0 else -1)) // 2
        out.append(v)
    return out


def downsample_2x(samples: list[int]) -> list[int]:
    """2:1 downsample with [0.25, 0.5, 0.25] anti-alias lowpass.
    Output length = floor(N/2).  The filter removes energy above the
    new Nyquist (11025 Hz) so we don't fold 22 kHz speech harmonics
    back into the audible band.
    """
    n = len(samples)
    out_len = n // 2
    out = [0] * out_len
    for i in range(out_len):
        a = samples[2 * i - 1] if 2 * i - 1 >= 0 else samples[2 * i]
        b = samples[2 * i]
        c = samples[2 * i + 1] if 2 * i + 1 < n else samples[2 * i]
        out[i] = int(round(ANTI_ALIAS[0] * a +
                           ANTI_ALIAS[1] * b +
                           ANTI_ALIAS[2] * c))
    return out


def trim_silence(samples: list[int], threshold: int = 200) -> tuple[int, int]:
    """Trim leading/trailing silence.  Threshold 200 (~-44 dBFS) keeps
    soft speech attacks; lower (e.g. 100) would eat more plosives."""
    n = len(samples)
    start = 0
    while start < n and abs(samples[start]) < threshold:
        start += 1
    end = n
    while end > start and abs(samples[end - 1]) < threshold:
        end -= 1
    return start, end


def peak_normalise(samples: list[int], target: float) -> list[int]:
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
    n_fade = int(TARGET_RATE * fade_ms / 1000)
    if n_fade <= 0 or n_fade >= len(samples):
        return samples
    out = list(samples)
    for i, s in enumerate(out[-n_fade:]):
        t = (n_fade - i) / n_fade
        amp = math.cos(math.pi / 2 * (1.0 - t))
        out[-n_fade + i] = int(s * amp)
    out[-1] = 0
    return out


def write_wav(path: Path, samples: list[int]) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(TARGET_RATE)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def main() -> int:
    if not SRC.is_file():
        print(f"missing {SRC}", file=sys.stderr)
        return 1

    rate, nch, sw, raw, n = read_wav(SRC)
    print(f"src: rate={rate} ch={nch} sw={sw} frames={n} ({n / rate * 1000:.1f} ms)")
    if nch != 2 or sw != 2 or rate != 44100:
        print("expected stereo int16 @ 44100 Hz", file=sys.stderr)
        return 1

    mono = stereo_to_mono_lr_avg(raw)
    print(f"  -> mono: {len(mono)} samples")
    down = downsample_2x(mono)
    print(f"  -> 22050: {len(down)} samples ({len(down) / TARGET_RATE * 1000:.1f} ms)")
    s, e = trim_silence(down)
    print(f"  -> trim: keep [{s},{e})  trimmed {(s) + (len(down) - e)} samples "
          f"(leading {s} + trailing {len(down) - e})")
    trimmed = down[s:e]
    if not trimmed:
        print("trim_silence ate the whole signal — sample is silent?", file=sys.stderr)
        return 1
    normed = peak_normalise(trimmed, TARGET_PEAK)
    faded = raised_cosine_fade_out(normed, FADE_MS)
    write_wav(DST, faded)
    print(f"  -> {DST}  {len(faded)} samples ({len(faded) / TARGET_RATE * 1000:.1f} ms)")
    return 0


if __name__ == "__main__":
    sys.exit(main())