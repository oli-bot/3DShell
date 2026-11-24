#pragma once
#include <stdint.h>
#include "terminal.h" /* term_theme_t, THEME_DARK, THEME_COUNT */

/*
 * Per-theme colour palette for the WHOLE bottom screen — soft keyboard,
 * status row, IME candidate strip, settings panel, debug page, bottom
 * info bar and the C2D clear colour behind it all.
 *
 * The top screen (terminal.c) has its own bg/fg/16-colour ANSI palette.
 * MATRIX, MONO and SOLAR take their .bottom_bg straight from theme_bg[]
 * so both screens clear to the identical colour; THEME_DARK keeps the
 * historical 0x181825 keyboard because that theme shipped as a Catppuccin
 * keyboard under a Tokyo Night terminal.  Likewise .label tracks
 * theme_fg[] where the terminal colour would be too dim on a keycap.
 *
 * Colours are RGBA 0xRRGGBBAA (same layout as the old COL_* constants),
 * NOT the C2D r/g/b/a argument order.
 *
 * This TU must stay free of citro3d/C2D includes so host-side tests
 * (make test-theme) can compile and assert against the table.
 */
typedef struct {
    uint32_t bottom_bg;     /* clear colour + corner chamfer punch-out +
                             * status-row background.  Must be handed to
                             * C2D_TargetClear on the SAME frame the
                             * chamfer is drawn, or the rounded corners
                             * leave stray pixels behind. */
    uint32_t panel_bg;      /* raised strips: IME candidate bar, settings
                             * /debug page panels — one step above bottom_bg */
    uint32_t key_border;    /* keycap outline */
    uint32_t key_body;      /* normal keycap face */
    uint32_t key_top;       /* 1px highlight on the keycap top edge */
    uint32_t key_shadow;    /* 1px shadow under the keycap */
    uint32_t key_special;   /* special-key face, alpha 0x8b */
    uint32_t key_space;     /* space bar — largest neutral surface */
    uint32_t key_accent;    /* page-switch key */
    uint32_t key_press;     /* tap highlight */
    uint32_t label;         /* keycap / candidate text */
    uint32_t ink_dark;      /* label drawn ON key_press — darkest value */
    uint32_t hold_fg;       /* status text while a modifier is held */
    uint32_t status_dim;    /* inactive status-row glyphs */
    uint32_t hold_bg;       /* tint behind a held modifier, alpha 0x50 */
    uint32_t slot_bg;       /* small label chips in the status row */
    uint32_t hint_fg;       /* tertiary text: page hint, clock */
    uint32_t pinyin_fg;     /* composed pinyin */
    uint32_t pinyin_bad_fg; /* pinyin with no candidates */
    uint32_t cand_sel_bg;   /* selected candidate, alpha 0x66 */
    uint32_t batt_warn;     /* battery digits below 20% */
    uint32_t batt_chg;      /* battery digits while the 3DS is charging
                             * — green in every built-in theme, so the
                             * "power in" state reads as healthy, not
                             * alarmed, and stays visually distinct from
                             * the amber warn the digits share when the
                             * battery is just low. */
    uint32_t batt_crit;     /* battery digits below 10% */
} kb_pal_t;

/* Returns the palette for `theme`; anything out of range — including a
 * garbage theme_mode read from settings.ini — falls back to THEME_DARK.
 * Never returns NULL. */
const kb_pal_t *kb_pal_for(term_theme_t theme);

/* Overwrite the THEME_CUSTOM slot.  The slot is seeded with THEME_DARK at
 * build time, so a build that never calls this — or a theme.ini that turns
 * out to be unreadable — still renders the default colours rather than
 * zeroes.  This is the only writer, and it owns the alpha bytes: `pal`'s
 * colours are taken hue-and-all, but every role is forced back to the alpha
 * the other four themes use (0xff, except key_special 0x8b / hold_bg 0x50 /
 * cand_sel_bg 0x66).  Without that, "a theme swap never changes how much of
 * the layer below shows through" would hold for the built-in themes only,
 * and a stray low alpha on .bottom_bg would punch holes in the rounded
 * corners it is cleared with.  Ignoring whatever alpha the file supplied
 * also means a caller cannot smuggle one through. */
void kb_theme_set_custom(const kb_pal_t *pal);
