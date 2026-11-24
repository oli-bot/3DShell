#!/usr/bin/env python3
"""dssh-whisper-daemon — long-running whisper.cpp transcription server.

Listens on /tmp/dssh-whisper.sock for incoming PCM16 little-endian mono
streams at the 3DS mic rate (16360 Hz, override with
DSSH_WHISPER_CAPTURE_RATE).  Each connection: read all bytes until the
client shuts down its write half, gate out anything that isn't speech,
resample to 16 kHz, transcribe with the preloaded multilingual model and
language='zh', send Chinese UTF-8 text back.  The model is loaded once at
daemon startup so per-call latency is just the actual transcription work.

Whisper large-v3 has no "no speech" answer: fed digital silence it still
emits YouTube/Bilibili subtitle boilerplate ("请不吝点赞 订阅 转发 打赏
支持明镜与点点栏目") at a *higher* confidence than real speech, so no
decode parameter or probability threshold suppresses it.  The gate below
runs before the model and rejects non-speech on spectral shape instead of
level, because loud room tone hallucinates just as reliably as silence.

Run via the launchd Agent `com.dssh.whisper` or the systemd user unit
`dssh-whisper.service` so the model stays in memory across voice triggers.
"""
import contextlib
import logging
import os
import signal
import socket
import sys
import threading
import time
import wave
from io import BytesIO
from pathlib import Path

import numpy as np
from pywhispercpp.model import Model

SOCK_PATH = "/tmp/dssh-whisper.sock"
# Multilingual; we always pass language='zh'.  Override with the env var
# DSSH_WHISPER_MODEL (e.g. 'tiny' for ~1-2 s on busy hosts but worse zh
# accuracy, 'medium' for ~10 s and near-state-of-the-art accuracy).
MODEL_NAME = os.environ.get("DSSH_WHISPER_MODEL", "small")
INSTALL_DIR = Path.home() / ".local/share/dssh-whisper"
MODELS_DIR = INSTALL_DIR / "models"
SAMPLE_RATE = 16000                     # what whisper.cpp expects
# What the 3DS mic actually produces (source/voice.c).  The shim has to state
# the same rate in the WAV it hands the cloud ASR on the api track, and the
# two processes cannot import each other, so this is deliberately the same
# env var + default in both files — move them together or one track lies.
CAPTURE_RATE = int(os.environ.get("DSSH_WHISPER_CAPTURE_RATE", "16360"))
MIN_PAYLOAD_BYTES = 1024  # below this is almost certainly an empty press

# Per-connection read deadline.  The accept loop used to be single-threaded
# with no timeout, so one client that connected and stalled (a 3DS-side SSH
# channel half-open mid-upload) wedged the daemon forever and every later
# voice press silently queued behind it.
CLIENT_TIMEOUT_SEC = float(os.environ.get("DSSH_WHISPER_CLIENT_TIMEOUT", "30"))

# ── Non-speech gate ────────────────────────────────────────────────
# Digital-silence floor: cheap early-out, deliberately low so it can never
# clip a quiet speaker.
MIN_RMS = float(os.environ.get("DSSH_WHISPER_MIN_RMS", "20"))
# Two conditions must both hold.  modDepth (per-frame energy std/mean) is
# genuinely level-invariant.  tonalFrac (share of frames whose spectrum looks
# peaky) is NOT invariant to silence: silent frames stay in its denominator,
# so a short word followed by a long pause drags it down.
#
# Measured over Chinese speech at RMS 4000 / 400 / 80 (identical results —
# both are ratios):
#
#                     modDepth   tonalFrac
#   white noise          0.02       0.00
#   low-pass rumble   0.10 - 0.29   0.07 - 0.78
#   mains hum / tone  0.00 - 0.02   1.00
#   speech, continuous 0.52 - 0.70  0.39 - 0.65
#
# ...and over 72 real 3DS captures (daemon.err.log 2026-09-06; 60 accepted,
# 12 rejected — every rejection reason reproduced by the synthetic fixtures in
# tools/test_whisper_gate.py):
#
#                      modDepth         tonalFrac
#   real speech     0.45 / 1.29 / 2.43  0.16 / 0.34 / 1.00  (min/median/max)
#
# The device's own framing — silence before and after the utterance — is why
# real modDepth runs above the continuous-speech range, and why real tonalFrac
# sits far *below* it: 0.16 against a 0.15 threshold.  The thin edge is
# tonalFrac, not modDepth, and it is thinnest for a short word with a long
# silent tail (see known_false_rejections in tools/test_whisper_gate.py).
#
# modDepth alone separates speech from every noise we generated; tonalFrac is
# the AND-term so a bursty click (high modulation, no harmonics) can't pass.
# Narrow-band rumble is what makes tonalFrac unusable on its own: its energy
# sits in one band, so it looks spectrally "peaky" (0.78).
MIN_MOD_DEPTH = float(os.environ.get("DSSH_WHISPER_MIN_MOD_DEPTH", "0.40"))
MIN_TONAL_FRAC = float(os.environ.get("DSSH_WHISPER_MIN_TONAL_FRAC", "0.15"))
FLATNESS_TONAL_MAX = 0.35   # per-frame spectral flatness below this == tonal
FRAME = 1024                # 64 ms at 16 kHz
HOP = 512

