#!/usr/bin/env python3
"""dssh-whisper-shim — dual-track dispatcher between OpenRouter API and
the local self-hosted whisper.cpp daemon, with an optional `--ask`
mode that pipes the transcribed question into a DeepSeek chat
completion and returns the answer.

Without --ask  (default, used by the 3DS voice-IME path):
    Read PCM16 LE mono 16 kHz from stdin → transcribe → write Chinese
    UTF-8 text to stdout.  Routed by the active "track" config:

      ~/.config/dssh-whisper/track == "api"   → OpenRouter Qwen3-ASR-Flash
      ~/.config/dssh-whisper/track == "local" → /tmp/dssh-whisper.sock

With --ask  (used by the 3DS R+START AI-question modal):
    Read a length-prefixed binary blob:
        4 bytes  PCM length (big-endian uint32)
        N bytes  PCM16 LE mono 16 kHz
        rest     UTF-8 history JSON, e.g. [["Q1","A1"], ["Q2","A2"]]
                 (an empty array `[]` means no prior context)

    Transcribe the PCM via the configured audio track, then call
    DeepSeek chat completions with the constructed messages and write
    a single JSON line to stdout:
        {"question": "<transcribed text>", "answer": "<DeepSeek reply>"}

    Exits non-zero on any failure with a short stderr diagnostic.

API key files (chmod 0600):
    ~/.config/dssh-whisper/api-key       — OpenRouter (audio)
    ~/.config/dssh-whisper/deepseek-key  — DeepSeek   (chat)

Optional AI-endpoint overrides (plain text, first line used):
    ~/.config/dssh-whisper/deepseek-url    — full chat-completions URL
    ~/.config/dssh-whisper/deepseek-model  — model name
    Useful when api.deepseek.com is unreachable (some networks get 403s)
    and an OpenAI-compatible gateway is used instead.

Stdlib-only on purpose so SSH-exec startup stays in the tens of ms.
"""
from __future__ import annotations

import argparse
import base64
import contextlib
import json
import os
import select
import socket
import struct
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

CONFIG_DIR    = Path.home() / ".config" / "dssh-whisper"
TRACK_FILE    = CONFIG_DIR / "track"
API_KEY_FILE  = CONFIG_DIR / "api-key"
DS_KEY_FILE   = CONFIG_DIR / "deepseek-key"
DS_URL_FILE   = CONFIG_DIR / "deepseek-url"
DS_MODEL_FILE = CONFIG_DIR / "deepseek-model"
SOCK_PATH     = "/tmp/dssh-whisper.sock"

OPENROUTER_AUDIO_URL = "https://openrouter.ai/api/v1/audio/transcriptions"
# Switched from openai/whisper-large-v3-turbo → qwen/qwen3-asr-flash-...
# in v1.2 because OpenRouter's Whisper turbo provider occasionally
# rate-limits / blocks requests from certain IP ranges, while the Qwen
# ASR endpoint has been stable for the same audio.  Same JSON request
# shape, same {text, usage} response shape — no protocol changes
# needed.  Qwen also tends to emit better Chinese punctuation
# ("你好，世界。" vs Whisper's "你好世界,").  Pricing is about 2× higher
# but still negligible at personal-use volumes.  Pin the dated snapshot
# because OpenRouter doesn't expose an unpinned alias.
OPENROUTER_AUDIO_MODEL = "qwen/qwen3-asr-flash-2026-02-10"
AUDIO_LANG = "zh"
AUDIO_TIMEOUT_SEC = 30
# Must outlast the daemon's own per-connection read deadline (30 s) plus the
# worst-case inference for a 60 s clip.  Without it the shim would sit in
# recv() forever if the daemon died mid-clip, which is how orphaned shims
# used to pile up on the server.
LOCAL_DAEMON_TIMEOUT_SEC = 90

# The 3DS mic's real rate (source/voice.c).  The daemon corrects for it with
# resample_to_16k(); the WAV this shim builds for the api track has to state it
# too, or the cloud ASR is handed 16360 Hz samples labelled 16000 Hz — exactly
# the 2.25 % stretch the daemon goes out of its way to avoid.  Env-overridable
# so both sides can be moved together.
CAPTURE_RATE = int(os.environ.get("DSSH_WHISPER_CAPTURE_RATE", "16360"))
# One floor for "this is not audio at all", shared by both stdin paths below.
MIN_PAYLOAD_BYTES = 1024
# Upper sanity bound for a length-prefixed --ask payload (16 MiB).
MAX_PAYLOAD_BYTES = 16 * 1024 * 1024

