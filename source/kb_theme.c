#include "kb_theme.h"

/*
 * Roles carrying a non-ff alpha byte — .key_special (0x8b), .hold_bg
 * (0x50), .cand_sel_bg (0x66) — keep the SAME alpha in every theme: only
 * the hue changes, so a theme swap never alters how much of the layer
 * below shows through.  THEME_CUSTOM is user data and obeys the same rule
 * because kb_theme_set_custom() rewrites the alpha bytes it is handed.
 *
 * THEME_MATRIX and THEME_MONO have no red.  Urgency is expressed by
 * brightness there — .pinyin_bad_fg and .batt_crit are the brightest
 * value the theme owns — because a hard red dropped into a green or grey
 * field reads as somebody else's UI.
 *
 * .key_shadow equals .bottom_bg for those two themes, so the 1px shadow
 * under a keycap goes invisible and their keycaps sit flatter than DARK's.
 * A light shadow line instead looked worse.
 */

/* THEME_DARK's 23 roles: a Tokyo Night terminal under the Catppuccin-ish
 * keycaps the keyboard has always used — byte-for-byte the pre-theme
 * values.  Named rather than inlined so THEME_CUSTOM can be seeded from the
 * same literal instead of a copy that could drift: until theme.ini
 * overwrites it, slot 4 *is* the default theme. */
#define KB_PAL_DARK                                                       \
    {                                                                     \
        .bottom_bg     = 0x181825ff, .panel_bg     = 0x1e1e2eff,          \
        .key_border    = 0x1e1e2eff, .key_body     = 0x313244ff,          \
        .key_top       = 0x45475aff, .key_shadow   = 0x06060eff,          \
        .key_special   = 0x45475a8b, .key_space    = 0x6c7086ff,          \
        .key_accent    = 0x74c7ecff, .key_press    = 0x89b4faff,          \
        .label         = 0xcdd6f4ff, .ink_dark     = 0x11111bff,          \
        .hold_fg       = 0xa6e3a1ff, .status_dim   = 0x45475aff,          \
        .hold_bg       = 0x89b4fa50, .slot_bg      = 0x313244ff,          \
        .hint_fg       = 0x6c7086ff, .pinyin_fg    = 0xa6e3a1ff,          \
        .pinyin_bad_fg = 0xf38ba8ff, .cand_sel_bg  = 0x89b4fa66,          \
        .batt_warn     = 0xe0af68ff, .batt_chg     = 0x9ece6aff,          \
        .batt_crit     = 0xf7768eff,                                          \
    }

