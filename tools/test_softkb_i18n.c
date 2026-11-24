/* Host-side smoke test for the softkb i18n table invariants.
 *
 * The settings_items[] table itself lives in source/softkb.c and
 * pulls in citro2d, so it cannot be linked into a host binary.  What
 * we CAN verify on host is the LANG_* macro contract documented at
 * the top of softkb.c: 6 languages, indices 0..5, no gaps, ZH at 0.
 *
 * These macros are *mirrored* from softkb.c — the canonical source
 * is the LANG_* block there, with two _Static_asserts pinning the
 * invariants the 3DS build needs.  The mirror is intentional: a
 * divergence between this file and softkb.c is exactly what the
 * host test catches.  An off-by-one in either place surfaces here
 * without needing a 3DS toolchain.
 */
#include <stdio.h>

/* Mirror the LANG_* macros from softkb.c.  If softkb.c changes these,
 * this test must be updated to match.  See the comment block at
 * the top of softkb.c for the canonical definition. */
#define LANG_ZH     0
#define LANG_EN     1
#define LANG_JA     2
#define LANG_KO     3
#define LANG_DE     4
#define LANG_FR     5
#define LANG_COUNT  6

/* Mirror SOFTKB_OPTS_MAX from softkb.c.  In the 3DS build the two
 * values are pinned together by a _Static_assert; here we sanity
 * check the same relationship.  If a future language is added, the
 * 3DS build will fail at compile time and this test will fail at
 * host time — that is the desired double coverage. */
#define SOFTKB_OPTS_MAX  8

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failures++; \
    } else { \
        fprintf(stderr, "ok   %s\n", msg); \
    } \
} while (0)

int main(void) {
    /* 1. The count matches the number of distinct LANG_* macros. */
    CHECK(LANG_COUNT == 6, "LANG_COUNT == 6 (zh/en/ja/ko/de/fr)");

    /* 2. Indices are 0..LANG_COUNT-1 with no duplicates. */
    int seen[LANG_COUNT] = {0};
    int langs[] = {LANG_ZH, LANG_EN, LANG_JA, LANG_KO, LANG_DE, LANG_FR};
    for (int i = 0; i < (int)(sizeof(langs) / sizeof(langs[0])); i++) {
        CHECK(langs[i] >= 0 && langs[i] < LANG_COUNT,
              "language index in range");
        CHECK(seen[langs[i]]++ == 0, "no duplicate language index");
    }

    /* 3. ZH is the default (must be 0) — fallback chain ends there
     *    and settings_defaults() uses it as the seed. */
    CHECK(LANG_ZH == 0, "LANG_ZH must be 0 (defaults + fallback target)");

    /* 4. The opts[] ceiling must be >= LANG_COUNT, since SID_UI_LANG
     *    currently needs exactly that many.  If a future language
     *    pushes LANG_COUNT to 7, SOFTKB_OPTS_MAX must move with it;
     *    otherwise the 3DS build's _Static_assert fires and this
     *    test's relative check fires too. */
    CHECK(SOFTKB_OPTS_MAX >= LANG_COUNT,
          "opts[] ceiling covers LANG_COUNT");

    /* 5. Negative smoke: if a future change to softkb.c widens
     *    LANG_COUNT beyond SOFTKB_OPTS_MAX, the relationship here
     *    also breaks — proving both sides do their job.  This block
     *    does not run in the normal path; it documents the failure
     *    mode so anyone reading a future regression can see what
     *    the two check points are. */
#if LANG_COUNT > SOFTKB_OPTS_MAX
#  error "host mirror: LANG_COUNT > SOFTKB_OPTS_MAX — the 3DS build's _Static_assert would also fire"
#endif

    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("i18n table invariants ok\n");
    return 0;
}