# Set DSSH_WHISPER_DUMP to a directory to keep every incoming clip as a WAV
# alongside its gate verdict — the only way to measure real 3DS room tone.
DUMP_DIR = os.environ.get("DSSH_WHISPER_DUMP", "")

logging.basicConfig(
    level=logging.INFO,
    format="[whisper-daemon %(asctime)s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger()
_transcribe_lock = threading.Lock()


def cleanup_socket() -> None:
    with contextlib.suppress(FileNotFoundError):
        os.unlink(SOCK_PATH)


def gate_verdict(samples: np.ndarray) -> tuple[str, dict]:
    """Classify raw int16 capture without touching the model.

    Returns ("speech" | "reject: <why>", metrics) so the caller can log the
    numbers either way — calibrating the thresholds needs the rejects.
    """
    x = samples.astype(np.float64)
    rms = float(np.sqrt((x * x).mean())) if x.size else 0.0
    metrics = {"rms": rms, "mod": float("nan"), "tonal": float("nan")}

    if x.size < FRAME:
        return "speech", metrics          # too short to judge; let the model try
    if rms < MIN_RMS:
        return f"reject: rms {rms:.1f} < {MIN_RMS:.0f}", metrics

    window = np.hanning(FRAME)
    frames = np.lib.stride_tricks.sliding_window_view(x, FRAME)[::HOP] * window
    energy = np.sqrt((frames * frames).mean(axis=1))
    mod = float(energy.std() / energy.mean()) if energy.mean() > 0 else 0.0
    mag = np.abs(np.fft.rfft(frames, axis=1)) + 1e-9
    flatness = np.exp(np.log(mag).mean(axis=1)) / mag.mean(axis=1)
    tonal = float((flatness < FLATNESS_TONAL_MAX).mean())
    metrics["mod"] = mod
    metrics["tonal"] = tonal

    if mod < MIN_MOD_DEPTH:
        return f"reject: modDepth {mod:.2f} < {MIN_MOD_DEPTH:.2f}", metrics
    if tonal < MIN_TONAL_FRAC:
        return f"reject: tonal {tonal:.2f} < {MIN_TONAL_FRAC:.2f}", metrics
    return "speech", metrics


def resample_to_16k(samples: np.ndarray) -> np.ndarray:
    """Linear-interp resample from the capture rate to whisper's 16 kHz.

    The 3DS samples at CAPTURE_RATE (16360 Hz by default); feeding those
    bytes to whisper.cpp as if they were SAMPLE_RATE stretches the audio by
    ~2.25 % and drops the pitch ~38 cents, which costs Mandarin accuracy and
    makes every logged duration a little wrong.
    """
    if CAPTURE_RATE == SAMPLE_RATE or samples.size < 2:
        return samples
    target = int(round(samples.size * SAMPLE_RATE / CAPTURE_RATE))
    src = np.arange(samples.size, dtype=np.float64)
    dst = np.linspace(0, samples.size - 1, target)
    return np.interp(dst, src, samples.astype(np.float64)).astype(np.int16)


def dump_clip(data: bytes, verdict: str) -> None:
    if not DUMP_DIR:
        return
    try:
        Path(DUMP_DIR).mkdir(parents=True, exist_ok=True)
        tag = "speech" if verdict == "speech" else "rejected"
        name = f"{time.strftime('%Y%m%d-%H%M%S')}-{tag}.wav"
        buf = BytesIO()
        with wave.open(buf, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(CAPTURE_RATE)
            wf.writeframes(data)
        (Path(DUMP_DIR) / name).write_bytes(buf.getvalue())
        log.info("dumped %s", name)
    except Exception as exc:  # noqa: BLE001 — dumping must never break audio
        log.warning("dump failed: %s", exc)


def handle(conn: socket.socket, model: Model) -> None:
    try:
        conn.settimeout(CLIENT_TIMEOUT_SEC)
        data = bytearray()
        while True:
            chunk = conn.recv(8192)
            if not chunk:
                break
            data.extend(chunk)

        if len(data) < MIN_PAYLOAD_BYTES:
            log.warning("rejecting tiny payload (%d bytes)", len(data))
            conn.sendall(b"")
            return

        raw = np.frombuffer(bytes(data), dtype=np.int16)
        verdict, metrics = gate_verdict(raw)
        dur = raw.size / CAPTURE_RATE
        log.info(
            "clip %.2fs rms=%.1f modDepth=%.2f tonal=%.2f -> %s",
            dur, metrics["rms"], metrics["mod"], metrics["tonal"], verdict,
        )
        dump_clip(bytes(data), verdict)
        if verdict != "speech":
            # Empty reply, not an error: the 3DS types nothing and shows IDLE.
            conn.sendall(b"")
            return

        samples = resample_to_16k(raw).astype(np.float32) / 32768.0
        with _transcribe_lock:      # whisper.cpp state is not reentrant
            segments = model.transcribe(
                samples,
                beam_search={"beam_size": 5, "patience": 1.0},  # zh accuracy
                temperature=0.0,
                temperature_inc=0.2,
            )
        text = "".join(seg.text for seg in segments).strip()
        log.info("→ %r", text[:80])
        conn.sendall(text.encode("utf-8"))
    except (TimeoutError, socket.timeout):
        log.warning("client stalled >%.0fs, dropping connection", CLIENT_TIMEOUT_SEC)
    except OSError as exc:
        log.warning("connection error: %s", exc)
    except Exception:  # noqa: BLE001 — daemon must never die on one clip
        log.exception("transcribe failed")
        with contextlib.suppress(Exception):
            conn.sendall(b"")
    finally:
        with contextlib.suppress(Exception):
            conn.close()


def main() -> int:
    log.info("loading whisper %r (multilingual, zh)...", MODEL_NAME)
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    model = Model(
        MODEL_NAME,
        models_dir=str(MODELS_DIR),
        n_threads=max(1, (os.cpu_count() or 2) - 0),
        language="zh",
        print_progress=False,
        print_realtime=False,
        print_timestamps=False,
    )
    log.info("model ready; opening %s", SOCK_PATH)

    cleanup_socket()
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(SOCK_PATH)
    os.chmod(SOCK_PATH, 0o666)
    srv.listen(8)

    def _shutdown(signum: int, _frame) -> None:
        log.info("signal %d → shutdown", signum)
        cleanup_socket()
        sys.exit(0)

    signal.signal(signal.SIGTERM, _shutdown)
    signal.signal(signal.SIGINT, _shutdown)

    log.info(
        "ready. capture=%dHz gate: rms>=%.0f modDepth>=%.2f tonal>=%.2f "
        "timeout=%.0fs dump=%s",
        CAPTURE_RATE, MIN_RMS, MIN_MOD_DEPTH, MIN_TONAL_FRAC,
        CLIENT_TIMEOUT_SEC, DUMP_DIR or "off",
    )
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=handle, args=(conn, model), daemon=True).start()


if __name__ == "__main__":
    sys.exit(main() or 0)
