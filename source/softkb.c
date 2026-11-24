#include "softkb.h"
#include "renderer.h"
#include "keyboard.h"
#include "audio.h"
#include "config.h"
#include "kb_theme.h"
#include "keyrepeat.h"
#include "theme_ini.h"   /* the 自定义 settings row reports what it found */
#include <citro2d.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ──────────────────────────────────────────────────────────────────────
 * UI language IDs — keep in sync with settings_items[] opts row in
 * SID_UI_LANG (the user picks the index from that list).  Moved up
 * here so softkb_apply_settings(), which sits early in this file,
 * can clamp the persisted value back into range.
 *
 * The two static_asserts below pin the language count to two
 * hand-maintained values that would silently corrupt a settings.ini
 * if they ever diverged:
 *   - settings_item_t::opts[] is a fixed-size array; if a row needs
 *     LANG_COUNT entries the array must be at least that wide.
 *   - The host-side smoke test in tools/test_softkb_i18n.c mirrors
 *     these macros to assert the same invariants on a system that
 *     cannot link citro2d.  If you change LANG_COUNT, update both.
 * ────────────────────────────────────────────────────────────────────── */
#define LANG_ZH 0
#define LANG_EN 1
#define LANG_JA 2
#define LANG_KO 3
#define LANG_DE 4
#define LANG_FR 5
#define LANG_COUNT 6

/* Number of opt buttons a settings row can carry at the widest.
 * The single ceiling is the SID_UI_LANG row (one entry per language);
 * 6 fits it with one cell to spare for any row that may one day
 * match it.  settings_item_t::opts[] is sized to this constant. */
#define SOFTKB_OPTS_MAX 8

/* Compile-time check: if anyone adds a 7th language they must also
 * widen settings_item_t::opts[].  Bumping LANG_COUNT alone would
 * overflow the table at row initialisation time and corrupt the
 * settings page, so refuse the build. */
_Static_assert(SOFTKB_OPTS_MAX >= LANG_COUNT,
               "settings_item_t::opts[] must hold LANG_COUNT entries");
_Static_assert(LANG_ZH == 0,
               "LANG_ZH must be 0 (defaults + set_lbl_list fallback target)");

/* ──────────────────────────────────────────────────────────────────────
 * Geometry — bottom screen 320×240, top-left = (0,0)
 * ──────────────────────────────────────────────────────────────────────
 *  y =  0 .. 33   status / candidate row (34 px — bumped from 30 in
 *                  M4 polish so IME candidates have more vertical room)
 *  y = 34 .. 35   margin
 *  y = 36 .. 78   key row 0 (43 px — 1 px shaved from M4-first to free
 *                  space for the bottom row at y=214..239)
 *  y = 81 .. 123  key row 1
 *  y = 126 .. 168 key row 2
 *  y = 171 .. 213 key row 3
 *  y = 214..239   bottom row owned by main.c — clock on the left,
 *                  Anthropic crab mascot on the right.  softkb does NOT
 *                  draw here.
 *
 *  Stagger (mimics PC keyboards: each row is offset by ~half a key):
 *    row 0: x offset =  0   (10 keys × 32 px = 320 — exact fit)
 *    row 1: x offset = 16   (9 keys → ends at 304, 16 px right margin)
 *    row 2: x offset = 32   (9 keys → ends at 320 — exact fit)
 *    row 3: x offset =  0   (control row; large keys, no stagger)
 *
 *  z layers (citro2d uses depth for overdraw ordering with alpha):
 *    0.05  bottom screen background
 *    0.10  key shadow / outline
 *    0.15  key body
 *    0.18  key top highlight
 *    0.20  key label glyph
 *    0.30  status row (drawn earlier so labels overlay)
 */

#define KEY_W       32
#define KEY_H       43
#define KEY_GAP_Y   2
#define ROW_BASE_Y  36
#define ROW_Y(r)    (ROW_BASE_Y + (r) * (KEY_H + KEY_GAP_Y))

#define ROW_X_0     0
#define ROW_X_1     16
#define ROW_X_2     32
#define ROW_X_3     0

#define STATUS_Y    0
#define STATUS_H    34
#define CELL_W      6
#define CELL_H      12

/* First row of the bottom bar main.c owns (clock + mascot).  Everything
 * softkb draws — key rows, and the settings card — stops at this line.
 * The value itself lives in softkb.h (SOFTKB_BOTTOM_ROW_Y) because main.c
 * needs the same boundary for its mascot touch strip; this file never
 * spelled it, so there was nothing here to keep in sync. */

/* ── Colors ───────────────────────────────────────────────────────────
 * Every bottom-screen colour is a ROLE in the active theme's palette, so
 * the whole keyboard, status row, IME strip, settings and debug pages
 * re-skin together with the top-screen terminal.  The values live in
 * source/kb_theme.h, whose header documents what each role paints.
 *
 * WARNING: these macros expand to a function call.  They are ordinary
 * expressions, NOT constant expressions — usable only inside function
 * bodies, never in a static initializer, a `case` label or an array
 * dimension.
 *
 * g_pal is (re)selected by softkb_use_theme_() at the top of both public
 * draw entries; a theme_mode write lands on the next frame's draw, so
 * the write sites need no sync of their own.
 */
static const kb_pal_t *g_pal;

static const kb_pal_t *kb_pal(void) {
    if (!g_pal) g_pal = kb_pal_for(THEME_DARK);
    return g_pal;
}

#define COL_STATUS_BG       (kb_pal()->bottom_bg)  /* status row background */
#define COL_CHAMFER         (kb_pal()->bottom_bg)  /* corner punch-out —
                                                    * same role as
                                                    * COL_STATUS_BG and
                                                    * main.c's clear colour,
                                                    * or the rounded corners
                                                    * leave stray pixels */
#define COL_CANDIDATE_BG    (kb_pal()->panel_bg)   /* raised strips */

#define COL_KEY_BODY        (kb_pal()->key_body)   /* normal key */
#define COL_KEY_TOP         (kb_pal()->key_top)    /* 1px top-edge highlight */
#define COL_KEY_BOT_SHADOW  (kb_pal()->key_shadow) /* 1px shadow under key */
#define COL_KEY_BORDER      (kb_pal()->key_border) /* outline */
#define COL_KEY_LABEL       (kb_pal()->label)

#define COL_KEY_SPECIAL     (kb_pal()->key_special)  /* alpha 0x8b */
#define COL_KEY_PG_BODY     (kb_pal()->key_accent)   /* page-switch key */
#define COL_KEY_SPACE_BODY  (kb_pal()->key_space)    /* space — widest surface */

#define COL_KEY_PRESSED     (kb_pal()->key_press)    /* tap highlight */
#define COL_KEY_PRESSED_FG  (kb_pal()->ink_dark)     /* dark label on bright press */

#define COL_STATUS_FG_HOLD  (kb_pal()->hold_fg)      /* modifier held */
#define COL_STATUS_DIM      (kb_pal()->status_dim)   /* inactive status glyphs */
#define COL_STATUS_HOLD_BG  (kb_pal()->hold_bg)      /* tint behind held mod */

#define COL_MODE_LBL_BG     (kb_pal()->slot_bg)      /* status-row chips */

/* ── key kinds ─────────────────────────────────────────────────────── */

typedef enum {
    KIND_CHAR,
    KIND_SEQ,
    KIND_PAGE_TOGGLE,  /* legacy two-page flip (unused by new layouts) */
    KIND_SPACE,        /* visual variant: large neutral */
    KIND_PAGE_BTN,     /* page switch — jumps to softkey_t.page */
    KIND_SHIFT,        /* ⇧ tap-toggle: sticky one-shot shift */
    KIND_BACKSPACE,    /* ⌫ IME buffer chew / 0x7f, auto-repeats */
    KIND_ENTER,        /* ↑ commit candidate + \r, or plain \r */
    KIND_IME_TOGGLE,   /* 拼/EN — toggle IME mode (CN ↔ EN). Replaces the
                        * old BSP soft key (physical B already handles
                        * backspace; physical R is now voice hold-to-talk
                        * and L is the START+L dual-screen modifier, so
                        * neither is available as a physical IME toggle). */
} key_kind_t;

typedef struct {
    int x, y, w, h;
    char base;
    const char *seq;
    const char *label;
    key_kind_t kind;
    int page;          /* KIND_PAGE_BTN destination page */
} softkey_t;

#define K(col, row, ch, lbl) \
    { ROW_X_##row + (col) * KEY_W + 1, ROW_Y(row), KEY_W - 2, KEY_H, \
      (ch), NULL, (lbl), KIND_CHAR, 0 }

/* Absolute-x key with explicit width — workhorse for the iOS-style
 * pages (wide #+= / ⌫ / space / ↑ bookends, CJK seq keys). */
#define KEYAT(ax, row, w_, ch, seq_, lbl, kind_, pg_) \
    { (ax) + 1, ROW_Y(row), (w_) - 2, KEY_H, (ch), (seq_), (lbl), (kind_), (pg_) }

#define KW(col, row, span, ch, lbl, kind_) \
    { ROW_X_##row + (col) * KEY_W + 1, ROW_Y(row), (span) * KEY_W - 2, KEY_H, \
      (ch), NULL, (lbl), (kind_), 0 }

#define KS(col, row, span, seq_, lbl, kind_) \
    { ROW_X_##row + (col) * KEY_W + 1, ROW_Y(row), (span) * KEY_W - 2, KEY_H, \
      0, (seq_), (lbl), (kind_), 0 }

#define KP(col, row, span, lbl) \
    { ROW_X_##row + (col) * KEY_W + 1, ROW_Y(row), (span) * KEY_W - 2, KEY_H, \
      0, NULL, (lbl), KIND_PAGE_BTN, 0 }

/* ── Page 1: Letters ───────────────────────────────────────────────── */
static const softkey_t keys_letters[] = {
    /* row 0 (qwerty), 10 keys, no stagger */
    K(0,0,'q',"q"), K(1,0,'w',"w"), K(2,0,'e',"e"), K(3,0,'r',"r"), K(4,0,'t',"t"),
    K(5,0,'y',"y"), K(6,0,'u',"u"), K(7,0,'i',"i"), K(8,0,'o',"o"), K(9,0,'p',"p"),
    /* row 1 (asdf), 9 keys, half-key stagger */
    K(0,1,'a',"a"), K(1,1,'s',"s"), K(2,1,'d',"d"), K(3,1,'f',"f"), K(4,1,'g',"g"),
    K(5,1,'h',"h"), K(6,1,'j',"j"), K(7,1,'k',"k"), K(8,1,'l',"l"),
    /* row 2: [⇧] z x c v b n m [⌫] — iOS-style edge keys, letters keep
     * the full-key stagger (ROW_X_2). */
    KEYAT(0,  2, 46, 0, NULL, "\xe2\x87\xa7",  KIND_SHIFT,     0),
    KEYAT(48, 2, 30, 'z', NULL, "z", KIND_CHAR, 0), KEYAT(80,  2, 30, 'x', NULL, "x", KIND_CHAR, 0),
    KEYAT(112,2, 30, 'c', NULL, "c", KIND_CHAR, 0), KEYAT(144, 2, 30, 'v', NULL, "v", KIND_CHAR, 0),
    KEYAT(176,2, 30, 'b', NULL, "b", KIND_CHAR, 0), KEYAT(208, 2, 30, 'n', NULL, "n", KIND_CHAR, 0),
    KEYAT(240,2, 30, 'm', NULL, "m", KIND_CHAR, 0),
    KEYAT(274,2, 46, 0, NULL, "\xe6\x8b\xbc/EN", KIND_IME_TOGGLE, 0),  /* 拼/EN */
    /* row 3: [123] [tab] [space (5 cols)] [↑ send] */
    KEYAT(0,  3, 64,  0, NULL,  "123",   KIND_PAGE_BTN, PAGE_SYMBOLS),
    KEYAT(64, 3, 32,  0, "\t",  "tab",   KIND_SEQ,      0),
    KEYAT(96, 3, 160, ' ', NULL, "space", KIND_SPACE,    0),
    KEYAT(256,3, 64,  0, NULL,  "\xe2\x86\x91", KIND_ENTER, 0),  /* ↑ */
};
#define N_LETTERS (sizeof(keys_letters) / sizeof(keys_letters[0]))

/* ── Pages 2/3: Symbols — CN and EN variants × two levels ────────────
 *
 * 符号1 = digits + common punctuation;  符号2 (via #+=) = brackets /
 * programming set.  CN pages carry full-width CJK punctuation (。、《》
 * etc., KIND_SEQ — verbatim UTF-8) plus the ASCII []<> needed for shell
 * tests/redirections; EN pages are all-ASCII.  Symbol pages are
 * SHIFT-INDEPENDENT (iOS-style 键面即所得): every face below is exactly
 * what a tap emits, ⇧/Caps have no effect here, and
 * all 32 ASCII punctuation marks have dedicated EN keys.
 * Row geometry is identical across the four arrays: rows 0-2 use
 * absolute x = 32*col; row-3 bookends [拼音/ABC] / [⌫] are w=64. */

#define SYM_ROW3_COMMON(pagelbl, page_) \
    KEYAT(0,  3, 64,  0, NULL, pagelbl, KIND_PAGE_BTN, (page_)), \
    KEYAT(64, 3, 32,  0, "\t", "tab",   KIND_SEQ,      0), \
    KEYAT(96, 3, 160, ' ', NULL, "space", KIND_SPACE,  0), \
    KEYAT(256,3, 64,  0, NULL, "\xe2\x86\x91", KIND_ENTER, 0)   /* ↑ */

#define SYM_DIGITS_ROW \
    KEYAT(1,  0, 32, '1', NULL, "1", KIND_CHAR, 0), KEYAT(33, 0, 32, '2', NULL, "2", KIND_CHAR, 0), \
    KEYAT(65, 0, 32, '3', NULL, "3", KIND_CHAR, 0), KEYAT(97, 0, 32, '4', NULL, "4", KIND_CHAR, 0), \
    KEYAT(129,0, 32, '5', NULL, "5", KIND_CHAR, 0), KEYAT(161,0, 32, '6', NULL, "6", KIND_CHAR, 0), \
    KEYAT(193,0, 32, '7', NULL, "7", KIND_CHAR, 0), KEYAT(225,0, 32, '8', NULL, "8", KIND_CHAR, 0), \
    KEYAT(257,0, 32, '9', NULL, "9", KIND_CHAR, 0), KEYAT(289,0, 32, '0', NULL, "0", KIND_CHAR, 0)

/* ── 符号1 CN ──────────────────────────────────── */
static const softkey_t keys_symbols_cn[] = {
    SYM_DIGITS_ROW,
    /* row 1: - / : ; ( ) ¥ @ “ ” */
    KEYAT(1,  1, 32, '-', NULL, "-",  KIND_CHAR, 0),
    KEYAT(33, 1, 32, '/', NULL, "/",  KIND_CHAR, 0),
    KEYAT(65, 1, 32, ':', NULL, ":",  KIND_CHAR, 0),
    KEYAT(97, 1, 32, ';', NULL, ";",  KIND_CHAR, 0),
    KEYAT(129,1, 32, '(', NULL, "(",  KIND_CHAR, 0),
    KEYAT(161,1, 32, ')', NULL, ")",  KIND_CHAR, 0),
    KEYAT(193,1, 32, 0, "\xc2\xa5",     "\xc2\xa5",     KIND_SEQ, 0),
    KEYAT(225,1, 32, '@', NULL, "@",    KIND_CHAR, 0),
    KEYAT(257,1, 32, 0, "\xe2\x80\x9c", "\xe2\x80\x9c", KIND_SEQ, 0),
    KEYAT(289,1, 32, 0, "\xe2\x80\x9d", "\xe2\x80\x9d", KIND_SEQ, 0),
    /* row 2: #+= | 。 ， 、 ？ ！ . | ⌫ */
    KEYAT(0,  2, 64, 0, NULL, "#+=", KIND_PAGE_BTN, PAGE_SYMBOLS2),
    KEYAT(64, 2, 32, 0, "\xe3\x80\x82", "\xe3\x80\x82", KIND_SEQ, 0),
    KEYAT(96, 2, 32, 0, "\xef\xbc\x8c", "\xef\xbc\x8c", KIND_SEQ, 0),
    KEYAT(128,2, 32, 0, "\xe3\x80\x81", "\xe3\x80\x81", KIND_SEQ, 0),
    KEYAT(160,2, 32, 0, "\xef\xbc\x9f", "\xef\xbc\x9f", KIND_SEQ, 0),
    KEYAT(192,2, 32, 0, "\xef\xbc\x81", "\xef\xbc\x81", KIND_SEQ, 0),
    KEYAT(224,2, 32, '.', NULL, ".", KIND_CHAR, 0),
    KEYAT(256,2, 64, 0, NULL, "\xe6\x8b\xbc/EN", KIND_IME_TOGGLE, 0),  /* 拼/EN */
    SYM_ROW3_COMMON("\xe6\x8b\xbc\xe9\x9f\xb3", PAGE_LETTERS),  /* 拼音 */
};
#define N_SYMBOLS_CN (sizeof(keys_symbols_cn) / sizeof(keys_symbols_cn[0]))

/* ── 符号1 EN ──────────────────────────────────── */
static const softkey_t keys_symbols_en[] = {
    SYM_DIGITS_ROW,
    /* row 1: - / : ; ( ) $ & @ " */
    KEYAT(1,  1, 32, '-', NULL, "-",  KIND_CHAR, 0),
    KEYAT(33, 1, 32, '/', NULL, "/",  KIND_CHAR, 0),
    KEYAT(65, 1, 32, ':', NULL, ":",  KIND_CHAR, 0),
    KEYAT(97, 1, 32, ';', NULL, ";",  KIND_CHAR, 0),
    KEYAT(129,1, 32, '(', NULL, "(",  KIND_CHAR, 0),
    KEYAT(161,1, 32, ')', NULL, ")",  KIND_CHAR, 0),
    KEYAT(193,1, 32, '$', NULL, "$",  KIND_CHAR, 0),
    KEYAT(225,1, 32, '&', NULL, "&",  KIND_CHAR, 0),
    KEYAT(257,1, 32, '@', NULL, "@",  KIND_CHAR, 0),
    KEYAT(289,1, 32, '"', NULL, "\"", KIND_CHAR, 0),
    /* row 2: #+= | . , ? ! ' ` | ⌫ */
    KEYAT(0,  2, 64, 0, NULL, "#+=", KIND_PAGE_BTN, PAGE_SYMBOLS2),
    KEYAT(64, 2, 32, '.', NULL, ".",  KIND_CHAR, 0),
    KEYAT(96, 2, 32, ',', NULL, ",",  KIND_CHAR, 0),
    KEYAT(128,2, 32, '?', NULL, "?",  KIND_CHAR, 0),
    KEYAT(160,2, 32, '!', NULL, "!",  KIND_CHAR, 0),
    KEYAT(192,2, 32, '\'',NULL, "'",  KIND_CHAR, 0),
    KEYAT(224,2, 32, '`', NULL, "`",  KIND_CHAR, 0),
    KEYAT(256,2, 64, 0, NULL, "\xe6\x8b\xbc/EN", KIND_IME_TOGGLE, 0),  /* 拼/EN */
    SYM_ROW3_COMMON("ABC", PAGE_LETTERS),
};
#define N_SYMBOLS_EN (sizeof(keys_symbols_en) / sizeof(keys_symbols_en[0]))

