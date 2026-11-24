#include "theme_ini.h"
#include "kb_theme.h"
#include "terminal.h"
#include "utf8_fit.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* One line, and one file, of sanity: a binary dropped at this path must not
 * turn startup into a scan of megabytes.  A longer line simply arrives in
 * pieces, and every piece but the first fails to parse — which is reported
 * as an invalid entry rather than silently swallowed. */
#define THEME_LINE_SZ   256
#define THEME_MAX_LINES 256

/* ── the key table ───────────────────────────────────────────────────
 *
 * K_PAL keys carry the byte offset of their kb_pal_t field, and PK() takes
 * the field name twice — once to stringify, once to measure — so a key name
 * can never drift from the struct it fills.  Renaming or reordering a
 * palette role updates this table by construction. */
enum { K_PAL = 0, K_ANSI, K_TERM_BG, K_TERM_FG };

typedef struct {
    const char *key;
    uint8_t     kind;
    uint8_t     index;   /* ANSI slot, K_ANSI only */
    uint16_t    off;     /* offsetof(kb_pal_t, field), K_PAL only */
} theme_key_t;

#define PK(field) { #field, K_PAL, 0, (uint16_t)offsetof(kb_pal_t, field) }
#define AK(n)     { "ansi" #n, K_ANSI, (n), 0 }

static const theme_key_t theme_keys[] = {
    PK(bottom_bg),     PK(panel_bg),      PK(key_border),
    PK(key_body),      PK(key_top),       PK(key_shadow),
    PK(key_special),   PK(key_space),     PK(key_accent),
    PK(key_press),     PK(label),         PK(ink_dark),
    PK(hold_fg),       PK(status_dim),    PK(hold_bg),
    PK(slot_bg),       PK(hint_fg),       PK(pinyin_fg),
    PK(pinyin_bad_fg), PK(cand_sel_bg),   PK(batt_warn),
    PK(batt_chg),      PK(batt_crit),
    { "term_bg", K_TERM_BG, 0, 0 },
    { "term_fg", K_TERM_FG, 0, 0 },
    AK(0),  AK(1),  AK(2),  AK(3),  AK(4),  AK(5),  AK(6),  AK(7),
    AK(8),  AK(9),  AK(10), AK(11), AK(12), AK(13), AK(14), AK(15),
};
#define THEME_NKEYS ((int)(sizeof(theme_keys) / sizeof(theme_keys[0])))

static const theme_key_t *find_key(const char *k) {
    for (int i = 0; i < THEME_NKEYS; i++)
        if (!strcmp(theme_keys[i].key, k)) return &theme_keys[i];
    return NULL;
}

/* In-place, like config.c's: drop leading spaces by sliding, trailing
 * spaces and the newline by shrinking. */
static void trim_edges(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = 0;
}

/* ["]['#]RRGGBB[AA]["], case-insensitive.  Bounded and all-or-nothing: a
 * 7-digit run, a non-hex character, or trailing junk other than whitespace
 * and the matching quote is a rejection, never a partial read or a clamp —
 * guessing at a colour the file did not say is worse than keeping the one
 * already in the slot. */
static int parse_rgba(const char *v, uint32_t *out) {
    char quote = 0;
    if (*v == '\'' || *v == '"') { quote = *v; v++; }
    if (*v == '#') v++;

    uint32_t acc = 0;
    int n = 0;
    while (n < 8 && isxdigit((unsigned char)*v)) {
        unsigned char c = (unsigned char)*v;
        int d = (c <= '9') ? (c - '0') : ((c | 0x20) - 'a' + 10);
        acc = (acc << 4) | (uint32_t)d;   /* one character, one nibble */
        n++; v++;
    }
    if (n != 6 && n != 8) return 0;
    if (n == 6) acc = (acc << 8) | 0xffu;   /* RRGGBB → opaque */

    while (isspace((unsigned char)*v)) v++;
    if (quote) {
        if (*v != quote) return 0;
        v++;
        while (isspace((unsigned char)*v)) v++;
    }
    if (*v) return 0;
    *out = acc;
    return 1;
}

/* bad_key[] is a 16-byte display slot, and the key is whatever the user
 * typed — six CJK characters plus one Latin letter is exactly the shape
 * that gets cut mid-glyph.  utf8_fit() (see utf8_fit.h) backs the cut off
 * to the last whole codepoint so the settings card never renders a run of
 * replacement characters. */
static void note_invalid(theme_ini_report_t *r, int line, const char *key) {
    r->invalid++;
    if (!r->bad_key[0]) {
        r->bad_line = line;
        snprintf(r->bad_key, sizeof(r->bad_key), "%s", key);
        size_t fit = utf8_fit(r->bad_key, strlen(r->bad_key));
        r->bad_key[fit] = 0;
    }
}