DEEPSEEK_OFFICIAL_URL = "https://api.deepseek.com/chat/completions"


def _config_override(path: Path, default: str) -> str:
    """Optional per-host override file (whole file, stripped).  Missing or
    empty file keeps the default.  The shim is short-lived (one SSH-exec
    per call), so reading at startup costs nothing."""
    try:
        value = path.read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return default
    return value or default


DEEPSEEK_URL   = _config_override(DS_URL_FILE, DEEPSEEK_OFFICIAL_URL)
DEEPSEEK_MODEL = _config_override(DS_MODEL_FILE, "deepseek-chat")
DEEPSEEK_TIMEOUT_SEC = 30
DEEPSEEK_SYSTEM_PROMPT = (
    "Answer the user's question thoroughly in 6-15 sentences.  "
    "Match the question's language (中文问题 → 中文回答).  "
    "No preamble, no sign-off."
)


# ────────────────────────────────────────────────────────────────────
def _err(msg: str) -> None:
    sys.stderr.write(f"[dssh-whisper-shim] {msg}\n")


# ── Stdin read deadline (anti-orphan) ──────────────────────────────
# The 3DS streams audio over an SSH channel and signals completion with
# EOF.  If the transport (Tailscale/WireGuard on the 3DS) half-opens,
# stdin never EOFs and a plain read() would hang this shim forever,
# leaving a zombie on the server.  Bound every stdin read: bail once no
# new bytes arrive for STDIN_IDLE_SEC, or after STDIN_TOTAL_SEC overall.
# The idle cap is the one that fires on a real stall; total is a safety
# net.  Both env-overridable so SSH-exec still starts in tens of ms.
STDIN_IDLE_SEC  = int(os.environ.get("DSSH_WHISPER_STDIN_IDLE_SEC", "15"))
STDIN_TOTAL_SEC = int(os.environ.get("DSSH_WHISPER_STDIN_TOTAL_SEC", "60"))


class _ShimReadTimeout(Exception):
    """Raised when stdin stalls past the deadline.  Callers map it to a
    clean error exit (and, in --ask mode, a parseable JSON error reply)."""


def _read_stdin_until_eof() -> bytes:
    """Read all of stdin up to EOF, bounded by the stall deadlines.
    Raises _ShimReadTimeout instead of blocking forever."""
    buf = bytearray()
    last = time.monotonic()
    start = time.monotonic()
    while True:
        r, _, _ = select.select([sys.stdin], [], [], 2.0)
        if r:
            chunk = sys.stdin.buffer.read(65536)
            if not chunk:
                return bytes(buf)          # EOF
            buf += chunk
            last = time.monotonic()
            continue
        now = time.monotonic()
        if now - last > STDIN_IDLE_SEC or now - start > STDIN_TOTAL_SEC:
            raise _ShimReadTimeout()


def _read_exact(n: int) -> bytes:
    """Read exactly n bytes from stdin (or fewer, up to EOF), bounded by
    the stall deadlines.  Raises _ShimReadTimeout on a stall."""
    buf = bytearray()
    last = time.monotonic()
    start = time.monotonic()
    while len(buf) < n:
        r, _, _ = select.select([sys.stdin], [], [], 2.0)
        if r:
            chunk = sys.stdin.buffer.read(n - len(buf))
            if not chunk:
                return bytes(buf)          # EOF before n bytes
            buf += chunk
            last = time.monotonic()
            continue
        now = time.monotonic()
        if now - last > STDIN_IDLE_SEC or now - start > STDIN_TOTAL_SEC:
            raise _ShimReadTimeout()
    return bytes(buf)


# ── Diagnostic log for --ask debugging ─────────────────────────────
# Every --ask invocation appends a timestamped block to /tmp/dssh-ai-
# debug.log so we can see what the 3DS actually sent and what we
# replied — the SSH client's terminal swallows stderr after the
# channel closes, so this is the only way to debug field-cases.
import datetime, os
DEBUG_LOG = "/tmp/dssh-ai-debug.log"

