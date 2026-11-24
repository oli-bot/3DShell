/*
 * Host-side test for source/utf8_next.c — the UTF-8 decoder behind every
 * label helper in renderer.c, plus the text measurement built on top of
 * it.
 *
 * Two things are being pinned, and the second is the reason this file
 * exists:
 *
 *  1. The decoder rejects what Unicode excludes.  The version that used
 *     to be static in renderer.c accepted overlong forms (0xC0/0xC1
 *     leads), UTF-16 surrogate halves (0xED 0xA0..) and codepoints past
 *     U+10FFFF (0xF4 0x90.., 0xF5..0xF7 leads) — three families of byte
 *     sequence that no conforming producer emits, so on the 3DS the only
 *     way to notice was a garbage glyph from a malformed CJK candidate
 *     string.  Each case below asserts the replacement codepoint *and*
 *     the number of bytes consumed: advancing by the wrong amount is the
 *     failure that shifts the rest of a label, and it was the easiest to
 *     get right by accident.
 *
 *  2. renderer_utf8_text_width_px() and renderer_draw_text_px_scaled()
 *     must agree on the advance.  The width function is what the IME
 *     candidate strip and the window chip fit-test and centre against
 *     (renderer.h), so any disagreement lands centred text off-centre.
 *     t_width_matches_draw_advance() recomputes the draw side's step for
 *     every test string at several scales and demands equality.
 *
 * Every case decodes out of an exact-size heap block so AddressSanitizer
 * catches a decoder that walks past the terminator: the "never reads past
 * the NUL" guarantee in utf8_next.h is the difference between a bad glyph
 * and a heap overflow, and plain -O2 cannot tell them apart.
 *
 * Compile and run via tools/test_utf8_next.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/utf8_next.h"
/* FA_CELL_W lives here too — utf8_next.h already includes it, and it is
 * the single definition renderer.h spells FONT_CELL_W. */
#include "../source/font_atlas.h"

static int failures = 0;

#define FAIL(msg, ...) do { \
    fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); \
    failures++; \
} while (0)

/* ── helpers ──────────────────────────────────────────────────────── */

/* Exact-size heap copy: ASan's redzone sits one byte past the terminator,
 * so reading past the NUL is a heap-buffer-overflow rather than a pass. */
static char *box(const char *bytes, int len) {
    char *p = (char *)malloc((size_t)len + 1u);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
    memcpy(p, bytes, (size_t)len);
    p[len] = '\0';
    return p;
}

/* The advance renderer_draw_text_px_scaled() accumulates, reproduced from
 * renderer.c's loop (step = FONT_CELL_W * scale, wide takes two steps).
 * FONT_CELL_W *is* FA_CELL_W (renderer.h:9), so this is the draw side's
 * own constant, not a second copy of the number. */
static int draw_advance(const char *text, int scale) {
    int step = FA_CELL_W * scale;
    int adv = 0;
    const char *s = text;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        adv += font_is_wide(cp) ? step * 2 : step;
    }
    return adv;
}

/* ── 1. well-formed sequences ─────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *bytes;
    int         len;
    uint32_t    want_cp;
    int         want_adv;
} dec_case_t;

static const dec_case_t GOOD[] = {
    { "U+0000 (NUL)",           "\x00",          1, 0x0000,   1 },
    { "U+0041 'A'",             "A",             1, 0x0041,   1 },
    { "U+007F (1-byte max)",    "\x7F",          1, 0x007F,   1 },
    { "U+0080 (2-byte min)",    "\xC2\x80",      2, 0x0080,   2 },
    { "U+07FF (2-byte max)",    "\xDF\xBF",      2, 0x07FF,   2 },
    { "U+0800 (3-byte min)",    "\xE0\xA0\x80",  3, 0x0800,   3 },
    { "U+4E2D 中",         "\xE4\xB8\xAD",  3, 0x4E2D,   3 },
    { "U+D7FF (pre-surrogate)", "\xED\x9F\xBF",  3, 0xD7FF,   3 },
    { "U+E000 (post-surrogate)","\xEE\x80\x80",  3, 0xE000,   3 },
    { "U+FFFF (3-byte max)",    "\xEF\xBF\xBF",  3, 0xFFFF,   3 },
    { "U+10000 (4-byte min)",   "\xF0\x90\x80\x80", 4, 0x10000, 4 },
    { "U+1F600 emoji",          "\xF0\x9F\x98\x80", 4, 0x1F600, 4 },
    { "U+10FFFF (max codepoint)","\xF4\x8F\xBF\xBF", 4, 0x10FFFF, 4 },
};

/* ── 2. ill-formed sequences ──────────────────────────────────────── */

