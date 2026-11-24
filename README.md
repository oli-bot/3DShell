<p align="center">
  <b>English</b> · <a href="README.zh.md">中文</a>
</p>

<h1 align="center">3DShell</h1>

<p align="center">
  <img src="icon.png" alt="DSSH icon" width="96" height="96">
</p>

<p align="center">
  <b>Nintendo 3DS SSH client — pinyin IME · voice input · ANSI terminal · dual-screen</b><br>
  Top screen runs a citro2d ANSI terminal · bottom screen draws its own
  soft keyboard · RSA public-key auth over libssh2 + mbedTLS<br>
  Press <b>START+L</b> to merge both screens into one 66×40 terminal (press
  again to bring the soft keyboard back).<br>
  Hold <b>R</b> to dictate Chinese into Claude Code, release to send;
  type pinyin on the soft keyboard for the rest.<br>
  Run <code>tmux</code> + <code>claude-code</code> from a 3DS — code from
  the couch without ever opening the laptop.
</p>

<p align="center">
  <a href="https://github.com/Fishason/DSSH"><img alt="based on" src="https://img.shields.io/badge/based%20on-Fishason%2FDSSH-blue?logo=github"></a>
  <img alt="platform" src="https://img.shields.io/badge/platform-Nintendo%203DS-FE0016?logo=nintendo3ds&logoColor=white">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-blue">
  <img alt="build" src="https://img.shields.io/badge/build-devkitARM-orange">
</p>