def _dlog(msg: str) -> None:
    try:
        # encoding + errors="replace" keep this infallible.  Without them the
        # log inherits the locale encoding: on a non-UTF-8 host, writing a
        # Chinese answer raised UnicodeEncodeError — a ValueError, so the
        # OSError guard below never saw it — straight out through
        # _emit_ask_json(), and the 3DS received half a JSON object.
        with open(DEBUG_LOG, "a", encoding="utf-8", errors="replace") as f:
            ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            f.write(f"[{ts}] {msg}\n")
    except (OSError, ValueError):
        pass


def _emit_ask_json(question: str, answer: str) -> None:
    """Single point where --ask writes JSON to stdout.  Always called
    on every code path so the 3DS never receives an empty reply."""
    out = json.dumps({"question": question, "answer": answer},
                     ensure_ascii=False)
    sys.stdout.write(out)
    sys.stdout.flush()
    _dlog(f"  out: {out[:200]}")


def _get_track() -> str:
    try:
        return (TRACK_FILE.read_text(encoding="utf-8", errors="replace").strip()
                or "api").lower()
    except FileNotFoundError:
        return "api"
    except OSError as exc:
        _err(f"track config unreadable ({exc}); defaulting to api")
        return "api"


def _pcm_to_wav(pcm: bytes,
                sample_rate: int,
                channels: int = 1,
                bits: int = 16) -> bytes:
    """Wrap raw PCM in a WAV container.

    `sample_rate` is deliberately required, with no default: the one caller
    used to rely on a 16000 default while the bytes it handed over are
    CAPTURE_RATE, so the header understated the rate by 2.25 %.  A required
    parameter makes that mistake unrepresentable.
    """
    byte_rate   = sample_rate * channels * bits // 8
    block_align = channels * bits // 8
    data_size   = len(pcm)
    return (
        b"RIFF" + struct.pack("<I", 36 + data_size) + b"WAVE"
        + b"fmt " + struct.pack(
            "<IHHIIHH", 16, 1, channels, sample_rate,
            byte_rate, block_align, bits)
        + b"data" + struct.pack("<I", data_size) + pcm
    )