static const dec_case_t BAD[] = {
    /* stray continuation bytes */
    { "stray 0x80",             "\x80",          1, UTF8_REPLACEMENT, 1 },
    { "stray 0xBF",             "\xBF",          1, UTF8_REPLACEMENT, 1 },
    /* overlong 2-byte forms (a 1-byte codepoint in two bytes) */
    { "overlong C0 80 (NUL)",   "\xC0\x80",      2, UTF8_REPLACEMENT, 1 },
    { "overlong C0 AF ('/')",   "\xC0\xAF",      2, UTF8_REPLACEMENT, 1 },
    { "overlong C1 BF",         "\xC1\xBF",      2, UTF8_REPLACEMENT, 1 },
    /* overlong 3-byte forms */
    { "overlong E0 80 AF",      "\xE0\x80\xAF",  3, UTF8_REPLACEMENT, 1 },
    { "overlong E0 9F BF",      "\xE0\x9F\xBF",  3, UTF8_REPLACEMENT, 1 },
    /* UTF-16 surrogate halves — no such scalar value exists */
    { "surrogate ED A0 80 (D800)", "\xED\xA0\x80", 3, UTF8_REPLACEMENT, 1 },
    { "surrogate ED AF BF (DBFF)", "\xED\xAF\xBF", 3, UTF8_REPLACEMENT, 1 },
    { "surrogate ED B0 80 (DC00)", "\xED\xB0\x80", 3, UTF8_REPLACEMENT, 1 },
    { "surrogate ED BF BF (DFFF)", "\xED\xBF\xBF", 3, UTF8_REPLACEMENT, 1 },
    /* overlong 4-byte forms */
    { "overlong F0 80 80 AF",   "\xF0\x80\x80\xAF", 4, UTF8_REPLACEMENT, 1 },
    { "overlong F0 8F BF BF",   "\xF0\x8F\xBF\xBF", 4, UTF8_REPLACEMENT, 1 },
    /* past U+10FFFF */
    { "F4 90 80 80 (110000)",   "\xF4\x90\x80\x80", 4, UTF8_REPLACEMENT, 1 },
    { "F4 BF BF BF",            "\xF4\xBF\xBF\xBF", 4, UTF8_REPLACEMENT, 1 },
    { "F5 80 80 80 (140000)",   "\xF5\x80\x80\x80", 4, UTF8_REPLACEMENT, 1 },
    { "F6 80 80 80",            "\xF6\x80\x80\x80", 4, UTF8_REPLACEMENT, 1 },
    { "F7 BF BF BF (1FFFFF)",   "\xF7\xBF\xBF\xBF", 4, UTF8_REPLACEMENT, 1 },
    /* never-assigned leads */
    { "F8 88 80 80 80",         "\xF8\x88\x80\x80\x80", 5, UTF8_REPLACEMENT, 1 },
    { "FE",                     "\xFE",          1, UTF8_REPLACEMENT, 1 },
    { "FF",                     "\xFF",          1, UTF8_REPLACEMENT, 1 },
    /* truncated: the tail is missing, so the sequence ends at the NUL */
    { "truncated C3 <NUL>",     "\xC3",          1, UTF8_REPLACEMENT, 1 },
    { "truncated E4 B8 <NUL>",  "\xE4\xB8",      2, UTF8_REPLACEMENT, 2 },
    { "truncated E4 <NUL>",     "\xE4",          1, UTF8_REPLACEMENT, 1 },
    { "truncated F0 9F <NUL>",  "\xF0\x9F",      2, UTF8_REPLACEMENT, 2 },
    { "truncated F0 9F 98 <NUL>","\xF0\x9F\x98", 3, UTF8_REPLACEMENT, 3 },
    { "truncated F0 <NUL>",     "\xF0",          1, UTF8_REPLACEMENT, 1 },
    /* truncated by a non-continuation byte: consume the maximal subpart */
    { "E4 B8 'A'",              "\xE4\xB8\x41",  3, UTF8_REPLACEMENT, 2 },
    { "E4 'A'",                 "\xE4\x41",      2, UTF8_REPLACEMENT, 1 },
    { "F0 9F 98 'A'",           "\xF0\x9F\x98\x41", 4, UTF8_REPLACEMENT, 3 },
    { "F0 'A'",                 "\xF0\x41",      2, UTF8_REPLACEMENT, 1 },
};

