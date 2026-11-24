#!/usr/bin/env python3
"""Regression test for the dssh-whisper daemon's non-speech gate.

The gate is the only thing standing between whisper.cpp and the subtitle
boilerplate it invents for silent recordings, and its thresholds were
calibrated against measured numbers (see whisper_daemon.gate_verdict).
Tuning them without this test flying is how the hallucination comes back.

Both directions are asserted:

  reject     — synthetic noise (always runs)
  accept     — synthetic speech-like audio (always runs), plus any real WAVs
               passed as arguments (16 kHz mono int16 from `say` or a recorder)
  known-bad  — a verified false rejection, pinned as a tripwire (see below)

The accept side used to run only when you handed it a recording, and the
Makefile target hands it nothing — so a threshold change that muted the user
outright still printed "0 failure(s)".  The fixtures below are synthesised in
code so the target has teeth with no external audio file:

    python3 tools/test_whisper_gate.py ~/some/clip.wav   # extra, optional
"""
import importlib.util
import math
import random
import sys
import wave
from pathlib import Path

import numpy as np

DAEMON = Path(__file__).with_name("whisper_daemon.py")


def load_daemon():
    spec = importlib.util.spec_from_file_location("whisper_daemon", DAEMON)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)     # module import must not need the model
    return mod


WD = load_daemon()
# Both of these live in the daemon.  Keeping a second copy here is how the
# test and the thing it tests drift apart, so there is exactly one definition.
CAPTURE_RATE = WD.CAPTURE_RATE


def scaled(x, target_rms):
    cur = math.sqrt((x ** 2).mean())
    f = target_rms / cur if cur > 0 else 1.0
    return np.clip(x * f, -32767, 32767).astype(np.int16)


def lowpass(x, taps):
    y = np.convolve(x, np.ones(taps) / taps, "same")
    peak = np.abs(y).max()
    return np.clip(y * 30000 / peak if peak > 0 else y, -32767, 32767).astype(np.int16)


def noise_fixtures(sr):
    random.seed(11)
    white = lambda sec, amp: np.array(                       # noqa: E731
        [random.randint(-amp, amp) for _ in range(int(sr * sec))], float)
    gauss = lambda sec, sd: np.array(                        # noqa: E731
        [int(random.gauss(0, sd)) for _ in range(int(sr * sec))], np.int16)
    n = lambda sec: np.arange(int(sec * sr))                 # noqa: E731
    return {
        "digital silence 1.5s": np.zeros(int(sr * 1.5), np.int16),
        "digital silence 15s":  np.zeros(int(sr * 15), np.int16),
        "white noise (quiet)":  lowpass(white(3, 48), 1),
        "white noise (loud)":   lowpass(white(3, 1500), 1),
        "gaussian hiss":        gauss(4, 400),
        "low-pass rumble x8":   lowpass(white(3, 3000), 8),
        "low-pass rumble x60":  lowpass(white(3, 3000), 60),
        "low-pass rumble x200": lowpass(white(3, 3000), 200),
        "50 Hz mains hum":      (3000 * np.sin(2 * np.pi * 50 * n(3) / sr)).astype(np.int16),
        "220 Hz tone":          (3000 * np.sin(2 * np.pi * 220 * n(2) / sr)).astype(np.int16),
        "click train":          np.array(
            [(random.choice([-1, 1]) * 20000 if i % 400 == 0 else 0)
             for i in range(int(sr * 3))], dtype=np.int16),
    }


# ── Synthetic speech ──────────────────────────────────────────────
# A real recording cannot be committed (size, privacy, and the machine that
# runs this may have no audio at all), but the accept side needs coverage just
# as much as the reject side does.  So build the acoustics the gate keys on:
# a harmonic glottal source shaped by three formants, a syllable-rate
# amplitude envelope with genuine breath pauses, and brief unvoiced
# fricative bursts.  Nothing here is tuned to the thresholds — the shapes are
# the textbook ones, and the gate either agrees or it doesn't.
FORMANTS = ((500.0, 90.0, 1.0), (1500.0, 120.0, 0.6), (2500.0, 180.0, 0.35))


def _glottal(f0_curve, t, sr, rng):
    out = np.zeros_like(t)
    phase = 2 * np.pi * np.cumsum(f0_curve) / sr
    for k in range(1, 41):
        f = k * float(np.mean(f0_curve))
        if f > 0.45 * sr:
            break
        gain = 0.0
        for fc, bw, amp in FORMANTS:
            gain += amp / (1.0 + ((f - fc) / bw) ** 2)
        if gain < 1e-4:
            continue
        out += gain * np.sin(k * phase + rng.uniform(0, 2 * np.pi))
    return out


