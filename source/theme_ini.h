#pragma once
/*
 * sdmc:/3ds/3dssh/theme.ini — user-supplied colours for the THEME_CUSTOM
 * slot of both palettes: the 22 bottom-screen roles and the top screen's
 * background, default foreground and 16 ANSI colours.
 *
 * A separate file rather than more settings.ini keys, because
 * settings_save() rewrites that file from scratch the first time any
 * setting changes — anything hand-written into it is erased.  theme.ini is
 * read-only as far as the app is concerned, which is also what lets it hold
 * forty colours without a palette editor in the UI.
 *
 * The comment rule, and the reason this parser is deliberately not
 * config.c's: a '#' opens a comment ONLY as the first non-blank character
 * of a line.  config.c strips an unquoted '#' anywhere, which would turn
 *     bottom_bg = #1a1b26
 * into an empty value — and colour files are exactly where a user pastes
 * that form out of a screenshot.  Everything else config.c does (its
 * quote-aware value unescaping) is not needed here, so the two parsers stay
 * independent instead of sharing a scanner whose one difference is wrong
 * for this file.
 *
 * This TU must stay free of citro3d/C2D includes so the host test
 * (make test-theme-ini) can run the scanner against real files.
 */
#include <stddef.h>
#include <stdint.h>

#define THEME_INI_PATH "sdmc:/3ds/3dssh/theme.ini"

typedef enum {
    THEME_INI_OK = 0,     /* at least one colour was applied */
    THEME_INI_MISSING,    /* no file at the path given */
    THEME_INI_EMPTY       /* read it, applied nothing */
} theme_ini_status_t;

typedef struct {
    theme_ini_status_t status;
    int   applied;        /* colours written into the THEME_CUSTOM slots */
    int   invalid;        /* unknown keys + values that could not be read */
    int   bad_line;       /* 1-based line of the first invalid entry, else 0 */
    /* Its key, truncated to fit; empty when there was none.  16 covers every
     * name this file accepts (the longest is pinyin_bad_fg) and is what keeps
     * the settings-card line inside its 49-column budget, so a misspelling
     * shows up shortened rather than pushing the rest off the card. */
    char  bad_key[16];
} theme_ini_report_t;

/* Read `path` (NULL → THEME_INI_PATH) and write what it holds into the
 * THEME_CUSTOM slots of kb_theme.c and terminal.c.  Call once at startup,
 * before settings_load(): even a missing file is worth calling for, because
 * that is what leaves both slots holding the default colours rather than
 * whatever the previous frame drew.
 *
 * Nothing degrades: a rejected key leaves its slot at the seeded value and
 * only bumps `invalid`, so a file with one typo in one colour still gets
 * nineteen working colours.  `report` may be NULL. */
theme_ini_status_t theme_ini_load(const char *path, theme_ini_report_t *report);

/* One line saying what the load did, in Chinese (lang 0) or English (lang 1),
 * e.g. "theme.ini: 2 项无效 · L12 bottom_bg".  Shared by the settings card
 * and the dim startup note in the terminal so the two can never describe the
 * same file differently.  Always writes something when `report` is non-NULL,
 * including for a clean load — the user chose 自定义 and deserves to know
 * where the colours came from. */
void theme_ini_describe(const theme_ini_report_t *report, int lang,
                        char *out, size_t out_sz);