/* ── 符号2 CN（#+= 进入，[123] 返回符号1）──────────── */
static const softkey_t keys_symbols2_cn[] = {
    /* row 0: 【 】 { } # % ^ * + = */
    KEYAT(1,  0, 32, 0, "\xe3\x80\x90", "\xe3\x80\x90", KIND_SEQ,  0),
    KEYAT(33, 0, 32, 0, "\xe3\x80\x91", "\xe3\x80\x91", KIND_SEQ,  0),
    KEYAT(65, 0, 32, '{', NULL, "{",  KIND_CHAR, 0),
    KEYAT(97, 0, 32, '}', NULL, "}",  KIND_CHAR, 0),
    KEYAT(129,0, 32, '#', NULL, "#",  KIND_CHAR, 0),
    KEYAT(161,0, 32, '%', NULL, "%",  KIND_CHAR, 0),
    KEYAT(193,0, 32, '^', NULL, "^",  KIND_CHAR, 0),
    KEYAT(225,0, 32, '*', NULL, "*",  KIND_CHAR, 0),
    KEYAT(257,0, 32, '+', NULL, "+",  KIND_CHAR, 0),
    KEYAT(289,0, 32, '=', NULL, "=",  KIND_CHAR, 0),
    /* row 1: _ — \ | ~ 《 》 $ & · */
    KEYAT(1,  1, 32, '_', NULL, "_",  KIND_CHAR, 0),
    KEYAT(33, 1, 32, 0, "\xe2\x80\x94", "\xe2\x80\x94", KIND_SEQ,  0),
    KEYAT(65, 1, 32, '\\',NULL, "\\", KIND_CHAR, 0),
    KEYAT(97, 1, 32, '|', NULL, "|",  KIND_CHAR, 0),
    KEYAT(129,1, 32, '~', NULL, "~",  KIND_CHAR, 0),
    KEYAT(161,1, 32, 0, "\xe3\x80\x8a", "\xe3\x80\x8a", KIND_SEQ,  0),
    KEYAT(193,1, 32, 0, "\xe3\x80\x8b", "\xe3\x80\x8b", KIND_SEQ,  0),
    KEYAT(225,1, 32, '$', NULL, "$",  KIND_CHAR, 0),
    KEYAT(257,1, 32, '&', NULL, "&",  KIND_CHAR, 0),
    KEYAT(289,1, 32, 0, "\xc2\xb7",     "\xc2\xb7",     KIND_SEQ,  0),
    /* row 2: [123] | … ： [ ] < > | ⌫  — ASCII []<> live here so CN mode
     * can still type shell tests / redirections without flipping R. */
    KEYAT(0,  2, 64, 0, NULL, "123", KIND_PAGE_BTN, PAGE_SYMBOLS),
    KEYAT(64, 2, 32, 0, "\xe2\x80\xa6", "\xe2\x80\xa6", KIND_SEQ,  0),
    KEYAT(96, 2, 32, 0, "\xef\xbc\x9a", "\xef\xbc\x9a", KIND_SEQ,  0),
    KEYAT(128,2, 32, '[', NULL, "[",  KIND_CHAR, 0),
    KEYAT(160,2, 32, ']', NULL, "]",  KIND_CHAR, 0),
    KEYAT(192,2, 32, '<', NULL, "<",  KIND_CHAR, 0),
    KEYAT(224,2, 32, '>', NULL, ">",  KIND_CHAR, 0),
    KEYAT(256,2, 64, 0, NULL, "\xe6\x8b\xbc/EN", KIND_IME_TOGGLE, 0),  /* 拼/EN */
    SYM_ROW3_COMMON("\xe6\x8b\xbc\xe9\x9f\xb3", PAGE_LETTERS),  /* 拼音 */
};
#define N_SYMBOLS2_CN (sizeof(keys_symbols2_cn) / sizeof(keys_symbols2_cn[0]))

/* ── 符号2 EN ──────────────────────────────────── */
static const softkey_t keys_symbols2_en[] = {
    /* row 0: [ ] { } # % ^ * + = */
    KEYAT(1,  0, 32, '[', NULL, "[",  KIND_CHAR, 0),
    KEYAT(33, 0, 32, ']', NULL, "]",  KIND_CHAR, 0),
    KEYAT(65, 0, 32, '{', NULL, "{",  KIND_CHAR, 0),
    KEYAT(97, 0, 32, '}', NULL, "}",  KIND_CHAR, 0),
    KEYAT(129,0, 32, '#', NULL, "#",  KIND_CHAR, 0),
    KEYAT(161,0, 32, '%', NULL, "%",  KIND_CHAR, 0),
    KEYAT(193,0, 32, '^', NULL, "^",  KIND_CHAR, 0),
    KEYAT(225,0, 32, '*', NULL, "*",  KIND_CHAR, 0),
    KEYAT(257,0, 32, '+', NULL, "+",  KIND_CHAR, 0),
    KEYAT(289,0, 32, '=', NULL, "=",  KIND_CHAR, 0),
    /* row 1: _ \ | ~ < > € £ ¥ •  — <> are the shell redirection pair,
     * previously unreachable from the soft keyboard at all. */
    KEYAT(1,  1, 32, '_', NULL, "_",  KIND_CHAR, 0),
    KEYAT(33, 1, 32, '\\',NULL, "\\", KIND_CHAR, 0),
    KEYAT(65, 1, 32, '|', NULL, "|",  KIND_CHAR, 0),
    KEYAT(97, 1, 32, '~', NULL, "~",  KIND_CHAR, 0),
    KEYAT(129,1, 32, '<', NULL, "<",  KIND_CHAR, 0),
    KEYAT(161,1, 32, '>', NULL, ">",  KIND_CHAR, 0),
    KEYAT(193,1, 32, 0, "\xe2\x82\xac", "\xe2\x82\xac", KIND_SEQ,  0),
    KEYAT(225,1, 32, 0, "\xc2\xa3",     "\xc2\xa3",     KIND_SEQ,  0),
    KEYAT(257,1, 32, 0, "\xc2\xa5",     "\xc2\xa5",     KIND_SEQ,  0),
    KEYAT(289,1, 32, 0, "\xe2\x80\xa2", "\xe2\x80\xa2", KIND_SEQ,  0),
    /* row 2: [123] | . , ? ! ' | ⌫ */
    KEYAT(0,  2, 64, 0, NULL, "123", KIND_PAGE_BTN, PAGE_SYMBOLS),
    KEYAT(64, 2, 32, '.', NULL, ".",  KIND_CHAR, 0),
    KEYAT(96, 2, 32, ',', NULL, ",",  KIND_CHAR, 0),
    KEYAT(128,2, 32, '?', NULL, "?",  KIND_CHAR, 0),
    KEYAT(160,2, 32, '!', NULL, "!",  KIND_CHAR, 0),
    KEYAT(192,2, 32, '\'',NULL, "'",  KIND_CHAR, 0),
    KEYAT(256,2, 64, 0, NULL, "\xe6\x8b\xbc/EN", KIND_IME_TOGGLE, 0),  /* 拼/EN */
    SYM_ROW3_COMMON("ABC", PAGE_LETTERS),
};
#define N_SYMBOLS2_EN (sizeof(keys_symbols2_en) / sizeof(keys_symbols2_en[0]))

/* ── softkb_t ──────────────────────────────────────────────────────── */

struct softkb_t {
    softkb_page_t page;
    int           pressed_idx;     /* index of currently-touched key, -1 = none */
    int           pressed_frames;  /* visual press-down animation timer */
    char          out_buf[16];
    int           out_len;
    int           soft_shift;      /* ⇧ tap-toggle state: 1 = next char is
                                     * shifted (one-shot, iOS-style). */
    int           caps_lock;       /* long-press ⇧ → CapsLock (persistent) */

    /* Hold-to-repeat state — independent from the visual press animation
     * because that one fades after 14 frames while a true hold may last
     * arbitrarily long.
     *   prev_pressed         touch state on the previous frame (for
     *                        down-edge detection inside softkb_touch)
     *   repeat_idx           key currently being held; -1 = none.  Set to
     *                        -2 ("swallow until release") right after a
     *                        page toggle so the new layout's key under
     *                        the finger doesn't get fired.
     *   repeat_held_frames   frames since first contact with repeat_idx;
     *                        compared against DPAD-style ramp constants.
     */
    int           prev_pressed;
    int           repeat_idx;
    int           repeat_held_frames;

    /* Debug page state */
    int           debug_mode;            /* 0 = keyboard, 1 = debug overlay */
    int           mascot_mode;           /* 0=关, 1=螃蟹, 2=蟹特Crab, 3=小鸡, 4=噬菌体, 5=外星人, 6=牛来, 7=刀盾狗 */

    /* Settings page (⚙ badge, top-right) — dual-screen 3DS-style:
     * item list on the top screen, detail pane on the bottom. */
    int           settings_open;         /* 1 = settings page visible */
    int           settings_page;         /* settings_page_t being shown */
    int           settings_sel;          /* row within that page, remembered
                                          * across open/close cycles */
    int           settings_from_page;    /* where B returns from a group */
    int           settings_from_sel;
    u32           settings_rep_key;      /* D-pad key auto-repeating, 0=none */
    int           settings_rep_frames;   /* frames settings_rep_key was held */
    /* Projects page: a dedicated full-screen editor that lives outside
     * the regular settings framework because rows are not scalar toggles.
     * Main.c watches this flag through softkb_projects_open(); closing it
     * (via B in our own handler) hands control back to the terminal. */
    int           projects_open;
    int           sticky_mode;           /* 0 = Hold, 1 = Sticky */
    int           key_sound_enabled;     /* 0 = Off, 1 = On (default 1) */
    int           repeat_style;          /* keyrepeat.h ramp: 0 slow,
                                          * 1 normal (default), 2 fast */
    int           ui_language;           /* 0 = Chinese (default), 1 =
                                          * English.  Language of these
                                          * strings only — what you type is
                                          * the 拼/EN key's business. */
    term_theme_t  theme_mode;            /* 0..THEME_CUSTOM */
    /* What sdmc:/3ds/3dssh/theme.ini did at startup.  The 自定义 option's
     * tip is drawn from this, and it starts at MISSING because a softkb
     * that has never been told otherwise has never read a file. */
    theme_ini_report_t theme_rep;
    char              theme_tip[96];     /* item_tip()'s scratch, see above */
    int           voice_max_sec;         /* 15, 30, 60 (default 30) */
    int           voice_input_mode;      /* 0 = START tap toggle,
                                           * 1 = L hold-to-talk (L now free) */
    int           bottom_info_mode;      /* 0 = Clock+Battery, 1 = Clock only,
                                           * 2 = Hidden (info cleared &
                                           * mascot walk area widened) */

    /* Dual-screen bottom-panel mode.  0 = AUTO (full-screen TUI →
     * center-crop at full 6 px, shell → squeeze all 66 cols), 1 = SQUEEZE
     * (always compress the whole grid), 2 = CENTER (always crop to the
     * middle R_BOT_COLS at full width), 3 = TRIM (crop 2 cols left +
     * 3 right, squeeze the remaining 61 at ~0.87×).  Cycled from the
     * debug page. */
    int           dual_bottom_mode;

    /* Settings dirty flag — set on any user-visible settings mutation so
     * main.c can flush to SETTINGS_PATH.  Cleared by
     * softkb_clear_settings_dirty() after a successful save. */
    int           settings_dirty;

    /* Recv ring — last up to 32 SSH-bound bytes for debug display.
     * recv_head is the next write slot; recv_count saturates at 32. */
    uint8_t       recv_ring[32];
    int           recv_head;
    int           recv_count;

    /* M7 IME: pinyin engine pointer (may be NULL if dict failed to load).
     * The candidate-strip layout is recomputed each frame in
     * draw_status_row and stored here so softkb_touch can hit-test
     * tapped candidates back to indices. */
    ime_t        *ime;
    int           cand_box_x[IME_PAGE_SIZE];
    int           cand_box_w[IME_PAGE_SIZE];
    int           cand_box_n;            /* candidates currently visible */

    /* M11 voice input — read-only handle, NULL when voice disabled.
     * draw_status_row queries it each frame so the REC / spinner / ERR
     * badge preempts the modifier label. */
    const voice_t *voice;
};

/* The settings page table lives further down this file; see its definition
 * for why the check has to run at all (softkb.c can never be host-compiled). */
static void settings_tables_check(void);

softkb_t *softkb_init(ime_t *ime) {
    softkb_t *kb = calloc(1, sizeof(*kb));
    if (!kb) return NULL;
    settings_tables_check();
    kb->page = PAGE_LETTERS;
    kb->soft_shift = 0;
    kb->caps_lock  = 0;
    kb->pressed_idx = -1;
    kb->repeat_idx = -1;
    kb->mascot_mode = 1;                 /* 1 = CRAB (螃蟹) by default */
    kb->dual_bottom_mode = 0;            /* AUTO by default */
    kb->sticky_mode = 0;
    kb->key_sound_enabled = 1;
    kb->repeat_style = KEYREPEAT_NORMAL;   /* the cadence every build shipped */
    kb->ui_language  = 0;                  /* 0 = Chinese, what every build shipped */
    kb->theme_mode = THEME_DARK;
    kb->theme_rep.status = THEME_INI_MISSING;
    kb->voice_max_sec = 30;
    kb->bottom_info_mode = 0;            /* 0 = Clock + Battery by default */
    kb->ime = ime;
    return kb;
}

void softkb_free(softkb_t *kb) { free(kb); }

void softkb_set_ime(softkb_t *kb, ime_t *ime) {
    if (!kb) return;
    kb->ime = ime;
}

void softkb_set_voice(softkb_t *kb, const voice_t *v) {
    if (!kb) return;
    kb->voice = v;
}

softkb_page_t softkb_current_page(const softkb_t *kb) {
    return kb ? kb->page : PAGE_LETTERS;
}

void softkb_record_recv(softkb_t *kb, const char *bytes, int n) {
    if (!kb || !bytes || n <= 0) return;
    for (int i = 0; i < n; i++) {
        kb->recv_ring[kb->recv_head] = (uint8_t)bytes[i];
        kb->recv_head = (kb->recv_head + 1) % 32;
        if (kb->recv_count < 32) kb->recv_count++;
    }
}

int softkb_in_debug(const softkb_t *kb) {
    return kb ? kb->debug_mode : 0;
}

int softkb_settings_open(const softkb_t *kb) {
    return kb ? kb->settings_open : 0;
}

/* See softkb.h: called every frame by main.c while another page owns the
 * soft keyboard.  Zeroing the flag is all it takes — every per-open
 * re-init (page = SETP_ROOT, selection re-validation, repeat cancel)
 * already lives in the badge handler at the moment it flips the flag to
 * 1, so a forced 0 cannot leave a stale selection behind. */
void softkb_leave_settings(softkb_t *kb) {
    if (kb) kb->settings_open = 0;
}

int softkb_projects_open(const softkb_t *kb) {
    return kb ? kb->projects_open : 0;
}

void softkb_projects_close(softkb_t *kb) {
    if (kb) kb->projects_open = 0;
}

int softkb_mascot_mode(const softkb_t *kb) {
    return kb ? kb->mascot_mode : 1;
}

int softkb_mascot_enabled(const softkb_t *kb) {
    return kb ? (kb->mascot_mode > 0) : 1;
}

mascot_species_t softkb_mascot_species(const softkb_t *kb) {
    if (!kb || kb->mascot_mode <= 0) return MASCOT_CRAB;
    int idx = kb->mascot_mode - 1;
    if (idx >= MASCOT_SPECIES_COUNT) idx = 0;
    return (mascot_species_t)idx;
}

/* Dual-screen bottom-panel mode: 0 AUTO, 1 SQUEEZE, 2 CENTER, 3 TRIM.
 * main.c reads this each frame to pick the bottom-screen renderer. */
int softkb_dual_bottom_mode(const softkb_t *kb) {
    return kb ? kb->dual_bottom_mode : 0;
}

int softkb_sticky_mode(const softkb_t *kb) {
    return kb ? kb->sticky_mode : 0;
}

int softkb_key_sound_enabled(const softkb_t *kb) {
    return kb ? kb->key_sound_enabled : 1;
}

/* keyrepeat.h ramp index for the held D-pad, held B and held soft keys.
 * main.c pushes it into keyboard.c every frame. */
int softkb_repeat_style(const softkb_t *kb) {
    return kb ? kb->repeat_style : KEYREPEAT_NORMAL;
}

/* 0 = Chinese, 1..5 = English / Japanese / Korean / German / French.
 * Only ever a language of *our* text: the IME typing mode lives in
 * keyboard.c and is not this. */
int softkb_ui_lang(const softkb_t *kb) {
    return kb ? kb->ui_language : 0;
}

term_theme_t softkb_theme(const softkb_t *kb) {
    return kb ? kb->theme_mode : THEME_DARK;
}

/* Point the file-scope palette at this keyboard's theme.  Called as the
 * first statement of both public draw entries — before their guard and
 * early-return branches — so the debug and settings pages, which are
 * drawn without ever touching the key loop, use the same theme. */
static void softkb_use_theme_(const softkb_t *kb) {
    g_pal = kb_pal_for(kb ? kb->theme_mode : THEME_DARK);
}

int softkb_voice_max_sec(const softkb_t *kb) {
    return kb ? kb->voice_max_sec : 30;
}

int softkb_bottom_info_mode(const softkb_t *kb) {
    return kb ? kb->bottom_info_mode : 0;
}

void softkb_apply_settings(softkb_t *kb, const struct dssh_settings_t *st) {
    if (!kb || !st) return;
    /* DSSH_MASCOT_MODE_MAX, i.e. the same bound settings_fields[] validated
     * this value with on the way in — a second literal here is how the two
     * drifted apart from the species list in the first place. */
    kb->mascot_mode       = (st->mascot_mode >= 0 &&
                             st->mascot_mode <= DSSH_MASCOT_MODE_MAX)
                                ? st->mascot_mode : 1;
    kb->dual_bottom_mode  = (st->dual_bottom_mode >= 0 && st->dual_bottom_mode <= 3) ? st->dual_bottom_mode : 0;
    kb->sticky_mode       = st->sticky_mode ? 1 : 0;
    kb->key_sound_enabled = st->key_sound_enabled ? 1 : 0;
    kb->repeat_style      = (st->repeat_style >= 0 && st->repeat_style < KEYREPEAT_STYLE_COUNT)
                                ? st->repeat_style : KEYREPEAT_NORMAL;
    kb->ui_language       = (st->ui_language >= 0 && st->ui_language < LANG_COUNT)
                               ? st->ui_language : LANG_ZH;
    kb->theme_mode        = (st->theme_mode >= 0 && st->theme_mode < THEME_COUNT) ? (term_theme_t)st->theme_mode : THEME_DARK;
    {
        int v = st->voice_max_sec;
        if (v == 15 || v == 30 || v == 60) kb->voice_max_sec = v;
        else kb->voice_max_sec = 30;
    }
    /* -1 = "auto" (L) — same resolution main.c applies, so the order of
     * the two call sites no longer decides the default mic key. */
    kb->voice_input_mode  = (st->voice_input_mode == 0) ? 0 : 1;
    kb->bottom_info_mode  = (st->bottom_info_mode >= 0 && st->bottom_info_mode <= 2) ? st->bottom_info_mode : 0;
    kb->settings_dirty    = 0;
}

void softkb_export_settings(const softkb_t *kb, struct dssh_settings_t *st) {
    if (!kb || !st) return;
    /* In-place overlay — caller hands us a settings struct it has
     * already loaded from disk.  We only touch the fields the soft
     * keyboard owns; everything else (e.g. the [projects] section
     * managed by main.c, or any future field) is preserved verbatim.
     *
     * The previous version seeded from settings_defaults() before
     * copying, which silently wiped the user's project bookmarks the
     * moment they tweaked any UI option.  Always overlay, never reset.
     * Fields this TU does not know about must travel through the
     * caller-supplied struct. */
    st->mascot_mode       = kb->mascot_mode;
    st->dual_bottom_mode  = kb->dual_bottom_mode;
    st->sticky_mode       = kb->sticky_mode;
    st->key_sound_enabled = kb->key_sound_enabled;
    st->repeat_style      = kb->repeat_style;
    st->ui_language       = kb->ui_language;
    st->theme_mode        = (int)kb->theme_mode;
    st->voice_max_sec     = kb->voice_max_sec;
    st->voice_input_mode  = kb->voice_input_mode;
    st->bottom_info_mode  = kb->bottom_info_mode;
}

int softkb_settings_dirty(const softkb_t *kb) {
    return kb ? kb->settings_dirty : 0;
}

/* main.c hands over the theme.ini load report once, before the first frame.
 * The 自定义 row is the only consumer; it says what the file did rather than
 * what a compile-time string could guess about it.  NULL means "nothing has
 * been read", which is exactly how softkb_init starts out. */
void softkb_set_theme_report(softkb_t *kb, const theme_ini_report_t *report) {
    if (!kb) return;
    if (report) kb->theme_rep = *report;
    else {
        memset(&kb->theme_rep, 0, sizeof(kb->theme_rep));
        kb->theme_rep.status = THEME_INI_MISSING;
    }
}

void softkb_clear_settings_dirty(softkb_t *kb) {
    if (kb) kb->settings_dirty = 0;
}

/* ── Hit-test geometry for the right-side ENG/CHN badge.  Mirrors the
 *    layout in draw_status_row so a tap maps 1:1.  (The debug page's old
 *    mascot / bottom-layout toggle buttons moved into the ⚙ settings
 *    panel, so the debug page itself is all read-only display now.) */
#define BADGE_W       (3 * CELL_W + 4)        /* 22 px — same as slot_w */
#define BADGE_H       (STATUS_H - 4)          /* 30 px — same as slot_h */
#define BADGE_X       (R_BOT_PX_W - BADGE_W - 2)   /* 296 */
#define BADGE_Y       2

static int badge_hit(int tx, int ty) {
    return tx >= BADGE_X && tx < BADGE_X + BADGE_W &&
           ty >= BADGE_Y && ty < BADGE_Y + BADGE_H;
}

static const softkey_t *current_layout(const softkb_t *kb, const keyboard_t *kbd,
                                       int *count) {
    int cn = (kb->ime && kbd && keyboard_get_mode(kbd) == MODE_CN);
    switch (kb->page) {
        case PAGE_SYMBOLS:
            if (cn) { *count = N_SYMBOLS_CN; return keys_symbols_cn; }
            *count = N_SYMBOLS_EN; return keys_symbols_en;
        case PAGE_SYMBOLS2:
            if (cn) { *count = N_SYMBOLS2_CN; return keys_symbols2_cn; }
            *count = N_SYMBOLS2_EN; return keys_symbols2_en;
        default:
            break;
    }
    *count = N_LETTERS; return keys_letters;
}

