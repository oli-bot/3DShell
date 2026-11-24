/*
 * Host-side test for source/json_find.c — the flat-JSON reader the voice
 * pipeline uses to pull "question" / "answer" out of the AI shim's reply.
 *
 * The reason this module exists at all (rather than living inside voice.c)
 * is the case in t_full_buffer_no_nul(): the reply buffer is a byte range,
 * never NUL-terminated, and a reply that exactly fills it contains no NUL
 * anywhere — which is precisely the input a strstr()-based search walks
 * off the end of.  That is only observable under AddressSanitizer, which
 * is why test_json_find.sh builds with it.
 *
 * Compile and run via tools/test_json_find.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/json_find.h"

static int failures = 0;

#define FAIL(msg, ...) do { \
    fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); \
    failures++; \
} while (0)

/* ── happy paths ───────────────────────────────────────────────────── */

static void t_basic(void) {
    const char body[] = "{\"question\":\"ni hao\",\"answer\":\"shi jie\"}";
    char out[64];

    if (json_find_string_field(body, (int)strlen(body),
                               "question", out, sizeof out) != 0)
        FAIL("question not found");
    else if (strcmp(out, "ni hao") != 0)
        FAIL("question = '%s', want 'ni hao'", out);

    if (json_find_string_field(body, (int)strlen(body),
                               "answer", out, sizeof out) != 0)
        FAIL("answer not found");
    else if (strcmp(out, "shi jie") != 0)
        FAIL("answer = '%s', want 'shi jie'", out);

    if (json_find_string_field(body, (int)strlen(body),
                               "nope", out, sizeof out) != -1)
        FAIL("absent key should give -1");
}

static void t_whitespace_tolerant(void) {
    const char body[] = "{ \"answer\" : \" spaced \" }";
    char out[64];
    if (json_find_string_field(body, (int)strlen(body),
                               "answer", out, sizeof out) != 0)
        FAIL("spaced pair not found");
    else if (strcmp(out, " spaced ") != 0)
        FAIL("spaced value = '%s'", out);
}

static void t_escapes_and_cjk(void) {
    /* JSON bytes: {"answer":"a\nb中"} written with escapes. */
    const char body[] = "{\"answer\":\"a\\nb\\u4e2d\"}";
    const char want[] = "a\nb\xe4\xb8\xad";
    char out[64];
    if (json_find_string_field(body, (int)strlen(body),
                               "answer", out, sizeof out) != 0)
        FAIL("escaped value not found");
    else if (strcmp(out, want) != 0)
        FAIL("escaped value mismatch");
}

/* A key that appears as a *value* first must not be mistaken for the key:
 * the reader has to keep scanning until it finds `"key" : "`. */
static void t_prefix_is_not_the_key(void) {
    const char body[] = "{\"note\":\"answer\",\"answer\":\"real\"}";
    char out[64];
    if (json_find_string_field(body, (int)strlen(body),
                               "answer", out, sizeof out) != 0)
        FAIL("answer not found after a decoy value");
    else if (strcmp(out, "real") != 0)
        FAIL("picked the decoy: '%s'", out);
}

static void t_truncation_never_splits_a_glyph(void) {
    const char body[] = "{\"answer\":\"\xe4\xbd\xa0\xe5\xa5\xbd\"}";
    char out[8];                      /* too small for 你 + NUL */
    if (json_find_string_field(body, (int)strlen(body),
                               "answer", out, sizeof out) != 0)
        FAIL("CJK value not found");
    /* Must be valid UTF-8 and must not end mid-glyph. */
    const unsigned char *u = (const unsigned char *)out;
    size_t i = 0, n = strlen(out);
    while (i < n) {
        unsigned char b = u[i];
        size_t need = (b < 0x80) ? 1 : (b < 0xE0) ? 2 : (b < 0xF0) ? 3 : 4;
        if (i + need > n) { FAIL("value ends mid-glyph"); break; }
        for (size_t k = 1; k < need; k++)
            if ((u[i + k] & 0xC0) != 0x80) { FAIL("invalid UTF-8 in out"); break; }
        i += need;
    }
}

/* ── the memory-safety cases ───────────────────────────────────────── */

/* A reply that exactly fills the receive buffer: no NUL anywhere in it.
 * malloc the exact size so ASan's redzone catches any read at [len]. */
static void t_full_buffer_no_nul(void) {
    const int len = 8192;
    char *body = (char *)malloc((size_t)len);
    if (!body) { FAIL("malloc"); return; }
    memset(body, 'x', (size_t)len);
    /* A partial needle at the very end, so the scan has to walk the whole
     * buffer and then run off it.  Copy 6 bytes, not sizeof("\"answe") —
     * the latter would drag the literal's terminating NUL in with it and
     * hand the search a terminator that a real reply does not have. */
    memcpy(body + len - 6, "\"answe", 6);

    char out[64];
    int rc = json_find_string_field(body, len, "answer", out, sizeof out);
    if (rc != -1)
        FAIL("full buffer with no NUL: expected -1, got %d", rc);
    free(body);
}

/* Bytes that sit past body_len are stale (a previous, longer reply) and
 * must be invisible even when they contain a perfectly valid pair. */
static void t_stale_tail_not_visible(void) {
    const char stale[] = "{\"answer\":\"LEAK\"}";
    const int visible = 4;                       /* only `{"a:` has arrived */
    size_t total = (size_t)visible + sizeof(stale);
    char *body = (char *)malloc(total);
    if (!body) { FAIL("malloc"); return; }
    memset(body, ' ', total);
    memcpy(body, "{\"a:", (size_t)visible);
    memcpy(body + visible, stale, sizeof(stale));

    char out[64];
    int rc = json_find_string_field(body, visible, "answer", out, sizeof out);
    if (rc == 0)
        FAIL("read a key that lives past body_len: '%s'", out);
    free(body);
}

/* A key that begins inside the range but would run past the end. */
static void t_needle_must_fit(void) {
    const char body[] = "{\"ans";
    char out[64];
    if (json_find_string_field(body, (int)strlen(body),
                               "answer", out, sizeof out) != -1)
        FAIL("a needle longer than the remainder should not match");
}

int main(void) {
    t_basic();
    t_whitespace_tolerant();
    t_escapes_and_cjk();
    t_prefix_is_not_the_key();
    t_truncation_never_splits_a_glyph();
    t_full_buffer_no_nul();
    t_stale_tail_not_visible();
    t_needle_must_fit();

    if (failures > 0) {
        fprintf(stderr, "\n%d failures\n", failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
