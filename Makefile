#---------------------------------------------------------------------------------
# 3dssh - Nintendo 3DS SSH client with Chinese IME
#
# M0 (this Makefile): minimal hello-world to verify toolchain.
# M1+ will add libssh2, mbedtls, citro2d, romfs etc.
#---------------------------------------------------------------------------------
.SUFFIXES:

# DEVKITARM is derived from DEVKITPRO when it is not exported explicitly —
# that is the layout every devkitPro install ships with, and deriving it
# keeps the failure message self-contained.  This guard used to tell the
# user to run `tools/dkp.sh make`, a script that has never existed in this
# repo: anyone who hit the error was pointed at a dead end.  Adding the
# wrapper now would only create one more file to keep in sync, so derive
# instead — an explicit DEVKITARM still wins when one is set.
ifeq ($(strip $(DEVKITARM)),)
  ifeq ($(strip $(DEVKITPRO)),)
    $(error "devkitPro not found — export DEVKITPRO (e.g. /opt/devkitpro); DEVKITARM is then derived as $$DEVKITPRO/devkitARM, or export it explicitly")
  endif
  # export, not just := — libts3ds is built by a sub-make whose own
  # Makefile.3ds hard-requires DEVKITARM in the environment, so a
  # derived-but-unexported value dies there with a message that points
  # back at the very thing we just derived it from.
  export DEVKITARM := $(DEVKITPRO)/devkitARM
endif

TOPDIR ?= $(CURDIR)
PROJECT_ROOT := $(patsubst %/,%,$(dir $(abspath $(firstword $(MAKEFILE_LIST)))))
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
TARGET		:=	3dssh
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include
ROMFS		:=	romfs
LIBTS3DS_DIR	:=	$(PROJECT_ROOT)/libts3ds
LIBTS3DS_A	:=	$(LIBTS3DS_DIR)/build/3ds/libts3ds.a

APP_TITLE	:=	3DS SSH Client
APP_DESCRIPTION	:=	SSH terminal with Chinese IME
APP_AUTHOR	:=	exdekotive

# The one version string: compiled into the binary (the boot screen prints it)
# and written into the CIA's SMDH long description.  Derived from the nearest
# tag so it cannot drift from what actually shipped — a clean build of the
# v1.4.0 tag reads "v1.4.0", a build off main reads how far off that tag it is.
# Pin it for a release build from a tarball (no git history):
#	make DSSH_VERSION=v1.4.0 cia
# exported because the objects are compiled by the sub-make under the $(BUILD)
# rule, which re-parses this file with cwd=build.
#
# An origin test with := rather than `export DSSH_VERSION ?= $(shell ...)`:
# ?= is recursive, so *every* expansion re-ran git describe, and one `make cia`
# expands this twice (the export into the sub-make, then the SMDH long
# description) — a commit landing in between would have stamped the binary and
# the SMDH with two different strings, which is the very inconsistency this
# feature exists to prevent.  `undefined` keeps the override semantics intact:
# both a command line and an inherited environment win, exactly as ?= allowed.
ifeq ($(origin DSSH_VERSION),undefined)
export DSSH_VERSION := $(shell git -C $(PROJECT_ROOT) describe --tags --always --dirty 2>/dev/null || echo unknown)
endif

# Two spellings of the same generated header, because this Makefile is parsed in
# two directories: the sub-make runs with cwd=build (where main.o is compiled and
# the header must be named bare), the top level runs with cwd=the project root
# (where dssh.smdh is packed and it is $(BUILD)/version.h).
ifeq ($(BUILD),$(notdir $(CURDIR)))
VERSION_H	:=	version.h
else
VERSION_H	:=	$(BUILD)/version.h
endif