static void run_dec(const dec_case_t *c) {
    char *buf = box(c->bytes, c->len);
    const char *s = buf;
    uint32_t cp = utf8_next(&s);
    size_t adv = (size_t)(s - buf);

    if (cp != c->want_cp)
        FAIL("%s: decoded U+%04X, want U+%04X", c->name, cp, c->want_cp);
    if (adv != (size_t)c->want_adv)
        FAIL("%s: advanced %zu byte(s), want %d", c->name, adv, c->want_adv);
    /* Never past the terminator, and never stuck. */
    if (adv < 1u)
        FAIL("%s: advanced 0 bytes — a caller looping on *s would spin",
             c->name);
    if (adv > (size_t)c->len)
        FAIL("%s: advanced %zu past the %d-byte buffer", c->name, adv,
             c->len);
    free(buf);
}

static void t_well_formed(void) {
    for (size_t i = 0; i < sizeof GOOD / sizeof GOOD[0]; i++) run_dec(&GOOD[i]);
}

static void t_ill_formed(void) {
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) run_dec(&BAD[i]);
}

/* Every one of the 256 possible lead bytes, with and without continuation
 * bytes after it: the decoder must always make progress, must never leave
 * the buffer, and must never claim more than four bytes.  This is the case
 * a hand-written table of "interesting" inputs misses. */
static void t_every_lead_byte_progresses(void) {
    for (int b = 0; b < 256; b++) {
        for (int tail = 0; tail < 2; tail++) {
            unsigned char raw[6];
            int len = 1;
            raw[0] = (unsigned char)b;
            if (tail) {
                raw[1] = raw[2] = raw[3] = 0x80;
                len = 4;
            }
            char *buf = box((const char *)raw, len);
            const char *s = buf;
            (void)utf8_next(&s);
            size_t adv = (size_t)(s - buf);
            if (adv < 1u || adv > (size_t)len || adv > 4u)
                FAIL("lead 0x%02X (tail=%d): advanced %zu, want 1..%d",
                     b, tail, adv, len);
            if (s != buf + (int)adv)
                FAIL("lead 0x%02X: *s not left at buf+adv", b);
            free(buf);
        }
    }
}

/* Decoding stops at the NUL: whatever follows an embedded terminator is
 * not part of the string, and must not be read. */
static void t_stops_at_embedded_nul(void) {
    static const char raw[] = { 'a', '\0', (char)0xC3, (char)0xA9, '\0' };
    const char *s = raw;
    uint32_t cp = utf8_next(&s);
    if (cp != 0x61)
        FAIL("embedded NUL: first codepoint U+%04X, want U+0041", cp);
    if (s != raw + 1)
        FAIL("embedded NUL: advanced %td, want 1", s - raw);
    if (*s != '\0') {
        FAIL("embedded NUL: decoder did not stop at the terminator");
        return;
    }
    /* A whole-string walk yields exactly the one codepoint before it. */
    int n = 0;
    s = raw;
    while (*s) { utf8_next(&s); n++; }
    if (n != 1)
        FAIL("embedded NUL: walked %d codepoints, want 1", n);
}

/* ── 3. width, and the contract with the draw path ────────────────── */

static const char *const STRINGS[] = {
    "",
    "a",
    "abc 123",
    "中",
    "中文",
    "a中b文c",
    "中a",
    "\xE2\x86\x92",                 /* → */
    "\xF0\x9F\x98\x80",             /* emoji */
    "hi \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x98\x80",   /* hi 世界 😀 */
};

/* The absolute rule, not just self-consistency: one cell per narrow
 * codepoint, two per wide one. */