/* ── touch hit-test ────────────────────────────────────────────────── */

static int hit_test(const softkb_t *kb, const keyboard_t *kbd, int tx, int ty) {
    int n;
    const softkey_t *layout = current_layout(kb, kbd, &n);
    for (int i = 0; i < n; i++) {
        const softkey_t *k = &layout[i];
        if (tx >= k->x && tx < k->x + k->w &&
            ty >= k->y && ty < k->y + k->h)
            return i;
    }
    return -1;
}

/* ⇧ long-press → CapsLock recognition threshold.  Deliberately NOT tied to
 * the repeat style: it answers "did the user mean to hold ⇧?", and someone
 * who asks for faster repeating keys wants that judgement to stay exactly
 * as easy to reach, not harder. */
#define CAPS_HOLD_FRAMES     30  /* hold ⇧ this long → CapsLock (~500 ms) */

/* ── Settings page — 3DS System-Settings style ──────────────────────
 *
 * Opened/closed by the ⚙ badge (bottom status row) or physical B.
 * While the page is open the status row is HIDDEN — the keyboard
 * underneath is irrelevant on this page (no typing, no modifier
 * state, no IME strip), and the user closes via physical B.  That
 * gives the detail card the full 320×240 of the bottom screen.
 *
 * TOP screen (400×240, no touch): one PAGE of the item list, D-pad
 * ↑/↓ moves the highlighted selection.  BOTTOM screen (320×240, the only
 * touch surface): the selected item's detail card — title, description
 * and every option as a real tappable button that fills the full
 * screen height.  ←/→ (or A) steps the selected value.
 *
 * The list is paged — a 9-row page is exactly as tall as the top screen
 * (see SET_PAGE_MAX) and the item count has long since passed that, so
 * the root page holds group headings (外观 / 输入 / 语音) plus the debug
 * action row instead of every setting.  Navigation deliberately adds no
 * on-screen control: a group is entered through the single 进入 button
 * its row already has, B is a context key (back to the root inside a
 * group, close the page at the root), and L/R step between sibling
 * groups — the page already owns both shoulders while it is open.
 *
 * Top screen layout (font 6×12, CJK = 2 cols):
 *   title band  y 0..22, rows start y=26 at a 22 px pitch —
 *   row 8 ends at 26 + 8·22 + 20 = 222, legend 226..238.  All ≤ 240.
 * Bottom screen layout (full 320×240, no status row):
 *   card 0,0 → 320,240; inside: title y=10, separator y=26,
 *   description y=30/44, option buttons fill the remaining
 *   y=58..236 range (178 px), scaling height + gap to the option
 *   count so 1..5 buttons always look balanced.
 */
#define SET_TOP_BAND_H 22
#define SET_LIST_X     8
#define SET_LIST_W     384
#define SET_LIST_Y0    26
#define SET_ROW_H      20
#define SET_ROW_GAP    2
#define SET_LEGEND_Y   226

/* Full-screen bottom card (status row is hidden on this page). */
#define SET_CARD_X     0
#define SET_CARD_Y     0
#define SET_CARD_W     R_BOT_PX_W
#define SET_CARD_H     R_PANEL_PX_H

/* Inner padding inside the full-screen card. */
#define SET_OPT_X      12
#define SET_OPT_W      296

/* Header band (title + current value + separator + 2 desc lines). */
#define SET_HEAD_TOP   10   /* baseline of the title row   */
#define SET_HEAD_SEP_Y 26   /* separator under the header  */
#define SET_DESC_Y0    30   /* desc[0] baseline            */
#define SET_DESC_Y1    44   /* desc[1] baseline (if any)   */

/* Option-button row: starts at SET_OPT_Y0 and stretches to ≤ 236
 * (4 px above the bottom).  Height + gap are recomputed every frame
 * from the active item's nopts so 1..5 buttons always fit. */
#define SET_OPT_Y0     58
#define SET_OPT_BOT    236
#define SET_OPT_MAX_H  40
/* 20 — not the 22 this comment used to claim.  The worst case is the
 * mascot row at seven options: 178 px of band (SET_OPT_BOT - SET_OPT_Y0)
 * less 6 gaps of SET_OPT_GAP (6 px each = 36 px) leaves 142 px, and
 * 142 / 7 = 20 px a button.  A six-option row gets (178 - 5*6)/6 ≈ 24.
 * At 22 the seven-option row would need 7*22 + 6*6 = 190 px and overrun
 * the band by 12 px — the old figure was not merely stale, acting on it
 * would have pushed the last button off the card. */
#define SET_OPT_MIN_H  20
#define SET_OPT_GAP    6

/* Rows the settings UI can show.  A row is addressed by this id, never by
 * its position on a page: the same table feeds the root page and the groups,
 * and settings_item_cur/_set switch on the id so a row can move between
 * pages without touching a field.  Append new rows — the *values* are
 * persisted as option indices, so an existing row's opts[] order is frozen
 * (see the note in config.h). */
typedef enum {
    SID_STICKY = 0,   /* 修饰键 */
    SID_DUAL_BOTTOM,  /* 下屏布局 */
    SID_THEME,        /* 主题 */
    SID_KEY_SOUND,    /* 按键音效 */
    SID_VOICE_MAX,    /* 录音上限 */
    SID_BOTTOM_INFO,  /* 底栏信息 */
    SID_MASCOT,       /* 吉祥物 */
    SID_VOICE_KEY,    /* 录音按键 */
    SID_REPEAT,       /* 按键重复速度 */
    SID_DEBUG,        /* 调试页面 — action row */
    SID_APPEAR,       /* 外观 — group row */
    SID_INPUT,        /* 输入 — group row */
    SID_VOICE,        /* 语音 — group row */
    SID_PROJECTS,     /* 项目 — group row; opens the bookmarks page */
    SID_UI_LANG,      /* 界面语言 — which language these strings are in */
    NSIDS
} settings_id_t;

/* Action rows show a single 进入 button instead of a value.  ACT_PAGE()
 * carries the page it opens; everything else about the row is drawn and
 * stepped exactly like the debug entry it reuses the code path of. */
#define ACT_NONE      0
#define ACT_DEBUG     1
#define ACT_PAGE(p)   (2 + (p))
/* ACT_PROJECTS is a sentinel value above ACT_PAGE()'s range so act_page()
 * never matches it (its -1 return short-circuits settings_fire_action's
 * page-routing).  main.c's settings_fire_action extension opens the
 * dedicated project-bookmarks page when it sees this act. */
#define ACT_PROJECTS  64
static int act_page(int act) { return act >= 2 && act < 64 ? act - 2 : -1; }

/* The 9-item list filled the top screen exactly (SET_LIST_Y0 + row 8 ends
 * at 222, the legend baseline is 226), so a page cannot grow past it. */
#define SET_PAGE_MAX 9

typedef enum {
    SETP_ROOT = 0,
    SETP_APPEAR,
    SETP_INPUT,
    SETP_VOICE,
    SETP_COUNT
} settings_page_t;
/* SETP_ROOT is page 0 and every page after it is a group reachable from it
 * — that is what lets the L/R group stepping below wrap among 1..COUNT-1. */

/* Mutable on purpose: softkb_init verifies these against settings_items and
 * flattens them into one root list if they disagree.  This file includes
 * citro2d.h, so no host test can ever catch a mistyped row here, and the
 * failure mode of a bad table is an out-of-bounds id rather than a compile
 * error.  A flattened list is ugly but always in range. */
static int settings_page_n[SETP_COUNT] = { 5, 5, 3, 2 };
static settings_id_t settings_pages[SETP_COUNT][SET_PAGE_MAX] = {
    [SETP_ROOT]   = { SID_APPEAR, SID_INPUT, SID_VOICE, SID_PROJECTS,
                      SID_DEBUG },
    /* 界面语言 sits right below 主题: someone who cannot read the current
     * language has to find the way out near the top of the first group. */
    [SETP_APPEAR] = { SID_THEME, SID_UI_LANG, SID_BOTTOM_INFO, SID_MASCOT,
                      SID_DUAL_BOTTOM },
    [SETP_INPUT]  = { SID_STICKY, SID_KEY_SOUND, SID_REPEAT },
    [SETP_VOICE]  = { SID_VOICE_MAX, SID_VOICE_KEY },
};
/* NULL = the plain 设置 title band; a group draws "设置 · <title>". */
static const char *const settings_page_title[SETP_COUNT] = {
    NULL, "外观", "输入", "语音",
};
/* Per-language page titles, in the same order as settings_page_title
 * above (NULL at index 0 means "no group suffix").  Every entry must
 * stay index-aligned with settings_page_t.  Missing translations
 * fall back to English. */
static const char *const settings_page_title_en[SETP_COUNT] = {
    NULL, "Appearance", "Input", "Voice",
};
static const char *const settings_page_title_ja[SETP_COUNT] = {
    NULL, "表示", "入力", "音声",
};
static const char *const settings_page_title_ko[SETP_COUNT] = {
    NULL, "외관", "입력", "음성",
};
static const char *const settings_page_title_de[SETP_COUNT] = {
    NULL, "Darstellung", "Eingabe", "Sprache",
};
static const char *const settings_page_title_fr[SETP_COUNT] = {
    NULL, "Affichage", "Saisie", "Voix",
};

typedef struct {
    const char *label;
    const char *desc[2];   /* desc[1] may be NULL */
    int         nopts;
    /* SOFTKB_OPTS_MAX is the ceiling the SID_UI_LANG row needs
     * (zh/en/ja/ko/de/fr).  settings_opt_rect() is built for ≤5 buttons
     * on a row that fits 5*SET_OPT_BOT_ROWS_H, so adding a 6th option
     * anywhere will resize the row vertically — see settings_opt_rect's
     * clamping.  Sized by macro, not literal, so the compile-time check
     * at the top of this file can pin the width to LANG_COUNT. */
    const char *opts[SOFTKB_OPTS_MAX];
    /* Optional tip per option, drawn on the 2nd description line when
     * the value is selected; NULL entries fall back to desc[1]. */
    const char *opt_desc[SOFTKB_OPTS_MAX];
    int         act;       /* ACT_*, see above */
    /* Per-language twins (English / Japanese / Korean / German / French),
     * picked by set_lbl() below.  They live in the same row rather than
     * in parallel tables because the six must agree on nopts, id and
     * page membership — an offset between parallel tables is exactly
     * the kind of drift no host test can catch here (this TU needs
     * citro2d).  Any twin may be NULL: it falls back to the previous
     * non-NULL language, which is correct for proper nouns (the
     * mascot names) and safe for anything a later edit forgets to
     * translate. */
    const char *label_en,  *label_ja,  *label_ko,  *label_de,  *label_fr;
    const char *desc_en[2], *desc_ja[2], *desc_ko[2], *desc_de[2], *desc_fr[2];
    /* Sized by the same macro as opts[]/opt_desc[], never by a literal:
     * set_lbl_arr5() indexes the canonical array and all five twins with
     * the same i, up to SOFTKB_OPTS_MAX-1.  刀盾狗 pushed the mascot row
     * to seven choices and these were still [6], so reading them at i==6
     * stepped one pointer past the end — into the *next* twin's [0]. */
    const char *opts_en[SOFTKB_OPTS_MAX], *opts_ja[SOFTKB_OPTS_MAX],
              *opts_ko[SOFTKB_OPTS_MAX], *opts_de[SOFTKB_OPTS_MAX],
              *opts_fr[SOFTKB_OPTS_MAX];
    const char *opt_desc_en[SOFTKB_OPTS_MAX], *opt_desc_ja[SOFTKB_OPTS_MAX],
              *opt_desc_ko[SOFTKB_OPTS_MAX], *opt_desc_de[SOFTKB_OPTS_MAX],
              *opt_desc_fr[SOFTKB_OPTS_MAX];
} settings_item_t;

/* Compile-time guard for the above: a twin narrower than the array it
 * mirrors is read past its end, and nothing at run time would notice —
 * the bytes one pointer on are still inside the same row. */
_Static_assert(sizeof(((settings_item_t *)0)->opts_en) ==
               sizeof(((settings_item_t *)0)->opts),
               "language twins must be exactly as wide as opts[]");
_Static_assert(sizeof(((settings_item_t *)0)->opt_desc_en) ==
               sizeof(((settings_item_t *)0)->opt_desc),
               "language twins must be exactly as wide as opt_desc[]");

