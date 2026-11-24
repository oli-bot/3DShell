/*
 * Host-side tests for sdmc:/3ds/3dssh/theme.ini — the one palette this
 * program builds from a file the user wrote, so the interesting cases are
 * all the ways a hand-edited file can be wrong:
 *
 *   1. a missing or useless file costs nothing: both THEME_CUSTOM slots keep
 *      rendering exactly like the default theme,
 *   2. the '#' rule — colour values are written '#rrggbb', so a '#' may only
 *      comment out a whole line, and the inline-comment behaviour config.c
 *      relies on must NOT leak in here,
 *   3. a bad value is refused, not clamped: the colour stays at its seed and
 *      the line gets counted,
 *   4. alpha is not the user's to choose — kb_theme.c's translucent-role
 *      invariant has to survive a file that says otherwise,
 *   5. the report line the settings card shows fits the card.
 *
 * Run with: make test-theme-ini
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/kb_theme.h"
#include "../source/terminal.h"
#include "../source/theme_ini.h"

/* terminal.c needs this one font-atlas query to link; the escape sequences
 * this suite writes are ASCII.  Same stub tools/test_terminal.c carries. */
int font_is_wide(uint32_t codepoint) {
    (void)codepoint;
    return 0;
}

static int failures = 0;
static int serial   = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; \
    } \
} while (0)

#define CHECKF(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: " fmt " (%s:%d)\n", __VA_ARGS__, __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

#define PAL(field) kb_pal_for(THEME_CUSTOM)->field
#define ALPHA(c)   ((c) & 0xffu)
#define DARK(i)    field_at(kb_pal_for(THEME_DARK), (i))

/* Every rejected key lands in a 16-byte display slot, so the same question
 * config.c and project_ui.c already answer applies here too: is what came
 * out of the cap still well-formed UTF-8? */
static int utf8_valid(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        size_t seq;
        if      ((*p & 0xE0) == 0xC0) seq = 2;
        else if ((*p & 0xF0) == 0xE0) seq = 3;
        else if ((*p & 0xF8) == 0xF0) seq = 4;
        else                          return 0;
        for (size_t i = 1; i < seq; i++)
            if ((p[i] & 0xC0) != 0x80) return 0;
        p += seq;
    }
    return 1;
}

/* A file on disk, because this parser is only interesting against fopen(). */
static const char *write_ini(const char *body) {
    static char path[64];
    snprintf(path, sizeof(path), "build/theme_ini_%d.ini", ++serial);
    FILE *fp = fopen(path, "w");
    if (!fp) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fputs(body, fp);
    fclose(fp);
    return path;
}

static uint32_t field_at(const kb_pal_t *p, int i) {
    return ((const uint32_t *)p)[i];
}

static int custom_matches_dark(void) {
    for (int i = 0; i < 22; i++)
        if (field_at(kb_pal_for(THEME_CUSTOM), i) != DARK(i)) return 0;
    return 1;
}

/* Display columns of a UTF-8 string under the bottom-screen font: a wide
 * glyph (CJK and friends) covers two cells.  Mirrors the renderer's rule and
 * the utf8_disp_cols() that softkb's card layout maths use, because the card
 * gives a tip one line of SET_OPT_W / CELL_W = 49 columns and drops whatever
 * hangs past the right edge. */
#define TIP_COLS 49
static int disp_cols(const char *s) {
    int cols = 0;
    while (*s) {
        unsigned char b = (unsigned char)*s;
        uint32_t cp = b;
        if (b >= 0xF0)      { cp = 0x10000u; s += 4; }
        else if (b >= 0xE0) { cp = ((uint32_t)(b & 0x0F) << 12) |
                                   ((uint32_t)(s[1] & 0x3F) << 6) |
                                    (uint32_t)(s[2] & 0x3F); s += 3; }
        else if (b >= 0xC0) { cp = ((uint32_t)(b & 0x1F) << 6) |
                                   (uint32_t)(s[1] & 0x3F); s += 2; }
        else                { s += 1; }
        cols += (cp >= 0x1100 && (cp <= 0x115F || cp >= 0x2E80)) ? 2 : 1;
    }
    return cols;
}

/* The 23 roles of kb_pal_t, spelled out here on purpose: a role renamed in
 * kb_theme.h would otherwise keep theme_ini.c's PK() table compiling while
 * quietly making that role unaddressable from theme.ini. */