theme_ini_status_t theme_ini_load(const char *path, theme_ini_report_t *report) {
    theme_ini_report_t r;
    memset(&r, 0, sizeof(r));

    /* Seed from the default theme, then overlay: every key the file omits
     * keeps a colour that is known to work rather than a zero. */
    kb_pal_t pal = *kb_pal_for(THEME_DARK);
    term_custom_pal_t tc;
    memset(&tc, 0, sizeof(tc));
    int saw_bottom_bg = 0, saw_term_bg = 0;

    FILE *fp = fopen(path ? path : THEME_INI_PATH, "r");
    if (!fp) {
        r.status = THEME_INI_MISSING;
        /* Still push the seeded palette: this is the call that makes
         * THEME_CUSTOM mean "the default" instead of "uninitialised". */
        kb_theme_set_custom(&pal);
        terminal_theme_set_custom(&tc);
        if (report) *report = r;
        return r.status;
    }

    char line[THEME_LINE_SZ];
    int lineno = 0;
    while (lineno < THEME_MAX_LINES && fgets(line, sizeof(line), fp)) {
        lineno++;
        size_t len = strlen(line);
        /* An unterminated over-long line: keep the first fragment, drop the
         * rest so the next fgets starts on a real line. */
        if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
            int c;
            while ((c = fgetc(fp)) != EOF && c != '\n') {}
        }
        trim_edges(line);
        if (!line[0] || line[0] == '#') continue;   /* blank or whole-line comment */

        char *eq = strchr(line, '=');
        if (!eq) continue;                          /* prose, not a key */
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim_edges(key);
        trim_edges(val);
        if (!key[0]) continue;

        const theme_key_t *k = find_key(key);
        if (!k) { note_invalid(&r, lineno, key); continue; }

        uint32_t v = 0;
        if (!parse_rgba(val, &v)) { note_invalid(&r, lineno, key); continue; }

        switch (k->kind) {
        case K_PAL:
            *(uint32_t *)((char *)&pal + k->off) = v;
            if (k->off == (uint16_t)offsetof(kb_pal_t, bottom_bg))
                saw_bottom_bg = 1;
            break;
        case K_TERM_BG: tc.bg = v;      tc.have_bg = 1; saw_term_bg = 1; break;
        case K_TERM_FG: tc.fg = v;      tc.have_fg = 1; break;
        case K_ANSI:    tc.ansi[k->index] = v; tc.have_ansi[k->index] = 1; break;
        }
        r.applied++;
    }
    /* "Is there more file?" cannot be inferred from feof() at this point:
     * a file of exactly THEME_MAX_LINES lines whose last one ends in a
     * newline has not hit EOF yet, so feof() is still 0 and every file of
     * exactly that length was reported as truncated.  Ask for one more
     * byte instead — that is the question actually being asked. */
    if (lineno == THEME_MAX_LINES && fgetc(fp) != EOF)
        note_invalid(&r, THEME_MAX_LINES, "(truncated)");
    fclose(fp);

    /* bottom_bg alone also sets the terminal clear colour.  That is what
     * MATRIX/MONO/SOLAR do (kb_theme.c takes their .bottom_bg straight from
     * theme_bg[]), so a user who writes one colour and not the other gets
     * the one-screen-covers-the-device result they asked for. */
    if (!saw_term_bg && saw_bottom_bg) {
        tc.bg = pal.bottom_bg;
        tc.have_bg = 1;
    }

    r.status = r.applied ? THEME_INI_OK : THEME_INI_EMPTY;
    kb_theme_set_custom(&pal);
    terminal_theme_set_custom(&tc);
    if (report) *report = r;
    return r.status;
}

void theme_ini_describe(const theme_ini_report_t *r, int lang,
                        char *out, size_t out_sz) {
    if (!out || !out_sz) return;
    out[0] = 0;
    if (!r) return;

    /* The budget is the settings card's one tip line: 49 display columns.
     * Every phrasing below has to still fit with " · L256 " and a 15-char key
     * hung on the end of it, which is what rules out the wordier
     * "…，显示默认配色" on the two statuses that can carry one. */
    switch (r->status) {
    case THEME_INI_MISSING:
        snprintf(out, out_sz, lang ? "theme.ini not found, default colors"
                                   : "theme.ini 未找到，显示默认配色");
        break;
    case THEME_INI_EMPTY:
        snprintf(out, out_sz, lang ? "theme.ini: nothing usable"
                                   : "theme.ini 无有效配色项");
        break;
    case THEME_INI_OK:
    default:
        if (r->invalid)
            snprintf(out, out_sz, lang ? "theme.ini: %d lines bad"
                                       : "theme.ini: %d 项无效",
                     r->invalid);
        else
            snprintf(out, out_sz, lang ? "Colors from theme.ini"
                                       : "配色来自 theme.ini");
        break;
    }

    /* Which one to go look at.  Pointless when the file is not there, and
     * "L<n>" reads in either language — the key name is the file's own
     * spelling, so translating it would only make it harder to search for. */
    if (r->status != THEME_INI_MISSING && r->invalid > 0 && r->bad_key[0]) {
        size_t used = strlen(out);
        if (used < out_sz)
            snprintf(out + used, out_sz - used, " \xc2\xb7 L%d %s",
                     r->bad_line, r->bad_key);
    }
}