static const settings_item_t settings_items[NSIDS] = {
    [SID_STICKY] = {
        "\xe4\xbf\xae\xe9\xa5\xb0\xe9\x94\xae",                       /* 修饰键 */
        { "Y(Ctrl) \xe5\x92\x8c X(Alt) \xe7\x9a\x84"                  /* Y(Ctrl) 和 X(Alt) 的 */
          "\xe8\xa7\xa6\xe5\x8f\x91\xe6\x96\xb9\xe5\xbc\x8f",         /* 触发方式 */
          "\xe6\x8c\x89\xe4\xbd\x8f=\xe9\x95\xbf\xe6\x8c\x89"         /* 按住=长按 */
          "\xe6\x9c\x9f\xe9\x97\xb4\xe7\x94\x9f\xe6\x95\x88 "         /* 期间生效␣ */
          "\xe7\xb2\x98\xe6\xbb\x9e=\xe7\x82\xb9\xe6\x8c\x89"         /* 粘滞=点按 */
          "\xe9\x94\x81\xe5\xae\x9a" },                               /* 锁定 */
        2, { "\xe6\x8c\x89\xe4\xbd\x8f",                              /* 按住 */
             "\xe7\xb2\x98\xe6\xbb\x9e" },                            /* 粘滞 */
        .label_en  = "Modifier keys",
        .desc_en   = { "How Y(Ctrl) and X(Alt) trigger",
                       "Hold: works while pressed; sticky: tap locks" },
        .opts_en   = { "Hold", "Sticky" },
        .label_ja  = "\xe4\xbf\xae\xe9\x9d\xb9\xe3\x82\xad\xe3\x83\xbc",  /* 修飾キー */
        .desc_ja   = { "Y(Ctrl) と X(Alt) のトリガー",
                       "長押し:押している間有効 / スティッキー:押下で固定" },
        .opts_ja   = { "\xe9\x95\xb7\xe6\x8a\xbc\xe3\x81\x97",           /* 長押し */
                       "\xe3\x82\xb9\xe3\x83\x86\xe3\x82\xa3\xe3\x83\x83\xe3\x82\xad\xe3\x83\xbc" }, /* スティッキー */
        .label_ko  = "\xec\x88\x98\xec\x8b\x9d\xec\x96\xb4",              /* 수식어 */
        .desc_ko   = { "Y(Ctrl)\xec\x99\x80 X(Alt) \xed\x8a\xb8\xeb\xa6\xac\xea\xb1\xb0", /* Y(Ctrl)와 X(Alt) 트리거 */
                       "\xea\xb0\x80\xeb\xa1\x9c: \xeb\x88\x84\xeb\x9d\xbc\xeb\x8a\x94 \xeb\x8f\x99\xec\x95\x88 \xed\x9a\xa8\xea\xb3\xb0" }, /* 가로: 누르는 동안 효고 */
        .opts_ko   = { "\xea\xb0\x80\xeb\xa1\x9c",                        /* 가로 */
                       "\xec\x8a\xa4\xed\x8b\xb0\xed\x82\xa4" },           /* 스티키 */
        .label_de  = "Modifikatortasten",
        .desc_de   = { "Auslöser für Y(Ctrl) und X(Alt)",
                       "Halten: aktiv während gedrückt; Klebrig: tippen fixiert" },
        .opts_de   = { "Halten", "Klebrig" },
        .label_fr  = "Touches modificatrices",
        .desc_fr   = { "Déclenchement de Y(Ctrl) et X(Alt)",
                       "Maintenir: actif pendant la pression; Verrou: tap pour fixer" },
        .opts_fr   = { "Maintenir", "Verrouiller" },
    },
    [SID_DUAL_BOTTOM] = {
        "\xe4\xb8\x8b\xe5\xb1\x8f\xe5\xb8\x83\xe5\xb1\x80",           /* 下屏布局 */
        { "\xe5\x8f\x8c\xe5\xb1\x8f\xe6\xa8\xa1\xe5\xbc\x8f"           /* 双屏模式 */
          "\xe4\xb8\x8b\xe5\xb1\x8f\xe7\x9a\x84\xe6\xb8\xb2\xe6\x9f\x93" /* 下屏的渲染 */
          "\xe6\x96\xb9\xe5\xbc\x8f",                                  /* 方式 */
          NULL },
        4, { "\xe8\x87\xaa\xe5\x8a\xa8",                              /* 自动 */
             "\xe5\x8e\x8b\xe7\xbc\xa9",                              /* 压缩 */
             "\xe5\xb1\x85\xe4\xb8\xad",                              /* 居中 */
             "\xe8\xa3\x81\xe8\xbe\xb9" },                            /* 裁边 */
        { "\xe8\x87\xaa\xe5\x8a\xa8=\xe5\x91\xbd\xe4\xbb\xa4\xe8\xa1\x8c" /* 自动=命令行 */
          "\xe5\x8e\x8b\xe6\x89\x81\xef\xbc\x8c"                      /* 压扁， */
          "TUI \xe5\xb1\x85\xe4\xb8\xad\xe8\xa3\x81\xe5\x88\x87",     /* TUI 居中裁切 */
          "\xe5\x8e\x8b\xe7\xbc\xa9=66\xe5\x88\x97\xe5\x85\xa8"      /* 压缩=66列全 */
          "\xe9\x83\xa8\xe5\x8e\x8b\xe6\x89\x81\xef\xbc\x8c\xe4\xb8\x8d" /* 部压扁，不 */
          "\xe4\xb8\xa2\xe4\xbb\xbb\xe4\xbd\x95\xe5\x88\x97",         /* 丢任何列 */
          "\xe5\xb1\x85\xe4\xb8\xad=\xe8\xa3\x81\xe6\x8e\x89\xe4\xb8\xa4" /* 居中=裁掉两 */
          "\xe4\xbe\xa7\xef\xbc\x8c\xe4\xb8\xad\xe9\x97\xb4"          /* 侧，中间 */
          "53\xe5\x88\x97\xe5\xad\x97\xe5\xae\xbd\xe4\xb8\x8d\xe5\x8f\x98", /* 53列字宽不变 */
          "\xe8\xa3\x81\xe8\xbe\xb9=\xe8\xa3\x81\xe5\xb7\xa6"         /* 裁边=裁左 */
          "2\xe5\x8f\xb3" "3\xe5\x88\x97\xef\xbc\x8c\xe4\xbd\x99"     /* 2右3列，余 */
          "61\xe5\x88\x97\xe8\xbd\xbb\xe5\xbe\xae\xe5\x8e\x8b\xe6\x89\x81" }, /* 61列轻微压扁 */
        .label_en  = "Bottom layout",
        .desc_en   = { "Dual-screen mode: how the bottom draws", NULL },
        .opts_en   = { "Auto", "Squeeze", "Center", "Trim" },
        .opt_desc_en = {
            "Auto: shell flattened, TUI center-cropped",
            "Squeeze: all 66 columns flattened, none lost",
            "Center: middle 53 columns at full width",
            "Trim: 2 left + 3 right cut, 61 slightly flat",
        },
        .label_ja  = "\xe4\xb8\x8b\xe7\x94\xbb\xe9\x9d\xa2\xe3\x83\xac\xe3\x82\xa4\xe3\x82\xa2\xe3\x82\xa6\xe3\x83\x88", /* 下画面レイアウト */
        .label_ko  = "\xed\x95\x98\xeb\x8b\xa8 \xeb\xa0\x88\xec\x9d\xb4\xec\x95\x84\xec\x9b\x83", /* 하단 레이아웃 */
        .label_de  = "Unteres Layout",
        .label_fr  = "Disposition du bas",
    },
    [SID_THEME] = {
        "\xe4\xb8\xbb\xe9\xa2\x98",                                    /* 主题 */
        { "\xe5\x90\x8c\xe6\xad\xa5\xe9\x87\x8d\xe7\xbb\x98"          /* 同步重绘 */
          "\xe4\xb8\x8a\xe4\xb8\x8b\xe4\xb8\xa4\xe5\xb1\x8f\xe7\x9a\x84" /* 上下两屏的 */
          "\xe9\x85\x8d\xe8\x89\xb2",                                  /* 配色 */
          "\xe6\x9c\xac\xe9\xa1\xb5\xe9\x9d\xa2\xe5\x8d\xb3\xe6\x97\xb6" /* 本页面即时 */
          "\xe9\xa2\x84\xe8\xa7\x88\xe6\x96\xb0\xe4\xb8\xbb\xe9\xa2\x98" }, /* 预览新主题 */
        /* Five is the ceiling, not a choice: settings_opt_rect() gives a
         * 6-option row buttons 24 px tall, pushes the row up over the desc
         * line and off the bottom of the card.  自定义 is therefore the last
         * option this row can ever hold.
         *
         * Its tip is not in the table: the honest thing to say about a
         * colour file the user edits by hand is what the load actually
         * reported, so item_tip() substitutes kb->theme_rep for this one
         * option.  See softkb_set_theme_report(). */
        5, { "\xe9\xbb\x98\xe8\xae\xa4",                              /* 默认 */
             "\xe9\xbb\x91\xe5\xae\xa2\xe7\xbb\xbf",                  /* 黑客绿 */
             "\xe9\xbb\x91\xe7\x99\xbd",                              /* 黑白 */
             "\xe6\xb7\xb1\xe6\xb5\xb7",                              /* 深海 */
             "\xe8\x87\xaa\xe5\xae\x9a\xe4\xb9\x89"                   /* 自定义 */
        },
        .label_en  = "Theme",
        .desc_en   = { "Recolors both screens at once",
                       "This page previews it live" },
        .opts_en   = { "Default", "Green", "Mono", "Ocean", "Custom" },
        .label_ja  = "\xe3\x83\x86\xe3\x83\xbc\xe3\x83\x9e",           /* テーマ */
        .opts_ja   = { "\xe3\x83\x87\xe3\x83\x95\xe3\x82\xa9\xe3\x83\xab\xe3\x83\x88", /* デフォルト */
                       "\xe3\x83\x8f\xe3\x82\xaf\xe3\x82\xba\xe3\x83\xbc\xe7\xb7\xa8", /* ハッカー緑 */
                       "\xe7\x99\xbd\xe9\xbb\x92",                       /* 白黒 */
                       "\xe6\xb7\xb1\xe6\xb5\xb7",                       /* 深海 */
                       "\xe3\x82\xab\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\xa0" }, /* カスタム */
        .label_ko  = "\xed\x85\x8c\xec\x9d\xb4\xed\x94\xbc",           /* 테마 */
        .opts_ko   = { "\xea\xb8\xb0\xeb\xb3\xb8",                       /* 기본 */
                       "\xed\x95\x9c\xec\xbb\xa4 \xeb\x81\xa0\xec\x83\x89", /* 한커 널색 */
                       "\xeb\xb0\x91\xed\x99\x94",                       /* 백화 */
                       "\xea\xb9\x8a\xed\x95\xb4",                       /* 심해 */
                       "\xec\xbb\xa4\xec\x8a\xa4\xed\x85\x9c" },           /* 커스텀 */
        .label_de  = "Design",
        .opts_de   = { "Standard", "Hacker-Grün", "Mono", "Ozean", "Eigen" },
        .label_fr  = "Thème",
        .opts_fr   = { "Défaut", "Vert hacker", "Mono", "Océan", "Perso" },
    },
    [SID_KEY_SOUND] = {
        "\xe6\x8c\x89\xe9\x94\xae\xe9\x9f\xb3\xe6\x95\x88",           /* 按键音效 */
        { "\xe8\xbd\xaf\xe9\x94\xae\xe7\x9b\x98\xe7\x82\xb9\xe6\x8c\x89" /* 软键盘点按 */
          "\xe6\x97\xb6\xe7\x9a\x84\xe5\x92\x94\xe5\x93\x92\xe5\xa3\xb0", /* 时的咔哒声 */
          NULL },
        2, { "\xe5\x85\xb3\xe9\x97\xad",                              /* 关闭 */
             "\xe5\xbc\x80\xe5\x90\xaf" },                            /* 开启 */
        .label_en  = "Key click sound",
        .desc_en   = { "Click when tapping the soft keyboard", NULL },
        .opts_en   = { "Off", "On" },
        .label_ja  = "\xe3\x82\xad\xe3\x83\xbc\xe3\x82\xaf\xe3\x83\xaa\xe3\x83\xa9\xe3\x83\x83\xe3\x82\xaf", /* キークリック音 */
        .opts_ja   = { "Off", "On" },
        .label_ko  = "\xed\x82\xa4\xec\x9d\xb4 \xed\x81\xb4\xeb\xa6\xad \xec\x9d\x8c", /* 키 클릭 음 */
        .opts_ko   = { "\xeb\x82\x98\xea\xb0\x9c",                       /* 나개 */
                       "\xec\xbc\x9c\xea\xb8\xb0" },                       /* 켜기) */
        .label_de  = "Tastenklick",
        .opts_de   = { "Aus", "An" },
        .label_fr  = "Clic des touches",
        .opts_fr   = { "Off", "On" },
    },
    [SID_REPEAT] = {
        "\xe6\x8c\x89\xe9\x94\xae\xe9\x87\x8d\xe5\xa4\x8d"            /* 按键重复 */
        "\xe9\x80\x9f\xe5\xba\xa6",                                   /* 速度 */
        { "\xe6\x8c\x89\xe4\xbd\x8f\xe4\xb8\x80\xe4\xb8\xaa"          /* 按住一个 */
          "\xe9\x94\xae\xe4\xb8\x8d\xe6\x94\xbe\xe6\x97\xb6"          /* 键不放时 */
          "\xe8\xbf\x9e\xe7\xbb\xad\xe8\xa7\xa6\xe5\x8f\x91"          /* 连续触发 */
          "\xe7\x9a\x84\xe9\x80\x9f\xe5\xba\xa6",                     /* 的速度 */
          "\xe9\x95\xbf\xe6\x8c\x89 \xe2\x87\xa7 \xe9\x94\x81"       /* 长按 ⇧ 锁 */
          "\xe5\xae\x9a\xe5\xa4\xa7\xe5\x86\x99\xe7\x9a\x84"          /* 定大写的 */
          "\xe5\x88\xa4\xe5\xae\x9a\xe4\xb8\x8d\xe9\x9a\x8f"          /* 判定不随 */
          "\xe6\xad\xa4\xe8\xae\xbe\xe7\xbd\xae\xe6\x94\xb9"          /* 此设置改 */
          "\xe5\x8f\x98" },                                           /* 变 */
        /* The ramp drives three surfaces at once: the physical D-pad, B as
         * backspace, and a held on-screen key.  Say so — a user who only
         * ever taps would otherwise guess this is about the soft keyboard. */
        3, { "\xe6\x85\xa2",                                          /* 慢 */
             "\xe6\xa0\x87\xe5\x87\x86",                              /* 标准 */
             "\xe5\xbf\xab" },                                        /* 快 */
        .label_en  = "Key auto-repeat",
        .desc_en   = { "Speed of a key held down",
                       "Holding \xe2\x87\xa7 for caps lock is unaffected" },
        .opts_en   = { "Slow", "Standard", "Fast" },
        .label_ja  = "\xe3\x82\xad\xe3\x83\xbc\xe8\xbf\x94\xe3\x82\x8a", /* キー返り */
        .opts_ja   = { "\xe9\x81\x85\xe3\x81\x84",                       /* 遅い */
                       "\xe6\xa8\x99\xe6\xba\x96",                       /* 標準 */
                       "\xe9\xab\x98\xe9\x80\x9f" },                     /* 高速 */
        .label_ko  = "\xed\x82\xa4 \xec\x9e\xac\xed\x95\xa0\xeb\xb0\xa9", /* 키 재호방 */
        .opts_ko   = { "\xeb\x8a\x90\xeb\xa6\xb4",                       /* 느림 */
                       "\xed\x91\x9c\xec\xa4\x80",                       /* 표준 */
                       "\xeb\xb9\xa0\xeb\xa6\x84" },                     /* 빠름 */
        .label_de  = "Tastenwiederholung",
        .opts_de   = { "Langsam", "Standard", "Schnell" },
        .label_fr  = "Répétition des touches",
        .opts_fr   = { "Lent", "Standard", "Rapide" },
    },
    [SID_VOICE_MAX] = {
        "\xe5\xbd\x95\xe9\x9f\xb3\xe4\xb8\x8a\xe9\x99\x90",           /* 录音上限 */
        { "\xe8\xaf\xad\xe9\x9f\xb3\xe8\xbe\x93\xe5\x85\xa5\xe5\x8d\x95" /* 语音输入单 */
          "\xe6\xac\xa1\xe5\xbd\x95\xe9\x9f\xb3\xe7\x9a\x84\xe6\x9c\x80" /* 次录音的最 */
          "\xe9\x95\xbf\xe6\x97\xb6\xe9\x97\xb4",                      /* 长时间 */
          "\xe5\xbd\x95\xe9\x9f\xb3\xe6\x97\xb6\xe4\xb8\x8b\xe5\xb1\x8f" /* 录音时下屏 */
          "\xe5\xba\x95\xe8\xbe\xb9\xe6\x98\xbe\xe7\xa4\xba\xe5\x89\xa9" /* 底边显示剩 */
          "\xe4\xbd\x99\xe6\x97\xb6\xe9\x97\xb4\xe5\x80\x92\xe8\xae\xa1" /* 余时间倒计 */
          "\xe6\x97\xb6\xe6\x9d\xa1" },                                /* 时条 */
        3, { "15\xe7\xa7\x92", "30\xe7\xa7\x92", "60\xe7\xa7\x92" },  /* 15秒 30秒 60秒 */
        .label_en  = "Recording limit",
        .desc_en   = { "Longest single voice recording",
                       "Time left counts down on the bottom edge" },
        .opts_en   = { "15 s", "30 s", "60 s" },
        .label_ja  = "\xe9\x8c\xb2\xe9\x9f\xb3\xe4\xb8\x8a\xe9\x99\x90", /* 録音上限 */
        .opts_ja   = { "15\xe7\xa7\x92", "30\xe7\xa7\x92", "60\xe7\xa7\x92" },
        .label_ko  = "\xeb\x85\xb9\xec\x9d\x8c \xec\x83\x81\xed\x95\x9c", /* 녹음 상한 */
        .opts_ko   = { "15\xe7\xb4\x94", "30\xe7\xb4\x94", "60\xe7\xb4\x94" },
        .label_de  = "Aufnahmelimit",
        .opts_de   = { "15 s", "30 s", "60 s" },
        .label_fr  = "Limite d'enregistrement",
        .opts_fr   = { "15 s", "30 s", "60 s" },
    },
    [SID_BOTTOM_INFO] = {
        "\xe5\xba\x95\xe6\xa0\x8f\xe4\xbf\xa1\xe6\x81\xaf",           /* 底栏信息 */
        { "\xe4\xb8\x8b\xe5\xb1\x8f\xe5\xba\x95\xe6\xa0\x8f\xe7\x9a\x84" /* 下屏底栏的 */
          "\xe6\x97\xb6\xe9\x92\x9f\xe4\xb8\x8e\xe7\x94\xb5\xe9\x87\x8f" /* 时钟与电量 */
          "\xe6\x98\xbe\xe7\xa4\xba",                                  /* 显示 */
          "\xe9\x9a\x90\xe8\x97\x8f\xe5\x90\x8e\xe5\x90\x89\xe7\xa5\xa5" /* 隐藏后吉祥 */
          "\xe7\x89\xa9\xe6\xb4\xbb\xe5\x8a\xa8\xe5\x8c\xba\xe5\x9f\x9f" /* 物活动区域 */
          "\xe6\x9b\xb4\xe5\xa4\xa7" },                                /* 更大 */
        3, { "\xe6\x97\xb6\xe9\x92\x9f\xe7\x94\xb5\xe9\x87\x8f",      /* 时钟电量 */
             "\xe4\xbb\x85\xe6\x97\xb6\xe9\x92\x9f",                  /* 仅时钟 */
             "\xe9\x9a\x90\xe8\x97\x8f" },                            /* 隐藏 */
        .label_en  = "Status bar",
        .desc_en   = { "Clock and battery on the bottom bar",
                       "Hiding them gives the mascot more room" },
        .opts_en   = { "Clock+Batt", "Clock", "Hidden" },
        .label_ja  = "\xe3\x82\xb9\xe3\x83\x86\xe3\x83\xbc\xe3\x82\xbf\xe3\x82\xb9\xe3\x83\x90\xe3\x83\xbc", /* ステータスバー */
        .label_ko  = "\xec\x83\x81\xeb\x8b\xa8 \xc2\xb7 \xec\xa0\x95\xeb\xb3\xb4", /* 상태바 */
        .label_de  = "Statusleiste",
        .opts_de   = { "Uhr+Akku", "Uhr", "Verborgen" },
        .label_fr  = "Barre d'état",
        .opts_fr   = { "Horloge+Batt", "Horloge", "Caché" },
    },
    [SID_MASCOT] = {
        "\xe5\x90\x89\xe7\xa5\xa5\xe7\x89\xa9",                       /* 吉祥物 */
        { "\xe4\xb8\x8b\xe5\xb1\x8f\xe5\xba\x95\xe6\xa0\x8f\xe6\xbc\xab" /* 下屏底栏漫 */
          "\xe6\xad\xa5\xe7\x9a\x84\xe5\x83\x8f\xe7\xb4\xa0\xe5\xae\xa0" /* 步的像素宠 */
          "\xe7\x89\xa9",                                              /* 物 */
          NULL },
        8, { "\xe5\x85\xb3",                                          /* 关 */
             "\xe8\x9e\x83\xe8\x9f\xb9",                              /* 螃蟹 (Upstream) */
             "\xe8\x9f\xb9\xe7\x89\xb9\x43\x72\x61\x62",              /* 蟹特Crab */
             "\xe5\x9d\xa4\xe5\x9d\xa4",                              /* 坤坤 */
             "\xe8\x8f\x8c\xe8\x8f\x8c",                              /* 菌菌 */
             "\xe5\xa4\xa7\xe7\x9c\xbc",                              /* 大眼 */
             "\xe7\x89\x9c\xe6\x9d\xa5",                            /* 牛来 */
             "\xe5\x88\x80\xe7\x9b\xbe\xe7\x8b\x97" },               /* 刀盾狗 */
        .label_en  = "Mascot",
        .desc_en   = { "Pixel pet walking along the bottom bar", NULL },
        /* The four pets are named, not described — their twins stay NULL so
         * the English UI shows the same names.  Only 关 is a state. */
        .opts_en   = { "Off" },
        .label_ja  = "\xe3\x83\x9e\xe3\x82\xb9\xe3\x82\xb3\xe3\x83\x83\xe3\x83\x88", /* マスコット */
        .label_ko  = "\xeb\xa7\x88\xec\x8a\xa4\xec\xbd\x94\xed\x8a\xb8", /* 마스코트 */
        .label_de  = "Maskottchen",
        .label_fr  = "Mascotte",
        .opts_de   = { "Aus" },
        .opts_fr   = { "Off" },
    },
    [SID_VOICE_KEY] = {
        "\xe5\xbd\x95\xe9\x9f\xb3\xe6\x8c\x89\xe9\x94\xae",           /* 录音按键 */
        { "R=\xe6\x8c\x89\xe4\xbd\x8f\xe8\xaf\xb4\xe8\xaf\x9d"          /* R=按住说话 */
          "\xef\xbc\x8c\xe6\x9d\xbe\xe6\x89\x8b\xe5\x8d\xb3\xe5\x8f\x91" /* ，松手即发 */
          "  R+START \xe7\xbb\x84\xe5\x90\x88\xe5\xa7\x8b\xe7\xbb\x88" /*  R+START 组合始终 */
          "\xe6\x98\xaf AI \xe9\x97\xae\xe7\xad\x94" },               /* 是 AI 问答 */
        /* M11 redesign: only R records now (one entry point, no
         * START-vs-L mode toggle), so the option list collapses to a
         * single informational row.  The old 0=START/1=L values in
         * settings.ini still load — see softkb_apply_settings. */
        1, { "R" },
        .label_en  = "Record key",
        .desc_en   = { "R: hold to talk, release to send",
                       "R+START always asks the AI instead" },
        .opts_en   = { NULL },          /* "R" is the same in both */
        .label_ja  = "\xe9\x8c\xb2\xe9\x9f\xb3\xe3\x83\x9c\xe3\x82\xbf\xe3\x83\xb3", /* 録音ボタン */
        .opts_ja   = { NULL },
        .label_ko  = "\xeb\x85\xb9\xec\x9d\x8c \xeb\xb2\x84\xed\x84\xb4", /* 녹음 버튼 */
        .opts_ko   = { NULL },
        .label_de  = "Aufnahmetaste",
        .opts_de   = { NULL },
        .label_fr  = "Touche d'enregistrement",
        .opts_fr   = { NULL },
    },
    [SID_DEBUG] = {
        "\xe8\xb0\x83\xe8\xaf\x95\xe9\xa1\xb5\xe9\x9d\xa2",           /* 调试页面 */
        { "\xe6\x9f\xa5\xe7\x9c\x8b SSH \xe5\x8e\x9f\xe5\xa7\x8b"     /* 查看 SSH 原始 */
          "\xe5\xad\x97\xe8\x8a\x82\xe6\xb5\x81\xe4\xb8\x8e\xe6\x8c\x89" /* 字节流与按 */
          "\xe9\x94\xae\xe9\x80\x9f\xe6\x9f\xa5",                     /* 键速查 */
          NULL },
        1, { "\xe8\xbf\x9b\xe5\x85\xa5" },                            /* 进入 */
        { 0 }, ACT_DEBUG,
        .label_en  = "Debug page",
        .desc_en   = { "Raw SSH byte stream and key map", NULL },
        .opts_en   = { "Open" },
        .label_ja  = "\xe3\x83\x87\xe3\x83\x90\xe3\x83\x83\xe3\x82\xb0", /* デバッグ */
        .opts_ja   = { "\xe9\x96\x8b\xe3\x81\x8f" },                     /* 開く */
        .label_ko  = "\xeb\x94\x94\xeb\xb2\x84\xea\xb7\xb8",           /* 디버그 */
        .opts_ko   = { "\xec\x97\xb4\xea\xb8\xb0" },                     /* 열기 */
        .label_de  = "Debug-Seite",
        .opts_de   = { "Öffnen" },
        .label_fr  = "Page de débogage",
        .opts_fr   = { "Ouvrir" },
    },
    /* Group headings: same action row, and the only way into a page — no
     * new on-screen control is introduced anywhere on this page. */
    [SID_APPEAR] = {
        "外观",
        { "主题、底栏信息、吉祥物与下屏布局", NULL },
        1, { "进入" },
        { 0 }, ACT_PAGE(SETP_APPEAR),
        .label_en  = "Appearance",
        .desc_en   = { "Theme, language, status bar, layout", NULL },
        .opts_en   = { "Open" },
        .label_ja  = "\xe8\xa1\xa8\xe7\xa4\xba",                       /* 表示 */
        .opts_ja   = { "\xe9\x96\x8b\xe3\x81\x8f" },
        .label_ko  = "\xec\x99\xb8\xea\xb4\x80",                       /* 외관 */
        .opts_ko   = { "\xec\x97\xb4\xea\xb8\xb0" },
    },
    [SID_INPUT] = {
        "输入",
        { "修饰键、按键音效与重复速度", NULL },
        1, { "进入" },
        { 0 }, ACT_PAGE(SETP_INPUT),
        .label_en  = "Input",
        .desc_en   = { "Modifiers, click sound, auto-repeat", NULL },
        .opts_en   = { "Open" },
        .label_ja  = "\xe5\x85\xa5\xe5\x8a\x9b",                       /* 入力 */
        .opts_ja   = { "\xe9\x96\x8b\xe3\x81\x8f" },
        .label_ko  = "\xec\x9e\x85\xeb\xa0\xa5",                       /* 입력 */
        .opts_ko   = { "\xec\x97\xb4\xea\xb8\xb0" },
    },
    [SID_VOICE] = {
        "语音",
        { "录音时长上限与触发按键", NULL },
        1, { "进入" },
        { 0 }, ACT_PAGE(SETP_VOICE),
        .label_en  = "Voice",
        .desc_en   = { "Recording limit and record key", NULL },
        .opts_en   = { "Open" },
        .label_ja  = "\xe9\x9f\xb3\xe5\xa3\xb0",                       /* 音声 */
        .opts_ja   = { "\xe9\x96\x8b\xe3\x81\x8f" },
        .label_ko  = "\xec\x9d\x8c\xec\x84\xb1",                       /* 음성 */
        .opts_ko   = { "\xec\x97\xb4\xea\xb8\xb0" },
    },
    [SID_PROJECTS] = {
        "项目",
        { "连接后 / 新窗口可选的目录与 agent 工具卡片",
          "在这里增删改，改完立即生效" },
        1, { "进入" },
        { 0 }, ACT_PROJECTS,
        .label_en  = "Projects",
        .desc_en   = { "Directory + agent CLI cards for new windows",
                       "Add and edit them here; changes apply at once" },
        .opts_en   = { "Open" },
        .label_ja  = "\xe3\x83\x97\xe3\x83\xad\xe3\x82\xb8\xe3\x82\xa7\xe3\x82\xaf\xe3\x83\x88", /* プロジェクト */
        .label_ko  = "\xed\x94\x84\xeb\xa1\x9c\xec\xa0\x9d\xed\x8a\xb8", /* 프로젝트 */
    },
    [SID_UI_LANG] = {
        "界面语言",
        { "本程序所有界面文字的语种",
          /* Must disambiguate from the on-screen 拼/EN key, which switches
           * what you TYPE, not what the program SAYS. */
          "与拼/EN 键的输入语种切换无关" },
        6, { "中文", "English",
             /* JA */ "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",            /* 日本語 */
             /* KO */ "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4",            /* 한국어 */
             /* DE */ "Deutsch",
             /* FR */ "Français" },
        /* No opt_desc here: the tip lookup falls back to desc[1], and
         * desc[1] *is* the message worth showing for every option. */
        .label_en  = "Interface language",
        .desc_en   = { "Language of every label in this program",
                       "Unrelated to the 拼/EN typing-mode key" },
        .opts_en   = { "Chinese", "English", "Japanese", "Korean",
                       "German", "French" },
        .label_ja  = "\xe7\x95\xaa\xe9\x9d\xa2\xe8\xa8\x80\xe8\xaa\x9e", /* 画面言語 */
        .desc_ja   = { "プログラム内全表示の言語",
                       /* 拼音/EN キーの入力言語切り替えとは無関係 */
                       "\xe6\x8b\xbc\xe9\x9f\xb3/EN \xe3\x82\xad\xe3\x83\xbc"
                       "\xe3\x81\xae\xe5\x85\xa5\xe5\x8a\x9b\xe8\xa8\x80\xe8\xaa\x9e"
                       "\xe5\x88\x87\xe3\x82\x8a\xe6\x9b\xbf\xe3\x81\x88\xe3\x81\xa8"
                       "\xe3\x81\xaf\xe7\x84\xa1\xe9\x96\xa2\xe4\xbf\x82" },
        .label_ko  = "\xed\x94\x84\xeb\xa1\x9c\xea\xb7\xb8\xeb\x9e\xa8 \xec\x96\xb8\xec\x96\xb4", /* 프로그램 언어 */
        .desc_ko   = { "\xed\x94\x84\xeb\xa1\x9c\xea\xb7\xb8\xeb\x9e\xa8 \xec\x95\x88\xec\x9d\x98"
                       " \xeb\xaa\xa8\xeb\x93\xa0 \xeb\xac\xb8\xec\x9e\x90 \xec\x96\xb8\xec\x96\xb4",
                       /* 한자/영어 키의 입력 언어 전환과 연관 없습니다 */
                       "\xed\x95\x9c\xec\x9e\x90/\xec\x98\x81\xec\x96\xb4 \xed\x82\xa4\xec\x9d\x98"
                       " \xec\x9e\x85\xeb\xa0\xa5 \xec\x96\xb8\xec\x96\xb4 \xec\xa0\x84\xed\x99\x98"
                       "\xea\xb3\xbc \xec\x97\xb0\xea\xb4\x80\xec\x97\x86\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4" },
        .label_de  = "Oberflächensprache",
        .desc_de   = { "Sprache aller Beschriftungen dieses Programms",
                       "Unabhängig von der 拼/EN Tippsprachen-Taste" },
        .label_fr  = "Langue de l'interface",
        .desc_fr   = { "Langue de chaque étiquette du programme",
                       "Indépendant de la touche 拼/EN (mode de saisie)" },
    },
};