# The boot screen prints DSSH_VERSION (source/main.c), so the string has to be a
# build input that main.o depends on rather than a -D on the command line: with a
# -D, main.o keeps standing from the previous version and `make DSSH_VERSION=v1.4.0
# cia` produces a CIA whose SMDH says v1.4.0 while the screen still names the old
# build — the same stale-ELF trap the $(CIA_TARGET) rule documents.
#
# FORCE, because no timestamp could ever show the change: the string comes from
# `git describe`, not from a file.  The recipe thus runs on every build, and the
# cmp inside it is what stops main.o from rebuilding when the string did not
# actually move.  build/ is already on the include path (see INCLUDE) and already
# gitignored.
#
# Only the COMMANDS live here, as a variable: the rules that use them are in the
# two context branches below, because a target defined in this shared region
# would become the default goal — `make all` then built nothing but the header.
define VERSION_H_RECIPE
	@printf '#define DSSH_VERSION "%s"\n' "$(DSSH_VERSION)" > $@.tmp; \
	 if cmp -s $@.tmp $@ 2>/dev/null; then rm -f $@.tmp; else mv $@.tmp $@; fi
endef

#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

# Diagnostic transport builds set this to auto, direct, peer-relay, or derp.
# It is compiled into the .3dsx and intentionally cannot be overridden by the
# SD-card configuration file.
DSSH_TAILSCALE_PATH ?= auto
CFLAGS	+=	-DDSSH_TAILSCALE_PATH=\"$(DSSH_TAILSCALE_PATH)\"

# Keep libts3ds/microlink diagnostics out of the user's terminal by default.
# Set to 1 for a troubleshooting build that shows startup transport logs.
DSSH_TAILSCALE_VERBOSE ?= 0
CFLAGS	+=	-DDSSH_TAILSCALE_VERBOSE=$(DSSH_TAILSCALE_VERBOSE)

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# M3: citro2d/citro3d for top-screen ANSI terminal rendering.
LIBS	:=	-lts3ds -lssh2 \
			-lmbedtls -lmbedx509 -lmbedcrypto \
			-lcitro2d -lcitro3d \
			-lctru -lm

LIBDIRS	:=	$(PROJECT_ROOT)/build-deps/portlibs/3ds $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN		:=	$(addsuffix .o,$(BINFILES))

# ── generated build inputs (absent from a fresh clone) ─────────────────
#
# source/font_data.c and romfs/pinyin_dict.bin are gitignored
# (.gitignore:17-20) and, until now, nothing in this Makefile could
# produce them: a fresh clone died in the link with a wall of
# `undefined reference to font_glyphs / FONT_NGLYPHS / ...` (make), and
# `make cia` died with `No rule to make target 'romfs/pinyin_dict.bin'`.
# Both get real rules below.
#
# They are pulled in *only while missing*.  Once a tree has them, a later
# edit to a generator script must not silently drag that tree through a
# font re-render or a ~40 MB dict re-download — say `make fonts` /
# `make ime-dict` when that is actually what you want.
FONT_DATA	:=	source/font_data.c
PY_DICT		:=	romfs/pinyin_dict.bin

MISSING_GEN	:=
ifeq ($(wildcard $(FONT_DATA)),)
MISSING_GEN	+=	$(FONT_DATA)
endif
ifeq ($(wildcard $(PY_DICT)),)
MISSING_GEN	+=	$(PY_DICT)
endif

# CFILES above is a parse-time glob, so a source this very run is about to
# generate is invisible to it — name its object explicitly.  The rule below
# runs before the link, and VPATH already points at source/, so the stock
# %.o: %.c rule finds it.
ifeq ($(wildcard $(FONT_DATA)),)
OFILES_SOURCES	+=	font_data.o
endif

export OFILES			:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES			:=	$(addsuffix .h,$(BINFILES))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(LIBTS3DS_DIR)/include \
					-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	-L$(LIBTS3DS_DIR)/build/3ds \
					$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) FORCE clean all check-libts3ds romfs ime-dict fonts test-ime test-json-find test-json-build test-keyboard test-font-atlas test-utf8-next test-config test-config-projects test-project-presets test-project-ui test-machine-ui test-ai-modal preview-project-page preview-machine-page test-softkb-i18n test-terminal test-theme test-theme-ini test-keyrepeat test-whisper-gate test-mascot test-mascot-e2e test-mascot-outline cia cia-tools cia-clean

