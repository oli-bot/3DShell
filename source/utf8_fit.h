#pragma once
/*
 * One UTF-8 boundary rule, shared.
 *
 * Every cap in this program counts *bytes* — DSSH_PROJECT_PATH_MAX,
 * AI_Q_MAX / AI_A_MAX, PUI_PATH_MAX, theme_ini's bad_key[16] — but the
 * text being capped is usually Chinese, and one glyph is three bytes, so
 * a byte cut lands inside a glyph about two times in three.  What is
 * left is invalid UTF-8: the renderer turns it into a run of U+FFFD,
 * and for anything later typed at a shell (a `cd` target) or folded into
 * a JSON request (the AI history) a malformed string is worse than a
 * short one.
 *
 * So: shorten n to the last complete codepoint boundary at or before it.
 *
 * This existed as four copies — config.c, project_ui.c, theme_ini.c, and
 * an int-flavoured twin in voice.c — which is four chances to fix a bug
 * in three of them.  It is small enough to be static inline, so sharing
 * it costs nothing at run time and needs no build-system change: include
 * this header and delete the local copy.
 *
 * It scans *backwards* from the cut, so it only ever reads s[0 .. n-1].
 * Do not rewrite it as a forward walk: a u8_next()-style decoder reads up
 * to three bytes past a truncated 4-byte lead byte, and the terminator
 * has not been written there yet — that is an out-of-bounds read.
 */
#include <stddef.h>

static inline size_t utf8_fit(const char *s, size_t n) {
    while (n > 0) {
        size_t i = n - 1;
        while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
        unsigned char b = (unsigned char)s[i];
        size_t seq;
        if      (b < 0x80)           seq = 1;
        else if ((b & 0xE0) == 0xC0) seq = 2;
        else if ((b & 0xF0) == 0xE0) seq = 3;
        else if ((b & 0xF8) == 0xF0) seq = 4;
        else                         seq = 0;   /* stray continuation */
        if (seq > 0 && i + seq <= n) return n;
        n = i;                                  /* drop the partial, retry */
    }
    return 0;
}
