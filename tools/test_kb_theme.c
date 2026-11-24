/*
 * Host-side tests for the bottom-screen theme table in source/kb_theme.c.
 * No 3DS hardware and no C2D here — this checks the table's invariants:
 *
 *   1. the default theme is byte-for-byte the colour set that shipped
 *      before theming existed (the "default looks identical" promise),
 *   2. every role keeps the alpha byte it has always had,
 *   3. text stays readable on the surface behind it, in every theme,
 *   4. an out-of-range theme index (a hand-edited settings.ini) falls back
 *      to the default instead of reading past the array.
 *
 * Run with: make test-theme
 */
#include <stdio.h>

#include "../source/kb_theme.h"
#include "test_wcag.h"

static int failures = 0;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        failures++; \
    } \
} while (0)

#define CHECKF(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: " fmt "\n", __VA_ARGS__); \
        failures++; \
    } \
} while (0)

#define ALPHA(c) ((c) & 0xffu)

/* The luminance/contrast maths lives in tools/test_wcag.h, shared with
 * the page tests.  Alpha is ignored by the plain ratio on purpose: the
 * translucent roles (key_special, hold_bg, cand_sel_bg) are never a text
 * background of their own — key_special sits on bottom_bg at a fixed
 * 0x8b in every theme.  Section 3b below is where the roles that ARE
 * derived by alpha get composited, and it uses wcag_contrast_over(). */

/* The exact hex literals softkb.c / main.c used before this palette
 * existed.  Changing one of these means changing what the default theme
 * looks like — if that is really intended, say so in the commit message. */
typedef struct { const char *name; uint32_t rgba; } expect_t;

static const expect_t dark_baseline[] = {
    { "bottom_bg      (COL_STATUS_BG/COL_CHAMFER/main clear)", 0x181825ff },
    { "panel_bg       (COL_CANDIDATE_BG)",                     0x1e1e2eff },
    { "key_border     (COL_KEY_BORDER)",                       0x1e1e2eff },
    { "key_body       (COL_KEY_BODY)",                         0x313244ff },
    { "key_top        (COL_KEY_TOP)",                          0x45475aff },
    { "key_shadow     (COL_KEY_BOT_SHADOW)",                   0x06060eff },
    { "key_special    (COL_KEY_SPECIAL)",                      0x45475a8b },
    { "key_space      (COL_KEY_SPACE_BODY)",                   0x6c7086ff },
    { "key_accent     (COL_KEY_PG_BODY)",                      0x74c7ecff },
    { "key_press      (COL_KEY_PRESSED)",                      0x89b4faff },
    { "label          (COL_KEY_LABEL/COL_IME_CANDIDATE_FG)",    0xcdd6f4ff },
    { "ink_dark       (COL_KEY_PRESSED_FG)",                   0x11111bff },
    { "hold_fg        (COL_STATUS_FG_HOLD)",                   0xa6e3a1ff },
    { "status_dim     (COL_STATUS_DIM)",                       0x45475aff },
    { "hold_bg        (COL_STATUS_HOLD_BG)",                   0x89b4fa50 },
    { "slot_bg        (COL_MODE_LBL_BG)",                      0x313244ff },
    { "hint_fg        (COL_IME_PAGE_HINT/main COLOR_DIM)",      0x6c7086ff },
    { "pinyin_fg      (COL_IME_PINYIN_FG)",                    0xa6e3a1ff },
    { "pinyin_bad_fg  (COL_IME_PINYIN_NOMATCH)",               0xf38ba8ff },
    { "cand_sel_bg    (COL_IME_SELECTED_BG)",                  0x89b4fa66 },
    { "batt_warn      (main bottom_batt_warn_color DARK)",     0xe0af68ff },
    { "batt_chg       (main bottom_batt_chg_color DARK)",      0x9ece6aff },
    { "batt_crit      (main bottom_batt_crit_color DARK)",     0xf7768eff },
};

#define FIELD_COUNT (int)(sizeof(dark_baseline) / sizeof(dark_baseline[0]))