all: $(BUILD)

# Regenerate the pinyin IME binary dict (rime-ice top-300k entries).
# Output goes into romfs/pinyin_dict.bin and is bundled by --romfs.
ime-dict:
	@bash tools/fetch_pinyin_dict.sh
	@python3 tools/gen_pinyin_dict.py

# Regenerate the bitmap font atlas -> source/font_data.c.  Mirrors
# ime-dict: no prerequisites, so it always runs when asked.
fonts:
	@bash tools/fetch_fonts.sh
	@python3 tools/gen_font.py

# ── file rules for the two gitignored inputs ───────────────────────────
# These are what make a clean clone build without the user first having to
# know that `fonts` / `ime-dict` exist.
#
# No prerequisites, on purpose.  Depending on the generator scripts would
# fire this rule every time one of them is touched — a font re-render costs
# minutes and the dict re-downloads ~40 MB, so a mere script edit must not
# silently trigger either.  With no prerequisites make runs the rule only
# when the target is genuinely missing, which is exactly the clean-clone
# case these rules exist for; a deliberate regeneration is `make fonts` /
# `make ime-dict`.  (Nor can the prerequisites be data/fonts/*.ttf or the
# dict sources: those are fetched by the scripts themselves, so depending
# on them would fail the rule before it ever got the chance to fetch.)
$(FONT_DATA):
	@echo "  generating $@ (not in git — .gitignore:17)"
	@bash tools/fetch_fonts.sh || { \
		echo "" >&2; \
		echo "error: $@ is missing and could not be generated — see above." >&2; \
		echo "  regenerate with: make fonts" >&2; \
		echo "  that needs the Terminus TTF (Debian/Ubuntu: apt install" >&2; \
		echo "  fonts-terminus) and DejaVuSansMono.ttf, i.e. a Linux/WSL" >&2; \
		echo "  box; on macOS/Windows copy a source/font_data.c over from" >&2; \
		echo "  a tree that already has one (it is gitignored)." >&2; \
		exit 1; \
	}
	@python3 tools/gen_font.py

$(PY_DICT):
	@echo "  generating $@ (not in git — .gitignore:20)"
	@bash tools/fetch_pinyin_dict.sh || { \
		echo "" >&2; \
		echo "error: $@ is missing and could not be generated — see above." >&2; \
		echo "  regenerate with: make ime-dict" >&2; \
		echo "  that downloads ~40 MB from the network; offline trees can" >&2; \
		echo "  copy a romfs/pinyin_dict.bin over from one that has it." >&2; \
		exit 1; \
	}
	@python3 tools/gen_pinyin_dict.py

# romfs/ carries the IME dict at runtime and is gitignored as a whole, so
# a fresh clone has no such directory and --romfs= fails on it.  Order-only
# everywhere: the directory's mtime says nothing about the artefacts.
romfs:
	@mkdir -p romfs

# Host-side smoke test for the IME engine — much faster than 3dslink.
test-ime:
	@bash tools/test_ime.sh

# Host-side flat-JSON reader test (voice/AI reply parsing).  ASan-backed:
# the reply buffer is not NUL-terminated, and searching it out of bounds
# is silent under plain -O2.
test-json-find:
	@bash tools/test_json_find.sh

# Host-side AI-request JSON writer test (the read direction's opposite
# number).  source/json_build.c decides how transcribed speech and server
# replies are escaped into the history array that goes up the wire, which
# is pure byte handling with no C2D_/C3D_ symbol and no 3DS service — so
# the escaping, the exact-fit caps and the injection cases are now checked
# instead of trusted.  ASan-backed for the same reason as json_find: the
# one-past-the-cap write is silent under plain -O2.
test-json-build:
	@bash tools/test_json_build.sh