/* ── language-aware text ──
 *
 * A row carries translations for every supported language (zh is the
 * canonical Chinese string and lives in the row's primary slot, not a
 * parallel array; the per-language twins are NULL where a translation
 * is missing).  set_lbl() picks the first non-NULL twin in priority
 * order — current language first, then walk back through the
 * fallbacks — so a row can never end up half-translated (label in one
 * language, options in another).  The NULL fallback is load-bearing
 * rather than tidy: utf8_disp_cols() walks its argument with no guard
 * and feeds the right-alignment maths, so a row with no twin must
 * resolve to a real string literal — a missing translation degrades
 * to a previous-language string, never to a NULL dereference.
 *
 * Supported languages (keep in sync with the LANG_* macros):
 *   0 Chinese    zh  default
 *   1 English    en
 *   2 Japanese   ja
 *   3 Korean     ko
 *   4 German     de
 *   5 French     fr
 */
/* Resolution order: target language first, then walk through the
 * others starting from English (which is most likely to be populated
 * even when the target isn't) and finally fall back to Chinese. */
static const char *set_lbl_list(const softkb_t *kb, int n,
                                const char *const *candidates) {
    if (!candidates) return NULL;
    int lang = kb ? kb->ui_language : LANG_ZH;
    if (lang < 0 || lang >= LANG_COUNT) lang = LANG_ZH;
    if (n > LANG_COUNT) n = LANG_COUNT;
    if (n <= 0) return NULL;
    /* Try the requested language first. */
    if (lang < n && candidates[lang]) return candidates[lang];
    /* Then walk English (1), then any remaining slot in declaration
     * order, ending with Chinese so a row missing every twin still
     * resolves to the literal. */
    static const int fallback_order[LANG_COUNT] = {
        LANG_EN, LANG_JA, LANG_KO, LANG_DE, LANG_FR, LANG_ZH
    };
    for (int i = 0; i < LANG_COUNT; i++) {
        int l = fallback_order[i];
        if (l >= n || l == lang) continue;
        if (candidates[l]) return candidates[l];
    }
    /* Last-ditch: even Chinese was NULL.  Walk forward to the first
     * non-NULL slot so the caller never gets a NULL deref. */
    for (int i = 0; i < n; i++)
        if (candidates[i]) return candidates[i];
    return NULL;
}

/* Wrap a primary Chinese string plus up to five per-language twins
 * into a transient array and hand it to set_lbl_list().  The array is
 * small and lives on the stack; the cost is a few pointer writes per
 * call, which is negligible compared to the snprintf/UTF-8 maths
 * these strings are about to feed. */
static const char *set_lbl(const softkb_t *kb, const char *zh,
                           const char *en, const char *ja, const char *ko,
                           const char *de, const char *fr) {
    const char *cands[LANG_COUNT] = { zh, en, ja, ko, de, fr };
    return set_lbl_list(kb, LANG_COUNT, cands);
}

/* Two-slot desc[] helper.  Same trick as set_lbl, applied to a row
 * with a primary Chinese slot and a parallel array of twins. */
static const char *set_lbl_desc2(const softkb_t *kb,
                                 const char *const *zh,
                                 const char *const *en, const char *const *ja,
                                 const char *const *ko,
                                 const char *const *de, const char *const *fr,
                                 int i) {
    if (i < 0 || i > 1) return NULL;
    const char *cands[LANG_COUNT];
    cands[LANG_ZH] = zh ? zh[i] : NULL;
    cands[LANG_EN] = en ? en[i] : NULL;
    cands[LANG_JA] = ja ? ja[i] : NULL;
    cands[LANG_KO] = ko ? ko[i] : NULL;
    cands[LANG_DE] = de ? de[i] : NULL;
    cands[LANG_FR] = fr ? fr[i] : NULL;
    return set_lbl_list(kb, LANG_COUNT, cands);
}

/* Eight-slot opts[] / opt_desc[] helper. */
static const char *set_lbl_arr5(const softkb_t *kb,
                                const char *const *zh,
                                const char *const *en, const char *const *ja,
                                const char *const *ko,
                                const char *const *de, const char *const *fr,
                                int i) {
    if (i < 0 || i >= SOFTKB_OPTS_MAX) return NULL;
    const char *cands[LANG_COUNT];
    cands[LANG_ZH] = zh ? zh[i] : NULL;
    cands[LANG_EN] = en ? en[i] : NULL;
    cands[LANG_JA] = ja ? ja[i] : NULL;
    cands[LANG_KO] = ko ? ko[i] : NULL;
    cands[LANG_DE] = de ? de[i] : NULL;
    cands[LANG_FR] = fr ? fr[i] : NULL;
    return set_lbl_list(kb, LANG_COUNT, cands);
}

static const char *item_label(const softkb_t *kb, const settings_item_t *it) {
    return set_lbl(kb, it->label, it->label_en, it->label_ja, it->label_ko,
                   it->label_de, it->label_fr);
}

static const char *item_desc(const softkb_t *kb, const settings_item_t *it,
                             int i) {
    const char *const zh[2] = { it->desc[0], it->desc[1] };
    return set_lbl_desc2(kb, zh, it->desc_en, it->desc_ja, it->desc_ko,
                         it->desc_de, it->desc_fr, i);
}

static const char *item_opt(const softkb_t *kb, const settings_item_t *it,
                            int i) {
    const char *const zh[SOFTKB_OPTS_MAX] = {
        it->opts[0], it->opts[1], it->opts[2],
        it->opts[3], it->opts[4], it->opts[5],
        it->opts[6], it->opts[7]
    };
    return set_lbl_arr5(kb, zh, it->opts_en, it->opts_ja, it->opts_ko,
                        it->opts_de, it->opts_fr, i);
}

/* The card's 2nd header line: this option's own tip, else the row's static
 * desc[1] — each picked in the language that resolved.
 *
 * 自定义 is the exception.  Its whole subject is a file the user wrote, so a
 * table literal could only ever be wrong about it: the tip reports what the
 * load found instead, formatted on the spot so it follows a language change
 * the same moment every other string on the page does. */
static const char *item_tip(softkb_t *kb, const settings_item_t *it, int i) {
    if (it == &settings_items[SID_THEME] && i == THEME_CUSTOM) {
        theme_ini_describe(&kb->theme_rep, kb->ui_language,
                           kb->theme_tip, sizeof(kb->theme_tip));
        return kb->theme_tip;
    }
    const char *tip = set_lbl_arr5(kb, it->opt_desc, it->opt_desc_en,
                                   it->opt_desc_ja, it->opt_desc_ko,
                                   it->opt_desc_de, it->opt_desc_fr, i);
    return tip ? tip : item_desc(kb, it, 1);
}

/* ── page-table verification ──
 *
 * This TU includes citro2d.h, so it can never join the host test suite: a
 * typo in the two tables above (a row listed twice, a group pointing at the
 * root, an item that fell off every page) compiles clean and turns into an
 * out-of-bounds settings_id_t read the first time someone opens that page.
 * So the invariants get asserted once at startup instead, and a violation
 * degrades to a flat list of every setting rather than to a corrupt index —
 * uglier, but always in range and always navigable.  Silent by design:
 * nothing in this binary writes to a console, and a one-screen settings
 * list with no groups is a symptom a developer notices immediately. */
static void settings_tables_flatten(void) {
    int n = 0;
    for (int id = 0; id < NSIDS && n < SET_PAGE_MAX; id++) {
        /* Groups are the only rows that vanish: with the pages gone there
         * is nothing for their 进入 button to open.  Leaves (ACT_NONE) and
         * the debug action both keep working with no page table at all. */
        if (act_page(settings_items[id].act) < 0)
            settings_pages[SETP_ROOT][n++] = (settings_id_t)id;
    }
    settings_page_n[SETP_ROOT] = n;
    for (int p = 1; p < SETP_COUNT; p++) settings_page_n[p] = 0;
}

static void settings_tables_check(void) {
    unsigned char seen[NSIDS];
    for (int id = 0; id < NSIDS; id++) seen[id] = 0;

    for (int p = 0; p < SETP_COUNT; p++) {
        int n = settings_page_n[p];
        if (n <= 0 || n > SET_PAGE_MAX) { settings_tables_flatten(); return; }
        for (int i = 0; i < n; i++) {
            int id = settings_pages[p][i];
            if (id < 0 || id >= NSIDS) { settings_tables_flatten(); return; }
            if (seen[id]++)            { settings_tables_flatten(); return; }
            const settings_item_t *it = &settings_items[id];
            if (!it->label || !it->desc[0] ||
                it->nopts < 1 || it->nopts > SOFTKB_OPTS_MAX) {
                settings_tables_flatten(); return;
            }
            for (int o = 0; o < it->nopts; o++)
                if (!it->opts[o]) { settings_tables_flatten(); return; }
            int page = act_page(it->act);
            if (it->act != ACT_NONE) {
                /* An action row is drawn as one button; a second option
                 * would never be reachable. */
                if (it->nopts != 1) { settings_tables_flatten(); return; }
                if (page < 0) continue;            /* ACT_DEBUG: fine */
                /* Groups live on the root only — L/R steps among the
                 * root's siblings, so a nested group would teleport the
                 * user between unrelated pages. */
                if (p != SETP_ROOT || page <= SETP_ROOT ||
                    page >= SETP_COUNT || settings_page_n[page] <= 0) {
                    settings_tables_flatten(); return;
                }
            } else if (page >= 0) {   /* unreachable: ACT_NONE ⇒ -1 */
                settings_tables_flatten(); return;
            }
        }
    }

    /* Every row must be reachable from some page. */
    for (int id = 0; id < NSIDS; id++)
        if (!seen[id]) { settings_tables_flatten(); return; }
}

/* ── page navigation ──
 * settings_sel is a row *within* the current page, so every read of the
 * selection goes through cur_sid() — nothing else may index the item table
 * with a raw settings_sel now that there are several pages. */
static settings_id_t cur_sid(const softkb_t *kb) {
    int page = kb->settings_page;
    int n    = SETP_COUNT;
    if (page < 0 || page >= n) page = SETP_ROOT;
    int rows = settings_page_n[page];
    int i    = kb->settings_sel;
    if (rows <= 0 || i < 0 || i >= rows) return SID_STICKY;
    return settings_pages[page][i];
}

/* Current option index of `id` (reverse-maps the backing field). */
static int settings_item_cur(const softkb_t *kb, settings_id_t id) {
    switch (id) {
    case SID_STICKY:      return kb->sticky_mode ? 1 : 0;
    case SID_DUAL_BOTTOM: return kb->dual_bottom_mode;
    case SID_THEME:       return (int)kb->theme_mode;
    case SID_KEY_SOUND:   return kb->key_sound_enabled ? 1 : 0;
    case SID_VOICE_MAX:   return kb->voice_max_sec == 15 ? 0
                         : kb->voice_max_sec == 60 ? 2 : 1;
    case SID_BOTTOM_INFO: return kb->bottom_info_mode;
    case SID_MASCOT:      return kb->mascot_mode;
    case SID_REPEAT:      return kb->repeat_style;
    case SID_UI_LANG:     return kb->ui_language;
    /* SID_VOICE_KEY deliberately has no case: since the R-only redesign it
     * is an informational row with a single option, and returning
     * voice_input_mode (which main.c resolves to 1) read opts[1] — NULL
     * one past a one-entry row.  A row's cur() must stay inside
     * opts[0 .. nopts-1]. */
    default:              return 0;   /* action + informational rows */
    }
}

/* Apply option `opt` to `id`; returns 1 iff the value changed (the
 * caller then marks settings dirty).  Action rows never change a value —
 * their caller performs the action itself. */
static int settings_item_set(softkb_t *kb, settings_id_t id, int opt) {
    switch (id) {
    case SID_STICKY: { int v = opt ? 1 : 0;
              if (v == kb->sticky_mode) return 0;
              kb->sticky_mode = v; return 1; }
    case SID_DUAL_BOTTOM:
            if (opt == kb->dual_bottom_mode) return 0;
            kb->dual_bottom_mode = opt; return 1;
    case SID_THEME:
            if ((int)kb->theme_mode == opt) return 0;
            kb->theme_mode = (term_theme_t)opt; return 1;
    case SID_KEY_SOUND: { int v = opt ? 1 : 0;
              if (v == kb->key_sound_enabled) return 0;
              kb->key_sound_enabled = v; return 1; }
    case SID_VOICE_MAX: { int v = opt == 0 ? 15 : opt == 2 ? 60 : 30;
              if (v == kb->voice_max_sec) return 0;
              kb->voice_max_sec = v; return 1; }
    case SID_BOTTOM_INFO:
            if (opt == kb->bottom_info_mode) return 0;
            kb->bottom_info_mode = opt; return 1;
    case SID_MASCOT:
            if (opt == kb->mascot_mode) return 0;
            kb->mascot_mode = opt; return 1;
    case SID_REPEAT:
            if (opt == kb->repeat_style) return 0;
            kb->repeat_style = opt; return 1;
    case SID_UI_LANG:
            if (opt == kb->ui_language) return 0;
            kb->ui_language = opt; return 1;
    /* SID_VOICE_KEY: no writer either — see settings_item_cur.  Tapping its
     * single button must not rewrite voice_input_mode and dirty the file. */
    default: return 0;
    }
}

/* Step the selected item's value by dir (±1), wrapping.  No-op on the
 * debug action item. */
static void settings_item_step(softkb_t *kb, settings_id_t id, int dir) {
    const settings_item_t *it = &settings_items[id];
    if (it->act != ACT_NONE || it->nopts <= 1) return;
    int cur = settings_item_cur(kb, id);
    int nxt = (cur + dir + it->nopts) % it->nopts;
    if (settings_item_set(kb, id, nxt)) kb->settings_dirty = 1;
}

/* Option-button geometry of the bottom detail card — single source for
 * the renderer and the hit test so they can never disagree.
 *
 * Stretches the row of nopts buttons from SET_OPT_Y0 down to
 * SET_OPT_BOT, choosing a per-item height that keeps the buttons
 * tall enough to look like real keys (≥ SET_OPT_MIN_H) and short
 * enough that even the worst case (5 options) doesn't overflow
 * (≤ SET_OPT_MAX_H).  Gap is a constant so the buttons stay evenly
 * spaced regardless of count. */
static void settings_opt_rect(const softkb_t *kb, int opt,
                              int *x, int *y, int *w, int *h) {
    int n    = settings_items[cur_sid(kb)].nopts;
    int room = SET_OPT_BOT - SET_OPT_Y0;
    int gap  = (n >= 8) ? 4 : SET_OPT_GAP;
    int h_   = (room - (n - 1) * gap) / n;
    if (h_ > SET_OPT_MAX_H) h_ = SET_OPT_MAX_H;
    if (h_ < 16) h_ = 16;
    /* Center the row vertically inside the available band so a smaller
     * item (e.g. 1 option) doesn't sit glued to the top. */
    int total = n * h_ + (n - 1) * gap;
    int y0    = SET_OPT_Y0 + (room - total) / 2;

    *x = SET_OPT_X;
    *y = y0 + opt * (h_ + gap);
    *w = SET_OPT_W;
    *h = h_;
}

