/* Host test for keyrepeat.h — the ramp behind the 按键重复速度 setting.
 *
 * The point of this file is the first block: KEYREPEAT_NORMAL must stay
 * byte-for-byte the constants every build shipped with, because that style is
 * what a machine with no settings.ini gets.  Nothing else in the tree can
 * check that; the three consumers (keyboard.c, softkb.c, and the settings
 * page's own D-pad) all pull in citro2d.h or libctru and never build on the
 * host.  The second block freezes the other two styles, since settings.ini
 * persists the option *index* — renumbering them would silently rewrite every
 * user's choice.
 *
 * build: tools/test_keyrepeat.sh  (make test-keyrepeat)
 */
#include "keyrepeat.h"
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, fmt, ...)                                                  \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: " fmt "\n", __func__, __LINE__,       \
                    ##__VA_ARGS__);                                            \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* Frames a hold of `style` emits a repeat over held frames [0, limit). */
static int fire_count(int style, int limit) {
    int n = 0;
    for (int f = 0; f < limit; f++) n += keyrepeat_fires(style, f) ? 1 : 0;
    return n;
}

/* ── the shipped default, frozen ─────────────────────────────────────── */
static void test_normal_is_what_shipped(void) {
    CHECK(keyrepeat_initial_delay(KEYREPEAT_NORMAL) == 15,
          "NORMAL delay is %d, must stay 15",
          keyrepeat_initial_delay(KEYREPEAT_NORMAL));

    /* DPAD_INITIAL_DELAY 15; dpad_repeat_period(): <30 → 5, <90 → 2, else 1 */
    for (int phase = 0; phase < 300; phase++) {
        int want = phase < 30 ? 5 : (phase < 90 ? 2 : 1);
        CHECK(keyrepeat_period(KEYREPEAT_NORMAL, phase) == want,
              "NORMAL phase %d: period %d, want %d", phase,
              keyrepeat_period(KEYREPEAT_NORMAL, phase), want);
    }

    /* Every frame the old inline arithmetic would have fired. */
    for (int held = 0; held < 300; held++) {
        int legacy = 0;
        if (held >= 15) {
            int phase  = held - 15;
            int period = phase < 30 ? 5 : (phase < 90 ? 2 : 1);
            legacy = (period > 0 && phase % period == 0);
        }
        CHECK(keyrepeat_fires(KEYREPEAT_NORMAL, held) == legacy,
              "NORMAL held %d: fires %d, legacy %d", held,
              keyrepeat_fires(KEYREPEAT_NORMAL, held), legacy);
    }
}

/* ── the two new styles, and the invariants every style must hold ────── */
static void test_style_tables(void) {
    CHECK(keyrepeat_initial_delay(KEYREPEAT_SLOW) == 25, "SLOW delay");
    CHECK(keyrepeat_initial_delay(KEYREPEAT_FAST) == 8,  "FAST delay");

    for (int phase = 0; phase < 300; phase++) {
        int s = keyrepeat_period(KEYREPEAT_SLOW, phase);
        int f = keyrepeat_period(KEYREPEAT_FAST, phase);
        CHECK(s == (phase < 30 ? 9 : (phase < 90 ? 5 : 3)),
              "SLOW phase %d: period %d", phase, s);
        CHECK(f == (phase < 30 ? 3 : (phase < 90 ? 2 : 1)),
              "FAST phase %d: period %d", phase, f);
    }

    for (int style = 0; style < KEYREPEAT_STYLE_COUNT; style++) {
        CHECK(keyrepeat_initial_delay(style) > 0, "style %d delay", style);
        /* keyrepeat_period's result is used as a divisor by callers that
         * step the phase by hand, so it may never reach 0. */
        for (int phase = 0; phase < 1000; phase++)
            CHECK(keyrepeat_period(style, phase) >= 1,
                  "style %d phase %d: period %d", style, phase,
                  keyrepeat_period(style, phase));
        /* Contract keyboard.c relies on: nothing fires before the delay, so
         * a caller can test keyrepeat_fires() without ordering its own
         * delay comparison first. */
        for (int held = 0; held < keyrepeat_initial_delay(style); held++)
            CHECK(!keyrepeat_fires(style, held),
                  "style %d held %d fired before its delay", style, held);
    }

    /* A style the settings page cannot offer must repeat like the default,
     * not like the SLOW row that 0 happens to name. */
    CHECK(keyrepeat_initial_delay(7) == keyrepeat_initial_delay(KEYREPEAT_NORMAL),
          "out-of-range style does not fall back to NORMAL");
}

/* ── slower and faster really do mean fewer and more repeats ─────────── */
static void test_ordering(void) {
    /* A pointwise comparison would be meaningless — the three ramps are out
     * of phase with each other — so compare how much of a long hold each one
     * spends emitting. */
    const int limits[] = { 60, 120, 240, 600, 1800 };
    for (unsigned i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
        int n = limits[i];
        int slow   = fire_count(KEYREPEAT_SLOW, n);
        int normal = fire_count(KEYREPEAT_NORMAL, n);
        int fast   = fire_count(KEYREPEAT_FAST, n);
        CHECK(slow < normal && normal < fast,
              "%d-frame hold: slow %d, normal %d, fast %d", n, slow, normal,
              fast);
    }
}

int main(void) {
    test_normal_is_what_shipped();
    test_style_tables();
    test_ordering();
    if (failures) {
        fprintf(stderr, "\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("keyrepeat: all checks passed (NORMAL == the shipped 15 / 5,2,1)\n");
    return 0;
}