static void t_width_rule(void) {
    if (renderer_utf8_text_width_px(NULL) != 0)
        FAIL("width(NULL) is not 0");
    if (renderer_utf8_text_width_px("") != 0)
        FAIL("width(\"\") is not 0");
    if (!font_is_wide(0x4E2D)) {
        FAIL("font_is_wide(U+4E2D) is false — the CJK cases below would "
             "not exercise the wide branch");
        return;
    }
    if (renderer_utf8_text_width_px("abc") != 3 * FA_CELL_W)
        FAIL("width(\"abc\") = %d, want %d",
             renderer_utf8_text_width_px("abc"), 3 * FA_CELL_W);
    if (renderer_utf8_text_width_px("\xE4\xB8\xAD\xE6\x96\x87") != 4 * FA_CELL_W)
        FAIL("width(\"中文\") = %d, want %d",
             renderer_utf8_text_width_px("\xE4\xB8\xAD\xE6\x96\x87"),
             4 * FA_CELL_W);
    if (renderer_utf8_text_width_px("a\xE4\xB8\xAD") != 3 * FA_CELL_W)
        FAIL("width(\"a中\") = %d, want %d",
             renderer_utf8_text_width_px("a\xE4\xB8\xAD"), 3 * FA_CELL_W);
}

/* If this disagrees with renderer_draw_text_px_scaled()'s step, anything
 * centred or right-aligned off renderer_utf8_text_width_px() lands in the
 * wrong place — which is the whole point of that function (renderer.h). */
static void t_width_matches_draw_advance(void) {
    static const int SCALES[] = { 1, 2, 3 };
    for (size_t i = 0; i < sizeof STRINGS / sizeof STRINGS[0]; i++) {
        const char *s = STRINGS[i];
        int w = renderer_utf8_text_width_px(s);
        for (size_t k = 0; k < sizeof SCALES / sizeof SCALES[0]; k++) {
            int scale = SCALES[k];
            int adv = draw_advance(s, scale);
            if (adv != w * scale)
                FAIL("advance mismatch for \"%s\" at scale %d: draw %d, "
                     "width_px*scale %d", s, scale, adv, w * scale);
        }
    }
}

/* Long input: no overflow, no early stop, no spin.  10k CJK codepoints is
 * ~5× any real candidate strip. */
static void t_long_input(void) {
    const int n = 10000;
    char *ascii = (char *)malloc((size_t)n + 1u);
    char *cjk   = (char *)malloc((size_t)n * 3u + 1u);
    if (!ascii || !cjk) { fprintf(stderr, "out of memory\n"); exit(2); }

    memset(ascii, 'a', (size_t)n);
    ascii[n] = '\0';
    char *p = cjk;
    for (int i = 0; i < n; i++) { *p++ = (char)0xE4; *p++ = (char)0xB8;
                                  *p++ = (char)0xAD; }
    *p = '\0';

    int w_ascii = renderer_utf8_text_width_px(ascii);
    int w_cjk   = renderer_utf8_text_width_px(cjk);
    if (w_ascii != n * FA_CELL_W)
        FAIL("10k ASCII: width %d, want %d", w_ascii, n * FA_CELL_W);
    if (w_cjk != n * 2 * FA_CELL_W)
        FAIL("10k CJK: width %d, want %d", w_cjk, n * 2 * FA_CELL_W);

    /* Every one of them decodes, and the walk terminates. */
    const char *s = cjk;
    int count = 0;
    while (*s) { if (utf8_next(&s) != 0x4E2D) { FAIL("10k CJK: bad decode");
                                                break; } count++; }
    if (count != n)
        FAIL("10k CJK: walked %d codepoints, want %d", count, n);

    /* A long run of the same illegal byte must not stall either. */
    char *junk = (char *)malloc((size_t)n + 1u);
    memset(junk, (char)0xF5, (size_t)n);
    junk[n] = '\0';
    s = junk;
    count = 0;
    while (*s) { if (utf8_next(&s) != UTF8_REPLACEMENT) {
                     FAIL("10k F5: expected U+FFFD"); break; } count++; }
    if (count != n)
        FAIL("10k F5: walked %d codepoints, want %d", count, n);

    free(ascii); free(cjk); free(junk);
}

int main(void) {
    t_well_formed();
    t_ill_formed();
    t_every_lead_byte_progresses();
    t_stops_at_embedded_nul();
    t_width_rule();
    t_width_matches_draw_advance();
    t_long_input();

    if (failures > 0) {
        fprintf(stderr, "\n%d failures\n", failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
