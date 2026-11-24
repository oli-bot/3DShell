#pragma once
#include <stdint.h>
#include <string.h>

#include "font_atlas.h"   /* FA_CELL_W, font_is_wide()  */
#include "utf8_next.h"    /* utf8_next(), UTF8_REPLACEMENT */
#include "renderer.h"     /* renderer_draw_text_px_z(), renderer_utf8_text_width_px() */

/*
 * Pixel-width text fitting for the page family (project_ui.c, machine_ui.c,
 * ai_modal.c).
 *
 * Why this is a header and not a function in a .c: three modules need
 * "draw this string, ellipsise it if it does not fit", and each had grown
 * its own private copy — project_ui.c's text_clip(), ai_modal.c's
 * draw_question_with_ellipsis(), and the two u8_next()/cp_is_wide() pairs
 * underneath them.  Two copies of a wide-cell rule is one copy too many:
 * the rule has to agree with font_atlas.c's, because that is what
 * renderer_draw_text_px() blits, and a copy that drifts mis-measures every
 * CJK label at once.  source/utf8_fit.h is the same arrangement for the
 * byte-boundary cut; this is the pixel one.
 *
 * Both halves are built on what already exists and is already host-tested
 * (tools/test_utf8_next.sh): utf8_next() is the one UTF-8 decoder, and
 * font_is_wide() is the one wide-cell rule.  Nothing here re-implements
 * either, so there is nothing here that can drift from them.
 *
 * static inline, not extern: the codebase builds one translation unit per
 * module with no shared utility object, and a header keeps the three
 * callers from needing a link line change.
 */

/* One codepoint from the NUL-terminated string at `p`; returns the bytes
 * it consumed (>= 1, so a caller looping on the return value always makes
 * progress) and writes the codepoint through `cp_out`.
 *
 * This is utf8_next() in the "return the advance" shape the drawing code
 * wants.  It exists so the pages can walk a string and still get the
 * decoder's RFC 3629 strictness — overlong forms, surrogate halves and
 * anything past U+10FFFF all come back as U+FFFD — rather than the laxer
 * hand-rolled decoders these modules used to carry. */
static inline int tf_u8_next(const char *p, uint32_t *cp_out) {
    const char *s = p;
    *cp_out = utf8_next(&s);
    return (int)(s - p);
}

/* Screen width of one codepoint at scale 1.  Delegates to font_is_wide()
 * so the two-glyph-per-CJK-glyph rule has exactly one definition. */
static inline int tf_cp_w(uint32_t cp) {
    return font_is_wide(cp) ? FA_CELL_W * 2 : FA_CELL_W;
}

/* Draw `s` at (x, y) clipped to `max_w` pixels, with a trailing "..."
 * when it did not fit, and return the width actually painted.
 *
 * A codepoint is never cut in half and a wide glyph is always charged its
 * full 12 px.  The version this replaced charged the width of the
 * codepoint's FIRST BYTE while backing up (buf[i] is the lead byte, so a
 * 3-byte CJK glyph was charged 6 px instead of 12): every subtraction was
 * short by the same 6 px, so the "is there room yet" test stayed false
 * after it should have gone true and the loop kept eating glyphs.  A
 * truncated CJK label therefore lost more of itself than the box required.
 *
 * When `max_w` leaves no room at all the ellipsis is still drawn — it is
 * three ASCII cells and the caller asked for a bounded box, so a visible
 * "..." that slightly overhangs is more useful than silence. */
static inline int tf_text_clip(int x, int y, float z, const char *s,
                               uint32_t c, int max_w) {
    if (!s || max_w <= 0) return 0;

    int full = renderer_utf8_text_width_px(s);
    if (full <= max_w) {
        renderer_draw_text_px_z(x, y, z, s, c);
        return full;
    }

    /* Does not fit.  Walk once more, keeping the longest prefix that still
     * leaves room for the ellipsis. */
    const int ell = 3 * FA_CELL_W;
    char buf[400];
    int  cut = 0, cutw = 0;
    const char *p = s;
    while (*p) {
        uint32_t cp;
        int adv = tf_u8_next(p, &cp);
        int cw  = tf_cp_w(cp);
        if (cutw + cw + ell > max_w) break;
        cutw += cw;
        cut  += adv;
        p    += adv;
    }
    if (cut > (int)sizeof(buf) - 4) cut = (int)sizeof(buf) - 4;
    if (cut < 0) cut = 0;
    memcpy(buf, s, (size_t)cut);
    buf[cut + 0] = '.';
    buf[cut + 1] = '.';
    buf[cut + 2] = '.';
    buf[cut + 3] = 0;
    renderer_draw_text_px_z(x, y, z, buf, c);
    return cutw + ell;
}
