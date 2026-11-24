/*
 * Host-side smoke test for source/ime_pinyin.c.
 *
 * Compile and run via tools/test_ime.sh.  Catches regressions in the
 * dict format, lookup logic, and pagination without needing a 3DS push.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/ime_pinyin.h"

#define DICT_PATH_DEFAULT "romfs/pinyin_dict.bin"

static int failures = 0;

#define FAIL(msg, ...) do { \
    fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); \
    failures++; \
} while (0)

/* Scan all candidates (across pages), not just the visible page. */
static int has_candidate(const ime_t *ime, const char *expected) {
    int total = ime_total_candidates(ime);
    for (int i = 0; i < total; i++) {
        const char *c = ime_candidate_at(ime, i);
        if (c && strcmp(c, expected) == 0) return i;
    }
    return -1;
}

static void print_candidates(const ime_t *ime, const char *label) {
    int n = ime_candidate_count(ime);
    printf("  %-12s →", label);
    for (int i = 0; i < n; i++) printf(" %s", ime_candidate(ime, i));
    printf("   (page %d/%d, total %d)\n",
           ime_page(ime) + 1, ime_page_count(ime),
           ime_total_candidates(ime));
}

static void type(ime_t *ime, const char *s) {
    for (; *s; s++) ime_input_letter(ime, *s);
}

/* ── Malformed / truncated dict fixtures ───────────────────────────
 *
 * ime_init() used to trust the header: magic "PYIN" + version 1 was
 * enough to hand back an ime_t, and every offset in the header was then
 * used unchecked.  A truncated dict still *starts* with a perfectly
 * valid header, so it was accepted and read past the end of the blob on
 * the first keystroke — turning main.c's documented "non-fatal, degrade
 * CN mode to passthrough" into a crash.
 *
 * These cases pack tiny dicts into build/ and assert init rejects the
 * broken ones and still accepts the good ones.  The on-disk layout is
 * duplicated here on purpose: it is a format contract, not an internal.
 */
#define FIXTURE "build/ime_fixture.bin"

/* On-disk layout (little-endian, packed) — mirror of source/ime_pinyin.c,
 * with the sizes pinned so a layout change fails the build rather than
 * silently mis-encoding every fixture below. */
struct __attribute__((packed)) fx_hdr {
    char     magic[4];
    uint32_t version;
    uint32_t n_entries;
    uint32_t pinyin_pool_off;
    uint32_t pinyin_pool_len;
    uint32_t word_pool_off;
    uint32_t word_pool_len;
};
struct __attribute__((packed)) fx_ent {
    uint32_t pinyin_off;
    uint32_t word_off;
    uint32_t freq;
};
_Static_assert(sizeof(struct fx_hdr) == 28, "dict header is 28 bytes");
_Static_assert(sizeof(struct fx_ent) == 12, "dict entry is 12 bytes");

#define FX_HDR_SZ ((size_t)sizeof(struct fx_hdr))
#define FX_ENT_SZ ((size_t)sizeof(struct fx_ent))

static void set_u32(unsigned char *p, size_t off, uint32_t v) {
    memcpy(p + off, &v, 4);
}

/* Pack a well-formed 2-entry dict: ni→你 (freq 90), wo→我 (freq 100).
 * Pools are "ni\0wo\0" (6 B) and "你\0我\0" (8 B).  Returns blob size. */
static size_t pack_good(unsigned char *buf, size_t cap) {
    static const char py[] = "ni\0wo\0";                       /* 6 of 7  */
    static const char wd[] = "\xe4\xbd\xa0\0\xe6\x88\x91\0";   /* 8 of 9  */
    size_t n = 0;

    if (cap < FX_HDR_SZ + 2 * FX_ENT_SZ + 6 + 8) {
        FAIL("fixture buffer too small");
        return 0;
    }
    memset(buf, 0, cap);
    memcpy(buf, "PYIN", 4);
    set_u32(buf,  4, 1);                             /* version         */
    set_u32(buf,  8, 2);                             /* n_entries       */
    set_u32(buf, 12, (uint32_t)(FX_HDR_SZ + 2 * FX_ENT_SZ)); /* py pool  */
    set_u32(buf, 16, 6);                             /* pinyin_pool_len */
    set_u32(buf, 20, (uint32_t)(FX_HDR_SZ + 2 * FX_ENT_SZ + 6)); /* wd   */
    set_u32(buf, 24, 8);                             /* word_pool_len   */
    n  = FX_HDR_SZ;
    set_u32(buf, n + 0, 0); set_u32(buf, n + 4, 0); set_u32(buf, n + 8, 90);
    n += FX_ENT_SZ;
    set_u32(buf, n + 0, 3); set_u32(buf, n + 4, 4); set_u32(buf, n + 8, 100);
    n += FX_ENT_SZ;
    memcpy(buf + n, py, 6); n += 6;
    memcpy(buf + n, wd, 8); n += 8;
    return n;
}