/* Mutable: THEME_CUSTOM is the one slot theme_ini.c writes at startup. */
static kb_pal_t kb_pals[THEME_COUNT] = {
    [THEME_DARK] = KB_PAL_DARK,
    [THEME_MATRIX] = {
        /* Green phosphor: three value ramps (dim → mid → hot) so the
         * keycap stack still reads as stacked plastic.  The four "hot"
         * roles were dialed back from neon (S≈70%) to forest (S≈40%)
         * so the page-switch accent / press highlight / IME hint /
         * battery warn don't shout — the keycap stack still anchors
         * the green identity, the bright bits just sit quieter. */
        .bottom_bg     = 0x0d1117ff, .panel_bg     = 0x1e293bff,
        .key_border    = 0x0e1a13ff, .key_body     = 0x1b3a29ff,
        .key_top       = 0x2c5c3fff, .key_shadow   = 0x000000ff,
        .key_special   = 0x2c5c3f8b, .key_space    = 0x2f6b45ff,
        .key_accent    = 0x3a9d6bff, .key_press    = 0x5fbb85ff,
        .label         = 0xa7f3d0ff, .ink_dark     = 0x04140aff,
        .hold_fg       = 0xdcfce7ff, .status_dim   = 0x2f7a4fff,
        .hold_bg       = 0x3a9d6b50, .slot_bg      = 0x1b3a29ff,
        .hint_fg       = 0x5fa886ff, .pinyin_fg    = 0x3a9d6bff,
        .pinyin_bad_fg = 0xf0fdf4ff, .cand_sel_bg  = 0x3a9d6b66,
        .batt_warn     = 0x7ec79dff, .batt_chg     = 0x6ee7b7ff,
        .batt_crit     = 0xf0fdf4ff,
    },
    [THEME_MONO] = {
        /* Six grey steps feed 23 roles, so unrelated roles legitimately
         * share a value — none of them are ever adjacent on screen. */
        .bottom_bg     = 0x000000ff, .panel_bg     = 0x0f172aff,
        .key_border    = 0x1e293bff, .key_body     = 0x334155ff,
        .key_top       = 0x475569ff, .key_shadow   = 0x000000ff,
        .key_special   = 0x4755698b, .key_space    = 0x64748bff,
        .key_accent    = 0xcbd5e1ff, .key_press    = 0xf8fafcff,
        .label         = 0xe2e8f0ff, .ink_dark     = 0x0f172aff,
        .hold_fg       = 0xffffffff, .status_dim   = 0x475569ff,
        .hold_bg       = 0xf8fafc50, .slot_bg      = 0x334155ff,
        .hint_fg       = 0x64748bff, .pinyin_fg    = 0xcbd5e1ff,
        .pinyin_bad_fg = 0xffffffff, .cand_sel_bg  = 0xf8fafc66,
        .batt_warn     = 0xcbd5e1ff, .batt_chg     = 0xe2e8f0ff,
        .batt_crit     = 0xffffffff,
    },
    [THEME_SOLAR] = {
        /* Solarized Dark.  Alarm roles use the palette's own base-red
         * dc322f — 3.24:1 on base03, the same headroom as orange cb4b16
         * and enough for a 12-px status glyph, and it keeps the battery
         * critical colour identical to what already shipped. */
        .bottom_bg     = 0x002b36ff, .panel_bg     = 0x073642ff,
        .key_border    = 0x02333fff, .key_body     = 0x0f4d5cff,
        .key_top       = 0x2b6b77ff, .key_shadow   = 0x001a20ff,
        .key_special   = 0x2b6b778b, .key_space    = 0x586e75ff,
        .key_accent    = 0x268bd2ff, .key_press    = 0x2aa198ff,
        .label         = 0xeee8d5ff, .ink_dark     = 0x001a20ff,
        .hold_fg       = 0x859900ff, .status_dim   = 0x586e75ff,
        .hold_bg       = 0x2aa19850, .slot_bg      = 0x0f4d5cff,
        .hint_fg       = 0x657b83ff, .pinyin_fg    = 0x859900ff,
        .pinyin_bad_fg = 0xdc322fff, .cand_sel_bg  = 0x2aa19866,
        .batt_warn     = 0xb58900ff, .batt_chg     = 0x859900ff,
        .batt_crit     = 0xdc322fff,
    },
    [THEME_CUSTOM] = KB_PAL_DARK,
};

const kb_pal_t *kb_pal_for(term_theme_t theme) {
    int i = (int)theme;
    if (i < 0 || i >= (int)THEME_COUNT) i = (int)THEME_DARK;
    return &kb_pals[i];
}

/* kb_pal_t is walked as a uint32_t[] below.  That is only legal while it is
 * nothing but plain colour fields, and the compiler is the one checking it. */
_Static_assert(sizeof(kb_pal_t) == 23 * sizeof(uint32_t),
               "kb_pal_t must stay 23 plain uint32_t colour fields");

void kb_theme_set_custom(const kb_pal_t *pal) {
    if (!pal) return;
    kb_pal_t *dst = &kb_pals[THEME_CUSTOM];
    *dst = *pal;
    uint32_t *w = (uint32_t *)dst;
    for (int i = 0; i < (int)(sizeof(*dst) / sizeof(uint32_t)); i++)
        w[i] = (w[i] & 0xffffff00u) | 0xffu;
    /* The three roles that let the layer below show through — and the only
     * ones that ever do, in this theme as in the four built-ins. */
    dst->key_special = (dst->key_special & 0xffffff00u) | 0x8bu;
    dst->hold_bg     = (dst->hold_bg     & 0xffffff00u) | 0x50u;
    dst->cand_sel_bg = (dst->cand_sel_bg & 0xffffff00u) | 0x66u;
}
