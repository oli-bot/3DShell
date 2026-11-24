#!/usr/bin/env bash
# Download bitmap fonts needed by tools/gen_font.py.
# Idempotent: skips files that already exist with the right size.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FONT_DIR="$PROJECT_ROOT/data/fonts"
mkdir -p "$FONT_DIR"

# Zpix — 12px Chinese pixel font (OFL-licensed) by SolidZORO.
# Covers Simplified + Traditional Chinese, Hiragana, Katakana, ASCII.
ZPIX_VERSION="v3.1.11"
ZPIX_URL="https://github.com/SolidZORO/zpix-pixel-font/releases/download/${ZPIX_VERSION}/zpix.ttf"
ZPIX_PATH="$FONT_DIR/zpix.ttf"
ZPIX_EXPECTED_BYTES=7179288

if [ -f "$ZPIX_PATH" ]; then
    # wc, not stat: GNU stat wants -c%s, BSD/macOS stat wants -f%z, and there
    # is no spelling that works on both.  On macOS `stat -c%s` errors and
    # leaves `actual` empty, which reads as a size mismatch and re-downloads
    # the 7 MB font on every run.  tr -d ' ' because the comparisons below are
    # string compares (`=`/`!=`) and wc's leading-space behaviour varies.
    actual=$(wc -c < "$ZPIX_PATH" | tr -d ' ')
    if [ "$actual" = "$ZPIX_EXPECTED_BYTES" ]; then
        echo "zpix.ttf already present ($actual bytes)"
    else
        echo "zpix.ttf size mismatch (got $actual, want $ZPIX_EXPECTED_BYTES) — redownloading"
        rm -f "$ZPIX_PATH"
    fi
fi

if [ ! -f "$ZPIX_PATH" ]; then
    echo "Downloading $ZPIX_URL -> $ZPIX_PATH"
    if command -v gh >/dev/null 2>&1; then
        # GitHub release downloads via gh CLI bypass Cloudflare anti-bot on AWS IPs.
        (cd "$FONT_DIR" && gh release download "$ZPIX_VERSION" --repo SolidZORO/zpix-pixel-font --pattern 'zpix.ttf')
    else
        curl -fLo "$ZPIX_PATH" "$ZPIX_URL"
    fi
fi

# Terminus TTF — narrow ASCII/box-drawing pixel font.
# Distributed by Debian/Ubuntu in the fonts-terminus apt package.
if [ ! -d /usr/share/fonts/truetype/terminus ]; then
    echo "Terminus TTF not found at /usr/share/fonts/truetype/terminus/" >&2
    echo "" >&2
    echo "The narrow glyphs (ASCII, box-drawing, arrows) are rendered from" >&2
    echo "Terminus, and it is only packaged by Debian/Ubuntu:" >&2
    echo "" >&2
    echo "    sudo apt install fonts-terminus" >&2
    echo "" >&2
    echo "so font generation runs on Linux/WSL, not on macOS or bare Windows." >&2
    echo "Either install it inside your Linux build environment, or copy" >&2
    echo "source/font_data.c over from a tree that already has one (it is" >&2
    echo "gitignored, so a fresh clone never has it)." >&2
    exit 1
fi

# Symbols Only Nerd Font — devicons / codicons / Material Design /
# Octicons / Font Awesome glyphs in PUA + supplementary plane.  Yazi,
# starship, lualine, etc. all use these.  ~2.5 MB (compressed in the
# upstream tar.xz; the extracted ttf is ~2.5 MB itself).
NF_VERSION="v3.4.0"
NF_URL="https://github.com/ryanoasis/nerd-fonts/releases/download/${NF_VERSION}/NerdFontsSymbolsOnly.tar.xz"
NF_TTF="$FONT_DIR/SymbolsNerdFontMono-Regular.ttf"
NF_EXPECTED_BYTES=2507556

if [ -f "$NF_TTF" ]; then
    # Same wc-not-stat reasoning as the zpix block above — see the comment
    # there.  Duplicated rather than factored into a helper so each block
    # still reads top-to-bottom.
    actual=$(wc -c < "$NF_TTF" | tr -d ' ')
    if [ "$actual" = "$NF_EXPECTED_BYTES" ]; then
        echo "SymbolsNerdFontMono-Regular.ttf already present ($actual bytes)"
    else
        echo "Nerd Font size mismatch (got $actual, want $NF_EXPECTED_BYTES) — redownloading"
        rm -f "$NF_TTF"
    fi
fi
if [ ! -f "$NF_TTF" ]; then
    echo "Downloading $NF_URL"
    tmp="$(mktemp -d)"
    curl -fLsS -o "$tmp/nf.tar.xz" "$NF_URL"
    tar -xf "$tmp/nf.tar.xz" -C "$tmp" SymbolsNerdFontMono-Regular.ttf
    mv "$tmp/SymbolsNerdFontMono-Regular.ttf" "$NF_TTF"
    rm -rf "$tmp"
fi

echo "Fonts ready:"
ls -la "$FONT_DIR" /usr/share/fonts/truetype/terminus/ 2>/dev/null