def _syllable_envelope(t, rng, rate=4.2):
    env = np.zeros_like(t)
    period = 1.0 / rate
    for i in range(int(t[-1] / period) + 1):
        centre = (i + 0.5) * period + rng.uniform(-0.25, 0.25) * period
        width = period * rng.uniform(0.22, 0.34)
        env += rng.uniform(0.55, 1.0) * np.exp(-0.5 * ((t - centre) / width) ** 2)
    for start in np.arange(1.2, t[-1] - 0.3, 1.4):          # a breath pause
        env *= 1.0 - 0.92 * np.exp(-0.5 * ((t - (start + 0.16)) / 0.13) ** 2)
    return np.clip(env, 0, None)


def _fricatives(t, rng):
    out = np.zeros_like(t)
    for start in np.arange(0.35, t[-1] - 0.2, 0.62):
        m = (t >= start) & (t < start + 0.07)
        noise = np.diff(np.concatenate([[0.0], rng.normal(0, 1, int(m.sum()))]))
        out[m] = noise * 0.5                                # first diff ≈ high-pass
    return out


def speech_like(sec, sr=CAPTURE_RATE, seed=7):
    """A speech-shaped waveform of `sec` seconds, peak-normalised float."""
    n = int(sr * sec)
    if n < 2:
        return np.zeros(0)
    rng = np.random.default_rng(seed)                       # deterministic
    t = np.arange(n) / sr
    f0 = (128.0 * (1.0 - 0.18 * t / max(t[-1], 1e-9))       # declination
          * (1.0 + 0.05 * np.sin(2 * np.pi * 1.7 * t)))     # jitter
    sig = _glottal(f0, t, sr, rng) * _syllable_envelope(t, rng) + _fricatives(t, rng) * 0.6
    peak = np.abs(sig).max()
    return sig / peak * 26000.0 if peak > 0 else np.zeros(n)


def framed(samples, sr, lead=0.3, tail=0.3):
    """Put an utterance inside the silence a real capture always has around
    it — the 3DS records from one START press to the next, so every real clip
    is speech padded with room tone.  This matters: the padding raises
    modDepth (silent frames add variance) and lowers tonalFrac (they also
    dilute it), which is exactly why real captures measure modDepth 1.0-2.4
    and tonalFrac 0.16-0.34 rather than the continuous-speech 0.52-0.70 /
    0.39-0.65 the thresholds were first calibrated against."""
    return np.concatenate([np.zeros(int(sr * lead), np.int16),
                           samples,
                           np.zeros(int(sr * tail), np.int16)])


def synthetic_speech(sr):
    """Utterances that MUST be accepted.  The gate is level-invariant (both
    conditions are ratios), so each duration is checked at whisper's three
    calibration levels: loud, normal, and the quiet end of a real 3DS mic.
    Durations start at 0.3 s because a one-word reply is the shortest thing
    the device can produce."""
    out = {}
    for sec in (0.3, 0.6, 0.8, 1.5, 3.0):
        base = speech_like(sec, sr)
        for level in (6000, 600, 80):
            out[f"synth {sec}s @{level}rms"] = framed(scaled(base, level), sr)
    return out


def known_false_rejections(sr):
    """A verified false rejection, pinned so that fixing it is noticed.

    tonalFrac counts silent frames in its denominator, so one short word
    followed by a long pause drops under MIN_TONAL_FRAC and is swallowed with
    no word to the user.  This is not hypothetical: the live daemon log
    contains exactly this rejection — a real capture logged as
    "reject: tonal 0.15 < 0.15" — and it is why the quietest *accepted* real
    capture measured tonalFrac 0.16 against a 0.15 threshold.

    Fixing it properly means scoring tonality over active frames only, which
    changes what the threshold means and needs re-calibrating against real
    audio; that is a product decision, so this round pins the current
    behaviour instead.  If these cases start passing, the defect is fixed —
    update this function and the calibration table in whisper_daemon.py.
    """
    out = {}
    for speech_sec, tail in ((0.5, 3.0), (0.5, 4.0)):
        out[f"{speech_sec}s word + {tail}s tail"] = framed(
            scaled(speech_like(speech_sec, sr), 400), sr, tail=tail)
    return out


def speech_fixtures(sr, paths):
    out = {}
    for p in paths:
        with wave.open(str(p)) as w:
            assert w.getnchannels() == 1 and w.getsampwidth() == 2, f"{p} not mono int16"
            x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64)
            rate = w.getframerate()
        x = x[np.linspace(0, len(x) - 1, int(len(x) * sr / rate)).astype(int)]
        name = Path(p).stem
        for level in (6000, 600, 80):
            out[f"{name} @{level}rms"] = scaled(x, level)
        # Short single-word replies are the tightest case for the gate.
        env = np.abs(x)
        start = int(np.argmax(env > env.max() * 0.2))
        for sec in (0.3, 0.8):
            out[f"{name} {sec}s short"] = scaled(x[start:start + int(sr * sec)], 400)
    return out