# Host-side physical-keyboard tests.  source/keyboard.c decides what
# actually reaches the SSH pipe — SELECT/A/B/D-pad and the Y/X modifier
# state machine — and is pure logic: no C2D_/C3D_ symbol, and <3ds.h> is
# only there for `u32` and the KEY_* bits.  tools/stub/3ds.h supplies
# both, so the binding table documented at the top of keyboard.h is now
# checked against real key sequences instead of trusted.
test-keyboard:
	@bash tools/test_keyboard.sh

# Host-side font atlas tests.  source/font_atlas.c decides how many
# character cells a codepoint occupies (font_is_wide) and which bitmap it
# resolves to (font_glyph_index / font_wide_glyph_index).  It is pure
# table lookup — no C2D_/C3D_ symbol, no 3DS service, and the one table it
# needs (source/font_data.c) already links on the host for test-project-ui
# — so the width rule the renderer measures with (renderer.c:489) and the
# one it draws with (renderer.c:396) are now checked against the Unicode
# block boundaries instead of trusted.
test-font-atlas:
	@bash tools/test_font_atlas.sh

# Host-side UTF-8 decoder tests.  source/utf8_next.c was pulled out of
# renderer.c for exactly this: it decided what every label helper draws
# and how wide the IME candidate strip thinks that is, and the version
# that lived in renderer.c accepted overlong sequences, surrogate halves
# and codepoints past U+10FFFF.  Pure byte handling — no C2D_/C3D_
# symbol — so the rejections, the per-sequence advance and the
# "renderer_utf8_text_width_px agrees with the draw path's step"
# contract are now checked.  ASan-backed: not reading past the NUL is not
# something -O2 can verify.
test-utf8-next:
	@bash tools/test_utf8_next.sh

# Host-side config parser tests (including quoted keychain passwords).
test-config:
	@bash tools/test_config.sh

# Host-side project bookmarks tests (settings.ini [projects] section).
test-config-projects:
	@bash tools/test_config_projects.sh

# Host-side agent-CLI preset table tests.  The flag list is the one part of
# the project-card feature that goes stale on its own — a tool renames a
# flag and nothing here notices — so at least the table's shape (no
# duplicates, no empty descriptions, every dangerous flag marked) is pinned,
# along with the toggler's boundary rules.
test-project-presets:
	@bash tools/test_project_presets.sh

# Host-side project-page state machine tests.  source/project_ui.c is the
# one part of the feature that cannot be checked by reading a table: it
# decides which tab a card opens on, whether switching tabs re-derives
# the "the hand-written string is what runs" flag, whether a delete needs
# its second press, and what a failed `pwd` probe does.  tools/stub/
# supplies the few 3DS symbols the module needs (it calls no C2D_* of its
# own) and captures the drawing instead of rasterising it, so the tests
# can assert on what the screens would say.
test-project-ui:
	@bash tools/test_project_ui.sh

# Host-side machine-picker tests.  This is the first screen of the whole
# program when more than one machine is configured, and its state machine
# decides which host gets logged into: an off-by-one in the row wrap, or a
# touch row mapped one slot out, produces a picker that looks right and
# connects to the wrong machine.  The same tools/stub/ as above covers it
# (machine_ui.c names no C2D_* symbol either), and the per-theme section
# pins the two roles that were wrong in the project page as well — the
# ink on the accent-filled row, and the secondary tone.
test-machine-ui:
	@bash tools/test_machine_ui.sh

# Host-side AI-ask modal tests.  source/ai_modal.c was written as a
# standalone citro2d module — it called C2D_DrawRectSolid directly and
# hand-rolled the 0xRRGGBBAA shuffle — which is why it had no host test
# and could not take the keyboard palette.  Routing its rects through
# renderer_draw_rect_px (as softkb.c and project_ui.c already did) makes
# it compile against tools/stub/ like the rest, so the fade ramp, the
# markdown transform and the wrap/ellipsis budget are now pinned here.
test-ai-modal:
	@bash tools/test_ai_modal.sh