static int write_fixture(const unsigned char *buf, size_t n) {
    FILE *f = fopen(FIXTURE, "wb");
    if (!f) { FAIL("cannot create %s", FIXTURE); return -1; }
    size_t w = fwrite(buf, 1, n, f);
    if (fclose(f) != 0) { FAIL("cannot flush %s", FIXTURE); return -1; }
    if (w != n) { FAIL("short write to %s", FIXTURE); return -1; }
    return 0;
}

static void expect_reject(const char *what, const unsigned char *buf, size_t n) {
    if (write_fixture(buf, n) != 0) return;
    ime_t *ime = ime_init(FIXTURE);
    if (ime) {
        FAIL("%s: ime_init accepted a broken dict", what);
        ime_free(ime);
    } else {
        printf("  rejected: %s\n", what);
    }
}

/* Assert the fixture loads and 'wo' resolves to 我 (the higher-freq entry). */
static void expect_wo_works(const char *what,
                            const unsigned char *buf, size_t n) {
    if (write_fixture(buf, n) != 0) return;
    ime_t *ime = ime_init(FIXTURE);
    if (!ime) {
        FAIL("%s: ime_init rejected a usable dict", what);
        return;
    }
    type(ime, "wo");
    const char *c = ime_candidate(ime, 0);
    if (!c || strcmp(c, "\xe6\x88\x91") != 0) {
        FAIL("%s: 'wo' should yield 我, got '%s'", what, c ? c : "(null)");
    } else {
        printf("  accepted: %s (wo → 我)\n", what);
    }
    ime_free(ime);
}

static void check_malformed_dicts(void) {
    unsigned char buf[256];
    unsigned char mut[256];
    size_t m;

    /* Positive control: bounds checking must not reject a good dict. */
    m = pack_good(buf, sizeof buf);
    if (m == 0) return;
    expect_wo_works("well-formed 2-entry dict", buf, m);

    /* 1. Truncated mid entry-table: header claims 2 entries, the file
     *    only has room for 1. */
    expect_reject("truncated entry table", buf, FX_HDR_SZ + FX_ENT_SZ + 3);

    /* 2. Entry count that cannot fit in any file. */
    m = pack_good(mut, sizeof mut);
    set_u32(mut, 8, 0xFFFFFFFFu);
    expect_reject("n_entries = 0xFFFFFFFF", mut, m);

    /* 3. pinyin pool starting past EOF. */
    m = pack_good(mut, sizeof mut);
    set_u32(mut, 12, (uint32_t)m + 4096);
    expect_reject("pinyin_pool_off past EOF", mut, m);

    /* 4. pinyin pool running past EOF. */
    m = pack_good(mut, sizeof mut);
    set_u32(mut, 16, 6 + 64);
    expect_reject("pinyin_pool_len past EOF", mut, m);

    /* 5. word pool running past EOF. */
    m = pack_good(mut, sizeof mut);
    set_u32(mut, 24, 8 + 64);
    expect_reject("word_pool_len past EOF", mut, m);

    /* 6. Last pool string runs to the very end of the file with no NUL.
     *    Must be *accepted* — the blob is allocated one byte wider and
     *    zeroed so the string terminates there instead of walking into
     *    the heap. */
    m = pack_good(mut, sizeof mut);
    set_u32(mut, 24, 7);            /* 你\0我 — final NUL dropped */
    expect_wo_works("unterminated final pool string", mut, m - 1);

    remove(FIXTURE);
}

