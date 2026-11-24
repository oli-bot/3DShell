/*
 * Host-side test for source/json_build.c — the write half of the AI-ask
 * request: the escaping that turns transcribed user speech and server
 * replies into the [["Q","A"], ...] history JSON voice.c ships to
 * dssh-whisper-shim --ask.
 *
 * Two things here are not "pin the current behaviour":
 *
 *  - t_history_cap_must_allow_for_escaping() reproduces the sizing bug the
 *    extraction exposed: voice.c sized the history buffer off the *input*
 *    length (64 KB) while a control byte costs six output bytes, so five
 *    full turns overflow it and json_build_history() refuses — killing the
 *    whole ask with "ai hist json" instead of sending it.
 *
 *  - t_half_glyph_is_forwarded_verbatim() pins that this module is a
 *    faithful escaper and *not* a sanitiser: a half glyph in comes out as
 *    a half glyph.  utf8_fit() therefore has to be applied at the byte cap
 *    that creates the cut (voice_ai_close_keep's snprintf into ai_hist_q /
 *    ai_hist_a), which is what the comment above utf8_fit's use in voice.c
 *    already claims happens.
 *
 * Injection cases are checked by re-parsing the output with a strict
 * reader (parse_history) rather than by strcmp against an expected string,
 * so a payload that adds an element or closes a bracket early has to
 * survive a real parse.
 *
 * Compile and run via tools/test_json_build.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/json_build.h"
#include "../source/utf8_fit.h"
/* For AI_HISTORY_MAX / AI_Q_MAX / AI_A_MAX: the test has to build history
 * entries the same size the product does, or it is checking a number that
 * does not exist at run time.  voice.h has no 3DS dependency (stdint.h
 * only), so it links here fine. */
#include "../source/voice.h"

static int failures = 0;

#define FAIL(msg, ...) do { \
    fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); \
    failures++; \
} while (0)

/* AI_HISTORY_MAX / AI_Q_MAX / AI_A_MAX come from voice.h.  They are
 * NUL-terminated arrays, so a full question/answer holds AI_Q_MAX-1 /
 * AI_A_MAX-1 bytes. */
#define PAIR_MAX  6
#define STR_MAX   4200

/* ── helpers ──────────────────────────────────────────────────────── */

/* Escape `src` into a heap block of exactly `cap` bytes, so ASan's
 * redzone sits one past the last byte the callee may touch.  Returns a
 * NUL-terminated copy on success, NULL on -1. */
static char *esc_in(const char *src, int cap, int *rc_out) {
    char *dst = (char *)malloc((size_t)(cap > 0 ? cap : 1));
    if (!dst) { FAIL("malloc"); return NULL; }
    memset(dst, '#', (size_t)(cap > 0 ? cap : 1));
    int p = 0;
    int rc = json_append_string(dst, cap, &p, src);
    *rc_out = rc;
    if (rc != 0) { free(dst); return NULL; }
    dst[p] = 0;
    return dst;
}

/* Decode one JSON string literal at *p into out.  Only the escapes
 * json_build emits are accepted; anything else is a parse failure. */
static int decode_str(const char **p, char *out, int cap) {
    if (**p != '"') return -1;
    (*p)++;
    int o = 0;
    while (**p && **p != '"') {
        unsigned char c = (unsigned char)**p;
        if (c == '\\') {
            (*p)++;
            char e = **p;
            (*p)++;
            unsigned char v;
            switch (e) {
                case '"':  v = '"';  break;
                case '\\': v = '\\'; break;
                case '/':  v = '/';  break;
                case 'b':  v = '\b'; break;
                case 'f':  v = '\f'; break;
                case 'n':  v = '\n'; break;
                case 'r':  v = '\r'; break;
                case 't':  v = '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *(*p)++;
                        cp <<= 4;
                        if      (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else return -1;
                    }
                    if (cp >= 0x80) return -1;   /* we never emit these */
                    v = (unsigned char)cp;
                    break;
                }
                default: return -1;
            }
            if (o < cap - 1) out[o++] = (char)v;
        } else {
            if (o < cap - 1) out[o++] = (char)c;
            (*p)++;
        }
    }
    if (**p != '"') return -1;
    (*p)++;
    out[o] = 0;
    return 0;
}

/* Accept ONLY `[["Q","A"], ...]` — no extra element, no trailing comma, no
 * trailing text — and hand back the decoded strings.  Returns the pair
 * count, or -1. */