# Renders the project page's screens to PNGs under design/project-page/.
# Same host stub as the test above, with text painting switched on, so a
# layout change (a moved mark, a re-flowed card) can be looked at without
# flashing the console.
preview-project-page:
	@bash tools/preview_project_page.sh

# Renders the machine picker's screens to PNGs under design/machine-page/:
# the list on all five keyboard palettes, the eight-machine scroll window,
# the rename field and the English footer.  Same stub as the test above with
# text painting on.  The theme sweep is the point — a role that reads in
# Tokyo Night and not in Solarized is invisible in a single render.
preview-machine-page:
	@bash tools/preview_machine_page.sh

# Host-side softkb i18n table invariants (LANG_* count, indices, opts[] ceiling).
test-softkb-i18n:
	@bash tools/test_softkb_i18n.sh

# Host-side terminal protocol tests (scrolling + fish cursor queries).
test-terminal:
	@bash tools/test_terminal.sh

# Host-side bottom-screen palette tests (default-theme regression,
# alpha bytes, WCAG contrast floors, theme index clamping).
test-theme:
	@bash tools/test_kb_theme.sh

# Host-side sdmc:/3ds/3dssh/theme.ini tests.  This is the only palette built
# from a file the user wrote, so the cases are all ways such a file goes
# wrong: an unquoted '#rrggbb' value, a typo'd role name, a 7-digit colour,
# and the alpha bytes that stay the program's rather than the file's.
test-theme-ini:
	@bash tools/test_theme_ini.sh

# Host-side key auto-repeat tests.  Guards that the default style is still
# the exact cadence every build shipped with, and that 慢/标准/快 stay
# ordered — settings.ini stores the style index, so renumbering is silent.
test-keyrepeat:
	@bash tools/test_keyrepeat.sh

# Host-side non-speech gate tests for the server-side whisper daemon.  The
# gate is what stops whisper typing subtitle boilerplate into the shell
# after a silent hold; its thresholds are measured, not guessed.
#
# Both directions are asserted with no external files: synthetic noise must
# be rejected and synthetic speech-like audio must be accepted.  The accept
# side used to need a WAV argument this target never passed, so a threshold
# change that muted the user outright still reported "0 failure(s)" — hence
# the fixtures are generated in code.  Pass WAV paths via WAVS= for extra
# real-recording cases.
test-whisper-gate:
	@bash tools/test_whisper_gate.sh $(WAVS)

# Host-side guard that tools/preview_mascot.py still matches source/mascot.c.
# The preview keeps its own copy of every walk cycle so it can render without
# a C toolchain; that copy silently went stale and shipped a docs image with
# a whole species missing, so it is now checked instead of trusted.
test-mascot:
	@python3 tools/check_mascot_sync.py
	@bash tools/test_mascot_e2e.sh

test-mascot-e2e:
	@bash tools/test_mascot_e2e.sh

# Host-side guard that 刀盾狗 keeps its 1-px dark outline.  The first bake
# shipped with bare steel on the knife grip and bare fur between the ears —
# both invisible at 1× until you magnify the sprite, so a human review
# missed them.  Only the sprites that declare an outline are checked.
test-mascot-outline:
	@python3 tools/audit_mascot_outline.py

# ── M9: CIA packaging ───────────────────────────────────────────────
#
# `make cia` produces DSSH.cia from the already-built ELF + romfs.
# Pipeline: gen_cia_assets.py → bannertool makesmdh / makebanner →
# makerom -f cia.  bannertool + makerom live in $HOME/bin (install via
# `make cia-tools` once); we put $HOME/bin first on PATH so the rule
# works in a fresh shell with no system-wide install.
CIA_BIN          := $(HOME)/bin
CIA_PATH_PREPEND := PATH=$(CIA_BIN):$$PATH
CIA_ASSETS       := $(CURDIR)/cia_assets
CIA_TARGET       := $(CURDIR)/DSSH.cia

cia-tools:
	@bash tools/install_cia_tools.sh