static int settings_opt_hit(const softkb_t *kb, int tx, int ty, int opt) {
    int x, y, w, h;
    settings_opt_rect(kb, opt, &x, &y, &w, &h);
    return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

/* ── settings page navigation ──
 * B is the context key: inside a group it returns to the root, at the root
 * it closes the page.  L/R step between sibling groups — the page already
 * owns both shoulder keys while it is open (see `owned` below, and main.c's
 * window chord requires the settings page to be closed), so jumping
 * sideways needs no control drawn on screen either.  The only new widget a
 * group adds is the 进入 button an action row already has. */
static void settings_enter_page(softkb_t *kb, int page) {
    kb->settings_from_page = kb->settings_page;
    kb->settings_from_sel  = kb->settings_sel;
    kb->settings_page      = page;
    kb->settings_sel       = 0;
    kb->settings_rep_key   = 0;
}

/* Groups are pages 1..SETP_COUNT-1; the root is not one of them. */
static void settings_step_group(softkb_t *kb, int dir) {
    int g = SETP_COUNT - 1;
    int i = (kb->settings_page - 1 + dir) % g;
    kb->settings_page = 1 + (i + g) % g;
    kb->settings_sel  = 0;
    /* from_* deliberately untouched: every group returns to the root row
     * the user entered from, not one group back. */
    kb->settings_rep_key = 0;
}

static void settings_fire_action(softkb_t *kb, settings_id_t sid) {
    int act = settings_items[sid].act;
    if (act == ACT_DEBUG) {
        kb->debug_mode = 1;
        kb->settings_open = 0;
        return;
    }
    if (act == ACT_PROJECTS) {
        /* main.c owns the projects UI; here we just expose the gate.
         * main.c polls softkb_projects_open() and raises the page on that
         * flag — there is no "last fired row" handed over, so do not go
         * looking for one. */
        kb->projects_open = 1;
        kb->settings_open  = 0;
        return;
    }
    int page = act_page(act);
    if (page > SETP_ROOT && page < SETP_COUNT && settings_page_n[page] > 0)
        settings_enter_page(kb, page);
}

/* The whole keypad belongs to the settings page while it is open.
 * Returns the owned mask so main.c strips it from `down` AND `held`
 * for the rest of the frame — nothing may leak to any other handler of
 * these keys (hold-to-talk voice on R, arrow keys, the AI-ask chord
 * R+START or the START+L dual view).
 *
 * Note: ZR was previously in the owned mask so its old ZR-as-dual-view
 * trigger wouldn't fire while settings was open.  After ZR was retired
 * as a dual-screen trigger (M11.2), ZR is functionally inert and
 * intentionally left out of the mask — leaving it in would still leak-
 * stop nothing (no consumer reads it) and waste an entry.  If ZR ever
 * gets repurposed, add it back here.
 *
 * Returns 0 when the page isn't showing (debug mode has no settings).
 *
 * Keys: ↑/↓ move the selection within the page (wrapping), ←/→ step the
 * selected row's value, A steps too (or fires an action row), B backs out,
 * L/R switch groups while inside one.  Held D-pad keys auto-repeat on the
 * same ramp as soft keys. */
u32 softkb_settings_keys(softkb_t *kb, u32 down, u32 held) {
    if (!kb || !kb->settings_open || kb->debug_mode) return 0;

    const u32 owned = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT |
                      KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R |
                      KEY_ZL | KEY_START | KEY_SELECT;

    if (kb->settings_page < 0 || kb->settings_page >= SETP_COUNT)
        kb->settings_page = SETP_ROOT;
    int page = kb->settings_page;
    int rows = settings_page_n[page];
    /* A page with no rows is only reachable through the flattened fallback
     * — consume the keys but never run the wrapping maths on it. */
    if (rows <= 0) return owned;
    if (kb->settings_sel < 0 || kb->settings_sel >= rows)
        kb->settings_sel = 0;
    settings_id_t sid = cur_sid(kb);

    if (down & KEY_B) {
        if (page == SETP_ROOT) {
            kb->settings_open = 0;
        } else {
            int back = kb->settings_from_page;
            kb->settings_page = (back >= 0 && back < SETP_COUNT)
                              ? back : SETP_ROOT;
            kb->settings_sel  = kb->settings_from_sel;
        }
        kb->settings_rep_key = 0;
        return owned;
    }

    /* Down edge — priority mirrors keyboard.c's D-pad handling. */
    u32 nav = 0;
    if (down & KEY_DUP) {
        kb->settings_sel = (kb->settings_sel + rows - 1) % rows;
        nav = KEY_DUP;
    } else if (down & KEY_DDOWN) {
        kb->settings_sel = (kb->settings_sel + 1) % rows;
        nav = KEY_DDOWN;
    } else if (down & KEY_DLEFT) {
        settings_item_step(kb, sid, -1);
        nav = KEY_DLEFT;
    } else if (down & KEY_DRIGHT) {
        settings_item_step(kb, sid, +1);
        nav = KEY_DRIGHT;
    } else if (down & KEY_A) {
        if (settings_items[sid].act != ACT_NONE)
            settings_fire_action(kb, sid);
        else
            settings_item_step(kb, sid, +1);
        /* A deliberately never auto-repeats. */
    } else if (down & (KEY_L | KEY_R)) {
        /* Stepping groups is a one-shot move, never a repeat. */
        if (page != SETP_ROOT)
            settings_step_group(kb, (down & KEY_R) ? +1 : -1);
    }

    if (nav) {
        kb->settings_rep_key = nav;
        kb->settings_rep_frames = 0;
    } else if (down & owned) {
        kb->settings_rep_key = 0;   /* any other press cancels repeat */
    }

    /* Held frames: D-pad auto-repeat after the initial delay — deliberately
     * the same ramp the terminal's held D-pad uses, so 按键重复速度 makes
     * this page scroll faster too. */
    if (kb->settings_rep_key && (held & kb->settings_rep_key)) {
        kb->settings_rep_frames++;
        if (keyrepeat_fires(kb->repeat_style, kb->settings_rep_frames)) {
            if (kb->settings_rep_key == KEY_DUP)
                kb->settings_sel = (kb->settings_sel + rows - 1) % rows;
            else if (kb->settings_rep_key == KEY_DDOWN)
                kb->settings_sel = (kb->settings_sel + 1) % rows;
            else if (kb->settings_rep_key == KEY_DLEFT)
                settings_item_step(kb, cur_sid(kb), -1);
            else
                settings_item_step(kb, cur_sid(kb), +1);
        }
    } else if (kb->settings_rep_key) {
        kb->settings_rep_key = 0;
    }

    return owned;
}

const char *softkb_touch(softkb_t *kb,
                         keyboard_t *kbd,
                         int tx, int ty,
                         int pressed) {
    if (!kb || !kbd) return NULL;

    /* Down-edge derived from per-frame state — required because main.c
     * now passes the held flag (not the down-edge flag) so we can track
     * holds for auto-repeat. */
    int down_edge = pressed && !kb->prev_pressed;
    kb->prev_pressed = pressed;

    if (!pressed) {
        /* Release: clear repeat tracking and let the visual press-down
         * animation fade out naturally. */
        kb->repeat_idx = -1;
        kb->repeat_held_frames = 0;
        if (kb->pressed_idx >= 0) {
            kb->pressed_frames++;
            if (kb->pressed_frames > 14) kb->pressed_idx = -1;
        }
        return NULL;
    }
    if (tx < 0 || ty < 0) return NULL;

    /* All non-key widgets (badge, IME candidate strip) fire only on the
     * down-edge.  Subsequent held frames in those regions are swallowed
     * via repeat_idx = -2 so a swipe from there to a real key during the
     * same gesture also does nothing. */

    /* ── Right-slot button: single tap → toggle settings (or exit debug) ──
     * Only while the badge is actually on screen.  The settings page draws
     * its own full-screen card instead of the status row (see
     * draw_settings_pane), so on that page the badge's x 296..317 /
     * y 2..31 rect is invisible — and it sits right on top of the card's
     * right-aligned current-value text, which ends at x=308 (SET_OPT_X +
     * SET_OPT_W).  Hit-testing it there meant tapping the last two
     * characters of the value silently closed the page. */
    if (!kb->settings_open && badge_hit(tx, ty)) {
        if (down_edge) {
            if (kb->debug_mode) {
                kb->debug_mode = 0;
            } else {
                kb->settings_open = !kb->settings_open;
                if (kb->settings_open) {
                    /* Fresh open always starts at the root page — the
                     * selection is a row *within* a page, so it must be
                     * re-validated against that page, never trusted from
                     * whatever page happened to be showing last time. */
                    kb->settings_page = SETP_ROOT;
                    if (kb->settings_sel < 0 ||
                        kb->settings_sel >= settings_page_n[SETP_ROOT])
                        kb->settings_sel = 0;
                    kb->settings_rep_key = 0;
                }
            }
        }
        kb->repeat_idx = -2;
        return NULL;
    }

    /* ── Settings page consumes all taps while open ──
     * The bottom card shows the selected item's options as real
     * buttons; taps apply directly (no cycling).  Everything fires on
     * the down edge only, and every tap keeps the swallow-until-
     * release sentinel so the keyboard underneath never sees this
     * gesture. */
    if (kb->settings_open) {
        if (down_edge) {
            settings_id_t sid = cur_sid(kb);
            const settings_item_t *it = &settings_items[sid];
            if (it->act != ACT_NONE) {
                if (settings_opt_hit(kb, tx, ty, 0))
                    settings_fire_action(kb, sid);
            } else {
                for (int i = 0; i < it->nopts; i++) {
                    if (settings_opt_hit(kb, tx, ty, i)) {
                        if (settings_item_set(kb, sid, i))
                            kb->settings_dirty = 1;
                        break;
                    }
                }
            }
        }
        kb->repeat_idx = -2;
        return NULL;
    }

    /* ── Debug mode is display-only: swallow taps so they never reach
     * the keyboard underneath.  The ⚙ badge (handled above) is the only
     * way out. ── */
    if (kb->debug_mode) {
        kb->repeat_idx = -2;
        return NULL;
    }

    /* ── IME candidate strip: tap on a visible candidate commits it. ── */
    if (kb->ime && ime_active(kb->ime) && ty < STATUS_H) {
        if (down_edge) {
            for (int i = 0; i < kb->cand_box_n; i++) {
                if (tx >= kb->cand_box_x[i] &&
                    tx <  kb->cand_box_x[i] + kb->cand_box_w[i]) {
                    kb->repeat_idx = -2;
                    return ime_select(kb->ime, i);
                }
            }
        }
        kb->repeat_idx = -2;
        return NULL;
    }

    /* ── Normal keyboard mode: hit-test keys, dispatch a byte. ── */
    int idx = hit_test(kb, kbd, tx, ty);
    if (idx < 0) {
        if (down_edge) kb->pressed_idx = -1;
        kb->repeat_idx = -1;
        kb->repeat_held_frames = 0;
        return NULL;
    }

    int n;
    const softkey_t *layout = current_layout(kb, kbd, &n);
    const softkey_t *k = &layout[idx];

    /* Effective shift = on-screen ⇧ one-shot OR CapsLock (long-press ⇧).
     * Physical L no longer contributes (free for AI combo / future use). */
    int eff_shift = kb->caps_lock || kb->soft_shift || keyboard_shift_active(kbd);

    /* Decide whether *this* frame fires an emission.  Three cases:
     *   a) repeat_idx == -2  → swallow-until-release sentinel from a
     *      non-key widget tap or a recent page toggle.  No fire.
     *   b) idx != repeat_idx → fresh contact OR finger swiped to a new
     *      key.  Fire once, reset hold counter.
     *   c) idx == repeat_idx → held on the same key.  Fire only once
     *      the initial delay has elapsed and we're on a period boundary.
     */
    int fire = 0;
    if (kb->repeat_idx == -2) {
        return NULL;
    } else if (idx != kb->repeat_idx) {
        kb->repeat_idx         = idx;
        kb->repeat_held_frames = 0;
        kb->pressed_idx        = idx;
        kb->pressed_frames     = 0;
        fire = 1;
    } else {
        kb->repeat_held_frames++;
        /* ⇧ long-press → CapsLock.  While the one-shot ⇧ is armed
         * (soft_shift==1, caps==0), holding the same key for
         * CAPS_HOLD_FRAMES promotes it to a persistent lock. */
        if (k->kind == KIND_SHIFT && kb->soft_shift && !kb->caps_lock &&
            kb->repeat_held_frames >= CAPS_HOLD_FRAMES) {
            kb->caps_lock  = 1;
            kb->soft_shift = 0;
            keyboard_notify_event(kbd, "CAP");
            kb->repeat_idx = -2; /* swallow until release */
            return NULL;
        }
        if (k->kind == KIND_PAGE_BTN || k->kind == KIND_PAGE_TOGGLE ||
            k->kind == KIND_SHIFT) {
            /* Page toggles never auto-repeat — would whip the page back
             * and forth every few frames.  ⇧ is the same argument: every
             * firing flips soft_shift, so a held ⇧ visibly strobes the
             * one-shot on and off — and the frame the CapsLock test above
             * lands on would be set by the repeat ramp rather than by
             * CAPS_HOLD_FRAMES, which no style changes. */
        } else if (keyrepeat_fires(kb->repeat_style, kb->repeat_held_frames)) {
            fire = 1;
        }
    }
    if (!fire) return NULL;

    /* Key tap sound — when the mascot is enabled (mascot_mode > 0)
     * 功能键 (Enter/Backspace/IME/page/tab) and 修饰键 (Shift) each
     * play a species-tinted voice; character keys always use the base
     * click.  When the mascot is off the legacy mapping is preserved
     * (page + Shift silent, everything else base click). */
    if (kb->key_sound_enabled) {
        int mascot_on = kb->mascot_mode > 0;
        int cat;
        if (k->kind == KIND_SHIFT) {
            cat = mascot_on ? AUDIO_KEY_MOD : -1;
        } else if (k->kind == KIND_ENTER || k->kind == KIND_BACKSPACE ||
                   k->kind == KIND_IME_TOGGLE || k->kind == KIND_PAGE_BTN ||
                   k->kind == KIND_PAGE_TOGGLE) {
            if (!mascot_on &&
                (k->kind == KIND_PAGE_BTN || k->kind == KIND_PAGE_TOGGLE))
                cat = -1;
            else
                cat = AUDIO_KEY_FUNC;
        } else {
            /* KIND_CHAR, KIND_SPACE, KIND_SEQ.
             * Tab is the only KIND_SEQ that is a function key. */
            if (k->kind == KIND_SEQ && k->seq &&
                k->seq[0] == '\t' && k->seq[1] == '\0')
                cat = AUDIO_KEY_FUNC;
            else
                cat = AUDIO_KEY_CHAR;
        }
        if (cat >= 0) {
            mascot_species_t sp = softkb_mascot_species(kb);
            audio_play_key(cat, mascot_on, sp);
        }
    }

    /* True iff we should route this tap through the IME instead of
     * sending it raw.  CN mode + a-z key + no modifier → IME.
     * Modifiers (Shift/Ctrl/Alt) always bypass IME so Ctrl-C, Alt-b,
     * sticky-Ctrl, etc. still work in CN mode — hence *_active. */
    int route_to_ime =
        kb->ime &&
        keyboard_get_mode(kbd) == MODE_CN &&
        !eff_shift &&
        !keyboard_ctrl_active(kbd) &&
        !keyboard_alt_active(kbd);

    switch (k->kind) {
        case KIND_PAGE_TOGGLE:
            kb->page = (kb->page == PAGE_LETTERS) ? PAGE_SYMBOLS : PAGE_LETTERS;
            kb->soft_shift = 0;   /* ⇧ doesn't survive a page flip (iOS) */
            kb->repeat_idx = -2;
            return NULL;
        case KIND_PAGE_BTN:
            kb->page = k->page;   /* explicit destination from the table */
            kb->soft_shift = 0;   /* ⇧ doesn't survive a page flip (iOS) */
            kb->repeat_idx = -2;
            return NULL;
        case KIND_SHIFT:
            /* ⇧ tap = one-shot (next char only); long-press = CapsLock.
             * CapsLock persists until next tap of ⇧. */
            if (kb->caps_lock) {
                kb->caps_lock  = 0;
                kb->soft_shift = 0;
                keyboard_notify_event(kbd, "SFT");
                kb->repeat_idx = -2;
                return NULL;
            }
            kb->soft_shift = !kb->soft_shift;
            if (kb->soft_shift) {
                /* Armed one-shot — keep tracking so a continued hold can
                 * promote to CapsLock (handled in the held branch above). */
                keyboard_notify_event(kbd, "SFT");
                return NULL;
            } else {
                /* Disarmed one-shot */
                keyboard_notify_event(kbd, "SFT");
                kb->repeat_idx = -2;
                return NULL;
            }
        case KIND_BACKSPACE:
            keyboard_notify_event(kbd, "BSP");
            if (kb->ime && ime_active(kb->ime)) {
                ime_backspace(kb->ime);
                return NULL;
            }
            return "\x7f";
        case KIND_IME_TOGGLE: {
            /* 拼/EN — flip CN/EN.  Disarm the hold-repeat: this is a
             * one-shot toggle, not a key you hold.  Use the keyboard_t
             * we already hold (same one R used to toggle from).  Flash a
             * transient label so the user sees the mode flip.
             *
             * The status chip just shows the *destination* mode (C / E)
             * — the source key is irrelevant to the user once the press
             * landed; the on-screen 拼/EN key already declares itself
             * visually, and the chip is too narrow for "L→C".
             *
             * If the IME has an in-flight pinyin buffer when the user
             * taps 拼/EN, flush the currently-selected candidate first
             * (same as tapping Space in CN mode) so the toggle does not
             * strand a half-committed buffer.
             *
             * Why flush at all?  This matches the iOS / sogou / fcitx
             * convention: the user has not said "I changed my mind", so
             * the words they typed should reach the remote shell.  The
             * alternative — silently drop the buffer on toggle — would
             * lose every Chinese phrase the user accidentally mistyped
             * and then noticed after switching modes.  We keep the
             * toggle stateless from the keyboard_t's perspective
             * (keyboard_toggle_mode() only flips kbd->mode) and put the
             * "flush before toggle" policy here, the only place that
             * knows the IME buffer is on the same call site.
             *
             * softkb_touch() returns one const char* per call, so we
             * capture the candidate text first (caller's send_to_ssh
             * ships it on the same frame), then flip the mode and
             * return.  The IME buffer is empty by the time the next
             * tap arrives because ime_select_current() committed and
             * cleared it.
             * leave a half-committed buffer stranded.  We can only
             * return one buffer's worth of bytes per call, so the
             * toggle itself runs on the next frame. */
            const char *pending = NULL;
            if (kb->ime && ime_active(kb->ime)) {
                pending = ime_select_current(kb->ime);
            }
            keyboard_toggle_mode(kbd);
            ime_mode_t m = keyboard_get_mode(kbd);
            keyboard_notify_event(kbd, m == MODE_CN ? "C" : "E");
            kb->repeat_idx = -2;
            return pending;
        }
        case KIND_ENTER: {
            /* ↑ send: with an active pinyin buffer, commit the selected
             * candidate AND newline in one atomic write; otherwise a
             * plain terminal Enter. */
            if (kb->ime && ime_active(kb->ime)) {
                return keyboard_commit_enter(kbd);
            }
            return keyboard_enter_key(kbd);
        }
        case KIND_SEQ:
            /* Shift + Tab = "cursor backward tabulation" (CSI Z) — the
             * terminal-standard escape for shift-tab.  Catches the
             * common case (only seq we currently bind is "\t"); other
             * KIND_SEQ keys pass through verbatim. */
            if (eff_shift && k->seq
                && k->seq[0] == '\t' && k->seq[1] == '\0') {
                return "\x1b[Z";
            }
            return k->seq;
        case KIND_SPACE:
            /* In CN mode with an active pinyin buffer, space commits
             * the currently-selected candidate (defaults to the first
             * one until the user moves the cursor with D-pad ←→). */
            if (route_to_ime && ime_active(kb->ime)) {
                if (!keyboard_shift_held(kbd)) kb->soft_shift = 0;
                return ime_select_current(kb->ime);
            }
            if (!keyboard_shift_held(kbd)) kb->soft_shift = 0;
            return keyboard_emit_for_ex(kbd, k->base, eff_shift);
        case KIND_CHAR:
            if (route_to_ime && k->base >= 'a' && k->base <= 'z') {
                ime_input_letter(kb->ime, k->base);
                return NULL;
            }
            /* One-shot: the sticky ⇧ disarms after emitting one char. */
            if (!keyboard_shift_held(kbd)) kb->soft_shift = 0;
            /* Symbol pages are 键面即所得: pass shifted=0 so the face
             * always equals the byte emitted (every glyph there already
             * has a dedicated key).  Letters page keeps ⇧/Caps upper-
             * casing; Ctrl/Alt apply on every page via emit_for_ex. */
            return keyboard_emit_for_ex(kbd, k->base,
                                kb->page == PAGE_LETTERS ? eff_shift : 0);
    }
    return NULL;
}

/* ── rendering helpers ─────────────────────────────────────────────── */

static u32 rgba_to_c2d_(uint32_t rgba) {
    return C2D_Color32((rgba >> 24) & 0xff,
                       (rgba >> 16) & 0xff,
                       (rgba >>  8) & 0xff,
                        rgba        & 0xff);
}

/* main.c owns the bottom-screen background fill and the info bar, so it
 * needs four roles out of this palette.  They deliberately re-derive from
 * softkb_theme() instead of reading the frame-local g_pal: main.c calls
 * C2D_TargetClear() before softkb_draw(), so a cached pointer would be one
 * frame stale — and the keycap corner chamfer punched during softkb_draw
 * must equal the clear colour of the very same frame, or a theme switch
 * leaves a ring of stray pixels in every rounded corner.
 *
 * softkb_bottom_c2d_bg() is also the clear colour of the TOP panel while a
 * page owns both screens; see softkb.h for why the name still says
 * "bottom". */
uint32_t softkb_bottom_c2d_bg(const softkb_t *kb) {
    return rgba_to_c2d_(kb_pal_for(softkb_theme(kb))->bottom_bg);
}

uint32_t softkb_info_dim(const softkb_t *kb) {
    return kb_pal_for(softkb_theme(kb))->hint_fg;
}

uint32_t softkb_batt_warn_color(const softkb_t *kb) {
    return kb_pal_for(softkb_theme(kb))->batt_warn;
}

uint32_t softkb_batt_chg_color(const softkb_t *kb) {
    return kb_pal_for(softkb_theme(kb))->batt_chg;
}

uint32_t softkb_batt_crit_color(const softkb_t *kb) {
    return kb_pal_for(softkb_theme(kb))->batt_crit;
}

uint32_t softkb_tab_active_color(const softkb_t *kb) {
    return kb_pal_for(softkb_theme(kb))->label;
}

uint32_t softkb_unread_color(const softkb_t *kb) {
    return kb_pal_for(softkb_theme(kb))->batt_crit;
}

uint32_t softkb_chip_bg(const softkb_t *kb) {
    return kb_pal_for(softkb_theme(kb))->slot_bg;
}

/* The whole palette, for the pages that draw their own layout on the
 * bottom screen (the project card page needs the panel/ink/border roles,
 * not just the handful main.c's bottom bar reads).  Same freshness rule
 * as the getters above: re-derived from the live theme, never cached. */
const kb_pal_t *softkb_palette(const softkb_t *kb) {
    return kb_pal_for(softkb_theme(kb));
}

/* Press-down animation timing.  When a key is tapped, pressed_frames
 * counts up from 0; press_depth() turns that frame counter into a
 * pixel-offset and an interpolation factor used to smoothly translate
 * the key downward and blend its body color toward the press color.
 *
 *   0          PEAK              END
 *   |-----------|-----------------|
 *   depth: 0 → MAX               → 0
 *
 * 60 fps means PRESS_ANIM_FRAMES=14 ≈ 230 ms — slow enough to feel
 * like a key going down and snapping back, fast enough to keep up
 * with rapid typing. */
#define PRESS_ANIM_FRAMES  14
#define PRESS_ANIM_PEAK     6
#define PRESS_MAX_DEPTH     2

static int press_depth(int frames) {
    if (frames < 0 || frames >= PRESS_ANIM_FRAMES) return 0;
    if (frames <= PRESS_ANIM_PEAK) {
        /* down phase: 0 → MAX */
        return (frames * PRESS_MAX_DEPTH + PRESS_ANIM_PEAK / 2)
             / PRESS_ANIM_PEAK;
    }
    /* up phase: MAX → 0 */
    int up    = PRESS_ANIM_FRAMES - PRESS_ANIM_PEAK;
    int phase = PRESS_ANIM_FRAMES - frames;
    return (phase * PRESS_MAX_DEPTH + up / 2) / up;
}

/* Channel-wise lerp between two RGBA colors.  t / range → 0..1 weight
 * toward `b`.  Saturating, so out-of-range t just clamps. */
static uint32_t blend_rgba(uint32_t a, uint32_t b, int t, int range) {
    if (range <= 0 || t <= 0) return a;
    if (t >= range) return b;
    uint32_t ar = (a >> 24) & 0xff, ag = (a >> 16) & 0xff;
    uint32_t ab = (a >>  8) & 0xff, aa =  a        & 0xff;
    uint32_t br = (b >> 24) & 0xff, bg = (b >> 16) & 0xff;
    uint32_t bb = (b >>  8) & 0xff, ba =  b        & 0xff;
    uint32_t r  = (ar * (range - t) + br * t) / range;
    uint32_t g  = (ag * (range - t) + bg * t) / range;
    uint32_t bl = (ab * (range - t) + bb * t) / range;
    uint32_t al = (aa * (range - t) + ba * t) / range;
    return (r << 24) | (g << 16) | (bl << 8) | al;
}

/* 3-pixel triangular round-corner.  Punches the key's four corners with
 * the screen bg color so the rectangle looks rounded:
 *
 *   ###..      (row 0:  3 px)
 *   ##...      (row 1:  2 px)
 *   #....      (row 2:  1 px)
 *   .....
 *
 * 3 px is ~10% of the 32 px key width — the same proportion iOS uses
 * for its keyboard, and visibly more rounded than the previous 2-px
 * L-shaped chamfer. */
static void round_corners(int x, int y, int w, int h, float z) {
    uint32_t c = rgba_to_c2d_(COL_CHAMFER);
    /* TL */
    C2D_DrawRectSolid((float)x,       (float)y,       z, 3, 1, c);
    C2D_DrawRectSolid((float)x,       (float)(y + 1), z, 2, 1, c);
    C2D_DrawRectSolid((float)x,       (float)(y + 2), z, 1, 1, c);
    /* TR */
    C2D_DrawRectSolid((float)(x+w-3), (float)y,       z, 3, 1, c);
    C2D_DrawRectSolid((float)(x+w-2), (float)(y + 1), z, 2, 1, c);
    C2D_DrawRectSolid((float)(x+w-1), (float)(y + 2), z, 1, 1, c);
    /* BL */
    C2D_DrawRectSolid((float)x,       (float)(y+h-1), z, 3, 1, c);
    C2D_DrawRectSolid((float)x,       (float)(y+h-2), z, 2, 1, c);
    C2D_DrawRectSolid((float)x,       (float)(y+h-3), z, 1, 1, c);
    /* BR */
    C2D_DrawRectSolid((float)(x+w-3), (float)(y+h-1), z, 3, 1, c);
    C2D_DrawRectSolid((float)(x+w-2), (float)(y+h-2), z, 2, 1, c);
    C2D_DrawRectSolid((float)(x+w-1), (float)(y+h-3), z, 1, 1, c);
}

/* Render one key.  `depth` is 0 when at rest and grows up to
 * PRESS_MAX_DEPTH (2 px) at the peak of the press animation.  The
 * whole key — border, body, top highlight, label, corners — translates
 * downward by `depth`, mimicking a real keyboard cap going down.
 *
 *   - At rest (depth=0):  shadow under the key + top highlight strip
 *                          give the 3D lifted look.
 *   - Mid-press:           shadow fades, body slides down, color blends
 *                          toward the press color.
 *
 * Z layers (base 0.10, increments of 0.01):
 *   0.10  shadow strip below resting key
 *   0.12  border (dark stroke around body)
 *   0.15  body fill
 *   0.18  top highlight strip (resting only)
 *   0.21  rounded-corner chamfer
 */
static void draw_key_button(int x, int y, int w, int h,
                            uint32_t body, int depth) {
    int yd = y + depth;

    /* Resting drop shadow, fades out as depth grows. */
    if (depth == 0) {
        C2D_DrawRectSolid((float)(x + 1), (float)(y + h), 0.10f,
                          (float)(w - 2), 1,
                          rgba_to_c2d_(COL_KEY_BOT_SHADOW));
    }

    /* Border around the (possibly translated) body. */
    C2D_DrawRectSolid((float)x, (float)yd, 0.12f,
                      (float)w, (float)h,
                      rgba_to_c2d_(COL_KEY_BORDER));
    /* Body fill, inset 1 px from the border. */
    C2D_DrawRectSolid((float)(x + 1), (float)(yd + 1), 0.15f,
                      (float)(w - 2), (float)(h - 2),
                      rgba_to_c2d_(body));
    /* Top highlight only when resting (the sliver at the top of a
     * physical keycap that catches light). */
    if (depth == 0) {
        C2D_DrawRectSolid((float)(x + 1), (float)(yd + 1), 0.18f,
                          (float)(w - 2), 1,
                          rgba_to_c2d_(COL_KEY_TOP));
    }
    /* Corners follow the body's translated position. */
    round_corners(x, yd, w, h, 0.21f);
}

/* ── vector icons for ⇧ / ⌫ ───────────────────────────────────────
 * The 6×12 bitmap font draws U+21E7 / U+232B as a 6 px spindly glyph
 * clipped at the right cell edge (HALF/QUART layers are all 0,
 * `source/font_data.c:7` + `source/font_atlas.h:22`).  Inside a 46 px
 * key the result is tiny and off-center.  These rect-based icons are
 * 14 px wide with a 2 px stroke — visually matching the text keys and
 * the iOS reference image. */
static void draw_shift_icon(int rx, int ry, int rw, int rh,
                            u32 fg_rgba, int depth) {
    int cx = rx + rw / 2;
    int cy = ry + rh / 2 + depth;
    float z = 0.20f;
    u32 c = rgba_to_c2d_(fg_rgba);
    /* Filled up-arrow: 4-step head (2 px rows) + 6×5 stem. */
    C2D_DrawRectSolid((float)(cx - 1), (float)(cy - 7), z, 2, 2, c);
    C2D_DrawRectSolid((float)(cx - 3), (float)(cy - 5), z, 6, 2, c);
    C2D_DrawRectSolid((float)(cx - 5), (float)(cy - 3), z, 10, 2, c);
    C2D_DrawRectSolid((float)(cx - 7), (float)(cy - 1), z, 14, 2, c);
    C2D_DrawRectSolid((float)(cx - 3), (float)(cy + 1), z, 6, 5, c);
}



/* Centered text label.  Like draw_key_button, the label translates
 * downward by `depth` so it stays glued to the moving body. */
static int utf8_disp_cols(const char *s) {
    /* Labels are single glyphs in practice; count a wide (CJK) glyph as
     * 2 cells, everything else 1.  Only used for centering math.
     *
     * The step used to come from the lead byte alone, which walks off the
     * end of any string whose last sequence is truncated — and a byte cap
     * anywhere upstream splits a CJK glyph about two times in three.
     * Validate the continuation bytes and collapse to a one-byte step when
     * they are not there, the same rule source/utf8_next.c's utf8_next()
     * follows for the same reason.  Identical result for well-formed
     * text. */
    int cols = 0;
    while (*s) {
        unsigned char b = (unsigned char)*s;
        uint32_t cp;
        int adv;
        if      (b < 0x80) { cp = b;        adv = 1; }
        else if (b < 0xC0) { cp = 0xFFFD;   adv = 1; }
        else if (b < 0xE0) { cp = b & 0x1F; adv = 2; }
        else if (b < 0xF0) { cp = b & 0x0F; adv = 3; }
        else if (b < 0xF8) { cp = b & 0x07; adv = 4; }
        else               { cp = 0xFFFD;   adv = 1; }
        for (int i = 1; i < adv; i++) {
            unsigned char c = (unsigned char)s[i];
            if ((c & 0xC0) != 0x80) { cp = 0xFFFD; adv = 1; break; }
            cp = (cp << 6) | (c & 0x3F);
        }
        s += adv;
        cols += (cp >= 0x1100 && (cp <= 0x115F || cp >= 0x2E80)) ? 2 : 1;
    }
    return cols ? cols : 1;
}

static void draw_label(int rx, int ry, int rw, int rh,
                       const char *text, u32 fg_rgba, int depth) {
    if (!text) return;
    int tw = utf8_disp_cols(text) * CELL_W;   /* CJK labels center right */
    int x0   = rx + (rw - tw) / 2;
    int y0   = ry + (rh - CELL_H) / 2 + depth;
    renderer_draw_text_px(x0, y0, text, fg_rgba);
}

/* Resting (un-pressed) body fill color per key kind.  The press
 * animation blends from this toward COL_KEY_PRESSED rather than
 * snapping. */
static uint32_t key_body_color_resting(const softkey_t *k) {
    switch (k->kind) {
        case KIND_PAGE_BTN:    return COL_KEY_PG_BODY;
        case KIND_SPACE:       return COL_KEY_SPACE_BODY;
        case KIND_PAGE_TOGGLE: return COL_KEY_SPECIAL;
        case KIND_SEQ:         return COL_KEY_SPECIAL;
        /* ⇧ / ⌫ / ↑ — iOS-style accessory keys, slightly darker grey. */
        case KIND_SHIFT:
        case KIND_BACKSPACE:
        case KIND_ENTER:
        case KIND_IME_TOGGLE:  return COL_KEY_SPECIAL;
        case KIND_CHAR:
        default:               return COL_KEY_BODY;
    }
}

static uint32_t key_label_color_resting(const softkey_t *k) {
    if (k->kind == KIND_PAGE_BTN) return COL_KEY_PRESSED_FG; /* dark on bright */
    return COL_KEY_LABEL;
}

/* ── status / candidate row ─────────────────────────────────────────── */

#define COL_IME_PINYIN_FG     (kb_pal()->pinyin_fg)     /* matched pinyin chars */
#define COL_IME_PINYIN_NOMATCH (kb_pal()->pinyin_bad_fg) /* unmatched buffer tail */
#define COL_IME_CANDIDATE_FG  (kb_pal()->label)          /* main candidate text */
#define COL_IME_SELECTED_BG   (kb_pal()->cand_sel_bg)    /* alpha 0x66 */
#define COL_IME_PAGE_HINT     (kb_pal()->hint_fg)        /* "1/4" page indicator */

/* Lay out the IME bar: pinyin buffer + visible candidates within the
 * candidate strip.  Records hit-test boxes into kb->cand_box_*.
 * `strip_x` is the strip's left edge, `strip_end` is one past the
 * right edge.  Returns how many candidates were rendered. */
static int draw_ime_strip(softkb_t *kb,
                          int strip_x, int strip_end, int strip_y) {
    if (!kb->ime || !ime_active(kb->ime)) return 0;

    int x = strip_x + 4;
    int y = strip_y + (STATUS_H - 4 - CELL_H) / 2;

    /* Buffer display — split into matched (green) and unmatched (red)
     * portions so the user sees which part of their input still has
     * candidates and which trailing letters they should backspace. */
    const char *buf  = ime_buffer(kb->ime);
    int buf_len      = ime_buffer_len(kb->ime);
    int matched_len  = ime_matched_prefix_len(kb->ime);
    if (matched_len > 0) {
        char head[IME_BUFFER_MAX + 1];
        memcpy(head, buf, (size_t)matched_len);
        head[matched_len] = 0;
        renderer_draw_text_px(x, y, head, COL_IME_PINYIN_FG);
        x += matched_len * CELL_W;
    }
    if (matched_len < buf_len) {
        renderer_draw_text_px(x, y, buf + matched_len,
                              COL_IME_PINYIN_NOMATCH);
        x += (buf_len - matched_len) * CELL_W;
    }
    x += 6;  /* gap between buffer and candidates */

    /* Page hint "p/n" before the candidates if multi-page. */
    int page_count = ime_page_count(kb->ime);
    if (page_count > 1) {
        char hint[24];
        snprintf(hint, sizeof(hint), "%d/%d",
                 ime_page(kb->ime) + 1, page_count);
        renderer_draw_text_px(x, y, hint, COL_IME_PAGE_HINT);
        x += renderer_utf8_text_width_px(hint) + 6;
    }

    /* Lay out the current page's candidates left-to-right.  The
     * selection cursor (driven by D-pad ←→) gets a stronger
     * highlight so the user can see which one A/Space commits. */
    int n_in_page = ime_candidate_count(kb->ime);
    int sel       = ime_selection_idx(kb->ime);
    int n_drawn   = 0;
    for (int i = 0; i < n_in_page && n_drawn < IME_PAGE_SIZE; i++) {
        const char *cand = ime_candidate(kb->ime, i);
        if (!cand) break;
        int w = renderer_utf8_text_width_px(cand);
        if (x + w > strip_end - 2) break;
        if (i == sel) {
            C2D_DrawRectSolid((float)(x - 2), (float)(strip_y + 2),
                              0.072f, (float)(w + 4), (float)(STATUS_H - 8),
                              rgba_to_c2d_(COL_IME_SELECTED_BG));
        }
        renderer_draw_text_px(x, y, cand, COL_IME_CANDIDATE_FG);
        kb->cand_box_x[n_drawn] = x - 2;
        kb->cand_box_w[n_drawn] = w + 4;
        n_drawn++;
        x += w + 6;
    }
    kb->cand_box_n = n_drawn;
    return n_drawn;
}

/* Layout (left → right):
 *   [2..2+slot_w]              left slot  (status indicator)
 *   [slot_w+6..320-slot_w-8]   candidate strip
 *   [320-slot_w-2..318]        right slot (mode badge)
 *
 * slot_w = 3 chars * 6 px + 4 px padding = 22 px.
 * slot_h = STATUS_H - 4 (2 px top/bot margin).
 *
 * All labels rendered with renderer_draw_text_px so 3-char labels land
 * exactly on their geometric centers, no cell-grid snapping. */
static void draw_status_row(softkb_t *kb, renderer_t *r,
                            const keyboard_t *kbd) {
    const int slot_w = 3 * CELL_W + 4;     /* 22 px */
    const int slot_h = STATUS_H - 4;
    const int slot_y = 2;
    const int label_tw = 3 * CELL_W;       /* 18 px */
    const int label_y  = slot_y + (slot_h - CELL_H) / 2;

    /* Status row band (full width). */
    C2D_DrawRectSolid(0, 0, 0.05f, R_BOT_PX_W, STATUS_H,
                      rgba_to_c2d_(COL_STATUS_BG));

    /* Candidate strip between the two slots. */
    int strip_x   = slot_w + 6;
    int strip_end = R_BOT_PX_W - slot_w - 6;
    C2D_DrawRectSolid((float)strip_x, (float)slot_y, 0.06f,
                      (float)(strip_end - strip_x), (float)slot_h,
                      rgba_to_c2d_(COL_CANDIDATE_BG));

    /* IME candidates over the strip (when buffer non-empty). */
    kb->cand_box_n = 0;
    if (kb && kb->ime && ime_active(kb->ime)) {
        draw_ime_strip(kb, strip_x, strip_end, slot_y);
    }

    /* ── Left slot: status indicator ─────────────────────────────────── */
    /* Voice has highest priority — when REC / spinner / ERR is showing
     * we suppress the modifier label entirely.  When voice is IDLE we
     * fall through to keyboard_status_label() for SFT/CTL/ALT/etc. */
    const char *voice_lbl = (kb && kb->voice) ? voice_status_label(kb->voice) : NULL;
    uint32_t voice_bg = voice_lbl ? voice_status_bg(kb->voice) : 0;
    uint32_t voice_fg = voice_lbl ? voice_status_fg(kb->voice) : 0;

    const char *status = kbd ? keyboard_status_label(kbd) : "   ";
    int kbd_active = (status && strcmp(status, "   ") != 0);
    /* On-screen ⇧ is a sticky one-shot (soft_shift) or CapsLock.
     * While armed/locked, mirror the display so the user sees the
     * state in the same slot that flashes "BSP"/"ENT".  CapsLock
     * shows "CAP" to distinguish from the one-shot "SFT". */
    if (kb && kb->caps_lock && !voice_lbl && !kbd_active) {
        status = "CAP";
        kbd_active = 1;
    } else if (kb && kb->soft_shift && !voice_lbl && !kbd_active) {
        status = "SFT";
        kbd_active = 1;
    }

    /* Always-drawn slot bg keeps the layout visually anchored. */
    C2D_DrawRectSolid(2, (float)slot_y, 0.07f,
                      (float)slot_w, (float)slot_h,
                      rgba_to_c2d_(COL_MODE_LBL_BG));

    if (voice_lbl) {
        /* Voice tint over the slot. */
        if (voice_bg) {
            C2D_DrawRectSolid(2, (float)slot_y, 0.072f,
                              (float)slot_w, (float)slot_h,
                              rgba_to_c2d_(voice_bg));
        }
        int vx = 2 + (slot_w - label_tw) / 2;
        renderer_draw_text_px(vx, label_y, voice_lbl, voice_fg);
    } else if (kbd_active) {
        /* Highlight overlay: faint blue tint behind the active label. */
        C2D_DrawRectSolid(2, (float)slot_y, 0.072f,
                          (float)slot_w, (float)slot_h,
                          rgba_to_c2d_(COL_STATUS_HOLD_BG));
        int x0 = 2 + (slot_w - label_tw) / 2;
        renderer_draw_text_px(x0, label_y, status, COL_STATUS_FG_HOLD);
    }

    /* ── Right slot: settings button (replaces old ENG/CHN badge) ────── */
    {
        int rx = R_BOT_PX_W - slot_w - 2;
        C2D_DrawRectSolid((float)rx, (float)slot_y, 0.07f,
                          (float)slot_w, (float)slot_h,
                          rgba_to_c2d_(COL_MODE_LBL_BG));
        /* Sliders icon — two horizontal tracks with hollow-knob handles.
         *
         * 18×14 layout, centered in the 22×30 slot.  X = foreground, . = bg.
         *
         *   col: 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17
         *   row 0: .  X  X  X  X  X  .  .  .  .  .  .  .  .  .  .  .  .   ← top knob top
         *   row 1: .  X  .  .  .  X  X  X  X  X  X  X  X  X  X  X  .  .   ← knob + track
         *   row 2: .  X  .  .  .  X  X  X  X  X  X  X  X  X  X  X  .  .   ← knob + track
         *   row 3: .  X  .  .  .  X  .  .  .  .  .  .  .  .  .  .  .  .   ← knob sides
         *   row 4: .  X  X  X  X  X  .  .  .  .  .  .  .  .  .  .  .  .   ← top knob bot
         *   row 5-7: blank (3-row gap between the two sliders)
         *   row 8: .  .  .  .  .  .  .  .  .  .  .  .  .  X  X  X  X  X   ← bot knob top
         *   row 9: .  X  X  X  X  X  X  X  X  X  X  X  X  X  .  .  .  X   ← track + knob
         *   row10: .  X  X  X  X  X  X  X  X  X  X  X  X  X  .  .  .  X   ← track + knob
         *   row11: .  .  .  .  .  .  .  .  .  .  .  .  .  X  .  .  .  X   ← bot knob sides
         *   row12: .  .  .  .  .  .  .  .  .  .  .  .  .  X  X  X  X  X   ← bot knob bot
         *   row13: blank
         *
         * Reads as two stacked sliders — a "tune / settings" glyph
         * (top knob left, bottom knob right).  Each knob is a 5×5
         * hollow square; each track is 2 px thick and runs through
         * the knob's middle.  All blocks axis-aligned so the icon
         * stays crisp at 3DS native res without anti-aliasing. */
        u32 ic = rgba_to_c2d_(COL_KEY_LABEL);
        int cx = rx + slot_w / 2;       /* slot center x */
        int cy = slot_y + slot_h / 2;   /* slot center y */
        float iz = 0.072f;
        int ox = cx - 9;                /* icon origin x; 18 wide centered on cx */
        int oy = cy - 7;                /* icon origin y; 14 tall centered on cy */

        /* Top slider — knob on the left (cols 1-5), track extending right. */
        C2D_DrawRectSolid((float)(ox + 1), (float)(oy + 0), iz, 5, 1, ic);  /* knob top */
        C2D_DrawRectSolid((float)(ox + 1), (float)(oy + 4), iz, 5, 1, ic);  /* knob bot */
        C2D_DrawRectSolid((float)(ox + 1), (float)(oy + 1), iz, 1, 3, ic);  /* knob L */
        C2D_DrawRectSolid((float)(ox + 5), (float)(oy + 1), iz, 1, 3, ic);  /* knob R */
        C2D_DrawRectSolid((float)(ox + 6), (float)(oy + 1), iz, 11, 2, ic); /* track */

        /* Bottom slider — knob on the right (cols 12-16), track extending left. */
        C2D_DrawRectSolid((float)(ox + 12), (float)(oy + 8),  iz, 5, 1, ic);  /* knob top */
        C2D_DrawRectSolid((float)(ox + 12), (float)(oy + 12), iz, 5, 1, ic);  /* knob bot */
        C2D_DrawRectSolid((float)(ox + 12), (float)(oy + 9),  iz, 1, 3, ic);  /* knob L */
        C2D_DrawRectSolid((float)(ox + 16), (float)(oy + 9),  iz, 1, 3, ic);  /* knob R */
        C2D_DrawRectSolid((float)(ox + 1),  (float)(oy + 9),  iz, 11, 2, ic); /* track */
    }

    (void)r;  /* unused with the px path */
}

/* Dual-screen overlay — while both screens show the SSH terminal,
 * softkb_draw is never called, so the REC / spinner / ERR voice badge
 * would otherwise vanish.  This draws just that badge floating in the
 * bottom-left corner of the bottom terminal (10 px from the left edge,
 * 20 px from the bottom), on an opaque backing plate so the squeezed
 * terminal text underneath can't bleed through the translucent tint.
 * No-op when voice is IDLE.  Must be called inside
 * C2D_SceneBegin(bottom_target). */
void softkb_draw_voice_badge(const softkb_t *kb) {
    softkb_use_theme_(kb);
    if (!kb || !kb->voice) return;
    const char *lbl = voice_status_label(kb->voice);
    if (!lbl) return;

    const int slot_w = 3 * CELL_W + 4;   /* 22 px — matches status slot */
    const int slot_h = STATUS_H - 4;
    const int bx = 10;                   /* 10 px from the left edge */
    const int by = R_PANEL_PX_H - 20 - slot_h;   /* 20 px above the edge */
    const int label_tw = 3 * CELL_W;     /* 18 px */
    const int label_y  = by + (slot_h - CELL_H) / 2;

    /* Z layering matters here: the terminal behind us draws backgrounds at
     * z=0.1 and glyphs at z=0.3, so the plates must sit ABOVE 0.3 or the
     * depth test buries them under the terminal (only the z=0.5 label text
     * would survive, floating plate-less over busy terminal content).
     * Plates go at 0.40/0.42 — above the glyphs, below the 0.5 text. */
    C2D_DrawRectSolid((float)bx, (float)by, 0.400f,
                      (float)slot_w, (float)slot_h,
                      rgba_to_c2d_(COL_MODE_LBL_BG));

    uint32_t bg = voice_status_bg(kb->voice);
    if (bg) {
        C2D_DrawRectSolid((float)bx, (float)by, 0.420f,
                          (float)slot_w, (float)slot_h,
                          rgba_to_c2d_(bg));
    }
    int vx = bx + (slot_w - label_tw) / 2;
    renderer_draw_text_px(vx, label_y, lbl, voice_status_fg(kb->voice));
}

/* ── debug page ────────────────────────────────────────────────────── */

static void draw_debug_screen(softkb_t *kb, renderer_t *r,
                              const keyboard_t *kbd) {
    /* Solid background over the whole bottom screen. */
    C2D_DrawRectSolid(0, 0, 0.05f, R_BOT_PX_W, R_PANEL_PX_H,
                      rgba_to_c2d_(COL_STATUS_BG));

    /* Keep the status bar so the ENG/CHN badge is still visible/tappable
     * — that's how the user gets back out. */
    draw_status_row(kb, r, kbd);

    /* Title + exit hint */
    renderer_draw_text_px(8, 40, "DEBUG", COL_STATUS_FG_HOLD);
    renderer_draw_text_px(48, 40, "tap top-right badge to exit",
                          COL_STATUS_DIM);

    /* Recv hex strip — last up to 32 SSH-bound bytes, two lines of 16
     * with their byte indices for byte-level traceback. */
    renderer_draw_text_px(8, 60, "recv:", COL_KEY_LABEL);
    int hex_x = 8 + 6 * 6;
    int hex_y = 60;
    int show  = kb->recv_count;          /* 0..32 */
    int start = (kb->recv_head - show + 32) % 32;
    for (int i = 0; i < show; i++) {
        char hex[4];
        uint8_t b = kb->recv_ring[(start + i) % 32];
        snprintf(hex, sizeof(hex), "%02x", b);
        renderer_draw_text_px(hex_x, hex_y, hex, COL_KEY_LABEL);
        hex_x += 18;                     /* 2 chars + 1 char gap */
        if (hex_x > R_BOT_PX_W - 18) {
            hex_x = 8 + 6 * 6;
            hex_y += 14;
        }
    }
    if (show == 0) {
        renderer_draw_text_px(hex_x, hex_y, "(no SSH bytes yet)",
                              COL_STATUS_DIM);
    }

    /* Physical-key bindings legend.  Mirrors keyboard.c / main.c — keep
     * in sync.  L is the START+L dual-screen toggle modifier (not a
     * chord that toggles IME), R is voice hold-to-talk, the on-screen
     * 拼/EN key is the only IME-mode switch. */
    int kb_y = 100;
    renderer_draw_text_px(8, kb_y +  0,
        "\xe2\x87\xa7=Shift (soft)  Y=Ctrl  X=Alt", COL_KEY_LABEL);
    renderer_draw_text_px(8, kb_y + 14,
        "A=Enter (IME:emit pinyin as English)", COL_KEY_LABEL);
    renderer_draw_text_px(8, kb_y + 28,
        "B=Backsp  SELECT=Esc  R=hold=voice", COL_KEY_LABEL);
    renderer_draw_text_px(8, kb_y + 42,
        "START+L=dual view  R+START=AI ask", COL_KEY_LABEL);
    renderer_draw_text_px(8, kb_y + 56,
        "D-pad=arrows/IME  Space=commit", COL_KEY_LABEL);
    renderer_draw_text_px(8, kb_y + 70,
        "Circle=scrollback (TUI: PgUp/PgDn)", COL_KEY_LABEL);
    renderer_draw_text_px(8, kb_y + 84,
        "X+Y \xe2\x96\xb2=new \xe2\x96\xbc=close \xe2\x97\x80\xe2\x96\xb6/LR=switch", COL_KEY_LABEL);
}

static void draw_settings_pane(softkb_t *kb, renderer_t *r, const keyboard_t *kbd) {
    /* Bottom screen of the settings page: the full-screen detail card
     * for kb->settings_sel.  The item list itself is on the top screen
     * (softkb_draw_settings).  The status row is intentionally NOT
     * drawn here — the keyboard underneath is irrelevant on this
     * page and the only close gesture is physical B, so the card
     * gets the full 320×240. */
    C2D_DrawRectSolid(0, 0, 0.05f, R_BOT_PX_W, R_PANEL_PX_H,
                      rgba_to_c2d_(COL_STATUS_BG));
    draw_key_button(SET_CARD_X, SET_CARD_Y, SET_CARD_W, SET_CARD_H,
                    COL_CANDIDATE_BG, 0);

    settings_id_t sid = cur_sid(kb);
    const settings_item_t *it = &settings_items[sid];
    int cur_idx = settings_item_cur(kb, sid);
    const char *cur = item_opt(kb, it, cur_idx);

    renderer_draw_text_px(SET_OPT_X, SET_HEAD_TOP, item_label(kb, it),
                          COL_KEY_LABEL);
    int cw = utf8_disp_cols(cur) * CELL_W;
    renderer_draw_text_px(SET_OPT_X + SET_OPT_W - cw, SET_HEAD_TOP,
                          cur, COL_IME_PAGE_HINT);

    /* Separator under the header pair. */
    C2D_DrawRectSolid((float)SET_OPT_X, (float)SET_HEAD_SEP_Y, 0.16f,
                      (float)SET_OPT_W, 1, rgba_to_c2d_(COL_STATUS_DIM));

    renderer_draw_text_px(SET_OPT_X, SET_DESC_Y0, item_desc(kb, it, 0),
                          COL_IME_PAGE_HINT);
    /* 2nd line: tip of the currently selected value if the item has
     * per-option tips, else the static desc[1]. */
    const char *tip = item_tip(kb, it, cur_idx);
    if (tip)
        renderer_draw_text_px(SET_OPT_X, SET_DESC_Y1, tip, COL_IME_PAGE_HINT);

    /* One tappable button per option; the current value wears the
     * accent body + dark ink (same treatment as page-switch keys). */
    for (int i = 0; i < it->nopts; i++) {
        int x, y, w, h;
        settings_opt_rect(kb, i, &x, &y, &w, &h);
        int active = (i == cur_idx);
        draw_key_button(x, y, w, h,
                        active ? COL_KEY_PG_BODY : COL_KEY_BODY, 0);
        draw_label(x, y, w, h, item_opt(kb, it, i),
                   active ? COL_KEY_PRESSED_FG : COL_KEY_LABEL, 0);
    }
}

/* Top screen of the settings page: the rows of ONE page, the highlighted
 * one of which the bottom card details.  Called from main.c inside
 * C2D_SceneBegin(top) in place of the terminal while settings are open.
 *
 * The value column is read straight from the page table rather than via
 * cur_sid()/settings_opt_rect — both resolve the *selected* row, so using
 * them to lay out a non-selected row would read out of bounds. */
void softkb_draw_settings(softkb_t *kb, renderer_t *r) {
    softkb_use_theme_(kb);
    if (!kb || !r) return;

    if (kb->settings_page < 0 || kb->settings_page >= SETP_COUNT)
        kb->settings_page = SETP_ROOT;
    int page = kb->settings_page;
    int rows = settings_page_n[page];
    if (rows > SET_PAGE_MAX) rows = SET_PAGE_MAX;
    if (kb->settings_sel < 0 || kb->settings_sel >= rows)
        kb->settings_sel = 0;

    /* Opaque backdrop — the terminal is not drawn underneath. */
    C2D_DrawRectSolid(0, 0, 0.05f, R_TOP_PX_W, R_PANEL_PX_H,
                      rgba_to_c2d_(COL_STATUS_BG));

    /* Title band + 1 px accent underline.  A group appends its own name
     * so it always stays obvious which subset of settings is on screen —
     * the middle dot is already drawn elsewhere (it is a keyboard face
     * key), so it needs no new glyph in the atlas. */
    C2D_DrawRectSolid(0, 0, 0.06f, R_TOP_PX_W, SET_TOP_BAND_H,
                      rgba_to_c2d_(COL_CANDIDATE_BG));
    C2D_DrawRectSolid(0, SET_TOP_BAND_H - 1, 0.062f, R_TOP_PX_W, 1,
                      rgba_to_c2d_(COL_KEY_PG_BODY));
    char title[48];
    const char *app = set_lbl(kb, "\xe8\xae\xbe\xe7\xbd\xae",        /* 设置 */
                              "Settings",
                              "\xe8\xa8\xad\xe5\xae\x9a",              /* 設定 */
                              "\xec\x84\xa4\xec\xa0\x95",              /* 설정 */
                              "Einstellungen",
                              "Paramètres");
    if (settings_page_title[page])
        snprintf(title, sizeof(title), "%s \xc2\xb7 %s", app,
                 set_lbl(kb, settings_page_title[page],
                         settings_page_title_en[page],
                         settings_page_title_ja[page],
                         settings_page_title_ko[page],
                         settings_page_title_de[page],
                         settings_page_title_fr[page]));
    else
        snprintf(title, sizeof(title), "%s", app);
    renderer_draw_text_px(8, (SET_TOP_BAND_H - CELL_H) / 2, title,
                          COL_KEY_LABEL);

    for (int i = 0; i < rows; i++) {
        settings_id_t sid  = settings_pages[page][i];
        const settings_item_t *it = &settings_items[sid];
        int y = SET_LIST_Y0 + i * (SET_ROW_H + SET_ROW_GAP);
        int selected = (i == kb->settings_sel);
        if (selected) {
            C2D_DrawRectSolid(SET_LIST_X, (float)y, 0.07f,
                              SET_LIST_W, SET_ROW_H,
                              rgba_to_c2d_(COL_IME_SELECTED_BG));
            C2D_DrawRectSolid(SET_LIST_X, (float)y, 0.08f,
                              3, SET_ROW_H,
                              rgba_to_c2d_(COL_KEY_PG_BODY));
        }
        renderer_draw_text_px(SET_LIST_X + 8, y + (SET_ROW_H - CELL_H) / 2,
                              item_label(kb, it), COL_KEY_LABEL);
        if (it->act == ACT_NONE) {
            const char *val = item_opt(kb, it, settings_item_cur(kb, sid));
            int vw = utf8_disp_cols(val) * CELL_W;
            renderer_draw_text_px(SET_LIST_X + SET_LIST_W - vw - 8,
                                  y + (SET_ROW_H - CELL_H) / 2, val,
                                  selected ? COL_KEY_LABEL : COL_IME_PAGE_HINT);
        }
    }

    /* Key legend replacing the old in-card paging hint.  Per page, because
     * B means something different at the root than it does in a group. */
    const char *legend = (page == SETP_ROOT)
        ? set_lbl(kb,
                  "\xe2\x86\x91\xe2\x86\x93 \xe9\x80\x89\xe6\x8b\xa9"   /* ↑↓ 选择 */
                  " A \xe8\xbf\x9b\xe5\x85\xa5"                          /* A 进入 */
                  " B \xe9\x80\x80\xe5\x87\xba",                         /* B 退出 */
                  /* EN */ "\xe2\x86\x91\xe2\x86\x93 Select  A Open  B Close",
                  /* JA */ "\xe2\x86\x91\xe2\x86\x93 \xe9\x81\xb8\xe6\x8a\x9e" /* ↑↓ 選択 */
                          " A \xe5\x85\xa5\xe3\x82\x8b"                  /* A 入る */
                          " B \xe7\xb5\x82\xe3\x82\x8f\xe3\x82\x8a",     /* B 終わり */
                  /* KO */ "\xe2\x86\x91\xe2\x86\x93 \xec\x84\xa0\xed\x83\x9d" /* ↑↓ 선택 */
                          " A \xec\x9e\x85\xeb\xa0\x9b"                  /* A 입젹 */
                          " B \xeb\x82\x98\xea\xb0\x80\xea\xb8\xb0",     /* B 나가기 */
                  /* DE */ "\xe2\x86\x91\xe2\x86\x93 Auswahl  A Öffnen  B Schließen",
                  /* FR */ "\xe2\x86\x91\xe2\x86\x93 Sélect.  A Ouvrir  B Fermer")
        : set_lbl(kb,
                  "\xe2\x86\x91\xe2\x86\x93 \xe9\x80\x89\xe9\xa1\xb9"   /* ↑↓ 选项 */
                  " \xe2\x86\x90\xe2\x86\x92/A \xe8\xb0\x83\xe6\x95\xb4" /* ←→/A 调整 */
                  " B \xe8\xbf\x94\xe5\x9b\x9e\xe4\xb8\x8a\xe7\xba\xa7" /* B 返回上级 */
                  " LR \xe5\x88\x86\xe7\xbb\x84",                       /* LR 分组 */
                  /* EN */ "\xe2\x86\x91\xe2\x86\x93 Item  \xe2\x86\x90\xe2\x86\x92/A"
                          " Change  B Back  LR Group",
                  /* JA */ "\xe2\x86\x91\xe2\x86\x93 \xe9\xa0\x85\xe7\x9b\xae"
                          /* ↑↓ 項目 */
                          " \xe2\x86\x90\xe2\x86\x92/A \xe8\xaa\xbf\xe6\x95\xb4"
                          /* ←→/A 調整 */
                          " B \xe6\x88\xbb\xe3\x82\x8a"                  /* B 戻る */
                          " LR \xe3\x82\xb0\xe3\x83\xab\xe3\x83\xbc\xe3\x83\x97",
                          /* LR グループ */
                  /* KO */ "\xe2\x86\x91\xe2\x86\x93 \xed\x95\xad\xeb\xaa\xa9"
                          /* ↑↓ 항목 */
                          " \xe2\x86\x90\xe2\x86\x92/A \xec\xa1\xb0\xec\xa0\x95"
                          /* ←→/A 조정 */
                          " B \xeb\x8f\x8c\xec\x95\x84\xea\xb0\x80\xea\xb8\xb0"
                          /* B 돌아가기 */
                          " LR \xea\xb7\xb8\xeb\xa3\xb9",                /* LR 그룹 */
                  /* DE */ "\xe2\x86\x91\xe2\x86\x93 Eintrag  \xe2\x86\x90\xe2\x86\x92/A"
                          " Ändern  B Zurück  LR Gruppe",
                  /* FR */ "\xe2\x86\x91\xe2\x86\x93 Option  \xe2\x86\x90\xe2\x86\x92/A"
                          " Régler  B Retour  LR Groupe");
    renderer_draw_text_px((R_TOP_PX_W - utf8_disp_cols(legend) * CELL_W) / 2,
                          SET_LEGEND_Y, legend, COL_IME_PAGE_HINT);
}

/* ── public draw ───────────────────────────────────────────────────── */

/* Effective face label for a KIND_CHAR key on the letters page under
 * Shift — uppercase for a-z, face unchanged otherwise.  Symbol pages
 * never call this (they are shift-independent: face == emitted bytes),
 * so no digit/symbol pair table is needed here anymore.
 * Returned strings live in a static buffer / string literals — fine,
 * they are consumed by the draw call in the same frame. */
static const char *char_key_label(int shifted, const softkey_t *k) {
    static char buf[2];
    if (!shifted) return k->label;
    char c = k->base;
    if (c >= 'a' && c <= 'z') {
        buf[0] = (char)(c - 'a' + 'A');
        buf[1] = 0;
        return buf;
    }
    return k->label;
}

void softkb_draw(softkb_t *kb, renderer_t *r, const keyboard_t *kbd) {
    softkb_use_theme_(kb);
    if (!kb || !r) return;

    if (kb->debug_mode) {
        draw_debug_screen(kb, r, kbd);
        return;
    }
    if (kb->settings_open) {
        draw_settings_pane(kb, r, kbd);
        return;
    }

    draw_status_row(kb, r, kbd);

    int n;
    const softkey_t *layout = current_layout(kb, kbd, &n);
    int eff_shift = kbd && (kb->caps_lock || kb->soft_shift || keyboard_shift_active(kbd));
    for (int i = 0; i < n; i++) {
        const softkey_t *k = &layout[i];
        /* Only the actively-pressed key has nonzero depth/blend; every
         * other key renders at rest with depth=0, which makes both
         * blend_rgba calls below short-circuit to the resting color. */
        int depth = (i == kb->pressed_idx)
                  ? press_depth(kb->pressed_frames)
                  : 0;
        uint32_t resting_body = key_body_color_resting(k);
        /* Armed ⇧ / CapsLock lights up sky-blue so the state is obvious. */
        if (k->kind == KIND_SHIFT && (kb->soft_shift || kb->caps_lock))
            resting_body = COL_KEY_PG_BODY;
        uint32_t resting_lbl  = key_label_color_resting(k);
        uint32_t body = blend_rgba(resting_body, COL_KEY_PRESSED,
                                   depth, PRESS_MAX_DEPTH);
        uint32_t lbl  = blend_rgba(resting_lbl,  COL_KEY_PRESSED_FG,
                                   depth, PRESS_MAX_DEPTH);
        draw_key_button(k->x, k->y, k->w, k->h, body, depth);
        if (k->kind == KIND_SHIFT) {
            draw_shift_icon(k->x, k->y, k->w, k->h, lbl, depth);
        } else if (k->kind == KIND_SPACE) {
            /* No label on the space bar — the dedicated 拼/EN key already
             * declares the current mode in the same row, and adding a
             * 拼 hint here would be visual noise. */
        } else if (k->kind == KIND_IME_TOGGLE) {
            /* The IME toggle key shows the CURRENT mode, not a literal
             * "拼/EN": in CN mode it reads 拼 (you are typing Chinese),
             * in EN mode it reads EN (you are typing English).  A tap
             * flips the mode and the label with it. */
            const char *face = (kbd && keyboard_get_mode(kbd) == MODE_CN)
                               ? "\xe6\x8b\xbc" : "EN";
            draw_label(k->x, k->y, k->w, k->h, face, lbl, depth);
        } else {
            /* Character keys re-label under Shift on the LETTERS page
             * only (a→A); symbol pages are shift-independent, so the
             * face there is always literally what a tap emits. */
            const char *face = (k->kind == KIND_CHAR
                                && kb->page == PAGE_LETTERS)
                             ? char_key_label(eff_shift, k) : k->label;
            draw_label(k->x, k->y, k->w, k->h, face, lbl, depth);
        }
    }
}
