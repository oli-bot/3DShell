#include "utf8_next.h"

/* Host-testable half of the renderer's text path — see utf8_next.h.
 *
 * These two functions used to be static inside renderer.c, which put them
 * out of reach of a host test: every label helper that measures or walks
 * UTF-8 (the IME candidate strip, the window chip, the key-popup bubble)
 * depends on them, and a decoder bug there only shows up as mis-centred
 * CJK on a screen nobody can attach a debugger to. */

uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    unsigned char b0 = *p;
    uint32_t cp;
    int extra;
    /* Allowed range of the *first* continuation byte.  Constraining it is
     * what rejects the ill-formed forms cheaply: each of the four cases
     * below is the one lead byte whose naive range would admit an
     * overlong form, a surrogate half, or a codepoint past U+10FFFF. */
    unsigned char lo, hi;

    if (b0 < 0x80) { *s = (const char *)(p + 1); return b0; }
    if (b0 < 0xC2) {
        /* 0x80..0xBF is a continuation byte with no lead; 0xC0/0xC1 can
         * only encode an overlong 2-byte form. */
        *s = (const char *)(p + 1);
        return UTF8_REPLACEMENT;
    }
    if (b0 < 0xE0) {                    /* 0xC2..0xDF */
        cp = b0 & 0x1Fu; extra = 1; lo = 0x80; hi = 0xBF;
    } else if (b0 < 0xF0) {             /* 0xE0..0xEF */
        cp = b0 & 0x0Fu; extra = 2;
        lo = (b0 == 0xE0) ? 0xA0 : 0x80;   /* 0xE0 0x80..0x9F: overlong  */
        hi = (b0 == 0xED) ? 0x9F : 0xBF;   /* 0xED 0xA0..0xBF: surrogate */
    } else if (b0 < 0xF5) {             /* 0xF0..0xF4 */
        cp = b0 & 0x07u; extra = 3;
        lo = (b0 == 0xF0) ? 0x90 : 0x80;   /* 0xF0 0x80..0x8F: overlong  */
        hi = (b0 == 0xF4) ? 0x8F : 0xBF;   /* 0xF4 0x90..: past U+10FFFF */
    } else {
        /* 0xF5..0xF7 encode past U+10FFFF; 0xF8..0xFF were never
         * assigned (and are invalid in every UTF-8 profile). */
        *s = (const char *)(p + 1);
        return UTF8_REPLACEMENT;
    }

    p++;
    /* Length check, not just a range check: a truncated tail hits the NUL
     * here and stops, so we never read the byte after the terminator. */
    if (*p == '\0' || *p < lo || *p > hi) {
        *s = (const char *)p;
        return UTF8_REPLACEMENT;
    }
    cp = (cp << 6) | (*p & 0x3Fu);
    p++;
    for (int i = 1; i < extra; i++) {
        if (*p == '\0' || (*p & 0xC0) != 0x80) {
            *s = (const char *)p;      /* stop at the NUL; never past it */
            return UTF8_REPLACEMENT;
        }
        cp = (cp << 6) | (*p & 0x3Fu);
        p++;
    }
    *s = (const char *)p;
    return cp;
}

int renderer_utf8_text_width_px(const char *text) {
    if (!text) return 0;
    int w = 0;
    const char *s = text;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        w += font_is_wide(cp) ? FA_CELL_W * 2 : FA_CELL_W;
    }
    return w;
}