# Generate icon (project root, used by the .3dsx too) + banner +
# silent.wav (CIA-only) from the source 162x102 logo.
#
# tools/gen_cia_assets.py rasterises PNGs, so it is the one tool here that
# needs PIL — and the python3 first on $PATH frequently does not have it
# (a venv or a version-manager shim won't).  Probe for an interpreter that
# does and stop with a message naming the fix, rather than dying inside a
# traceback that never mentions PIL.  The other tools/*.py are stdlib-only
# and run under whatever python3 is, so only this rule needs the probe.
PYTHON_PIL := $(shell for p in python3 /usr/local/bin/python3 /usr/bin/python3; do \
		command -v "$$p" >/dev/null 2>&1 || continue; \
		"$$p" -c 'import PIL' >/dev/null 2>&1 || continue; \
		echo "$$p"; break; done)

# silent.wav is written by write_silent_wav() inside gen_cia_assets.py.
# It used to be generated by an inline python one-liner here, whose b'\\x00'
# survived make -> shell -> python as four literal characters — so the
# "silence" was a full-scale DC offset (L 30812, R 12336) for two seconds.
# Generating it in the .py has no backslash layers to lose.
icon.png $(CIA_ASSETS)/banner.png $(CIA_ASSETS)/silent.wav: \
		tools/gen_cia_assets.py 69633.PNG
	@test -n "$(PYTHON_PIL)" || { \
		echo "tools/gen_cia_assets.py needs PIL and no python3 with PIL was found." >&2; \
		echo "  /usr/bin/python3 -m pip install --user pillow" >&2; \
		exit 1; }
	@$(PYTHON_PIL) tools/gen_cia_assets.py

# Top-level context (cwd = project root): dssh.smdh needs the header for its long
# description.  VERSION_H_RECIPE and the reason for FORCE are up by VERSION_H.
$(VERSION_H): FORCE
	$(VERSION_H_RECIPE)

FORCE:

# Run bannertool to package the SMDH (icon + metadata) and banner
# (selected-card image + audio).
$(BUILD)/dssh.smdh: icon.png $(VERSION_H)
	@$(CIA_PATH_PREPEND) bannertool makesmdh \
		-s "DSSH" \
		-l "DSSH $(DSSH_VERSION) — SSH client with Chinese IME" \
		-p "exdekotive" \
		-i icon.png \
		-o $@ >/dev/null

$(BUILD)/dssh.bnr: $(CIA_ASSETS)/banner.png $(CIA_ASSETS)/silent.wav
	@$(CIA_PATH_PREPEND) bannertool makebanner \
		-i $(CIA_ASSETS)/banner.png \
		-a $(CIA_ASSETS)/silent.wav \
		-o $@ >/dev/null

# The CIA depends on the ELF, the SMDH, the banner, the RSF, and the
# romfs blob (pinyin dict).  makerom rebuilds everything every time;
# romfs is read fresh so any dict change picks up automatically.
$(CIA_TARGET): $(OUTPUT).elf $(BUILD)/dssh.smdh $(BUILD)/dssh.bnr app.rsf \
		romfs/pinyin_dict.bin
	@$(CIA_PATH_PREPEND) makerom \
		-f cia -target t \
		-elf $(OUTPUT).elf -rsf app.rsf \
		-icon $(BUILD)/dssh.smdh -banner $(BUILD)/dssh.bnr \
		-o $@
	@echo "built ... $(notdir $@)"

# Depend on `all` too: 3ds_rules' `%.elf:` pattern rule carries no
# prerequisites, so depending on $(OUTPUT).elf alone only compares
# timestamps — a changed source would be missed and the CIA would be
# re-packed from a stale ELF.  `all` forces the real rebuild first.
cia: all $(CIA_TARGET)

cia-clean:
	@rm -rf $(CIA_ASSETS) icon.png $(BUILD)/dssh.smdh $(BUILD)/dssh.bnr $(CIA_TARGET)

