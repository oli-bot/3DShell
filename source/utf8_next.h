#pragma once
#include <stdint.h>
#include "font_atlas.h"

/*
 * UTF-8 decoding and on-screen text measurement, split out of renderer.c
 * so both compile on the host: no citro2d, no 3DS service, no C2D_/C3D_
 * symbol anywhere in this translation unit.  That absence is the only
 * thing keeping tools/test_utf8_next.sh buildable, so keep it that way.
 */

/* What an ill-formed sequence decodes to.  U+FFFD is also what the atlas
 * resolves "no such glyph" to, so a rejected byte still paints a visible
 * replacement cell rather than a blank gap. */
#define UTF8_REPLACEMENT 0xFFFDu

/* Pull one codepoint from the NUL-terminated string at *s and advance *s
 * past it.
 *
 * Rejects what the Unicode standard and RFC 3629 exclude — overlong forms
 * (0xC0/0xC1 leads, 0xE0 0x80.., 0xF0 0x80..), UTF-16 surrogate halves
 * (0xED 0xA0..) and anything above U+10FFFF (0xF4 0x90.., 0xF5..0xFF
 * leads).  An ill-formed sequence yields U+FFFD; the advance is the
 * maximal subpart (1 byte when the very next byte already disqualifies
 * the sequence, otherwise the bytes consumed before it fell apart), so a
 * caller looping on *s always makes progress and never spins.
 *
 * Never reads past the terminator: a truncated tail stops at the NUL. */
uint32_t utf8_next(const char **s);

/* Pixel width renderer_draw_text_px()/renderer_draw_text_px_scaled()
 * will consume for `text` at scale 1 — FA_CELL_W per narrow codepoint,
 * 2*FA_CELL_W per wide one.  Scaled drawing multiplies the whole width by
 * `scale`, so the IME candidate strip can fit-test and centre/right-align
 * with this and land exactly where the blit lands. */
int renderer_utf8_text_width_px(const char *text);