# ── Audio: API track via OpenRouter ────────────────────────────────
def _transcribe_via_api(pcm: bytes) -> str | None:
    """Returns transcribed text on success, None on failure (logs to stderr)."""
    try:
        key = API_KEY_FILE.read_text(encoding="utf-8", errors="replace").strip()
    except FileNotFoundError:
        _err(f"no audio API key at {API_KEY_FILE}")
        return None
    if not key:
        _err(f"empty API key at {API_KEY_FILE}")
        return None

    body = json.dumps({
        "model": OPENROUTER_AUDIO_MODEL,
        "language": AUDIO_LANG,
        "input_audio": {
            "data": base64.b64encode(_pcm_to_wav(pcm, CAPTURE_RATE)).decode("ascii"),
            "format": "wav",
        },
    }).encode("utf-8")

    req = urllib.request.Request(
        OPENROUTER_AUDIO_URL,
        data=body,
        method="POST",
        headers={
            "Authorization": f"Bearer {key}",
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=AUDIO_TIMEOUT_SEC) as resp:
            payload = json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")[:200]
        _err(f"audio API HTTP {exc.code}: {body}")
        return None
    except urllib.error.URLError as exc:
        _err(f"audio network error: {exc.reason}")
        return None
    except (TimeoutError, socket.timeout):
        _err(f"audio API timeout (>{AUDIO_TIMEOUT_SEC}s)")
        return None
    except Exception as exc:  # noqa: BLE001
        _err(f"audio API parse error: {exc}")
        return None

    return (payload.get("text") or "").strip() or None


# ── Audio: local track via Unix socket ─────────────────────────────
def _transcribe_via_local(pcm: bytes) -> str | None:
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(LOCAL_DAEMON_TIMEOUT_SEC)
        s.connect(SOCK_PATH)
    except (FileNotFoundError, ConnectionRefusedError) as exc:
        _err(f"local daemon not running ({exc})")
        return None
    chunks = []
    try:
        s.sendall(pcm)
        s.shutdown(socket.SHUT_WR)
        while True:
            c = s.recv(4096)
            if not c:
                break
            chunks.append(c)
    except socket.timeout:
        _err(f"local daemon silent for >{LOCAL_DAEMON_TIMEOUT_SEC}s")
        return None
    except OSError as exc:
        _err(f"local daemon connection failed: {exc}")
        return None
    finally:
        with contextlib.suppress(OSError):
            s.close()
    text = b"".join(chunks).decode("utf-8", errors="replace").strip()
    return text or None


def _transcribe(pcm: bytes) -> str | None:
    track = _get_track()
    if track == "api":
        return _transcribe_via_api(pcm)
    if track == "local":
        return _transcribe_via_local(pcm)
    _err(f"unknown track {track!r} in {TRACK_FILE}")
    return None


# Backstop for the subtitle boilerplate whisper large-v3 invents on
# non-speech.  The daemon's spectral gate is the real defence; this only
# catches what slips past it and everything the "api" track returns (that
# path has no gate at all).  Markers are deliberately multi-character
# endcard phrases: bare 点赞 / 订阅 / 转发 are excluded because a user
# legitimately typing "帮我看看这个订阅列表" would then silently lose their
# text — a hit here replaces the transcription with the "empty" sentinel.
_HALLUCINATION_MARKERS = (
    "吝啬", "字幕志愿者", "独播剧场", "优优独播", "yoyo television",
    "明镜与点点", "感谢观看", "打赏支持", "一键三连",
)


def _is_hallucination(text: str) -> bool:
    low = text.lower()
    return any(marker in low for marker in _HALLUCINATION_MARKERS)


# ── Chat: DeepSeek ─────────────────────────────────────────────────
def _ask_deepseek(question: str, history: list[list[str]]) -> str | None:
    """history is a list of [Q, A] pairs (oldest first).  Each entry expands
    into two messages (user, assistant) appended before the new user turn."""
    try:
        key = DS_KEY_FILE.read_text(encoding="utf-8", errors="replace").strip()
    except FileNotFoundError:
        _err(f"no DeepSeek key at {DS_KEY_FILE}; "
             "re-run install or create it manually")
        return None
    if not key:
        _err(f"empty DeepSeek key at {DS_KEY_FILE}")
        return None

    messages: list[dict] = [{"role": "system", "content": DEEPSEEK_SYSTEM_PROMPT}]
    for pair in history or []:
        if isinstance(pair, list) and len(pair) == 2:
            q, a = pair
            messages.append({"role": "user", "content": q})
            messages.append({"role": "assistant", "content": a})
    messages.append({"role": "user", "content": question})

    body = json.dumps({
        "model": DEEPSEEK_MODEL,
        "messages": messages,
        "temperature": 0.4,
        "max_tokens": 2400,
    }).encode("utf-8")

    headers = {
        "Authorization": f"Bearer {key}",
        "Content-Type": "application/json",
    }
    if DEEPSEEK_URL != DEEPSEEK_OFFICIAL_URL:
        # Non-official gateways commonly sit behind Cloudflare, which
        # rejects urllib's default User-Agent with a 403.
        headers["User-Agent"] = (
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
            "AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/126.0.0.0 Safari/537.36"
        )

    req = urllib.request.Request(
        DEEPSEEK_URL,
        data=body,
        method="POST",
        headers=headers,
    )
    try:
        with urllib.request.urlopen(req, timeout=DEEPSEEK_TIMEOUT_SEC) as resp:
            payload = json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")[:200]
        _err(f"DeepSeek HTTP {exc.code}: {body}")
        return None
    except urllib.error.URLError as exc:
        _err(f"DeepSeek network error: {exc.reason}")
        return None
    except (TimeoutError, socket.timeout):
        _err(f"DeepSeek API timeout (>{DEEPSEEK_TIMEOUT_SEC}s)")
        return None
    except Exception as exc:  # noqa: BLE001
        _err(f"DeepSeek parse error: {exc}")
        return None

    try:
        return payload["choices"][0]["message"]["content"].strip()
    except (KeyError, IndexError, AttributeError):
        _err(f"DeepSeek malformed response: {json.dumps(payload)[:200]}")
        return None


# ── Mode dispatch ──────────────────────────────────────────────────
def _run_default() -> int:
    """Track-routed transcription only.  Used by voice IME path."""
    try:
        pcm = _read_stdin_until_eof()
    except _ShimReadTimeout:
        _err(f"audio stdin stalled (>={STDIN_IDLE_SEC}s idle / "
             f"{STDIN_TOTAL_SEC}s total); aborting")
        return 1
    if len(pcm) < MIN_PAYLOAD_BYTES:
        _err(f"PCM too short ({len(pcm)} bytes); ignored")
        return 1
    text = _transcribe(pcm)
    if text is None:
        return 1
    if _is_hallucination(text):
        # Write nothing: the 3DS types the channel contents verbatim into
        # the shell, and an empty reply makes voice.c fall back to IDLE.
        _err(f"hallucination marker in {text[:40]!r}; suppressing")
        return 0
    sys.stdout.write(text)
    sys.stdout.flush()
    return 0


def _run_ask() -> int:
    """Length-prefixed PCM + history JSON → DeepSeek answer JSON.

    Every exit path emits a JSON object on stdout via _emit_ask_json so
    the 3DS-side parser always sees a parseable response (worst-case
    one with a useful `[error]` answer field), never an empty channel.
    """
    _dlog("=" * 60)
    _dlog("--ask invoked")

    try:
        raw_len = _read_exact(4)
    except _ShimReadTimeout:
        _err("--ask: stdin timeout waiting for PCM length header")
        _dlog("  ABORT: stdin timeout waiting for header")
        _emit_ask_json("", "[shim: timeout waiting for audio header]")
        return 1
    if len(raw_len) != 4:
        _err("--ask: short read on PCM length header")
        _dlog(f"  ABORT: short header read ({len(raw_len)}/4)")
        _emit_ask_json("", "[shim: short PCM length header]")
        return 1
    (pcm_len,) = struct.unpack(">I", raw_len)
    _dlog(f"  PCM length header = {pcm_len}")
    if pcm_len < MIN_PAYLOAD_BYTES or pcm_len > MAX_PAYLOAD_BYTES:
        _err(f"--ask: implausible PCM length {pcm_len}")
        _dlog(f"  ABORT: implausible PCM length")
        _emit_ask_json("", f"[shim: bad PCM length {pcm_len}]")
        return 1

    # Read PCM in chunks — stdin.buffer.read(N) on SSH-piped input may
    # return fewer than N bytes per call; loop until we have all PCM
    # bytes or stdin closes.  Bounded by the stall deadline.
    try:
        pcm = _read_exact(pcm_len)
    except _ShimReadTimeout:
        _err("--ask: stdin timeout reading PCM")
        _dlog(f"  ABORT: stdin timeout reading PCM (wanted {pcm_len})")
        _emit_ask_json("", "[shim: timeout reading PCM]")
        return 1
    _dlog(f"  PCM bytes received = {len(pcm)} (wanted {pcm_len})")
    if len(pcm) != pcm_len:
        _err(f"--ask: short PCM read ({len(pcm)}/{pcm_len})")
        _emit_ask_json("", f"[shim: short PCM ({len(pcm)}/{pcm_len})]")
        return 1

    try:
        history_raw = _read_stdin_until_eof() \
            .decode("utf-8", errors="replace").strip()
    except _ShimReadTimeout:
        _err("--ask: stdin timeout reading history")
        _dlog("  ABORT: stdin timeout reading history")
        _emit_ask_json("", "[shim: timeout reading history]")
        return 1
    _dlog(f"  history_raw = {history_raw[:120]!r}")
    if not history_raw:
        history_raw = "[]"
    try:
        history = json.loads(history_raw)
        if not isinstance(history, list):
            raise ValueError("history must be a JSON array")
    except Exception as exc:  # noqa: BLE001
        _err(f"--ask: bad history JSON ({exc}); ignoring")
        _dlog(f"  WARN: history JSON parse failed ({exc}); using []")
        history = []
    _dlog(f"  history turns = {len(history)}")

    question = _transcribe(pcm)
    _dlog(f"  question = {question!r}")
    if not question:
        _emit_ask_json("", "[transcription failed]")
        return 1

    if _is_hallucination(question):
        # Silence in → YouTube boilerplate out.  Skip the DeepSeek call
        # entirely (don't pollute history, don't burn a token) and emit
        # a literal "empty" sentinel for both the question and answer
        # so the modal makes the empty-input case obvious.
        _dlog("  → hallucination markers detected; emitting 'empty'")
        _emit_ask_json("empty", "empty")
        return 0

    answer = _ask_deepseek(question, history)
    _dlog(f"  answer = {(answer or '')[:200]!r}")
    if not answer:
        _emit_ask_json(question, "[DeepSeek call failed; check daemon stderr]")
        return 1

    _emit_ask_json(question, answer)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--ask", action="store_true",
                        help="length-prefixed mode: PCM + history JSON → "
                             "transcribe + chat → JSON output")
    args, _ = parser.parse_known_args()
    return _run_ask() if args.ask else _run_default()


if __name__ == "__main__":
    sys.exit(main())