# ── resample_to_16k() coverage ────────────────────────────────────
def check_resample(fails):
    """resample_to_16k() had no coverage at all, and it is the fix for a
    2.25 % stretch / ~38-cent pitch drop.  Asserting only the output length
    would pass for an identity function, so assert the *pitch* survives."""
    n = CAPTURE_RATE
    tone = (12000 * np.sin(2 * np.pi * 220.0 * np.arange(n) / CAPTURE_RATE)).astype(np.int16)

    def peak_hz(x, rate):
        spec = np.abs(np.fft.rfft(x.astype(np.float64) * np.hanning(x.size)))
        return float(np.fft.rfftfreq(x.size, 1.0 / rate)[int(np.argmax(spec))])

    out = WD.resample_to_16k(tone)
    want = int(round(n * WD.SAMPLE_RATE / CAPTURE_RATE))
    hz_in, hz_out = peak_hz(tone, CAPTURE_RATE), peak_hz(out, WD.SAMPLE_RATE)
    naive = peak_hz(tone, WD.SAMPLE_RATE)       # what the bug looked like

    cases = [
        ("length scales to 16 kHz", abs(out.size - want) <= 1, f"{out.size} vs {want}"),
        ("dtype stays int16", out.dtype == np.int16, str(out.dtype)),
        ("220 Hz stays 220 Hz", abs(hz_out - 220.0) < 3.0, f"{hz_out:.1f} Hz"),
        ("resample is not a no-op", abs(hz_in - 220.0) < 3.0, f"input {hz_in:.1f} Hz"),
        # Self-check: if the fix were reverted the pitch check above would have
        # to fail, otherwise it is not actually testing anything.
        ("pitch check has teeth", abs(naive - 220.0) > 3.0,
         f"unresampled reads {naive:.1f} Hz"),
        ("degenerate input passes through", WD.resample_to_16k(np.zeros(1, np.int16)).size == 1,
         "size<2"),
    ]
    for name, ok, detail in cases:
        fails += not ok
        print(f"{'PASS' if ok else 'FAIL'}  resample {name:26s} {detail}")
    return fails


def main(argv):
    fails = 0

    for name, samples in noise_fixtures(CAPTURE_RATE).items():
        verdict, m = WD.gate_verdict(samples)
        ok = verdict != "speech"
        fails += not ok
        print(f"{'PASS' if ok else 'FAIL'}  reject  {name:26s} "
              f"rms={m['rms']:8.1f} mod={m['mod']:.2f} tonal={m['tonal']:.2f}"
              + ("" if ok else f"   -> {verdict}"))

    for name, samples in synthetic_speech(CAPTURE_RATE).items():
        verdict, m = WD.gate_verdict(samples)
        ok = verdict == "speech"
        fails += not ok
        print(f"{'PASS' if ok else 'FAIL'}  accept  {name:26s} "
              f"rms={m['rms']:8.1f} mod={m['mod']:.2f} tonal={m['tonal']:.2f}"
              + ("" if ok else f"   -> {verdict}"))

    for name, samples in known_false_rejections(CAPTURE_RATE).items():
        verdict, m = WD.gate_verdict(samples)
        detail = (f"rms={m['rms']:8.1f} mod={m['mod']:.2f} tonal={m['tonal']:.2f}")
        if verdict.startswith("reject: tonal"):
            # Pinned defect, not desired behaviour — see the docstring.
            print(f"PASS  known-bad {name:26s} {detail}   -> {verdict}")
        else:
            fails += 1
            print(f"FAIL  known-bad {name:26s} {detail}   -> {verdict}")
            print("      the silence-dilution rejection is gone; if that was "
                  "deliberate, update known_false_rejections() and the "
                  "calibration table in whisper_daemon.py")

    fails = check_resample(fails)

    if argv:
        for name, samples in speech_fixtures(CAPTURE_RATE, argv).items():
            verdict, m = WD.gate_verdict(samples)
            ok = verdict == "speech"
            fails += not ok
            print(f"{'PASS' if ok else 'FAIL'}  accept  {name:26s} "
                  f"rms={m['rms']:8.1f} mod={m['mod']:.2f} tonal={m['tonal']:.2f}"
                  + ("" if ok else f"   -> {verdict}"))
    else:
        print("\n(no WAV arguments — real-recording cases skipped, "
              "synthetic accept cases above already ran)")

    print(f"\n{fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