static int parse_history(const char *s, char q[][STR_MAX],
                         char a[][STR_MAX], int max) {
    const char *p = s;
    if (*p != '[') return -1;
    p++;
    if (*p == ']') { p++; return (*p == 0) ? 0 : -1; }
    int n = 0;
    for (;;) {
        if (n >= max) return -1;
        if (*p != '[') return -1;
        p++;
        if (decode_str(&p, q[n], STR_MAX) != 0) return -1;
        if (*p != ',') return -1;
        p++;
        if (decode_str(&p, a[n], STR_MAX) != 0) return -1;
        if (*p != ']') return -1;
        p++;
        n++;
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; break; }
        return -1;
    }
    return (*p == 0) ? n : -1;
}

/* Build into a heap block of exactly `cap` bytes, NUL-terminate at the
 * returned length (json_build_history does not terminate) and return it. */
static char *build_nul(const json_history_entry_t *e, int n, int cap,
                       int *len_out) {
    char *raw = (char *)malloc((size_t)cap);
    if (!raw) { FAIL("malloc"); return NULL; }
    memset(raw, '#', (size_t)cap);
    int len = json_build_history(e, n, raw, cap);
    *len_out = len;
    if (len < 0) { free(raw); return NULL; }
    char *out = (char *)malloc((size_t)len + 1);
    if (!out) { FAIL("malloc"); free(raw); return NULL; }
    memcpy(out, raw, (size_t)len);
    out[len] = 0;
    free(raw);
    return out;
}

/* ── escaping ─────────────────────────────────────────────────────── */

static void t_escapes_quotes_and_backslash(void) {
    int rc;
    char *s = esc_in("a\"b", 32, &rc);
    if (rc != 0)      FAIL("quote: rc=%d", rc);
    else if (strcmp(s, "\"a\\\"b\"") != 0) FAIL("quote: %s", s);
    free(s);

    s = esc_in("a\\b", 32, &rc);
    if (rc != 0)      FAIL("backslash: rc=%d", rc);
    else if (strcmp(s, "\"a\\\\b\"") != 0) FAIL("backslash: %s", s);
    free(s);

    /* A trailing backslash must not be able to swallow the closing quote. */
    s = esc_in("end\\", 32, &rc);
    if (rc != 0)      FAIL("trailing backslash: rc=%d", rc);
    else if (strcmp(s, "\"end\\\\\"") != 0) FAIL("trailing backslash: %s", s);
    free(s);
}

static void t_escapes_named_controls(void) {
    int rc;
    char *s = esc_in("\b\f\n\r\t", 32, &rc);
    if (rc != 0)      FAIL("named controls: rc=%d", rc);
    else if (strcmp(s, "\"\\b\\f\\n\\r\\t\"") != 0) FAIL("named controls: %s", s);
    free(s);
}

/* Every C0 byte becomes \u00xx except the five with a JSON shorthand
 * (\b \f \n \r \t, covered above); 0x20 and 0x7f stay literal, both being
 * legal unescaped in a JSON string. */