# MISSING_GEN is empty on a tree that already has the generated files, so
# this costs nothing there; | romfs because --romfs= needs the directory to
# exist even when the dict is not rebuilt.
#
# check-libts3ds is deliberately FIRST: prerequisites are built left to
# right, so a fresh clone hears about the missing submodule *before* it
# spends ~47 MB downloading the font and the dict, not after.
$(BUILD): check-libts3ds $(MISSING_GEN) | romfs

# Fail fast, before anything expensive or network-bound runs.  The same
# test is repeated inside the $(BUILD) recipe below as a backstop — this
# one exists only to get the ordering right.
check-libts3ds:
	@test -f "$(LIBTS3DS_DIR)/Makefile.3ds" || { \
		echo "error: libts3ds submodule is missing" >&2; \
		echo "run: git submodule update --init --recursive" >&2; \
		exit 1; \
	}

$(BUILD):
	@test -f "$(LIBTS3DS_DIR)/Makefile.3ds" || { \
		echo "error: libts3ds submodule is missing" >&2; \
		echo "run: git submodule update --init --recursive" >&2; \
		exit 1; \
	}
	@$(MAKE) --no-print-directory -C $(LIBTS3DS_DIR) -f Makefile.3ds all
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Delete what this Makefile builds — and *only* that.  It deliberately is
# not `rm -fr $(BUILD)`, because $(BUILD) is gitignored and also holds
# hand-made things that no rule can regenerate:
#
#   build/mascot_speech/niulai_src.wav  the raw recording the 牛来 mascot's
#       voice was made from.  tools/import_niulai.py reads it by absolute
#       path; there is no other copy anywhere.
#   build/.verify/                      the simulator E2E harness: CIA
#       installer, .ctm movie builders, SSH stand-in, screenshot analysers.
#
# A blanket rm -fr takes both.  So the rule names the outputs it owns and
# leaves everything else alone — a stale object file is a nuisance that the
# next build fixes, a deleted recording is not recoverable.
clean:
	@echo clean ...
	@rm -f $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf
	@if [ -d $(BUILD) ]; then \
		rm -f $(BUILD)/*.o $(BUILD)/*.d $(BUILD)/*.lst $(BUILD)/*.map \
		      $(BUILD)/theme_ini_*.ini $(BUILD)/kb_ime_fixture.bin \
		      $(BUILD)/dssh.bnr $(BUILD)/dssh.smdh $(BUILD)/version.h \
		      $(BUILD)/test_* $(BUILD)/preview_* ; \
	fi

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)
$(OFILES_SOURCES) : $(HFILES)
$(OUTPUT).elf	:	$(OFILES)

# This is the context that compiles, so this is where the version header has to
# hang off main.o.  VERSION_H is the bare `version.h` here because cwd is build/.
# Both rules sit this far down on purpose — a target defined before $(OUTPUT).3dsx
# would take over the default goal, and `make all` would build only the header.
main.o: $(VERSION_H)

# The .PHONY declaration near the top of this file lives in the *other* context
# branch, and this one decides what the binary says.  Without it a stray file
# named FORCE in build/ older than version.h keeps the header "up to date"
# forever: `make DSSH_VERSION=v1.4.0 all` then reports "Nothing to be done" and
# ships the previous string — reproduced 2026-10-14.  `clean` names its own
# outputs and matches no such file, so that stray file would survive every
# rebuild.
.PHONY: FORCE

$(VERSION_H): FORCE
	$(VERSION_H_RECIPE)

FORCE:

$(OUTPUT).elf: $(LIBTS3DS_A)

$(LIBTS3DS_A):
	@test -f "$(LIBTS3DS_DIR)/Makefile.3ds" || { \
		echo "error: libts3ds submodule is missing" >&2; \
		echo "run: git submodule update --init --recursive" >&2; \
		exit 1; \
	}
	@$(MAKE) --no-print-directory -C $(LIBTS3DS_DIR) -f Makefile.3ds all

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
