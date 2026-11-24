/*
 * WCAG 2.x contrast maths for the host tests — one copy.
 *
 * The reason this is a header rather than a few lines pasted per test is
 * the byte order, which is the one thing here that is easy to get wrong
 * and impossible to notice.  Every colour in this project is 0xRRGGBBAA:
 * renderer.c's rgba_to_c2d() reads R from bits 24-31 and the alpha from
 * bits 0-7.  A composite that walks bits 16/8/0 therefore mixes G, B and
 * the alpha into the R, G and B slots and leaves red pinned at zero,
 * which understates every ratio on a dark surface.
 *
 * That is not hypothetical: a copy of this walk in tools/test_ai_modal.c
 * reported a bullet at label@0x99 as 4.35:1 (below the 4.5 floor, so the
 * test failed) when it actually measures 4.94:1.  One header, no copies.
 *
 * test_kb_theme.c ignores alpha on purpose — the translucent roles
 * (key_special, hold_bg, cand_sel_bg) are never a text background of
 * their own — so these are opt-in helpers, not a replacement for its
 * maths.  The derived-alpha floors for the new pages do use them.
 */
#ifndef TEST_WCAG_H
#define TEST_WCAG_H

#include <stdint.h>
#include <math.h>

#include "kb_theme.h"

/* Replace a colour's alpha byte, keeping its hue. */
static inline uint32_t wcag_alpha(uint32_t rgba, unsigned a) {
    return (rgba & 0xFFFFFF00u) | (a & 0xffu);
}

/* Composite `fg` (alpha in its low byte) onto an opaque `bg`, rounding to
 * nearest.  The result is 0x00RRGGBB — the alpha has been spent. */
static inline uint32_t wcag_over(uint32_t fg, uint32_t bg) {
    unsigned a = fg & 0xffu;
    uint32_t out = 0;
    for (int sh = 24; sh >= 8; sh -= 8) {
        unsigned f = (fg >> sh) & 0xffu, b = (bg >> sh) & 0xffu;
        out |= (((f * a + b * (255u - a) + 127u) / 255u) & 0xffu) << sh;
    }
    return out;
}

static inline double wcag_channel_lum(unsigned v8) {
    double c = (double)v8 / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static inline double wcag_lum(uint32_t rgba) {
    return 0.2126 * wcag_channel_lum((rgba >> 24) & 0xff)
         + 0.7152 * wcag_channel_lum((rgba >> 16) & 0xff)
         + 0.0722 * wcag_channel_lum((rgba >>  8) & 0xff);
}

static inline double wcag_contrast(uint32_t a, uint32_t b) {
    double la = wcag_lum(a), lb = wcag_lum(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

/* What the eye actually gets from a translucent ink on a surface — the
 * only form that is meaningful for the 0x99/0xB3/0xC0 tones the pages
 * derive, since those are never painted opaque. */
static inline double wcag_contrast_over(uint32_t fg, uint32_t bg) {
    return wcag_contrast(wcag_over(fg, bg), bg);
}

static inline const char *wcag_theme_name(term_theme_t t) {
    switch (t) {
        case THEME_MATRIX: return "MATRIX";
        case THEME_MONO:   return "MONO";
        case THEME_SOLAR:  return "SOLAR";
        case THEME_CUSTOM: return "CUSTOM";
        default:           return "DARK";
    }
}

#endif /* TEST_WCAG_H */