static const char *const pal_roles[23] = {
    "bottom_bg", "panel_bg", "key_border", "key_body", "key_top",
    "key_shadow", "key_special", "key_space", "key_accent", "key_press",
    "label", "ink_dark", "hold_fg", "status_dim", "hold_bg",
    "slot_bg", "hint_fg", "pinyin_fg", "pinyin_bad_fg", "cand_sel_bg",
    "batt_warn", "batt_chg", "batt_crit",
};

int main(void) {
    CHECK(sizeof(kb_pal_t) == 23 * sizeof(uint32_t),
          "kb_pal_t is no longer 23 plain colours — update pal_roles[]");
    CHECK(THEME_COUNT == 5, "expected 5 theme slots, the 5th being THEME_CUSTOM");

    /* ── 0. Before anyone loads a file, slot 4 IS the default theme. ── */
    CHECK(custom_matches_dark(), "unloaded THEME_CUSTOM must equal DARK role for role");

    /* ── 1. A missing file is reported and changes nothing. ── */
    theme_ini_report_t r;
    CHECK(theme_ini_load("build/definitely-not-here.ini", &r) == THEME_INI_MISSING,
          "missing file should report MISSING");
    CHECK(r.applied == 0 && r.invalid == 0, "a missing file must count nothing");
    CHECK(custom_matches_dark(), "a missing file must leave the palette at the seed");

    terminal_t *t = terminal_init(80, 24);
    CHECK(t != NULL, "terminal_init");
    terminal_set_theme(t, THEME_CUSTOM);
    CHECK(terminal_get_bg(t) == 0x1a1b26ff, "missing file: term_bg stays DARK");
    CHECK(terminal_get_fg(t) == 0xc0caf5ff, "missing file: term_fg stays DARK");

    /* ── 2. The value forms a user will actually write. ── */
    const char *p;
    p = write_ini("bottom_bg = 112233\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "bare hex should load");
    CHECK(r.applied == 1 && r.invalid == 0, "one colour, nothing rejected");
    CHECK(PAL(bottom_bg) == 0x112233ff, "bare RRGGBB must be opaque");

    p = write_ini("bottom_bg = #112233\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK,
          "a '#' at the start of the VALUE must parse — that is the whole point");
    CHECK(PAL(bottom_bg) == 0x112233ff, "#RRGGBB read wrong");

    p = write_ini("bottom_bg = \"#112233\"\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "a quoted value must parse");
    CHECK(PAL(bottom_bg) == 0x112233ff, "quoted #RRGGBB read wrong");

    p = write_ini("bottom_bg = 11AABB\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "mixed case must parse");
    CHECK(PAL(bottom_bg) == 0x11aabbff, "hex must be case-insensitive");

    /* ── 3. Comments: only at the start of a line. ── */
    p = write_ini("# bottom_bg = 999999\n\n  \t# indented comment\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_EMPTY, "a file of comments holds nothing");
    CHECK(r.invalid == 0, "comments are not invalid entries");

    p = write_ini("panel_bg = 1e1e2e # the rest is NOT a comment\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_EMPTY,
          "an inline '#' must not be treated as a comment");
    CHECK(r.invalid == 1, "…but it must be reported as one invalid line");
    CHECK(r.bad_line == 1 && !strcmp(r.bad_key, "panel_bg"),
          "the report must name the line and the key");

    /* ── 4. Bad values are refused, not clamped toward the garbage. ── */
    p = write_ini("key_body = 3132\nkey_top = zzzzzz\nlabel = 1234567\n"
                  "ink_dark = cdd6f4 extra\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_EMPTY, "not one valid colour here");
    CHECK(r.invalid == 4, "four bad values must count four");
    CHECK(field_at(kb_pal_for(THEME_CUSTOM), 3) == DARK(3),
          "a rejected key must leave its field at the seed");

    p = write_ini("label = 123456789\n");        /* nine hex digits: 6 or 8 only */
    CHECK(theme_ini_load(p, &r) == THEME_INI_EMPTY, "nine hex digits must be refused");

    /* An unknown key counts too: a misspelled role name is the failure a
     * user can otherwise never see coming. */
    p = write_ini("botom_bg = 112233\nlabel = cdd6f4ff\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "the one good key still applies");
    CHECK(r.applied == 1 && r.invalid == 1, "an unknown key must count as invalid");
    CHECK(!strcmp(r.bad_key, "botom_bg"), "the unknown key must be named");

    /* ── 5. Alpha belongs to the role, not to the file. ── */
    p = write_ini("key_special = 45475aff\nhold_bg = 89b4fa01\n"
                  "cand_sel_bg = 89b4fa\nbottom_bg = 18182500\n"
                  "term_bg = 1a1b2680\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "a translucent file must still load");
    CHECK(ALPHA(PAL(key_special)) == 0x8b, "key_special is alpha 0x8b in every theme");
    CHECK(ALPHA(PAL(hold_bg))     == 0x50, "hold_bg is alpha 0x50 in every theme");
    CHECK(ALPHA(PAL(cand_sel_bg)) == 0x66, "cand_sel_bg is alpha 0x66 in every theme");
    CHECK(ALPHA(PAL(bottom_bg))   == 0xff,
          "bottom_bg must stay opaque — it is also the corner clear colour");
    CHECK(PAL(key_special) == 0x45475a8b, "hue from the file, alpha from the role");
    terminal_set_theme(t, THEME_DARK);
    terminal_set_theme(t, THEME_CUSTOM);
    CHECK(ALPHA(terminal_get_bg(t)) == 0xff, "term_bg alpha must be forced opaque");
    CHECK(terminal_get_bg(t) == 0x1a1b26ff, "term_bg hue must survive");

    /* ── 6. term_bg follows bottom_bg, and an omitted key reverts. ── */
    p = write_ini("bottom_bg = 223344ff\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "bottom_bg on its own");
    terminal_set_theme(t, THEME_DARK);
    terminal_set_theme(t, THEME_CUSTOM);
    CHECK(terminal_get_bg(t) == 0x223344ff,
          "bottom_bg alone must repaint both screens, as MATRIX/MONO/SOLAR do");
    CHECK(terminal_get_fg(t) == 0xc0caf5ff, "term_fg must stay at the DARK seed");

    p = write_ini("bottom_bg = 223344ff\nterm_bg = 556677ff\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "both background keys");
    terminal_set_theme(t, THEME_DARK);
    terminal_set_theme(t, THEME_CUSTOM);
    CHECK(terminal_get_bg(t) == 0x556677ff, "an explicit term_bg must win");

    /* Re-seeding is what makes that third load honest: a key the file stops
     * mentioning goes back to the default instead of lingering. */
    p = write_ini("bottom_bg = 223344ff\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "term_bg dropped from the file");
    terminal_set_theme(t, THEME_DARK);
    terminal_set_theme(t, THEME_CUSTOM);
    CHECK(terminal_get_bg(t) == 0x223344ff,
          "term_bg must follow bottom_bg again, not keep the last load's value");

    /* ── 7. The ANSI rows reach the screen through a real escape sequence. ── */
    p = write_ini("ansi1 = ff0000ff\nterm_fg = e0e0e0ff\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "ansi1 + term_fg");
    CHECK(r.applied == 2, "two colours applied");
    terminal_set_theme(t, THEME_DARK);
    terminal_set_theme(t, THEME_CUSTOM);
    terminal_reset(t);
    CHECK(terminal_get_cell(t, 0, 0).bg == 0x1a1b26ff,
          "with no bottom_bg in the file the terminal keeps the DARK background");
    terminal_write(t, "\x1b[31mR");
    CHECK(terminal_get_cell(t, 0, 0).fg == 0xff0000ff,
          "SGR 31 must pick up the custom ansi1");
    CHECK(terminal_get_cell(t, 0, 0).codepoint == 'R', "the glyph landed where expected");
    terminal_reset(t);
    terminal_write(t, "plain");
    CHECK(terminal_get_cell(t, 0, 0).fg == 0xe0e0e0ff, "term_fg feeds default-fg cells");

    /* ── 8. Every role is addressable by the name of its struct field. ── */
    {
        char body[4096];
        size_t used = 0;
        for (int i = 0; i < 23; i++)
            used += (size_t)snprintf(body + used, sizeof(body) - used,
                                     "%s = 010203\n", pal_roles[i]);
        CHECK(used < sizeof(body), "the 23-role body overflowed the test buffer");
        p = write_ini(body);
        CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "all 23 roles");
        CHECKF(r.applied == 23 && r.invalid == 0,
               "roles: applied %d want 23, invalid %d", r.applied, r.invalid);
        /* alpha-forced roles do not come back as 010203 */
        CHECK(PAL(key_body) == 0x010203ff, "an ordinary role takes the file's hue");
    }
    {
        char body[2048];
        size_t used = (size_t)snprintf(body, sizeof(body),
                                       "term_bg = 0a0b0c\nterm_fg = 0d0e0f\n");
        for (int i = 0; i < 16; i++)
            used += (size_t)snprintf(body + used, sizeof(body) - used,
                                     "ansi%d = %02x%02x%02x\n", i, i, i + 1, i + 2);
        CHECK(used < sizeof(body), "the terminal body overflowed the test buffer");
        p = write_ini(body);
        CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "the 18 terminal keys");
        CHECKF(r.applied == 18 && r.invalid == 0,
               "terminal keys: applied %d want 18, invalid %d", r.applied, r.invalid);
        terminal_set_theme(t, THEME_DARK);
        terminal_set_theme(t, THEME_CUSTOM);
        CHECK(terminal_get_bg(t) == 0x0a0b0cff, "term_bg slot");
        CHECK(terminal_get_fg(t) == 0x0d0e0fff, "term_fg slot");
    }

    /* ── 9. CRLF, the form a Mac or Windows editor writes. ── */
    p = write_ini("bottom_bg = 112233\r\nlabel = e0e0e0\r\n");
    CHECK(theme_ini_load(p, &r) == THEME_INI_OK, "a CRLF file must parse");
    CHECK(r.invalid == 0, "the carriage return must not invalidate a value");

    /* ── 10. The card tip has to fit the card, in both languages. ── */
    {
        char tip[160];
        theme_ini_report_t each[3];
        for (int i = 0; i < 3; i++) {
            memset(&each[i], 0, sizeof(each[i]));
            each[i].status = (theme_ini_status_t)i;
            each[i].applied = 1;
            for (int lang = 0; lang < 2; lang++) {
                /* clean, then with one rejection, then with the worst case
                 * the report can describe: three digits of line and a key
                 * long enough to fill the field. */
                theme_ini_describe(&each[i], lang, tip, sizeof(tip));
                CHECK(tip[0] != 0, "describe must produce a line");
                CHECKF(disp_cols(tip) <= TIP_COLS, "status %d lang %d clean: %s",
                       i, lang, tip);
                each[i].invalid = 3;
                each[i].bad_line = 256;
                memcpy(each[i].bad_key, "abcdefghijklmno", 15);
                each[i].bad_key[15] = 0;
                theme_ini_describe(&each[i], lang, tip, sizeof(tip));
                CHECKF(disp_cols(tip) <= TIP_COLS, "status %d lang %d worst: %s",
                       i, lang, tip);
                if (i == THEME_INI_MISSING)
                    CHECK(strstr(tip, "L256") == NULL,
                          "a file that is not there has no line to point at");
                else
                    CHECK(strstr(tip, "L256") != NULL,
                          "a rejected key must be named where the user can find it");
            }
        }
        theme_ini_describe(NULL, 0, tip, sizeof(tip));
        CHECK(tip[0] == 0, "a NULL report must produce an empty line, not a crash");
    }

    /* ── N. A key too long for the 16-byte slot must still come out as whole
     * characters.  "a" + five CJK glyphs is 16 bytes, so the 15-byte cap
     * falls one byte into the last glyph rather than on an edge. ── */
    p = write_ini("a\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89"
                  " = #112233\n");
    theme_ini_load(p, &r);
    CHECK(r.invalid == 1, "an unknown CJK key is one invalid entry");
    CHECK(utf8_valid(r.bad_key),
          "a key cut by the 16-byte slot stays valid UTF-8");
    CHECKF(strlen(r.bad_key) == 13,
           "the cut backs off to the last whole glyph (got %zu bytes)",
           strlen(r.bad_key));

    /* ── N+1. A file of exactly THEME_MAX_LINES lines is complete, not
     * truncated.  feof() is still 0 at that point even when the last line
     * ends in a newline, so "is there more?" has to be asked of the stream
     * rather than inferred from the EOF flag. ── */
    {
        char big[32 * 256 + 1];
        size_t o = 0;
        for (int i = 0; i < 256; i++)
            o += (size_t)snprintf(big + o, sizeof(big) - o,
                                  "bottom_bg = 1122%02x\n", i & 0xff);
        p = write_ini(big);
        theme_ini_load(p, &r);
        CHECKF(r.invalid == 0,
               "a file of exactly 256 lines is complete (invalid=%d)",
               r.invalid);
        CHECK(r.applied == 256, "all 256 lines of a full-size file apply");
    }

    terminal_free(t);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok: theme_ini — 41 keys, alpha forced, both screens\n");
    return 0;
}