> [!NOTE]
> **3DShell** is an enhanced fork of [Fishason/DSSH](https://github.com/Fishason/DSSH). It adds an on-device multi-host SSH machine picker, project card presets for terminal AI agents (`claude`, `codex`, `opencode`), multi-window terminal switching, and custom themes & pet mascots.

<p align="center">
  <img src="design/machine-page/list-dark-top.png" alt="Machine Picker Top Screen" width="360">
  <img src="design/machine-page/list-dark-bot.png" alt="Machine Picker Bottom Screen" width="300"><br>
  <sub>SSH Machine Picker (Top: Machine Details · Bottom: Machine List)</sub>
</p>

<p align="center">
  <img src="design/project-page/card-list-top.png" alt="Project Cards Top Screen" width="360">
  <img src="design/project-page/card-list.png" alt="Project Cards Bottom Screen" width="300"><br>
  <sub>Project Cards (Top: Selected Project Summary · Bottom: Interactive Cards)</sub>
</p>

<p align="center">
  <img src="docs/media/mascot_preview.png" alt="Mascot Preview" width="600"><br>
  <sub>Pixel Mascot Companions: Crab, Crab (Xiete), Chick, Phage, Alien, Niulai, Dog</sub>
</p>

---

## Features

- **Full ANSI / VT100 terminal** — tmux status bar, claude-code spinner,
  box-drawing borders, 256-color, TrueColor, Braille; everything renders.
- **Chinese rendering** — bundled Zpix 12px pixel font covers 21,000+ CJK
  unified ideographs, Terminus 6×12 for ASCII; mixed CJK/ASCII baselines
  align cleanly on the same line.
- **Self-drawn soft keyboard** — iOS-style 3px rounded keys with smooth
  press-down animation; letters / symbols pages.
- **Pinyin input method** — top 300k entries from rime-ice, plus
  abbreviation matching (`nh` → 你好), prefix fallback (`nihaoz`
  auto-falls-back to `nihao`), and a candidate cursor.
- **Voice input (v1.0)** — hold **R**, speak a Chinese sentence, release;
  ~1-2 s later the transcribed text drops straight into the SSH
  terminal.  Default backend is Qwen3-ASR-Flash on OpenRouter over the
  cloud (`qwen/qwen3-asr-flash-2026-02-10`) — it replaced Whisper Large
  V3 Turbo in v1.2 and costs roughly 2× per audio-hour, which is still
  only a few cents a month for personal use.  A self-hosted whisper.cpp
  track is available if you'd rather not depend on an external API.
- **Voice AI ask (NEW in v1.1)** — hold **R** and press **START** to
  ask DeepSeek-Chat a question by voice; the answer pops up in a
  bottom-screen modal with markdown-styled rendering (headers in
  yellow, code in cyan, bullets, etc.) without disturbing the SSH
  session above.  Press **A** in the modal to keep history for follow-
  up questions, **B** to clear and start a new conversation.
- **Uniform key surface across every 3DS model** — voice, AI-ask,
  IME-mode toggle, dual-screen, and multi-window all use physical
  buttons present on the original 3DS, 3DS XL, 2DS, New 3DS, New 3DS XL,
  and New 2DS XL.  No model detection needed; full feature parity from
  the cheapest 2DS to the latest New 3DS.  See [`KEYBINDING.md`](KEYBINDING.md)
  for the complete cheat sheet.
- **RSA-4096 public-key auth** — libssh2 + mbedTLS, private key read
  from the SD card.
- **Multi-window terminal (up to 4)** — **X + Y + ▲** opens another shell
  channel on the *same* SSH login (no second handshake, no second
  Tailscale tunnel), **X + Y + ▼** closes the one you are looking at,
  **X + Y + ◀ ▶** (or **L / R**) switches.  Only one window is on screen at a
  time, but all of them keep running — enough to drive three `opencode`
  sessions side by side and flip between them.
- **Native Tailscale transport** — optionally join the 3DS to a tailnet and
  carry SSH over direct UDP, Tailscale Peer Relay, or DERP without running Go
  or `tailscaled`.
- **Full physical-key mapping** — D-pad arrow keys, hold-style modifiers
  (Y = Ctrl, X = Alt), Circle Pad scrollback (PageUp / PageDown inside a
  full-screen app).  Shift lives on the on-screen ⇧ soft key.
- **Chibi pet mascots** — choose from 7 animated pixel-art companions (🦀 Crab, 🦀 Crab (Xiete), 🐣 Chick, 🔬 Phage, 👽 Alien, 🐂 牛来, 🐕 刀盾狗) scampering along the bottom row, reacting to typing, reconnects, touches, and voice input.  Each species also has its own key-click voice.
- **Settings & debug page** — tap the ⚙ badge to open the dual-screen settings page (top-screen item list + bottom detail card: mascot selection, dual-screen layout, themes, debug entry); the debug page is display-only and shows the live SSH byte stream and key bindings.
- **Multiple SSH machines** — with more than one machine in `config.ini`, launch (and SELECT-reconnect) first shows a **machine picker**: the bottom screen lists each machine by its **name over its address** (both halves follow the theme), the top screen carries the highlighted one's name / host / user / port / key. Pick one, connect, and the project page opens behind it. Machines can be **renamed on the device** (**X**, or a tap on a row's ✎ strip). Every machine keeps **its own** `settings.<N>.ini` — cards, theme, interface language, mascot and recording limit are all per machine. See [SSH machine picker](#ssh-machine-picker-multiple-machines).
- **Project cards** — after the handshake, a dual-screen page lets you pick which project to enter and which terminal agent to launch there (`claude`, `codex`, `opencode`, `agy`, `gemini`, `aider`, or your own). The bottom screen is the card list and a four-tab editor (`目录 / 工具 / 参数 / 自定义`); the top screen describes the highlighted card and previews the exact command it will send. One card per window, so different windows can run different agents in different directories. See [Project cards](#project-cards).

## Table of contents

- [Install](#install)
- [Server-side setup](#server-side-setup-one-time)
- [Configure config.ini](#configure-configini)
- [Voice input](#voice-input)
- [SSH machine picker](#ssh-machine-picker-multiple-machines)
- [Key bindings](KEYBINDING.md) (full cheat sheet; condensed table also below)
- [Using the IME](#using-the-ime)
- [Settings & debug page](#settings--debug-page)
- [Project cards](#project-cards)
- [Known limitations](#known-limitations)
- [Build from source](#build-from-source)
- [Project layout](#project-layout)
- [Credits](#credits)
- [License](#license)

---

## Install

DSSH runs on a **modded 3DS / 2DS / New 3DS**.  You need either the
Homebrew Launcher (HBL) or a CIA installer like FBI.

### Option A — `.cia` install (recommended)

1. Grab `DSSH.cia` (~14 MB) from the [latest release](../../releases/latest).
2. Copy it anywhere on the SD card (e.g. `/cias/DSSH.cia`).
3. Open FBI → SD → select `DSSH.cia` → `Install CIA`.
4. The orange DSSH icon shows up on the HOME menu.

### Option B — `.3dsx` direct launch

1. Grab `3dssh.3dsx` from the latest release.
2. Copy to `/3ds/dssh/3dssh.3dsx` on the SD card.
3. Open HBL → pick DSSH.

### Option C — `3dslink` over Wi-Fi (developer flow)

```bash
# On the 3DS: launch HBL, press Y → "Waiting for 3dslink..."
3dslink -a <3DS-LAN-IP> 3dssh.3dsx
```

---

## Server-side setup (one-time)

The 3DS libssh2 build uses mbedTLS as its crypto backend and
**hardcodes-disables ed25519**.  So you generate a fresh RSA-4096
keypair just for the 3DS — your existing ed25519 key on the PC keeps
working untouched.

> ⚠️ **The `-m PEM` flag matters.**  devkitPro's `3ds-mbedtls`
> package is pinned at 2.28.x, which can only parse the **traditional
> PEM** private-key format (`-----BEGIN RSA PRIVATE KEY-----`).  Recent
> `ssh-keygen` (Ubuntu 18.04+, macOS) defaults to the **new OpenSSH
> format** (`-----BEGIN OPENSSH PRIVATE KEY-----`) which mbedtls 2.28
> *cannot* read — DSSH will fail at handshake with "auth failed" if
> you skip `-m PEM`.

```bash
# 1. Generate a 3DS-only RSA key on your PC.  -m PEM forces the
#    traditional PEM format so the on-3DS mbedtls can parse it.
ssh-keygen -t rsa -b 4096 -m PEM -f ~/.ssh/id_rsa_3ds -C "3ds-ssh-client"

# 2. Copy the public half into the server's authorized_keys
ssh-copy-id -i ~/.ssh/id_rsa_3ds.pub user@your-server.example.com

# 3. Verify the new RSA key works from your PC
ssh -i ~/.ssh/id_rsa_3ds user@your-server.example.com 'echo OK'
```

**Already have an OpenSSH-format key?**  Convert it in place — no
need to re-add it to the server (the public half is unchanged):

```bash
ssh-keygen -p -m PEM -f ~/.ssh/id_rsa_3ds
# enter old passphrase (empty if none) and accept any new passphrase.
# The file's first line should now read: -----BEGIN RSA PRIVATE KEY-----
```

You can sanity-check the format with `head -1 ~/.ssh/id_rsa_3ds`.

**Recommended hardening**: prepend the new line in the server's
`~/.ssh/authorized_keys` with `from="<your-home-public-IP>"` so a lost
SD card can only log in from your home network.

Copy the **private key** `~/.ssh/id_rsa_3ds` onto the SD card at
`/3ds/3dssh/id_rsa` (the path is fixed even when DSSH is installed as a
.cia — config + key always read from `sdmc:/3ds/3dssh/`).

> ⚠️ The SD card stores the key in plain text.  Anyone holding the SD
> can log in to your server.  Add `from="..."` IP restriction or
> `command="..."` lockdown in `authorized_keys`.
>
> ⚠️ The other half of the risk is on the wire: DSSH **does not verify the
> server's host key**, so someone in the middle can pose as your server.
> See [Known limitations](#known-limitations).

---

## Configure config.ini

Copy `sd_template/3ds/3dssh/config.ini.example` to the SD card at
`/3ds/3dssh/config.ini` and edit the values:

```ini
host       = your-server.example.com
port       = 22
user       = ubuntu
key_path   = sdmc:/3ds/3dssh/id_rsa
passphrase =

# Optional: unlock the current SSH user's macOS login keychain
macos_keychain_password =

# Optional: connect SSH through the tailnet
tailscale_auth_key =
tailscale_hostname = dssh-3ds
tailscale_state = sdmc:/3ds/3dssh/tailscale.state
tailscale_control_url = https://controlplane.tailscale.com
```

| Field | Meaning |
|---|---|
| `host` | Server IP or hostname |
| `port` | Port (default 22) |
| `user` | SSH login user |
| `key_path` | Private key path; `sdmc:/...` is the 3DS standard SD prefix |
| `passphrase` | Optional key passphrase; leave empty (typing one on the soft keyboard is awkward) |
| `macos_keychain_password` | Optional macOS login password; setting it enables automatic unlock for the current SSH user |
| `tailscale_auth_key` | Auth key used for registration; setting it enables Tailscale automatically |
| `tailscale_hostname` | Device name shown in the tailnet |
| `tailscale_state` | Persistent machine/WireGuard/DISCO identity file |
| `tailscale_autostart` | `1` = bring Tailscale up before connecting. Written by DSSH itself after a registration succeeds, so the node returns on the next boot without the auth key; set it to `0` (or delete the line) to stop that |
| `config_version` | Format marker DSSH writes and reads; not a setting to edit by hand. See the note below |
| `tailscale_control_url` | Coordination server; defaults to Tailscale SaaS |

### Multiple machines

To reach more than one server, suffix each group of `host` / `port` / `user` /
`key_path` with `_N` (`N` = 0 … 7).  Suffixed keys win; an unsuffixed group
becomes machine 0 automatically, so an existing file keeps working untouched:

```ini
name_0     = home-nas          # optional display name; the picker shows this
host_0     = server-a.example.com
user_0     = ubuntu
key_path_0 = sdmc:/3ds/3dssh/id_a

name_1     = build-box         # a name with # or " must be quoted: "a # b"
host_1     = server-b.example.com
user_1     = mac
key_path_1 = sdmc:/3ds/3dssh/id_b

selected_machine = 0     # which one the picker pre-highlights (clamped to range)
```

`name_N` is optional and capped at 31 bytes (a longer value is cut by the
loader).  A machine without one is listed by its host, exactly as before the
key existed.  The name is **editable on the device** — see
[SSH machine picker](#ssh-machine-picker-multiple-machines).

Number the machines from `0` up with **no gap**.  The list stops at the first
empty slot, so `host_0` plus `host_2` with no `host_1` is one machine, not
three — DSSH would otherwise offer you the hole as a row with nothing to
connect to.  Nothing is deleted or edited in the file, and the boot screen
says how many machines were left out, but they cannot be reached until the
numbering is filled in.

With two or more configured, launch opens the **machine picker** first — see
[SSH machine picker](#ssh-machine-picker-multiple-machines).  Tailscale is
**global** and stays unsuffixed: every machine shares it.

When `macos_keychain_password` is set, DSSH waits for the current interactive PTY
shell to finish initialization, then runs
`/usr/bin/security unlock-keychain "$HOME/Library/Keychains/login.keychain-db"`.
It waits until macOS prints `password to unlock` before sending the password
through the PTY—not `-p`, the remote process arguments, or shell history. This
matches macOS Keychain's interactive-session requirement and works with fish,
zsh, and bash. DSSH then clears its in-memory copy. If unlock fails, the normal
SSH shell remains available and a warning is shown. A later `claude` launch can
then read the credential already stored in that login keychain.

Successful unlock diagnostics are hidden and the bootstrap output is cleared
before fish redraws its prompt. On failure, DSSH keeps one concise warning with
the unlock and verification status codes. The password itself is never shown.

Quote a password containing `#` or leading/trailing spaces:

```ini
macos_keychain_password = "my # password"
```

> ⚠️ This option stores your **macOS login password in plaintext on the
> removable SD card**. That is more sensitive than a dedicated SSH key. Use a
> dedicated macOS account where practical, keep the SD card physically secure,
> and leave this field empty unless you accept this risk.

### Native Tailscale transport

Tailscale support is provided by
[`cadl/libts3ds`](https://github.com/cadl/libts3ds), an experimental native C
client for Nintendo 3DS derived from MicroLink. It is not the official Go
`tailscale/libtailscale` library and does not run `tailscaled`. DSSH pins the
audited source as the `libts3ds` git submodule at the
[`libts3ds-v0.1.0`](https://github.com/cadl/libts3ds/releases/tag/libts3ds-v0.1.0)
release. The top-level `make` builds the static library automatically.

When enabled, SSH uses the private libts3ds/lwIP TCP stack and can select
direct UDP, Tailscale Peer Relay, or DERP. The default build uses automatic
path selection. Diagnostic builds can fix the data path at compile time with
`DSSH_TAILSCALE_PATH=direct`, `peer-relay`, or `derp`.

Tailscale starts before a connection when an auth key is configured, or when
`tailscale_autostart = 1` says the user wants it — and in no other case.  The
`tailscale.state` file on its own starts nothing: it is an identity file, and
letting its mere presence decide launch behaviour meant a leftover from an old
build spent every later boot's first seconds on a Tailscale bring-up nobody
asked for.  DSSH writes `tailscale_autostart = 1` itself after a registration
succeeds, so a node registered with an auth key still returns on later boots
after the key has been removed from the file; delete the line (or set it to
`0`) to stop that.  `tailscale_hostname` defaults to `dssh-3ds`, and
`tailscale_state` defaults to `sdmc:/3ds/3dssh/tailscale.state`.  The auth key
may remain in the configuration after registration, although removing it
reduces exposure if the SD card is lost.

Both files on the card carry a format marker — `config_version` in `config.ini`
and `settings_version` in `settings.ini` — written by the app, not by the user.
Its only job is to protect a card that has been used by a NEWER DSSH: an older
build loads such a file (refusing would lose the machines on it) but will not
write it, and says so on the boot screen, because it cannot know what the newer
keys mean.  A file without the marker, or with a lower one, is upgraded in
place on the next save.

The state file contains private machine, WireGuard, and DISCO identity keys.
Treat both it and the auth key as secrets. This is an unofficial community
integration and is not affiliated with or endorsed by Tailscale Inc.


Final SD layout:

```
sdmc:/3ds/3dssh/
├── config.ini
├── id_rsa
├── theme.ini.example          # optional; see "Custom theme" below
└── tailscale.state            # created after Tailscale registration
```

---

## Voice input

> **TL;DR**: install the **API track** (one curl + one API key, ~30 KB
> on disk, ~1-2 s end-to-end).  The self-hosted track exists for
> completeness but is *not recommended* for typical servers — see the
> warning at the bottom of this section.

Hold **R** on the 3DS, speak a Chinese sentence, release — the
transcribed UTF-8 text drops into the SSH terminal as if you had typed
it.  Full sentences flow into Claude Code without ever opening the soft
keyboard.  The voice key is **R** in all builds since v1.2; earlier
versions used **L** as the voice key (and a *Voice Key* setting to swap
between L and START), but the L/START toggle was retired in favour of
a single, model-uniform entry point.  See [`KEYBINDING.md`](KEYBINDING.md).

The 3DS records 16 kHz PCM mono via its built-in microphone, ships up to
one take's worth of audio (15 / 30 / 60 s, from the *Max Voice Limit*
setting) over a **second libssh2 channel** on the same
SSH session (no new ports, no new auth, no firewall changes), and a
small server-side shim transcribes it — via OpenRouter on the `api`
track, or via a local whisper.cpp daemon on the `local` track.

**Status indicator** (top-left of the soft keyboard top row):
- 🔴 **REC** (red, pulsing) — recording in progress; a thin bar along the
  bottom edge of the touch screen shrinks with the time left in the take
- ⠋⠙⠹⠸ (cyan, spinning) — uploading + transcribing
- **ERR** (red, 2 s) — request failed; press the record key again to
  retry

### Recommended install — API track

The voice features need **two API keys** on the server.  Both
together cost a few cents per month for personal use:

| Key | Where to get it | What it powers | Required? |
|---|---|---|---|
| **OpenRouter** | [openrouter.ai/settings/keys](https://openrouter.ai/settings/keys) | Qwen3-ASR-Flash — `qwen/qwen3-asr-flash-2026-02-10` (speech → text) | **Required** for the voice IME and AI ask (R+START) |
| **DeepSeek** | [platform.deepseek.com/api_keys](https://platform.deepseek.com/api_keys) | DeepSeek-Chat (AI question answering) | Optional — only needed for R+START AI ask; voice IME works without it |

Pricing: OpenRouter's Qwen3-ASR-Flash costs roughly **2× Whisper Turbo
per audio-hour** — still only a few cents per month for an individual,
but check openrouter.ai for the current rate rather than trusting a
number in this file.  DeepSeek-Chat is roughly `$0.0001 per question`.
Both providers give a small free credit on sign-up, more than enough to
test.

#### One-command install

SSH into your server, then:

```bash
git clone https://github.com/oli-bot/3DShell.git ~/dssh-repo
bash ~/dssh-repo/tools/install_whisper_api.sh
```

The installer will interactively prompt for both keys:

```
▶ checking prerequisites...
✓ python3 3.10
▶ writing track config...
✓ default track set to 'api' (use 'dssh-whisper switch' to change)

Need your OpenRouter API key (https://openrouter.ai/settings/keys).
Used for Whisper transcription.  Looks like:  sk-or-v1-...
Paste your OpenRouter key (or empty to skip): █

✓ api-key saved at /home/you/.config/dssh-whisper/api-key (chmod 0600)

Optional: DeepSeek API key (https://platform.deepseek.com/api_keys).
Used only by the R+START voice AI-ask modal — skip if you only
want plain voice IME.  Looks like:  sk-...
Paste your DeepSeek key (or empty to skip): █

✓ deepseek-key saved at /home/you/.config/dssh-whisper/deepseek-key (chmod 0600)
✓ dssh-whisper-api installed (lightweight, OpenRouter Qwen3-ASR-Flash).
```

**Non-interactive** install (handy for CI / Ansible / clean rebuilds):

```bash
OPENROUTER_API_KEY="sk-or-v1-..." \
DEEPSEEK_API_KEY="sk-..." \
bash ~/dssh-repo/tools/install_whisper_api.sh
```

#### Verify the install worked

```bash
$ dssh-whisper status
active track: api
(API-only install — no local daemon)
api-key:  ✓ /home/you/.config/dssh-whisper/api-key
```

The 3DS calls `~/.local/bin/dssh-whisper-shim` over SSH-exec on every
**recording take** (held R releases trigger it); no further server-side
action is needed.

#### What the installer dropped on disk

```
~/.config/dssh-whisper/
├── track            # "api" or "local"
├── api-key          # chmod 0600 — OpenRouter
├── deepseek-key     # chmod 0600 — DeepSeek (optional)
~/.local/bin/
├── dssh-whisper          # CLI wrapper (start/stop/status/switch/uninstall)
└── dssh-whisper-shim     # symlink the 3DS reaches over SSH-exec
~/.local/share/dssh-whisper/
└── whisper_shim.py       # the actual Python that talks to both APIs
```

Footprint: ~30 KB.  No daemon, no model, nothing to monitor.

#### Optional per-host overrides (not created by the installer)

Two extra files let you point the R+START AI-ask call at a different
OpenAI-compatible gateway and model.  **Neither installer writes them** —
create them by hand only if you need them; when they're absent the shim
falls back to its built-in defaults:

```
~/.config/dssh-whisper/
├── deepseek-url     # full chat-completions URL
│                    #   default: https://api.deepseek.com/chat/completions
└── deepseek-model   # model name
                     #   default: deepseek-chat
```

Useful when `api.deepseek.com` is unreachable from your network (some
ISPs/CDNs return 403) and you route through a gateway instead.  Plain
text, first line used, read on every shim invocation — no restart
needed.  Voice IME is unaffected; these only apply to the AI-ask
modal.

#### Rotating a key later

Compromised key, expired credit, swapping providers — overwrite the
file:

```bash
echo 'sk-or-v1-NEW_KEY' > ~/.config/dssh-whisper/api-key
chmod 0600 ~/.config/dssh-whisper/api-key
# (no service to restart — the shim re-reads the key on every call)
```

| Field | Value |
|---|---|
| Inference | Qwen3-ASR-Flash on OpenRouter (cloud) + DeepSeek-Chat |
| Latency (4 s clip) | ~1-2 s STT, +1-2 s LLM for AI ask |
| Cost | ~2× Whisper-Turbo rate per audio-hour STT (a few cents/month) + ~$0.0001 / AI question |
| Server install size | ~30 KB |
| Server CPU load | negligible |
| Internet | required (HTTPS to openrouter.ai + api.deepseek.com) |

That's enough for 99% of users — install it, hold **R**, done.

### `dssh-whisper` CLI

```
dssh-whisper status                # active track + daemon status + key presence
dssh-whisper switch                # toggle api ↔ local
dssh-whisper switch [api|local]    # set explicit track
dssh-whisper start                 # start local daemon (dual install only)
dssh-whisper stop | close          # stop local daemon
dssh-whisper restart
dssh-whisper logs [-f]             # tail systemd-user logs (dual install)
dssh-whisper uninstall             # remove all dssh-whisper files
```

Daemon-related commands degrade gracefully on the API-only install
(they print "no daemon to start", non-fatal).

### Quick AI questions (R + START) — v1.1

Stuck in `yazi` and forgot the keybind for hidden files?  Need a
one-liner regex for `vim`?  Hold **R** and press **START** — you're
now asking the AI instead of typing into the SSH session.  A modal
pops up on the bottom screen with the answer; the SSH terminal on top
is left untouched.

| In modal | Effect |
|---|---|
| **A** | close, **keep** the Q&A in history; next R+START continues the conversation |
| **B** or any **touch** on the bottom screen | close, **clear** history; next R+START is fresh |

Conversation history caps at 5 turns; if you keep pressing A past
that, the oldest turn drops off (FIFO).

The model is `deepseek-chat` (DeepSeek direct API, not via
OpenRouter) — picked for its sub-second latency and very low pricing
(~$0.0001 per question for personal use).  Answers are 6-15 sentences
typically; long answers wrap inside the modal and overflow truncates
with a trailing `...`.

#### Markdown rendering in the modal

DeepSeek tends to use markdown.  The modal renders common forms in
colour rather than showing raw syntax characters:

| Markdown source | Modal rendering |
|---|---|
| `# Heading` / `## Heading` / `### Heading` | yellow accent, `#` markers stripped |
| `` `inline code` `` | cyan, backticks stripped |
| ` ``` …code block… ``` ` | cyan multi-line, fences stripped |
| `- item` / `* item` | `• item` (dim grey bullet + normal text) |
| `**bold**` / `*italic*` | normal text — syntax stripped, no emphasis (no bold font at this size) |
| `[link text](url)` | `link text` only — URL dropped |
| `~~strikethrough~~` | normal text — syntax stripped |

#### DeepSeek key

You'll need a **DeepSeek API key** from
[platform.deepseek.com/api_keys](https://platform.deepseek.com/api_keys).
The installer prompts for it alongside the OpenRouter key (skippable
— plain voice IME works without it).  Stored at
`~/.config/dssh-whisper/deepseek-key` chmod 0600 — see [Rotating a
key later](#rotating-a-key-later) above to change it.

If your network can't reach `api.deepseek.com` (e.g. it 403s), create
`deepseek-url` (full chat-completions URL) and `deepseek-model` (model
name) in the same directory to point AI Q&A at any OpenAI-compatible
gateway; without those files the official endpoint is used.

### Notes

- How long one take runs is the **Max Voice Limit** setting (15 / 30 / 60 s);
  the hard ceiling is ~63 s, where the 2 MB mic buffer fills at
  16.36 kHz × 16-bit.  To end a take early, just release **R** — that
  stops the take and ships it.
- Use **HOME** (not START) to exit DSSH — START is dedicated to the
  AI-ask chord.
- `~/.config/dssh-whisper/` is on `.gitignore` already; rotating the
  API key is one `echo > api-key` away.
- The 3DS code calls `~/.local/bin/dssh-whisper-shim` over a libssh2
  exec channel.  The shim reads the active track and dispatches —
  switching tracks doesn't require restarting the 3DS or the SSH
  session.

### Advanced — Dual track (self-hosted, ⚠️ not recommended)

> ⚠️ **Heads-up**: the self-hosted track loads the `whisper-small`
> model (~1 GB resident) and runs CPU inference for every recording.
> On a 2-vCPU AWS t3.medium with VS Code Remote, claude-code, tmux, and
> chrome-devtools-mcp running, transcribing 4 seconds of audio took
> **~40 seconds** — vs. **~1.5 seconds** through OpenRouter at the same
> moment.  The cloud rate is roughly 2× Whisper Turbo per *audio* hour,
> which is still pennies/month for personal use — so we strongly
> recommend the cloud path unless you have a hard reason to keep audio
> on-prem.
>
> If you do go local, plan on:
>
> - **4+ idle vCPU cores** at 3+ GHz — anything less and the 3DS UX
>   spinner becomes painful.
> - **2+ GB free RAM** for the small.zh model + buffers.
> - **No competing CPU consumers** during transcription windows.
> - **~600 MB disk** for the model + venv.
> - **systemd** — `install_whisper_dual.sh` registers a user service and
>   stops with `systemctl not available. This installer assumes systemd.`
>   when it is missing.  macOS has no systemd, so on a Mac server the
>   dual installer is not an option: stay on the API track, or run
>   `tools/whisper_daemon.py` as a Launch Agent you write yourself (the
>   socket path, `/tmp/dssh-whisper.sock`, is all the shim requires).

If you genuinely want the offline path, the same `dssh-whisper` CLI
manages both tracks side-by-side — install the dual variant and flip
on demand:

```bash
git clone https://github.com/oli-bot/3DShell.git ~/dssh-repo
bash ~/dssh-repo/tools/install_whisper_dual.sh
```

The dual install still defaults to `track=api`; flip to local only when
needed:

```bash
dssh-whisper switch local   # next take → self-hosted whisper.cpp
dssh-whisper switch api     # next take → OpenRouter (default)
dssh-whisper switch         # no arg = toggle
```

---

## Key bindings

### Physical buttons

| Button | Function | Notes |
|---|---|---|
| **A** | Enter (EN) / **emit pinyin buffer as English** (IME) | Accidentally typed English in CN mode? Press A and the buffer flies to SSH as raw ASCII — no need to backspace and switch modes. |
| **B** | Backspace / consume one pinyin letter | Hold-style auto-repeat (peaks at 60 / sec) |
| **X** | Alt modifier | Hold-style — held when the next key fires |
| **Y** | Ctrl modifier | Hold-style — Y + tap `c` → Ctrl-C |
| **X + Y + ▲** | **New window** | Opens a second (third, fourth) shell channel on the *same* SSH login.  Both modifiers must be physically held — a sticky-latched X/Y does not count.  See [Multi-window terminal](#multi-window-terminal) below. |
| **X + Y + ▼** | **Close current window** | Tears that channel down — the remote shell gets `SIGHUP`, so it works even when a full-screen TUI has swallowed stdin.  Inert on the only remaining window. |
| **X + Y + ◀** or **X + Y + L** | Previous window | Wraps around.  The chord swallows that press, so no stray `←` reaches the shell and `L` does not trigger the dual-screen toggle.  Nothing is lost from arrow use: this client never encodes Alt/Ctrl onto the D-pad (`X + ◀` is just a plain `←`), so only the two-modifier chord changes meaning |
| **X + Y + ▶** or **X + Y + R** | Next window | Same, forward; `R` does not start a voice take on that press (the chord steals R out of the held bit) |
| **R** | **Push-to-talk recording** | Hold R, speak, release — press starts the take, release stops and sends it.  **R + START** = AI ask.  See [Voice input](#voice-input) below |
| **R + START** | **AI-ask voice modal** | Hold R, tap START — record a question, the answer pops up in a bottom-screen modal.  See [Voice input](#voice-input) |
| **L** | (idle) / **+ START → dual-screen toggle** | L alone does nothing.  As the **START + L** modifier it merges both screens into one 66×40 terminal (see [Dual-screen one-terminal](#dual-screen-one-terminal)).  L is *not* Shift any more: shifted keys live on the soft keyboard's ⇧ |
| **START** | **+ R → AI ask** | START alone does nothing.  Combined with held R it triggers the AI-ask modal; combined with held L it triggers the dual-screen toggle.  Use **HOME** to exit DSSH |
| **SELECT** | Esc | Tap fires immediately.  When the SSH session is dead (e.g. after lid-close sleep), SELECT triggers the reconnect instead |
| **拼/EN** (soft keyboard) | **Toggle CN/EN IME mode** | Replaces the old BSP soft key.  Status chip flashes "C" / "E" as a transient event label (the destination mode — physical L is the **START + L** dual-screen modifier and does not itself toggle IME).  See [Using the IME](#using-the-ime) |
| **Space** (soft keyboard) | Plain space (EN) / **commit highlighted candidate** (IME) | Matches sogou / fcitx convention |
| **。** (soft keyboard) | **。** (full-width Chinese period, U+3002) | Its own key on the CN symbol page (**123**).  Not a `Shift + .` result: the letters page has no `.` key, and the symbol pages are shift-independent (key face = what a tap emits) |
| **⇧** (soft keyboard) | One-shot shift: uppercases the next letter (`a` → `A`).  Long-press = CapsLock | Affects the **letters** page only.  The symbol pages are shift-independent — every face there is exactly what a tap emits, so `{ } "` `\|` `_` `:` `?` `~` each have their own key instead of a Shift pair |
| **D-pad ↑↓** | Arrow keys / IME page nav | When the IME buffer is active, ↑↓ paginates candidates.  With **X + Y** held they open / close a window instead |
| **D-pad ←→** | Arrow keys / IME selection cursor | When active, ←→ moves the candidate cursor within the page.  With **X + Y** held they switch windows instead |
| **Circle Pad ↑↓** | Scrollback / full-screen app paging | A normal shell scrolls DSSH's **local scrollback**; when the remote is in a full-screen app (opencode, htop, vim…) DSSH sends **PageUp / PageDown** instead — those apps redraw in place, so nothing ever scrolls off and the local buffer would be empty |

> **Full key surface model** lives in [`KEYBINDING.md`](KEYBINDING.md) — that
> file is the canonical, model-uniform cheat sheet (every key documented
> there exists on every 3DS model).  The table above is the in-README
> condensed version.

> Long-press D-pad or B (the 标准 / Standard setting): 250 ms initial
> delay, then 12 / sec, 30 / sec from 0.75 s, and 60 / sec from 1.75 s.

### Project card page

While the [project-card page](#project-cards) is up it **owns the whole
keypad** — the X + Y window chords, the START + L dual-screen toggle and
R's voice take are all suppressed, and the D-pad does not emit arrows
into the shell.  Its own keys are a small set:

| Button | Effect |
|---|---|
| **D-pad ↑↓** | Move the selection |
| **D-pad ◀▶** / **L** / **R** | Switch editor tab (in the editor) |
| **A** | Run the selected card / act on the form row |
| **X** | Edit the selected card |
| **Y** | Delete — twice (the first press only arms it) |
| **B** | Save the draft and go back, or close the page from the list |
| **START** | Save and close the page |

Everything on the page is also reachable by stylus — the card list, the
four tabs, the tool and flag presets, and the 保存 / 取消 buttons.

### Dual-screen one-terminal

Press **START + L** to merge the two physical screens into **one
continuous 66×40 terminal**: the focused window's PTY is live-resized to
40 rows (other windows follow the frame you tab to them), the top
screen shows rows 0–19 (identical to normal mode) and the bottom screen
shows rows 20–39.  Your scrollable viewport doubles for long output
(`less`, `git log`, build logs) and for full-screen tools
(`vim`/`htop`/`top`).  Press **START + L** again to shrink back to 66×20
and bring the soft keyboard back.

| Action | Effect |
|---|---|
| **START + L** | Enter / leave dual-screen mode (SSH PTY is renegotiated live; the remote shell repaints) |
| Circle Pad ↑↓ | Scroll the whole merged terminal through scrollback |

- Entering/leaving sends SIGWINCH and makes the remote shell redraw once —
  expected behaviour.
- The bottom screen is only 320 px wide, not enough for 66 columns
  (396 px), so the bottom 20 rows are **squeezed horizontally to ~0.81×**
  across the full panel — **no column is lost or clipped**; the bottom
  glyphs are just ~20% narrower and a little softer than the crisp
  full-width top screen.
- When the remote enters a **full-screen TUI** (opencode, vim, htop, … —
  i.e. anything that switches to the alternate screen), the bottom panel
  automatically switches from squeezing to **center-crop**: the edge
  columns are cropped and the middle 53 columns render at full 6 px, so
  the text stays as crisp as the top screen.  You can force any of four
  behaviours from the settings list (**下屏布局 / Bottom Layout:
  AUTO / SQUEEZE / CENTER / TRIM**) — **TRIM** crops just 2 columns on
  the left and 3 on the right,
  then squeezes the remaining 61 at ~0.87× (a middle ground between
  SQUEEZE and CENTER).
- There is no soft keyboard while dual-screen is active (that is what you
  are toggling back to for typing).
- A dropped connection restores your current dual-screen state when the
  session reconnects.

### Multi-window terminal

One SSH login, up to **four interactive shells**.  A new window is *not* a new
connection: it opens another channel on the session you already
authenticated, so there is no second handshake, no second keychain unlock and
no second Tailscale tunnel — and the keepalive covers all of them.

| Action | Effect |
|---|---|
| **X + Y + ▲** | Open a new window and switch to it (its shell starts at whatever size you are currently viewing) |
| **X + Y + ▼** | Close the window you are looking at and fall back to a surviving one |
| **X + Y + ◀** / **X + Y + L** | Previous window (wraps) |
| **X + Y + ▶** / **X + Y + R** | Next window (wraps) |
| `exit` in a window | Also closes it — the remote shell quits on its own, its channel ends, and the window dissolves |

- **Closing is a channel teardown, not a typed `exit`.**  DSSH sends EOF and
  frees the channel, the remote PTY goes away and the process gets `SIGHUP`.
  That is deliberate: a full-screen TUI like `opencode` owns stdin and would
  eat the word `exit`, and a hung process cannot be asked politely.  Anything
  still running in that window dies with it, unsaved work included.
- The chord is **inert on the last window** — that shell *is* the session, and
  `exit` is how you end it (which lands you on the same SELECT-to-reconnect
  prompt a dropped connection does).
- X and Y are physical buttons on **every** model, so this works on
  old 3DS and 2DS — same as the **START + L** dual-screen trigger.  Both
  modifiers have to be held at the same moment — a sticky-latched X or
  Y will not arm the chord, and the chord eats the third key, so
  `X+Y+L` never triggers the dual-screen toggle, `X+Y+R` never starts
  a voice take, and `X+Y+◀` never reaches the shell as `←`.  An arrow
  chord stays swallowed until you physically let go of it, because
  arrow auto-repeat runs off the held mask — one press means one
  window, not a slide through them.
- Hidden windows are **not paused**.  Every window is drained each frame, so a
  background `opencode` keeps running instead of blocking on SSH flow control;
  the focused one gets the larger share of each frame's read budget.
- Once there is more than one window, the bottom bar grows a strip —
  `1·2●·3` — with the focused number in the theme's primary text colour and a
  `●` after any background window that has produced output you have not
  looked at (it clears the moment you switch to it).  The dot means
  *traffic that window received on its own*, never a repaint the local view
  change provoked.  The strip is drawn even if **底栏信息 / Bottom Info** is
  set to hidden.
- **Dual-screen mode (START + L) is global** in effect, but it resizes
  only the window you are looking at: a resize is a `SIGWINCH`, and a
  hidden TUI answering one by repainting would look like unread output.
  Hidden windows keep the grid they had when you last saw them and
  renegotiate the frame you tab to them.  In that mode the strip moves
  to a small chip floating over the top-right of the bottom screen
  (there is no bottom bar).
- The settings page owns the whole keypad while it is open — including X and Y
  — so window chords do nothing there, by design.  Same for the AI-answer
  modal.
- Switching windows discards a **half-typed pinyin buffer** (it would
  otherwise commit into whichever shell you land in).  CN/EN mode, keyboard
  page and ⇧ state are shared across windows — they are UI state, not stream
  state.
- In-flight voice dictation stays pinned to the window it started in, so
  tabbing over mid-transcription cannot drop Chinese into the wrong shell.
- Server-side caveats: sshd's `MaxSessions` (default 10) has to leave room —
  four shells plus the one the voice subsystem borrows; and if your login
  shell starts `tmux` automatically, all four "windows" become clients of the
  same tmux session rather than four independent shells.

### Soft keyboard

The bottom screen is the soft keyboard — three pages:

- **Letters page** (default): QWERTY, plus ⇧, the **拼/EN** IME toggle,
  the `123` page switch, Tab, a wide Space and the ↑ send key.  There is
  **no punctuation on this page** — not even `.` or `,` — which is why
  `。` lives on the CN symbol page and not on a Shift pair.
- **Symbols page 1** (toggle via the bottom-left `123` key):
  `1234567890` on the top row, then `- / : ; ( ) $ & @ "` (EN) or
  `- / : ; ( ) ¥ @ “ ”` (CN), then `` . , ? ! ' ` `` (EN) or `。， 、 ？ ！ .`
  (CN).
- **Symbols page 2** (via `#+=` on page 1): the programming set —
  `[ ] { } # % ^ * + =`, then `_ \ | ~ < > € £ ¥ •`.  The ASCII
  `[] <>` shells need for tests and redirections live here.

Every page keeps Tab, the wide Space and ↑ on its bottom row.  All four
symbol variants (page 1 / 2 × CN / EN) are **shift-independent**: ⇧ and
CapsLock do nothing there, so a key face is exactly what a tap emits.

Any key supports hold-style modifier combos.  Example:
**hold Y + tap b** = `Ctrl-B` (the tmux prefix).

### Status bar (top 30 px)

```
┌──────────────────────────────────────────────────────┐
│ [SFT]   candidate strip / pinyin buffer / cands   [⚙]│
└──────────────────────────────────────────────────────┘
```

- **Left slot [STA]**: 3-letter modifier indicator (SFT/CTL/ALT stays lit
  while held; ENT/BSP/ESC/`C`/`E` flashes for 200 ms on transient events).
- **Middle**: pinyin buffer + candidates in CN mode; empty in EN mode.
- **Right slot [⚙]**: settings menu button.  **Tap to open the
  dual-screen settings page** (which contains an entry to the debug
  page); press **B** or tap the badge again to close it.

---

## Using the IME

### Full pinyin

Tapping letters in CN mode brings up the candidate strip:

```
ni       → 年 你 牛棒 娘 念   (page 1/52, total 256)
nihao    → 你好 你好吗 你好啊 拟好 你好呀
shijie   → 世界 世界上 世界杯 世界各地 世界里
```

- **A** or **Space** commits the currently highlighted candidate.
- **D-pad ←→** moves the highlight within the current page.
- **D-pad ↑↓** flips between pages.
- **Tap** a candidate to commit it directly.
- **B** consumes one letter from the pinyin buffer.

### Abbreviation (initials)

Every multi-syllable word gets an extra entry keyed by its initials —
typing the initials still surfaces it (with weight × 0.3, so the
full-pinyin form still ranks first when typed in full):

```
nh → 你好  (around the 8th candidate)
wm → 我们  (top candidate)
sj → 世界
zw → 中文
xx → 谢谢
```

Page or cursor over to your target, then commit with A.

### Prefix fallback

Typed an extra letter past a valid prefix?  The engine automatically
matches the longest valid prefix and shows the surplus letters in red:

```
buffer:  niha[oz]    ← niha in green + oz in red
candidates:           still showing what nihao would produce
```

Press B to chew the red tail back to a clean prefix.

### Modifiers always bypass the IME

In CN mode, **hold Y + tap c** still sends `Ctrl-C`; **hold X + tap f**
still sends `Alt-F` (ESC + `f`).  Modifiers take priority over IME
routing, so vim / tmux / claude-code shortcuts keep working.  Case is
not a physical modifier any more — it comes from the soft keyboard's ⇧.

### Bail out: emit pinyin as English

CN mode + non-empty buffer + press **A** = the buffer flies to SSH as
raw ASCII letters and clears.  Example: you accidentally typed
`cd /etc` while in CN mode and the candidate strip is showing strange
Chinese.  One press of **A** delivers `cd /etc` to the shell — no
backspacing, no mode-toggle, no retyping.

> Difference: **Space** commits the highlighted candidate (Chinese
> chars on screen).  **A** sends the typed letters as-is.

---

## Settings & debug page

Tap the **⚙** badge in the top-right corner (or close it again with
**B** / a second tap on the badge).  The page takes over **both
screens**, 3DS System-Settings style — the top screen is not touch,
so the navigable list lives there and the touch surface below holds
the detail card:

- **Top screen**: the current page's items as a list; the right side of
  each row shows the current value and a highlight bar marks the
  selection.  A dim legend at the bottom repeats the key model.
- **Bottom screen**: the selected item's detail card — title, current
  value, a one-or-two-line description of what the item does, and
  **every option as a real tappable button** (the current one wears
  the accent colour).

| Key | Effect |
|---|---|
| **D-pad ↑↓** | Move the selection (wraps; holding auto-repeats) |
| **D-pad ←→ / A** | Step the selected item's value (wraps); on 调试页面, A enters the debug screen |
| **L / R** | Step between the groups — 外观 → 输入 → 语音, wrapping.  Only inside a group; the root is not one of them |
| **B** | Back out one level (group → root); from the root it closes settings |

While the page is open it owns the whole keypad: R does not start
hold-to-talk voice, START does not trigger AI-ask (R+START), START + L
does not flip the dual-screen view, the D-pad does not send arrows,
and the X + Y window chords do nothing.  (The debug sub-page is a
different page — it does not hold the keypad, so window chords work
there.)  The selection is remembered across open/close cycles.

The root page holds five rows.  Three of them are **groups** — they open a
sub-page, and **B** backs out to the root.  (The debug entry is *not* a
group: it opens a separate page that does not hold the keypad.)

- **Appearance (外观)** `[ 进入 / Open ]` — theme, language, status bar,
  mascot and bottom layout.
- **Input (输入)** `[ 进入 / Open ]` — modifiers, click sound and
  auto-repeat.
- **Voice (语音)** `[ 进入 / Open ]` — recording limit and record key.
- **Projects (项目)** `[ 进入 / Open ]` — opens the
  [project-card page](#project-cards).  From here it is a pure manager:
  **A** never runs a card (use a window for that), but add / edit /
  delete all work, and the changes are written to `settings.ini` at
  once.  This row and the picker that appears after connecting raise
  the *same* page, so the keys are identical in both.
- **Debug Page (调试页面)** `[ 进入 ]` — opens the debug screen
  described below.

**外观 / Appearance** — the five rows of this group, in order:

- **Theme (主题)**: `[ 默认 Tokyo Night / 黑客绿 Matrix / 黑白
  Mono / 深海 Solarized / 自定义 Custom ]` (high-contrast syntax
  themes).  Renamed from
  **终端主题**: it now repaints the **whole bottom screen** together
  with the top-screen terminal — keycaps and labels, status row, IME
  candidate strip, settings page, debug page, bottom info text and the
  bottom-screen background.  The settings page itself is drawn from the
  theme palette, so **switching themes previews the new colours live,
  right on the page**.  Deliberately *not* themed: the mascot sprites
  (they are the character art that identifies each species), the AI
  answer modal, and the voice badge's REC/spinner/ERR status tints.
  The table lives in `source/kb_theme.c` and is checked on the host by
  `make test-theme` (default theme byte-for-byte regression + per-role
  alpha + WCAG contrast floors).  The fifth entry, **自定义 / Custom**,
  is not a colour scheme — it reads **`theme.ini`** off the SD card
  (see [Custom theme](#custom-theme-themeini)).
- **Interface Language (界面语言)**: `[ 中文 / English / 日本語 /
  한국어 / Deutsch / Français ]` — the language of **every label in the
  program**: settings rows, keycaps, legends and modal buttons.  Six
  options, the widest row on any page.  Unrelated to the on-screen
  **拼 / EN** key, which switches what you *type*, not what the program
  *says*.  It sits second in this group on purpose — someone who cannot
  read the current language has to find the way out near the top of the
  first group.
- **Bottom Info (底栏信息)**: `[ 时钟电量 Clock+Battery / 仅时钟
  Clock Only / 隐藏 Hidden ]` — real-time 3DS battery % and charging
  indicator; only the battery digits tint — yellow below 20%, green
  while charging, red below 10% — in shades matched to the active theme,
  while the clock stays on the theme's dim text colour; **Hidden**
  frees the whole bottom row for the mascot, whose walkable region
  always tracks the width of whatever is shown.
- **Mascot (吉祥物)**: `[ 关 / 螃蟹 / 蟹特Crab / 坤坤 / 菌菌 / 大眼 / 牛来 / 刀盾狗 ]` —
  bottom bar pixel companion (crab / crab (xiete) / chick / phage / alien / 牛来 / 刀盾狗
  species).  Each species has its own key-click voice.
- **Bottom Layout (下屏布局)**: `[ 自动 AUTO / 压缩 SQUEEZE / 居中
  CENTER / 裁边 TRIM ]` — how the bottom screen renders in dual-screen
  mode (toggled by **START + L**); AUTO switches intelligently with the
  remote app.

**输入 / Input** — three rows:

- **Sticky Modifiers (修饰键)**: `[ 按住 Hold / 粘滞 Sticky ]` — the
  trigger style of **Y(Ctrl) and X(Alt)**: Hold means the modifier must
  be held together with the next key; Sticky latches it on a single tap
  for stylus-friendly one-handed typing.
- **Key Click Sound (按键音效)**: `[ 关闭 Off / 开启 On ]` —
  soft-keyboard tap click audio feedback.
- **Key auto-repeat (按键重复速度)**: `[ 慢 Slow / 标准 Standard /
  快 Fast ]` — how fast a **held** key auto-repeats.  The ramp drives
  three surfaces at once: the physical D-pad, **B** as backspace, and a
  held on-screen key.  Holding **⇧** for caps lock is unaffected.

**语音 / Voice** — two rows:

- **Max Voice Limit (录音上限)**: `[ 15秒 / 30秒 / 60秒 ]` — voice
  recording cap; 60 s fits the 2 MB mic buffer with headroom,
  auto-clamped to ~63 s hard ceiling.  While a take is running, a bar
  along the very bottom edge of the bottom screen shrinks with the time
  left off this cap — green, then yellow over the last 20%, red over the
  last 10%.  It disappears the moment the take ends (release, or the
  cap cutting it off).
- **Voice Key (录音按键)**: `[ R = 按住说话 ]` — single, model-uniform
  voice trigger: hold **R** to record, release to ship.  Combined with
  **START** (held R, then tap START), the same take becomes an AI-ask
  query and the answer pops up in a bottom-screen modal.  Earlier builds
  exposed `[ START / L ]` here as a user-selectable voice-key scheme; that
  toggle was retired in M11 in favour of the single R entry point — this
  row is now a reminder of the binding, not a control: its only button
  does nothing when tapped.

Two more notes that apply to the page as a whole:

- **Persistence**: settings save immediately to
  `sdmc:/3ds/3dssh/settings.ini` (next to `config.ini`); delete the
  file to restore defaults. The same file is round-trip tested on the
  host (`make test-config`).  Custom colours are **not** stored here:
  `settings_save` rewrites the whole file, so hand-edited content would
  be erased — they live in `theme.ini`, which the app never writes
  (see below).
- **Debug page**: tap the top-right **⚙** button at any time to exit
  back to the keyboard:
  - Title + exit hint.
  - **recv hex**: the last 32 bytes received from SSH — for diagnosing
    ANSI / SCS / mouse-protocol issues at the byte level.
  - **Physical key cheat sheet**: a condensed version of the bindings
    table above.
  - The page is display-only: the old **MASCOT** and **BOTTOM** toggle
    buttons live in the settings list now (吉祥物 and 下屏布局).

### Custom theme (`theme.ini`)

The **自定义 / Custom** theme entry is not a built-in colour scheme — it
loads whatever is in `sdmc:/3ds/3dssh/theme.ini`.  Start from
`sd_template/3ds/3dssh/theme.ini.example` in the repo, copied by hand
like `config.ini.example`:

```
sdmc:/3ds/3dssh/
├── config.ini
├── theme.ini            # optional; custom colours
└── theme.ini.example    # full key reference, copied alongside
```

Every key is optional — a missing key keeps the default colour, so a
one-line file that changes a single role works.  The file accepts all
23 bottom-screen roles (`bottom_bg`, `key_body`, `label`, …) plus the
top screen's `term_bg`, `term_fg` and `ansi0`–`ansi15`; values are
`RRGGBB` or `RRGGBBAA`, case-insensitive.  Two deliberate rules:

- **`#` starts a comment only at the start of a line.**  Inline
  comments are not supported, precisely so `bottom_bg = #1a1b26` works
  — a line-start-only rule is what keeps a `#` inside a colour value
  from being eaten as a comment.
- **The alpha byte is ignored.**  DSSH forces the translucency the
  built-in themes use for the three translucent roles
  (`key_special`, `hold_bg`, `cand_sel_bg`); everything else is
  opaque.  That is what keeps the layered look intact no matter what
  is pasted in.

Unknown keys and unreadable values are rejected one at a time — the
rest of the file still applies, and the row under the **自定义** option
reports how many were rejected (the same message appears as a dim line
in the terminal at startup).  The file is read **once at startup**, so
after editing it, switch the theme away from 自定义 and back (or
restart the app).  Like the other themes, it repaints both screens,
but it deliberately does not tint the mascot sprites, the AI answer
modal, or the voice badge's status tints.  The parser is host-tested
(`make test-theme-ini`).

---

## SSH machine picker (multiple machines)

With more than one machine in `config.ini`, DSSH **asks which one first** — the
project cards, theme, interface language, mascot and recording limit are all
stored **per machine**, so until it knows which machine it is, it does not know
which settings to load.

```
launch → machine picker → connect → project page → cd <dir> && <tool> <args>
          (>1 machine)         (when cards exist and show_project_picker = 1)
```

- With exactly one machine (or none) configured the page never appears and
  start-up behaves as it always did: connect straight away.
- **SELECT** after a disconnect reopens it too, so changing machines does not
  need a relaunch.
- The bottom screen shows **five rows at a time** and scrolls, so the highlight
  is always on the display.  Each row is the machine's **name** over its
  **address** (`host`, or `host:port` when the port is not 22) — two lines, so a
  long name and a long host do not compete for one line's width.  A machine with
  no `name_N` in `config.ini` keeps the single centred `1. host` line it always
  had.
- The top screen carries the highlighted machine's `Name` / `Host` / `User` /
  `Port` / `Key` and its position (`3 / 8`), each clipped to the panel — a long
  host or key path is ellipsised rather than run off the right edge.
- Both halves of this page take the **live keyboard palette** (`softkb_palette()`),
  so the page follows the theme like every other screen.  A `theme.ini` that
  colours the keyboard therefore colours this page too; there is no separate
  machine-page theme.
- The choice is written back to `selected_machine` in `config.ini` and
  pre-highlighted next launch — but the page still **asks every time**; it
  never decides for you.
- One settings file per machine: machine 0 uses `settings.ini` (so existing
  installs keep working), machine N uses `settings.<N>.ini`.  Switching
  machines applies that file immediately — theme, language, mascot and
  recording limit all follow.

| Key | Effect |
|---|---|
| **D-pad ↑↓** | Move the selection (wraps; the list scrolls past the fifth row) |
| **A** | Connect to that machine; the project page opens once it is up |
| **B** | Cancel.  There is **no session yet** — press **SELECT** to bring this page back |
| **X** | Rename the highlighted machine (see below) |
| Tap a row | Select and connect.  The gaps between rows and the hint line at the bottom are not hits |
| Tap the ✎ strip | Rename that machine — the rightmost 30 px of any row.  The strip never connects, so a touch-only user has a route to renaming that does not fight the row body |

### Renaming a machine on the device

**X**, or a tap on a row's **✎** strip, opens a text field on the top screen
pre-filled with the current name, typed with the soft keyboard.  **A** or the
keyboard's ✓ saves; **B**, **START** or **SELECT** discards.  An empty name is a
legitimate save — it clears the name and the list goes back to showing the host.

Saving writes `name_<N>` into `config.ini` on the SD card and nothing else: the
file is rebuilt line by line, every other byte (comments, blank lines, key
order, line endings) is preserved.  Values that would not survive a round trip
— a `#`, a `"`, a backslash, leading or trailing spaces — are written quoted,
because the loader strips comments and trims whitespace before it parses.

The rest of the list is still **edited on a computer only**: it lives in
`config.ini` on the SD card (`/3ds/3dssh/config.ini`) and there is no on-device
editor for hosts, ports or key paths (project cards do have one).  See the
`host_0` / `host_1` block in `sd_template/3ds/3dssh/config.ini.example`.

To just look at it: `make preview-machine-page`.  It renders the list on **all
five keyboard palettes**, plus the eight-machine scroll window, the rename field
and the English footer, into `design/machine-page/*.png` using the same host
stub the tests use — the text is blitted from the real glyph atlas rather than
mocked.  The theme sweep is the point: a role that reads in Tokyo Night and not
in Solarized is invisible in a single render.

---

## Project cards

A **card** is a directory plus the terminal agent to launch in it.  The
point is that a 3DS shell is painful to type into: `cd ~/work/dssh &&
claude --continue` is not something you want to enter on a soft
keyboard every time you connect.

Three things raise the page, and they all raise the **same** page, so
the keys below are the same however you got there:

1. **After the SSH handshake**, when `show_project_picker` is on and you
   have at least one card.  The page is opened *before* the idle loop and
   serviced by it (unlike the pre-4.0 picker, which ran its own nested
   loop) — that is what lets it be reopened later.
2. **`X + Y + ▲`** — a brand-new window picks its *own* card.  This is
   the feature's whole point: window 1 can run `claude` in `~/a` while
   window 2 runs `opencode` in `~/b`.
3. **设置 → 项目** — the manager.  Identical UI; **A** just never runs
   anything from here.

### The card list (bottom screen)

Five 36-px cards, D-pad or stylus.  The top screen shows the highlighted
card: its directory, **the exact command it will send**, your own note,
and how often / how recently you used it.

| Key | Effect |
|---|---|
| **D-pad ↑↓** | Move the selection (wraps over the cards *and* the trailing **+ 新增项目** row) |
| **A** | Run the selected card in the focused window.  On **+ 新增项目** it opens the editor instead |
| **X** | Edit the selected card |
| **Y** | Delete — **press twice**; the first press only arms it (the 删除 button changes label), so a stray Y cannot wipe a card |
| **B** / **START** | Close the page |
| Tap a card | Select it; tapping the card that is *already* selected runs it |

### The editor (bottom screen)

Four tabs, one per section.  The top screen shows what the resulting
command will be, live.

| Tab | What it is |
|---|---|
| **目录** | The directory the card runs in.  Type it, or press **取用当前目录** to take the current window's.  Plus a free-form **备注** for the top screen |
| **工具** | Which agent CLI.  Presets for `claude` / `codex` / `opencode` / `agy` / `gemini` / `aider`, each with [its own mark](#tool-marks) in the chip's top-left corner, or type your own command name |
| **参数** | Flags for the chosen tool, as tappable presets — `--dangerously-skip-permissions`, `--continue`, `--yolo`, `--approval-mode auto_edit` and so on.  The list changes with the tool, and you can append anything by hand |
| **自定义** | Skip all three parts and write the whole line yourself — for sourcing a venv, exporting vars, or chaining commands.  No `cd` is prepended |

| Key | Effect |
|---|---|
| **D-pad ◀▶** / **L** / **R** | Switch tab |
| **D-pad ▲▼** | Move within the tab |
| **A** | Act on the row — pick the tool, tick a flag, take the current directory, or start typing |
| **B** | Save and go back to the list |
| **START** | Save and close the page |

Text fields borrow the soft keyboard.  Switching from 自定义 back to a
part-tab is non-destructive: your hand-written line stays in `cmd` and
comes back if you return — but what *runs* is the three parts, because
that is what the form is showing.

### Tool marks

Every known agent CLI carries a 16×16 pixel mark (`proj_logo_t`, data in
`source/project_presets.c`): claude is a radiating burst, codex a
hexagon, opencode a terminal window, agy an arrow lifting off a pad,
gemini a four-point sparkle, aider two commits merging into one, and the
custom slot a pencil.  The mark shows up in three places — the top-left
of each chip on the 工具 tab, the left of every card in the list, and
beside the title on the top screen (at 2×; in the editor it sits in the
top-right corner and shows what the draft currently runs).  So which CLI
a card uses is legible without reading its name.

The marks are **original, drawn in the spirit of each tool rather than
copied from anyone's logo**: 16 px cannot carry a real one, and shipping
someone else's trademark inside a homebrew app is not something to do
casually.  Nor do they follow brand colours — each mark is drawn in the
ink of the surface it sits on (`'#'` solid, `'+'` at half alpha), which
is what keeps it legible under every keyboard theme.  That matters most
on the light ones: a white mark on a white card is nothing at all.

A `raw=1` card reads as "custom" everywhere else on the page, so it gets
the pencil rather than the mark of whatever name is left in its `tool`
field.  The shape of the data is pinned by `make test-project-presets`
(every slot has one, the grids are rectangular, no two are identical);
where it lands on screen is checked pixel by pixel by
`make test-project-ui` — which self-checks first, verifying three
deliberately wrong marks so that "the checker passes" means something.

To just look at it: `make preview-project-page`.  It renders the list,
the 工具 tab and the 参数 tab, both screens, into
`design/project-page/*.png` using the same host stub — the text is
blitted from the real glyph atlas rather than mocked, so a layout change
can be reviewed without flashing the console.

### Where the "current directory" comes from

Two sources, in this order:

1. **What DSSH last told this window's shell to `cd` into.**  Ground
   truth, because DSSH is the one that typed it.
2. **A `pwd` over an auxiliary exec channel.**  Worth knowing what this
   actually answers: a second SSH channel starts its *own* process in
   the **login directory**, so `pwd` there says "where a new shell
   lands", not "where this window's shell has wandered to".  That is
   exactly right for a window you just opened, and wrong for a shell you
   have been `cd`-ing around in — which is why (1) wins.  DSSH does not
   watch what you type, so if you `cd` by hand the page will not know.

If the probe fails, the button says so and a tap retries.

### Storage

Cards live in `settings.ini`, in a `[projects]` section:

```ini
[projects]
count = 2
selected = 0
project_0 = work|/home/me/work|claude|--continue|0||my main repo|12|1757836800
project_1 = venv|/srv/app|||1|source .venv/bin/activate && aider|needs the venv|3|1757830000
```

Fields are
`name|path|tool|args|raw|cmd|note|uses|last_used`, with `\|` and `\\`
escaped, so a pipeline in a command is fine.  `raw=1` means `cmd` is
what runs and `path`/`tool`/`args` are ignored.

**Pre-4.0 rows migrate themselves.**  The old format was
`name|path|cmd|raw`; the shape is picked off the *unescaped* bar count,
so an existing file keeps working without you touching it, and a
`raw=0` row's single command string splits losslessly into tool + args
(byte-identical commands before and after).  The page writes the new
format the next time you save anything.

Round-trip and migration are host-tested (`make test-config-projects`),
as is the preset table (`make test-project-presets`) and the page's own
state machine (`make test-project-ui`).

---

## Known limitations

A few things DSSH does not do (yet), listed plainly so you don't bump
into them:

- **The server's host key is never checked**: DSSH accepts whatever key the
  far end presents — no prompt, no `known_hosts`, nothing to pin.  Anyone who
  can terminate the connection in the middle (an open Wi-Fi network, a relay
  you don't control, a Tailscale exit node you don't control) can pose as your
  server: they read and forge everything in the session, and your keystrokes
  go to them rather than to the machine you think you are typing into.  The
  macOS keychain unlock is exposed through the same door — that feature sends
  your login password when the *terminal* shows an unlock prompt, and a fake
  server can print one.  Your private key never leaves the SD card, so this
  does not hand an attacker your login; it costs you the secrecy of the
  session.  Treat it as a tool for networks you control, or connect over a
  tailnet.
- **No clipboard / copy-paste**: scrollback is view-only — you can't
  select and copy text out of the terminal or paste content into the
  remote.  Dictating via voice input is the current workaround for
  getting longer text into the shell.
- **Public-key auth only**: the mbedTLS backend parses RSA (PEM-format)
  keys only; there is no password login, no ed25519, and no fallback
  when the key is unavailable.
- **The machine list is edited on a computer**: `config.ini` takes up to eight
  machines (`host_0` … `host_7`, offered by the picker at launch), but there is
  no on-device editor for the connection details — adding a machine means
  editing the file on the SD card.  **Names are the exception**: the picker can
  rename a machine in place (`name_N`), because that is the one field you want
  to fix while looking at the list.  And it is one machine at a time: switching
  is a reconnect (or a relaunch), not a parallel session.
- **Only the settings page speaks six languages**: it offers zh / en / ja / ko /
  de / fr, while the machine picker and the project page carry a two-column
  zh/en table and fall back to English for the rest.
- **No file transfer**: no built-in SFTP/SCP; moving files between the
  3DS and the server has to go through `git` / `curl` or similar on the
  remote side.
- **Project cards don't follow a hand-typed `cd`**: "use current
  directory" knows where *DSSH* last sent this window (`cd <card path>`)
  and, failing that, the login directory a fresh shell would land in.
  It does not watch what you type, so a window you `cd`'d around in by
  hand is still remembered as wherever the card last put it.  Nothing
  runs in the wrong place — you just get a stale suggestion.
- **Agent flag presets go stale**: the `参数` tab's list is a snapshot
  of each tool's documented flags (`source/project_presets.c`).  A tool
  that renames one will not be noticed — the `自定义` tab is the
  escape hatch, and `make test-project-presets` at least pins the
  table's shape.

---

## Build from source

### Prerequisites

- Linux x86\_64 (tested on Ubuntu 22.04; other distros need the obvious
  package-name adjustments).
- [devkitPro / devkitARM](https://devkitpro.org/wiki/Getting_Started)
  release 65+, GCC 14.2.0.  `DEVKITPRO` has to be exported in the shell
  that runs `make` (that is what `/etc/profile.d/devkit-env.sh` does on a
  standard install); `DEVKITARM` is derived from it as
  `$DEVKITPRO/devkitARM` unless you export it explicitly to override.
  With neither set, `make` stops with a one-line message naming the fix.
- Python 3.10+ with Pillow (for font + dictionary generators; `make cia`
  probes for an interpreter that has PIL and prints the fix if none does).

### Steps

```bash
# 1. Install devkitPro
wget https://apt.devkitpro.org/install-devkitpro-pacman
bash install-devkitpro-pacman
sudo dkp-pacman -S 3ds-dev 3ds-mbedtls 3ds-libpng 3ds-zlib

# 2. Clone recursively so libts3ds and its private lwIP are checked out
git clone --recurse-submodules https://github.com/oli-bot/3DShell.git
cd DSSH

# Existing non-recursive clone only:
git submodule update --init --recursive

# 3. Cross-compile libssh2 (one-time, drops into $DEVKITPRO/portlibs/3ds/lib/)
bash build-libssh2.sh

# 4. Install system fonts (Terminus provides ASCII / box-drawing)
sudo apt install fonts-terminus

# 5. Fetch font sources (Zpix)
bash tools/fetch_fonts.sh

# 6. Generate the font atlas (→ source/font_data.c, ~3 MB)
python3 tools/gen_font.py

# 7. Fetch + build the pinyin dictionary (→ romfs/pinyin_dict.bin, ~13 MB)
bash tools/fetch_pinyin_dict.sh
python3 tools/gen_pinyin_dict.py
# `make ime-dict` runs exactly these two commands.

# 8. Build the .3dsx
make

# 9. (Optional) build the .cia
bash tools/install_cia_tools.sh   # installs bannertool + makerom into ~/bin
                                  # `make cia-tools` runs the same script
make cia                          # → DSSH.cia
```

The build command itself is unchanged: `make` first builds the pinned
`libts3ds/build/3ds/libts3ds.a`, then links DSSH. If the submodule is missing,
the Makefile prints the initialization command instead of failing later with a
missing directory or header.

### Test the IME engine on the host (no 3DS needed)

```bash
make test-ime
```

Compiles `tools/test_ime.c` linked against `source/ime_pinyin.c` and
runs nine smoke-test queries (`ni → 你`, `nihao → 你好`, `nh → 你好`,
etc.).

### The rest of the host tests

Each host-side test has a `make test-<name>` target (`preview-project-page`
and `preview-machine-page` render preview PNGs instead of asserting).  The
targets are declared on
the `.PHONY` line in `Makefile` — that line is the authoritative list, not
anything restated here: a list copied into prose is a list that goes stale
the next time somebody adds a test, and nothing fails to tell you.  They
all run on the host with no 3DS attached, and none of them needs the ARM
toolchain.

---

## Project layout

```
DSSH/
├── 69633.PNG                  # Source icon (162×102)
├── icon.png                   # 48×48 icon for .3dsx / SMDH (derived)
├── app.rsf                    # makerom CIA spec
├── Makefile                   # Top-level build (make / make cia / make test-*)
├── build-libssh2.sh           # libssh2 + mbedTLS ARM cross-compile
├── libts3ds/                  # Pinned native Tailscale client submodule
├── source/
│   ├── main.c                 # Poll loop, SSH receive, window multiplexing, input routing
│   ├── ssh_client.{c,h}       # libssh2 wrapper — shell channels + aux exec channel
│   ├── config.{c,h}           # config.ini / settings.ini parser (incl. [projects])
│   ├── terminal.{c,h}         # ANSI/VT100 parser (forked from skmtrd)
│   ├── renderer.{c,h}         # citro2d rendering (terminal, text, CJK)
│   ├── keyboard.{c,h}         # Physical buttons + IME routing
│   ├── keyrepeat.h            # Auto-repeat timing tables
│   ├── softkb.{c,h}           # Soft keyboard + candidate strip + settings page
│   ├── ime_pinyin.{c,h}       # Pinyin engine
│   ├── ai_modal.{c,h}         # R+START AI answer modal
│   ├── voice.{c,h}            # Mic capture + upload to the whisper shim
│   ├── audio.{c,h}            # Key-click sound playback
│   ├── key_sound_*.h          # Generated key-click samples (one per mascot species)
│   ├── mascot.{c,h}           # Bottom-bar pixel pets
│   ├── kb_theme.{c,h}         # Per-theme bottom-screen palette
│   ├── theme_ini.{c,h}        # theme.ini parser (自定义 theme)
│   ├── project_ui.{c,h}       # Project-card page: list, editor, pwd probe
│   ├── machine_ui.{c,h}       # SSH machine picker (the screen before connect)
│   ├── project_presets.{c,h}  # Agent-CLI preset table + their pixel marks
│   ├── keychain_protocol.h    # macOS keychain helper IPC
│   ├── font_atlas.{c,h}       # Codepoint → glyph index
│   ├── json_find.{c,h}        # Flat-JSON reader (voice / AI reply fields)
│   ├── utf8_fit.h             # Cut-to-codepoint-boundary helper
│   └── font_data.c            # Font bitmaps (gen_font.py output)
├── tools/
│   ├── stub/                  # Host-build stubs so citro2d-free logic can be tested
│   ├── test_*.{c,sh}          # Host-side tests — one per `make test-*` target
│   ├── preview_project_page.* # Project-page screens → design/project-page/*.png
│   ├── preview_machine_page.* # Machine-picker screens → design/machine-page/*.png
│   ├── fetch_fonts.sh         # Download Zpix
│   ├── gen_font.py            # Font atlas generator
│   ├── fetch_pinyin_dict.sh   # Download rime-ice
│   ├── gen_pinyin_dict.py     # Dictionary → binary
│   ├── bake_key_sound.py      # Key-click samples → key_sound_*.h
│   ├── gen_mascot_speech.py   # Mascot speech-bubble glyphs
│   ├── gen_mascot_dog32.py    # 32×32 刀盾狗 master design
│   ├── bake_mascot_dog20.py   # Master → the real 20×18 canvas + C code
│   ├── explore_dog_variants.py# Knife / shield shape variants for review
│   ├── render_dog_before_after.py # Old-vs-new sprite contact sheet
│   ├── preview_mascot.py      # Mascot contact sheet
│   ├── check_mascot_sync.py   # Guards preview_mascot.py's frame copy
│   ├── audit_mascot_outline.py# Outline-gap audit
│   ├── import_niulai.py       # 牛来 sprite importer
│   ├── whisper_daemon.py      # Local whisper.cpp daemon (offline track)
│   ├── whisper_shim.py        # Server-side dual-track transcription shim
│   ├── test_whisper_gate.py   # Whisper-shim gate check
│   ├── install_whisper_*.sh   # Server-side installers
│   ├── gen_cia_assets.py      # Icon / banner derivation
│   ├── modern_words.txt       # Extra words merged into the pinyin dict
│   ├── yazi_icons.txt         # yazi icon codepoints baked into the font
│   └── install_cia_tools.sh   # bannertool + makerom installer
├── design/                    # Committed previews: mascot/ + project-page/*.png
├── romfs/                     # gitignored — packs pinyin_dict.bin
├── data/                      # gitignored — font + dict sources
└── sd_template/               # SD-card deployment template
    ├── README.md
    └── 3ds/3dssh/
        ├── config.ini.example
        └── theme.ini.example
```

## Architecture

```
SSH server (somewhere on the internet)
     ▲ libssh2 over mbedTLS-RSA-4096
     │
┌────┴──────────────────────────────────────────────────┐
│  main.c poll loop @ 60 fps                            │
│   ├─ ssh_read → softkb_record_recv → utf8 reassemble  │
│   │                ↓                                  │
│   │   terminal_write_n → ANSI parser → cell grid      │
│   ├─ hidScanInput → keyboard_handle_input             │
│   │   ├─ IME mode? → ime_input_letter / page / select │
│   │   ├─ R held → voice_record_start / _stop → upload │
│   │   └─ R + START → ai_modal_open (bottom-screen)    │
│   ├─ hidTouchRead → softkb_touch                      │
│   │   ├─ candidate strip hit → ime_select             │
│   │   ├─ key hit → keyboard_emit_for / ime_input      │
│   │   └─ badge tap → settings page (debug row)        │
│   ├─ project_ui_tick — the card page owns the keypad  │
│   │   ├─ list / editor state machine                  │
│   │   └─ "use current dir" → ssh_aux_exec on a 2nd    │
│   │        channel (a fresh shell's login dir, so     │
│   │        the window's own cd target is asked first) │
│   └─ render: top = renderer_draw_terminal, or the     │
│                 settings list while settings are open │
│              bot = softkb_draw + clock + mascot, or   │
│                 the settings detail card              │
│              (both = project_ui_draw_* while it's up) │
└───────────────────────────────────────────────────────┘
     │
   citro2d (3DS 2D rendering)
     │
   GPU (top 400×240 + bottom 320×240, 24-bit color)
```

The build went through milestones M0 → M11; see the commit history for
the full progression.

---

## Credits

3DShell is built on top of incredible open-source projects:

### Upstream Project

- **[Fishason/DSSH](https://github.com/Fishason/DSSH)** — Upstream project created by [Fishason](https://github.com/Fishason). It established the core 3DS SSH architecture, ANSI terminal integration, Pinyin IME engine, and cloud voice dictation workflow.

### Core Dependencies & Inspirations

- **[skmtrd/3dssh](https://github.com/skmtrd/3dssh)** — The original Japanese 3DS SSH client (MIT License, Copyright (c) 2026 skmtrd); provided the foundation for terminal parsing and citro2d rendering.
- **[rime-ice](https://github.com/iDvel/rime-ice)** — pinyin dictionary
  source (pinned at commit `3f57a6f6`).
- **[Zpix Pixel Font](https://github.com/SolidZORO/zpix-pixel-font)** —
  12 px CJK pixel font (OFL 1.1).
- **[Terminus TTF](https://terminus-font.sourceforge.net/)** — ASCII
  and box-drawing pixel font.
- **[libssh2](https://www.libssh2.org/)** + **[mbedTLS](https://www.trustedfirmware.org/projects/mbed-tls/)** —
  SSH / TLS protocol stack.
- **[cadl/libts3ds](https://github.com/cadl/libts3ds)** — native Nintendo 3DS
  Tailscale-compatible transport, derived from
  **[CamM2325/microlink](https://github.com/CamM2325/microlink)**.
- **[devkitPro](https://devkitpro.org/) libctru / citro2d / citro3d** —
  3DS user-mode runtime and rendering.
- **[carstene1ns/3ds-bannertool](https://github.com/carstene1ns/3ds-bannertool)**
  + **[3DSGuy/Project_CTR makerom](https://github.com/3DSGuy/Project_CTR)** —
  CIA packaging tools.

## License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE) for details.

3DShell is a derivative work based on **[Fishason/DSSH](https://github.com/Fishason/DSSH)** and **[skmtrd/3dssh](https://github.com/skmtrd/3dssh)** (MIT License, Copyright (c) 2026 skmtrd).

The bundled fonts, dictionary, and upstream SSH/TLS libraries retain their respective licenses (SIL OFL 1.1 / LGPL-3.0 / BSD-3-Clause / Apache-2.0 / zlib). Please respect these licenses when redistributing binaries (`.cia` or `.3dsx`).