static uint32_t field_at(const kb_pal_t *p, int i) {
    const uint32_t *base = (const uint32_t *)p;
    return base[i];
}

int main(void) {
    /* The test walks the struct as uint32_t[]; that only holds while
     * kb_pal_t is exactly FIELD_COUNT plain colour fields. */
    CHECK(sizeof(kb_pal_t) == FIELD_COUNT * sizeof(uint32_t),
          "kb_pal_t gained/lost a field — update dark_baseline[]");
    CHECK(THEME_COUNT == 5, "expected 5 themes (4 built-in + THEME_CUSTOM)");

    /* 1. Default theme regression. */
    const kb_pal_t *dark = kb_pal_for(THEME_DARK);
    for (int i = 0; i < FIELD_COUNT; i++) {
        CHECKF(field_at(dark, i) == dark_baseline[i].rgba,
               "DARK.%s changed: was %08x, now %08x",
               dark_baseline[i].name, dark_baseline[i].rgba,
               field_at(dark, i));
    }
    /* THEME_CUSTOM ships seeded with those same bytes.  Nothing has loaded a
     * theme.ini here, so the slot must be indistinguishable from the default
     * — that is what makes picking 自定义 without the file a no-op instead of
     * a black-on-black screen, and it holds for every check in loops 2 and 3
     * below, which now cover five themes rather than four. */
    const kb_pal_t *custom = kb_pal_for(THEME_CUSTOM);
    for (int i = 0; i < FIELD_COUNT; i++) {
        CHECKF(field_at(custom, i) == dark_baseline[i].rgba,
               "unloaded THEME_CUSTOM.%s differs from DARK: %08x vs %08x",
               dark_baseline[i].name, field_at(custom, i),
               dark_baseline[i].rgba);
    }

    /* 2. Alpha bytes are per-role constants, not per-theme choices. */
    static const struct { int idx; unsigned char alpha; } alpha_roles[] = {
        { 6,  0x8b },   /* key_special   */
        { 14, 0x50 },   /* hold_bg       */
        { 19, 0x66 },   /* cand_sel_bg   */
    };
    for (term_theme_t t = 0; t < THEME_COUNT; t++) {
        const kb_pal_t *p = kb_pal_for(t);
        for (int i = 0; i < FIELD_COUNT; i++) {
            unsigned char want = 0xff;
            for (unsigned a = 0; a < sizeof(alpha_roles)/sizeof(alpha_roles[0]); a++)
                if (alpha_roles[a].idx == i) want = alpha_roles[a].alpha;
            CHECKF(ALPHA(field_at(p, i)) == want, "%s field %d: alpha %02x, want %02x",
                   wcag_theme_name(t), i, ALPHA(field_at(p, i)), want);
        }
    }

    /* 3. Readability.  4.5:1 for anything you must read (keycap labels,
     *    pinyin, the held-modifier status word, the selected-candidate
     *    text); 3:1 for short tertiary text and alarm tints.
     *    status_dim is NOT in here: at ~1.9:1 it is a deliberately
     *    ghosted inactive glyph, both before and after theming. */
    for (term_theme_t t = 0; t < THEME_COUNT; t++) {
        const kb_pal_t *p = kb_pal_for(t);
        const char *n = wcag_theme_name(t);
        CHECKF(wcag_contrast(p->label, p->key_body)      >= 4.5, "%s label/key_body %.2f < 4.5",  n, wcag_contrast(p->label, p->key_body));
        CHECKF(wcag_contrast(p->label, p->key_special)   >= 4.5, "%s label/key_special %.2f < 4.5", n, wcag_contrast(p->label, p->key_special));
        CHECKF(wcag_contrast(p->label, p->panel_bg)      >= 4.5, "%s label/panel_bg %.2f < 4.5",    n, wcag_contrast(p->label, p->panel_bg));
        CHECKF(wcag_contrast(p->pinyin_fg, p->bottom_bg) >= 4.5, "%s pinyin_fg/bottom_bg %.2f < 4.5", n, wcag_contrast(p->pinyin_fg, p->bottom_bg));
        CHECKF(wcag_contrast(p->hold_fg, p->bottom_bg)   >= 4.5, "%s hold_fg/bottom_bg %.2f < 4.5", n, wcag_contrast(p->hold_fg, p->bottom_bg));
        /* Dark label on the two bright faces that use it. */
        CHECKF(wcag_contrast(p->ink_dark, p->key_press)  >= 4.5, "%s ink_dark/key_press %.2f < 4.5", n, wcag_contrast(p->ink_dark, p->key_press));
        CHECKF(wcag_contrast(p->ink_dark, p->key_accent) >= 4.5, "%s ink_dark/key_accent %.2f < 4.5", n, wcag_contrast(p->ink_dark, p->key_accent));
        /* Tertiary / transient text.  key_space is here rather than with
         * the 4.5:1 labels because the default theme shipped at 3.38:1 on
         * the space bar and THEME_DARK's values are frozen. */
        CHECKF(wcag_contrast(p->label, p->key_space)     >= 3.0, "%s label/key_space %.2f < 3",     n, wcag_contrast(p->label, p->key_space));
        CHECKF(wcag_contrast(p->hint_fg, p->bottom_bg)   >= 3.0, "%s hint_fg/bottom_bg %.2f < 3",   n, wcag_contrast(p->hint_fg, p->bottom_bg));
        CHECKF(wcag_contrast(p->pinyin_bad_fg, p->bottom_bg) >= 3.0, "%s pinyin_bad/bottom_bg %.2f < 3", n, wcag_contrast(p->pinyin_bad_fg, p->bottom_bg));
        CHECKF(wcag_contrast(p->batt_warn, p->bottom_bg) >= 3.0, "%s batt_warn/bottom_bg %.2f < 3", n, wcag_contrast(p->batt_warn, p->bottom_bg));
        CHECKF(wcag_contrast(p->batt_chg, p->bottom_bg)  >= 3.0, "%s batt_chg/bottom_bg %.2f < 3",  n, wcag_contrast(p->batt_chg,  p->bottom_bg));
        CHECKF(wcag_contrast(p->batt_crit, p->bottom_bg) >= 3.0, "%s batt_crit/bottom_bg %.2f < 3", n, wcag_contrast(p->batt_crit, p->bottom_bg));

        /* 3b. The tones the full-screen pages DERIVE by alpha rather than
         *     reading a role.  These have no entry in the table above, so
         *     before this section nothing pinned them and a page could
         *     quietly pick an alpha that only works in one theme — which
         *     is exactly what happened: hint_fg looked like "the dim ink"
         *     but is tuned against bottom_bg and drops to 2.92:1 on
         *     panel_bg in SOLAR, so the secondary lines are derived from
         *     the surface's OWN ink instead (label@0xB3, and ink_dark on
         *     the accent fill).
         *
         *     Composited, not compared raw: an alpha byte is not a colour
         *     the eye ever sees.  See tools/test_wcag.h. */
        CHECKF(wcag_contrast_over(wcag_alpha(p->label, 0xB3), p->panel_bg) >= 4.5,
               "%s label@0xB3/panel_bg %.2f < 4.5", n,
               wcag_contrast_over(wcag_alpha(p->label, 0xB3), p->panel_bg));
        CHECKF(wcag_contrast_over(wcag_alpha(p->label, 0xB3), p->key_body) >= 4.5,
               "%s label@0xB3/key_body %.2f < 4.5", n,
               wcag_contrast_over(wcag_alpha(p->label, 0xB3), p->key_body));
        CHECKF(wcag_contrast_over(wcag_alpha(p->label, 0x99), p->panel_bg) >= 4.5,
               "%s label@0x99/panel_bg %.2f < 4.5", n,
               wcag_contrast_over(wcag_alpha(p->label, 0x99), p->panel_bg));
        /* The sub-line of a SELECTED card, on the accent fill.  It is a
         * supporting line, so it takes the 3:1 tier — but it must not be
         * the light ink at any alpha: label on key_accent is 1.2:1 in
         * DARK and MONO, i.e. invisible, which is why the selected
         * surface's text comes from the ink_dark family at all. */
        CHECKF(wcag_contrast_over(wcag_alpha(p->ink_dark, 0xC0), p->key_accent) >= 3.0,
               "%s ink_dark@0xC0/key_accent %.2f < 3", n,
               wcag_contrast_over(wcag_alpha(p->ink_dark, 0xC0), p->key_accent));

        /* The 1px rule the modal frame and the picker header are drawn
         * with.  It cannot be key_border: that equals panel_bg in DARK
         * and CUSTOM, so the line would simply not be there. */
        CHECKF(wcag_contrast(p->key_top, p->panel_bg) >= 1.5,
               "%s key_top/panel_bg %.2f < 1.5 — the hairline is invisible", n,
               wcag_contrast(p->key_top, p->panel_bg));

        /* 4. Surfaces must be distinguishable and stack upward in
         *    brightness: background < panel < keycap < top highlight, and
         *    a pressed keycap must be brighter than any resting keycap. */
        double l_bg   = wcag_lum(p->bottom_bg);
        double l_pan  = wcag_lum(p->panel_bg);
        double l_body = wcag_lum(p->key_body);
        double l_top  = wcag_lum(p->key_top);
        double l_space= wcag_lum(p->key_space);
        double l_press= wcag_lum(p->key_press);
        CHECKF(l_bg <= l_pan, "%s panel_bg (%.3f) darker than bottom_bg (%.3f)", n, l_pan, l_bg);
        CHECKF(l_body > l_bg, "%s key_body not lighter than bottom_bg", n);
        CHECKF(l_top >= l_body, "%s key_top not lighter than key_body", n);
        CHECKF(l_space >= l_body, "%s key_space not lighter than key_body", n);
        CHECKF(l_press > l_space && l_press > l_top, "%s key_press not the brightest face", n);
        CHECK(p->key_border != p->bottom_bg, "key_border invisible against bottom_bg");
    }

    /* 5. bottom_bg must equal the top-screen clear colour (terminal.c
     *    theme_bg[]) wherever the theme re-skins both screens, so the two
     *    halves of the device match.  DARK is the documented exception:
     *    its keyboard shipped in a different palette from its terminal. */
    static const uint32_t term_bg[THEME_COUNT] = {
        0x1a1b26ff, 0x0d1117ff, 0x000000ff, 0x002b36ff,
        /* THEME_CUSTOM keeps DARK's keyboard colour and DARK's terminal
         * colour as two different values, exactly as DARK does. */
        0x1a1b26ff,
    };
    CHECK(term_bg[THEME_MATRIX] == kb_pal_for(THEME_MATRIX)->bottom_bg,
          "MATRIX bottom_bg != terminal theme_bg");
    CHECK(term_bg[THEME_MONO] == kb_pal_for(THEME_MONO)->bottom_bg,
          "MONO bottom_bg != terminal theme_bg");
    CHECK(term_bg[THEME_SOLAR] == kb_pal_for(THEME_SOLAR)->bottom_bg,
          "SOLAR bottom_bg != terminal theme_bg");

    /* 6. Garbage from settings.ini must not read past the table.
     *    THEME_CUSTOM is legal now, so 4 is no longer on this list. */
    CHECK(kb_pal_for((term_theme_t)THEME_COUNT) == dark, "index == COUNT not clamped");
    CHECK(kb_pal_for((term_theme_t)99) == dark, "index 99 not clamped");
    CHECK(kb_pal_for((term_theme_t)-1) == dark, "negative index not clamped");
    CHECK(kb_pal_for(THEME_DARK) == dark, "DARK not stable across calls");

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok: kb_theme — %d fields x %d themes\n", FIELD_COUNT, (int)THEME_COUNT);
    return 0;
}