static void t_escapes_every_c0_byte(void) {
    for (int c = 1; c < 0x20; c++) {
        if (c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t')
            continue;                       /* shorthand, not \u00xx */
        char src[2] = { (char)c, 0 };
        char want[10];
        snprintf(want, sizeof want, "\"\\u%04x\"", (unsigned)c);
        int rc;
        char *got = esc_in(src, 32, &rc);
        if (rc != 0) { FAIL("0x%02x: rc=%d", c, rc); }
        else if (strcmp(got, want) != 0)
            FAIL("0x%02x: got %s want %s", c, got, want);
        free(got);
    }
    int rc;
    char *sp = esc_in(" ", 32, &rc);
    if (rc != 0 || strcmp(sp, "\" \"") != 0) FAIL("space should stay literal: %s", sp);
    free(sp);
    char *del = esc_in("\x7f", 32, &rc);
    if (rc != 0 || strcmp(del, "\"\x7f\"") != 0) FAIL("0x7f should stay literal");
    free(del);
}

/* U+2028 / U+2029 are valid JSON but terminate a line in JS source.  The
 * consumer here is Python json.loads (tools/whisper_shim.py), so passing
 * them through raw is correct — pin it so a future change is deliberate. */
static void t_u2028_u2029_pass_through_raw(void) {
    int rc;
    char *s = esc_in("\xe2\x80\xa8\xe2\x80\xa9", 32, &rc);
    if (rc != 0)      FAIL("U+2028/9: rc=%d", rc);
    else if (strcmp(s, "\"\xe2\x80\xa8\xe2\x80\xa9\"") != 0)
        FAIL("U+2028/9 should not be \\u-escaped: %s", s);
    free(s);
}

/* A CJK glyph must survive as bytes, never as \uXXXX. */
static void t_utf8_is_not_uescaped(void) {
    const char ni[] = "\xe4\xbd\xa0\xe5\xa5\xbd";   /* 你好 */
    int rc;
    char *s = esc_in(ni, 32, &rc);
    if (rc != 0)      FAIL("CJK: rc=%d", rc);
    else if (strstr(s, "\\u") != NULL) FAIL("CJK was \\u-escaped: %s", s);
    else if (strstr(s, ni) == NULL)    FAIL("CJK bytes missing: %s", s);
    free(s);
}

/* The cut is all-or-nothing: either the whole glyph fits (rc 0) or the
 * call fails and leaves *pos alone, so a caller that checks rc never
 * ships half a glyph. */
static void t_exact_fit_never_emits_a_partial_glyph(void) {
    const char ni[] = "\xe4\xbd\xa0";               /* 你 */
    for (int cap = 3; cap <= 7; cap++) {
        char *dst = (char *)malloc((size_t)cap);
        memset(dst, '#', (size_t)cap);
        int p = 0;
        int rc = json_append_string(dst, cap, &p, ni);
        if (cap >= 6 && rc != 0)
            FAIL("cap=%d should fit \"你\" + NUL, rc=%d", cap, rc);
        if (cap < 6) {
            if (rc != -1) FAIL("cap=%d should overflow, rc=%d", cap, rc);
            if (p != 0)   FAIL("cap=%d overflow left *pos=%d, want 0", cap, p);
        }
        free(dst);
    }
    /* And when it does fit, the glyph is complete. */
    int rc;
    char *s = esc_in(ni, 6, &rc);
    if (rc != 0) FAIL("cap=6: rc=%d", rc);
    else if (strcmp(s, "\"\xe4\xbd\xa0\"") != 0) FAIL("cap=6: %s", s);
    free(s);
}

/* "Exactly one byte short" vs "exactly enough", for both the plain and
 * the escape path — cap counts the closing quote and the NUL slot. */
static void t_exact_fit_boundaries(void) {
    int rc;
    /* "ab" is 4 bytes → needs cap 5. */
    free(esc_in("ab", 4, &rc));
    if (rc != -1) FAIL("plain: cap=4 should overflow, rc=%d", rc);
    char *s = esc_in("ab", 5, &rc);
    if (rc != 0 || strcmp(s, "\"ab\"") != 0) FAIL("plain: cap=5 rc=%d s=%s", rc, s);
    free(s);

    /* "\u001f" is 8 bytes → needs cap 9. */
    free(esc_in("\x1f", 8, &rc));
    if (rc != -1) FAIL("escape: cap=8 should overflow, rc=%d", rc);
    s = esc_in("\x1f", 9, &rc);
    if (rc != 0 || strcmp(s, "\"\\u001f\"") != 0) FAIL("escape: cap=9 rc=%d s=%s", rc, s);
    free(s);
}

/* Sweep mixed content across every cap from empty to one over: the
 * success threshold must be exactly len+1 and nothing may be written
 * past the cap (ASan owns that half). */
static void t_cap_sweep_mixed_content(void) {
    const char src[] = "a\x1f\xe4\xbd\xa0";   /* a + \u001f + 你 → 12 bytes */
    for (int cap = 0; cap <= 14; cap++) {
        int rc;
        char *s = esc_in(src, cap, &rc);
        int want = (cap >= 13) ? 0 : -1;
        if (rc != want) FAIL("cap=%d rc=%d want %d", cap, rc, want);
        if (rc == 0 && strcmp(s, "\"a\\u001f\xe4\xbd\xa0\"") != 0)
            FAIL("cap=%d: %s", cap, s);
        free(s);
    }
}

/* Overflow must leave *pos where the caller left it, so the partial bytes
 * already in dst are never mistaken for output. */
static void t_overflow_leaves_pos_untouched(void) {
    char dst[16];
    memset(dst, '#', sizeof dst);
    int p = 3;
    if (json_append_string(dst, 4, &p, "aaaa") != -1) FAIL("expected -1");
    if (p != 3) FAIL("*pos moved to %d on overflow", p);
}

/* ── history assembly ─────────────────────────────────────────────── */

static void t_history_shape(void) {
    int len;
    char *s = build_nul(NULL, 0, 64, &len);
    if (!s)          FAIL("empty history: rc=%d", len);
    else if (strcmp(s, "[]") != 0) FAIL("empty history: %s", s);
    free(s);

    json_history_entry_t e[2] = { { "q1", "a1" }, { "q2", "a2" } };
    s = build_nul(e, 2, 256, &len);
    if (!s)          FAIL("two pairs: rc=%d", len);
    else if (strcmp(s, "[[\"q1\",\"a1\"],[\"q2\",\"a2\"]]") != 0)
        FAIL("two pairs: %s", s);
    free(s);
}

/* A history entry carrying " or \ must not be able to add an element,
 * close a bracket early or break out of the array. */
static void t_injection_cannot_break_the_structure(void) {
    json_history_entry_t e[2] = {
        { "say \"hi\", ok", "\\" },
        { "\"],[\"injected", "a\\\",b" },
    };
    static char q[PAIR_MAX][STR_MAX], a[PAIR_MAX][STR_MAX];
    int len;
    char *s = build_nul(e, 2, 512, &len);
    if (!s) { FAIL("injection build: rc=%d", len); return; }
    int n = parse_history(s, q, a, PAIR_MAX);
    if (n != 2) FAIL("injection broke the structure (n=%d): %s", n, s);
    else {
        if (strcmp(q[0], "say \"hi\", ok") != 0) FAIL("q0 = %s", q[0]);
        if (strcmp(a[0], "\\") != 0)             FAIL("a0 = %s", a[0]);
        if (strcmp(q[1], "\"],[\"injected") != 0) FAIL("q1 = %s", q[1]);
        if (strcmp(a[1], "a\\\",b") != 0)        FAIL("a1 = %s", a[1]);
    }
    free(s);
}

/* Text that already looks like an escape must come back as the literal
 * text, not as the character it spells. */
static void t_unicode_escape_lookalike_is_not_decoded(void) {
    json_history_entry_t e[1] = { { "\\u0041", "\\n" } };
    static char q[PAIR_MAX][STR_MAX], a[PAIR_MAX][STR_MAX];
    int len;
    char *s = build_nul(e, 1, 256, &len);
    if (!s) { FAIL("lookalike build: rc=%d", len); return; }
    int n = parse_history(s, q, a, PAIR_MAX);
    if (n != 1) FAIL("lookalike parse: n=%d", n);
    else {
        if (strcmp(q[0], "\\u0041") != 0) FAIL("q0 decoded to %s, want \\u0041", q[0]);
        if (strcmp(a[0], "\\n") != 0)     FAIL("a0 decoded to %s, want \\n", a[0]);
    }
    free(s);
}

/* The defect this extraction was for.  voice.c sized the history buffer
 * at 64 KB "for JSON", which is the *input* size — but a control byte
 * costs six output bytes, so five full turns of AI_Q_MAX + AI_A_MAX need
 * ~138 KB.  Overflow makes json_build_history() return -1, which
 * begin_transcribe turns into enter_error("ai hist json"): the user's ask
 * never leaves the console.  Control bytes do reach a history entry,
 * because a reply the parser cannot read is dumped raw into ai_answer. */
static void t_history_cap_must_allow_for_escaping(void) {
    static char qs[AI_HISTORY_MAX][AI_Q_MAX];
    static char as[AI_HISTORY_MAX][AI_A_MAX];
    json_history_entry_t e[AI_HISTORY_MAX];
    for (int i = 0; i < AI_HISTORY_MAX; i++) {
        memset(qs[i], 0x1f, AI_Q_MAX - 1);   /* worst case: 6 bytes out each */
        memset(as[i], 0x1f, AI_A_MAX - 1);
        qs[i][AI_Q_MAX - 1] = 0;
        as[i][AI_A_MAX - 1] = 0;
        e[i].question = qs[i];
        e[i].answer   = as[i];
    }
    int len;
    /* OLD_BUGGY_CAP is the literal that used to sit in begin_transcribe —
     * a hand-written 64 KB, not derived from anything.  It is kept as a
     * named counter-example so the regression is still demonstrated
     * against the value that actually shipped; do not turn it into a
     * formula, the point is that the product's number was arbitrary. */
    const int OLD_BUGGY_CAP = 64 * 1024;
    free(build_nul(e, AI_HISTORY_MAX, OLD_BUGGY_CAP, &len));
    if (len >= 0)
        FAIL("the old %d-byte cap was expected to be too small, got %d",
             OLD_BUGGY_CAP, len);

    /* The same mistake restated, this time derived so it tracks the
     * constants: sizing the buffer off the *input* length.  Always
     * insufficient, whatever AI_Q_MAX / AI_A_MAX are. */
    int by_input_len = AI_HISTORY_MAX * (AI_Q_MAX + AI_A_MAX);
    free(build_nul(e, AI_HISTORY_MAX, by_input_len, &len));
    if (len >= 0)
        FAIL("a cap sized off the input length (%d) was expected to be too "
             "small, got %d", by_input_len, len);

    int worst = JSON_HISTORY_CAP(AI_HISTORY_MAX, AI_Q_MAX, AI_A_MAX);
    char *s = build_nul(e, AI_HISTORY_MAX, worst, &len);
    if (!s) FAIL("worst-case cap %d still overflows (rc=%d)", worst, len);
    else if (len <= by_input_len)
        FAIL("worst case %d should exceed the input length %d", len, by_input_len);
    free(s);
}

/* ── what this module is not ──────────────────────────────────────── */

/* json_append_string is a faithful escaper: invalid UTF-8 goes in and
 * comes straight back out.  So the utf8_fit() has to sit at the byte cap
 * that creates the cut — voice_ai_close_keep()'s snprintf into ai_hist_q /
 * ai_hist_a — which is exactly what the comment in voice.c says and what
 * that function did not do before this pass. */
static void t_half_glyph_is_forwarded_verbatim(void) {
    const char torn[] = "\xe4\xbd";      /* first two bytes of 你 */
    int rc;
    char *s = esc_in(torn, 32, &rc);
    if (rc != 0) FAIL("torn glyph: rc=%d", rc);
    else if (strcmp(s, "\"\xe4\xbd\"") != 0)
        FAIL("torn glyph should pass through: %s", s);
    free(s);

    /* The remedy is upstream, and it drops the partial. */
    if (utf8_fit(torn, 2) != 0)
        FAIL("utf8_fit should drop a 2-of-3 byte glyph, got %lu",
             (unsigned long)utf8_fit(torn, 2));
    if (utf8_fit("\xe4\xbd\xa0\xe5\xa5\xbd", 5) != 3)
        FAIL("utf8_fit should keep 你 and drop the tail, got %lu",
             (unsigned long)utf8_fit("\xe4\xbd\xa0\xe5\xa5\xbd", 5));
}

/* ── the caller contract ──────────────────────────────────────────── */

/* -1 on a too-small cap is only useful if somebody checks it.
 * begin_transcribe does (voice.c: `if (hist_len < 0) ... enter_error`), so
 * the failure is loud rather than a truncated request on the wire.  Pin
 * that the contract is at least detectable: every cap from 0 up rejects
 * rather than silently succeeding. */
static void t_too_small_cap_always_reports(void) {
    json_history_entry_t e[1] = { { "q", "a" } };
    for (int cap = 0; cap < 6; cap++) {
        int len;
        free(build_nul(e, 1, cap, &len));
        if (len >= 0) FAIL("cap=%d should report overflow, got %d", cap, len);
    }
}

int main(void) {
    t_escapes_quotes_and_backslash();
    t_escapes_named_controls();
    t_escapes_every_c0_byte();
    t_u2028_u2029_pass_through_raw();
    t_utf8_is_not_uescaped();
    t_exact_fit_never_emits_a_partial_glyph();
    t_exact_fit_boundaries();
    t_cap_sweep_mixed_content();
    t_overflow_leaves_pos_untouched();
    t_history_shape();
    t_injection_cannot_break_the_structure();
    t_unicode_escape_lookalike_is_not_decoded();
    t_history_cap_must_allow_for_escaping();
    t_half_glyph_is_forwarded_verbatim();
    t_too_small_cap_always_reports();

    if (failures > 0) {
        fprintf(stderr, "\n%d failures\n", failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