int main(int argc, char **argv) {
    /* Malformed-dict handling runs first: it needs no real dict and its
     * fixtures only live for the duration of the call. */
    check_malformed_dicts();

    const char *path = (argc > 1) ? argv[1] : DICT_PATH_DEFAULT;
    ime_t *ime = ime_init(path);
    if (!ime) {
        fprintf(stderr,
            "FATAL: ime_init(\"%s\") failed.\n"
            "  Run `python3 tools/gen_pinyin_dict.py` first to build it.\n",
            path);
        return 1;
    }
    printf("Loaded dict: %s\n", path);

    /* ── 1. ni → 你 in candidates ───────────────────────────── */
    ime_clear(ime);
    type(ime, "ni");
    print_candidates(ime, "ni");
    if (has_candidate(ime, "你") < 0) FAIL("'你' not in 'ni' candidates");

    /* ── 2. nihao → 你好 in top results ──────────────────────── */
    ime_clear(ime);
    type(ime, "nihao");
    print_candidates(ime, "nihao");
    int idx = has_candidate(ime, "你好");
    if (idx < 0) FAIL("'你好' not in 'nihao' candidates");
    if (idx > 3) FAIL("'你好' should be in top 4 for nihao, got idx %d", idx);

    /* ── 3. shijie → 世界 reachable ──────────────────────────── */
    ime_clear(ime);
    type(ime, "shijie");
    print_candidates(ime, "shijie");
    if (has_candidate(ime, "世界") < 0) FAIL("'世界' not in 'shijie' candidates");

    /* ── 4. wo → 我 should be top candidate (very high freq) ── */
    ime_clear(ime);
    type(ime, "wo");
    print_candidates(ime, "wo");
    if (has_candidate(ime, "我") < 0) FAIL("'我' not in 'wo' candidates");

    /* ── 5. ime_select returns committed string + clears buffer ── */
    ime_clear(ime);
    type(ime, "nihao");
    const char *got = ime_select(ime, 0);
    if (!got) {
        FAIL("ime_select(0) returned NULL");
    } else {
        printf("  selected from nihao → %s\n", got);
        if (ime_buffer_len(ime) != 0) FAIL("buffer not cleared after select");
    }

    /* ── 6. backspace narrows / restores prefix ───────────────── */
    ime_clear(ime);
    type(ime, "wome");
    int n_wome = ime_candidate_count(ime);
    ime_backspace(ime);
    if (strcmp(ime_buffer(ime), "wom") != 0) {
        FAIL("buffer should be 'wom' after bs, got '%s'", ime_buffer(ime));
    }
    int n_wom = ime_candidate_count(ime);
    /* Sanity: a shorter prefix usually has more candidates (not strictly
     * true but is here).  Just print for inspection. */
    printf("  wome n=%d, after bs wom n=%d\n", n_wome, n_wom);

    /* ── 7. 'n' has many pages ────────────────────────────────── */
    ime_clear(ime);
    type(ime, "n");
    if (ime_page_count(ime) < 2)
        FAIL("'n' should have multiple pages, got %d", ime_page_count(ime));
    print_candidates(ime, "n p0");
    ime_page_next(ime);
    print_candidates(ime, "n p1");
    if (ime_page(ime) != 1) FAIL("page should be 1 after page_next");
    ime_page_prev(ime);
    if (ime_page(ime) != 0) FAIL("page should be 0 after page_prev");

    /* ── 8. fallback prefix: typing past valid pinyin still yields
     *      candidates for the longest valid prefix.  The exact
     *      matched length depends on what's in the dict (abbrev
     *      entries can make "zz", "zzz" valid too). ─────────── */
    ime_clear(ime);
    type(ime, "zzzzz");
    int mlen = ime_matched_prefix_len(ime);
    if (mlen <= 0 || mlen >= 5)
        FAIL("'zzzzz' should fall back to a strict prefix, got matched_prefix_len=%d", mlen);
    if (ime_candidate_count(ime) == 0)
        FAIL("'zzzzz' fallback should yield candidates");
    printf("  zzzzz matched_prefix_len=%d, top: %s\n",
           mlen, ime_candidate(ime, 0));

    /* ── 8b. Prefix fallback: typing past a valid prefix still
     *      surfaces the matches for the prefix that fits. ──────── */
    ime_clear(ime);
    type(ime, "nihaoz");   /* nihao + accidental z */
    if (ime_matched_prefix_len(ime) != 5)
        FAIL("matched_prefix_len should be 5 for 'nihaoz', got %d",
             ime_matched_prefix_len(ime));
    if (has_candidate(ime, "你好") < 0)
        FAIL("'你好' should appear via prefix fallback for 'nihaoz'");
    print_candidates(ime, "nihaoz→ni..");

    /* ── 8c. Abbreviation entries: nh → 你好, wm → 我们, sj → 世界 ── */
    const char *abbrev_cases[][2] = {
        { "nh",   "你好" },
        { "wm",   "我们" },
        { "sj",   "世界" },
        { "zw",   "中文" },
        { "xx",   "谢谢" },
        { NULL, NULL },
    };
    int abbrev_hits = 0;
    for (int i = 0; abbrev_cases[i][0]; i++) {
        ime_clear(ime);
        type(ime, abbrev_cases[i][0]);
        int found = has_candidate(ime, abbrev_cases[i][1]);
        if (found >= 0) {
            abbrev_hits++;
            printf("  %s → %s (idx=%d)\n",
                   abbrev_cases[i][0], abbrev_cases[i][1], found);
        } else {
            printf("  %s → ✗ '%s' not in abbrev candidates\n",
                   abbrev_cases[i][0], abbrev_cases[i][1]);
            FAIL("abbrev '%s' missing", abbrev_cases[i][0]);
        }
    }
    printf("  abbrev: %d/5 hit\n", abbrev_hits);

    /* ── 8d. Selection cursor: left/right within page ──────────── */
    ime_clear(ime);
    type(ime, "nihao");
    if (ime_selection_idx(ime) != 0)
        FAIL("selection should default to 0");
    ime_selection_right(ime);
    if (ime_selection_idx(ime) != 1)
        FAIL("selection_right should give 1, got %d", ime_selection_idx(ime));
    ime_selection_right(ime);
    if (ime_selection_idx(ime) != 2)
        FAIL("selection should be 2 after 2x right");
    ime_selection_left(ime);
    if (ime_selection_idx(ime) != 1)
        FAIL("selection_left from 2 should give 1");
    /* commit_current at idx 1 */
    const char *cur = ime_candidate(ime, 1);
    const char *committed = ime_select_current(ime);
    if (!cur || !committed || strcmp(cur, committed) != 0)
        FAIL("ime_select_current should commit at selection idx");
    if (ime_buffer_len(ime) != 0)
        FAIL("buffer should be empty after select_current");

    /* ── 8e. Blocked words gone ─────────────────────────────────── */
    ime_clear(ime);
    type(ime, "jiangzemin");
    if (has_candidate(ime, "江泽民") >= 0)
        FAIL("'江泽民' should be blocked");
    if (has_candidate(ime, "江泽民同志") >= 0)
        FAIL("'江泽民同志' should be blocked");

    /* ── 9. several common everyday words ─────────────────────── */
    const char *cases[][2] = {
        { "zhongwen",  "中文" },
        { "yingwen",   "英文" },
        { "diannao",   "电脑" },
        { "shoufuji",  "首富级" },     /* may or may not be in top */
        { "shouji",    "手机" },
        { "xiexie",    "谢谢" },
        { "huanying",  "欢迎" },
        { "yemian",    "页面" },
        { NULL, NULL },
    };
    int hits = 0, hit_top = 0;
    for (int i = 0; cases[i][0]; i++) {
        ime_clear(ime);
        type(ime, cases[i][0]);
        int found = has_candidate(ime, cases[i][1]);
        if (found >= 0) {
            hits++;
            if (found < 3) hit_top++;
            printf("  %s → %s (idx=%d)\n",
                   cases[i][0], cases[i][1], found);
        } else {
            printf("  %s → ✗ '%s' not found in candidates:",
                   cases[i][0], cases[i][1]);
            int n = ime_candidate_count(ime);
            for (int j = 0; j < n; j++) printf(" %s", ime_candidate(ime, j));
            printf("\n");
        }
    }
    printf("  %d/8 expected words found, %d in top-3.\n", hits, hit_top);

    ime_free(ime);

    if (failures > 0) {
        fprintf(stderr, "\n%d failures\n", failures);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}
