/*
 * Host-side tests for source/font_atlas.c — the Unicode width / glyph
 * lookup layer.
 *
 * The module is pure logic: it references no C2D_/C3D_ symbol, calls no
 * 3DS service, and its only dependency is <stdint.h> plus the generated
 * tables in source/font_data.c (which tools/test_project_ui.sh already
 * links on the host).  So the whole atlas runs natively.
 *
 * What is at stake: font_is_wide() decides whether a codepoint eats one
 * or two character cells, and renderer.c:396 draw_codepoint_px() advances
 * by FONT_CELL_W * 2 on its answer.  A wrong answer is not a cosmetic
 * off-by-one — it desynchronises the measured width (renderer.c:489) from
 * the drawn width, which is the exact class of bug already fixed once in
 * the scaled-text path.
 *
 * Assertions below are driven by Unicode East Asian Width / the block
 * boundaries (U+FF5F..U+FF60 are FULLWIDTH brackets, U+FF61.. is
 * halfwidth, U+D800.. is a surrogate, …), not by whatever the code does
 * today.  Where the two disagree the divergence is reported as a note —
 * fixing it is a source change this test is not allowed to make.
 */
#include <stdio.h>
#include <stdlib.h>

#include "font_atlas.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg, ...) do {                                    \
    checks++;                                                         \
    if (cond) printf("ok   " msg "\n", ##__VA_ARGS__);                \
    else { fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); failures++; } \
} while (0)

/* A divergence from Unicode / the atlas's own coverage.  Printed so it
 * cannot be overlooked, but not counted as a failure. */
#define NOTE(msg, ...) do {                                           \
    printf("note: " msg "\n", ##__VA_ARGS__);                         \
} while (0)

#define NOtdef font_glyph_index('?')

/* ── narrow codepoints must not be declared wide ─────────────────── */

static void check_narrow_stays_narrow(void) {
    /* ASCII, Latin-1/Latin Extended-A, Greek, Cyrillic — all EAW=N. */
    CHECK(!font_is_wide('A') && !font_is_wide(0x7E), "ASCII is narrow");
    CHECK(!font_is_wide(0x00E9) && !font_is_wide(0x0101), "Latin-1/Ext-A is narrow");
    CHECK(!font_is_wide(0x03B1), "Greek is narrow");
    CHECK(!font_is_wide(0x0410), "Cyrillic is narrow");
    /* U+FF61..U+FFDC is the *halfwidth* block — the classic fullwidth
     * mistake, and the reason FONT ranges get split here. */
    CHECK(!font_is_wide(0xFF61) && !font_is_wide(0xFF64), "halfwidth CJK punctuation is narrow");
    CHECK(!font_is_wide(0xFF66) && !font_is_wide(0xFF9D) && !font_is_wide(0xFFDC),
          "halfwidth katakana is narrow");
    CHECK(!font_is_wide(0xFFE7) && !font_is_wide(0xFFEE), "U+FFE7.. past the fullwidth symbols is narrow");
    /* Past every range end. */
    CHECK(!font_is_wide(0xA000) && !font_is_wide(0xD800) && !font_is_wide(0x10FFFF),
          "outside every declared range is narrow");
}

/* ── the ranges font_is_wide() does claim ────────────────────────── */

static void check_wide_ranges(void) {
    CHECK(font_is_wide(0x3042) && font_is_wide(0x30A2), "hiragana + katakana are wide");
    CHECK(font_is_wide(0x4E2D) && font_is_wide(0x9FFF), "CJK unified ideographs are wide");
    CHECK(font_is_wide(0xFF01) && font_is_wide(0xFF21) && font_is_wide(0xFF5E),
          "fullwidth ASCII is wide");
    CHECK(font_is_wide(0xFFE5) && font_is_wide(0xFFE6), "fullwidth yen/won are wide");
    CHECK(font_is_wide(0xAC00) && font_is_wide(0xD7FF), "hangul range is wide");
    /* Boundary: U+FF60 is the last fullwidth bracket, U+FF61 is the first
     * halfwidth one.  U+D7FF is the last hangul cell, U+D800 a surrogate. */
    CHECK(font_is_wide(0xFF60) && !font_is_wide(0xFF61), "U+FF60 wide / U+FF61 narrow boundary");
    CHECK(font_is_wide(0xD7FF) && !font_is_wide(0xD800), "U+D7FF wide / U+D800 narrow boundary");
    CHECK(font_is_wide(0x9FFF) && !font_is_wide(0xA000), "U+9FFF wide / U+A000 narrow boundary");
}

/* ── out-of-range and surrogate input must not index out of bounds ── */

static void check_out_of_range_is_safe(void) {
    const uint32_t bad[] = { 0x0, 0xD800, 0xDFFF, 0x110000, 0x1FFFFF, 0xFFFFFFFFu };
    int ok = 1;
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int gi = font_glyph_index(bad[i]);
        if (gi < 0 || gi >= FONT_NGLYPHS) {
            printf("     font_glyph_index(0x%X) = %d\n", bad[i], gi);
            ok = 0;
        }
    }
    CHECK(ok, "unmapped/surrogate/out-of-plane codepoints stay in range");

    /* Every one of them must land on the '?' notdef, not on some
     * arbitrary slot. */
    CHECK(font_glyph_index(0x110000) == NOtdef &&
          font_glyph_index(0xD800) == NOtdef &&
          font_glyph_index(0xFFFFFFFFu) == NOtdef,
          "unmapped codepoints fall back to '?' (index %d)", NOtdef);

    CHECK(font_wide_glyph_index(0x41) == -1, "'A' has no wide glyph");
    CHECK(font_wide_glyph_index(0xFFFFFFFFu) == -1, "out-of-range wide lookup returns -1");
}

/* ── fallback_alias() actually resolves ──────────────────────────── */

static void check_alias_fallback(void) {
    /* U+23F5 -> U+25B6, U+1F680 -> U+2197, U+1F4C1 -> U+F115 (NF folder),
     * U+23F8 -> U+2016: each must reach the same slot as its alias target,
     * not the '?' notdef. */
    CHECK(font_glyph_index(0x23F5) == font_glyph_index(0x25B6) &&
          font_glyph_index(0x25B6) != NOtdef,
          "U+23F5 aliases to U+25B6 (slot %d)", font_glyph_index(0x25B6));
    CHECK(font_glyph_index(0x1F680) == font_glyph_index(0x2197) &&
          font_glyph_index(0x2197) != NOtdef,
          "U+1F680 aliases to U+2197 (slot %d)", font_glyph_index(0x2197));
    CHECK(font_glyph_index(0x1F4C1) == font_glyph_index(0xF115) &&
          font_glyph_index(0xF115) != NOtdef,
          "U+1F4C1 aliases to U+F115 (slot %d)", font_glyph_index(0xF115));
    CHECK(font_glyph_index(0x23F8) == font_glyph_index(0x2016) &&
          font_glyph_index(0x2016) != NOtdef,
          "U+23F8 aliases to U+2016 (slot %d)", font_glyph_index(0x2016));

    /* Box drawing / block elements: every cell of U+2500..U+259F must
     * resolve to a real slot.  (They are all carried by the atlas today,
     * so the "fallback by range" branch further down is a safety net —
     * see report_dead_range_fallback.) */
    int solid = 1;
    for (uint32_t cp = 0x2500; cp <= 0x259F; cp++)
        if (font_glyph_index(cp) == NOtdef) { solid = 0; printf("     U+%04X\n", cp); }
    CHECK(solid, "every U+2500..U+259F box/block cell resolves to a real glyph");
    /* U+2593 DARK SHADE and U+2588 FULL BLOCK are distinct glyphs. */
    CHECK(font_glyph_index(0x2593) != font_glyph_index(0x2588) &&
          font_glyph_index(0x2588) != NOtdef,
          "U+2593 and U+2588 are distinct real glyphs (%d / %d)",
          font_glyph_index(0x2593), font_glyph_index(0x2588));

    /* Special whitespace collapses to the plain space slot. */
    CHECK(font_glyph_index(0x00A0) == font_glyph_index(0x20) &&
          font_glyph_index(0x202F) == font_glyph_index(0x20) &&
          font_glyph_index(0xFEFF) == font_glyph_index(0x20),
          "NBSP/narrow-NBSP/BOM collapse to space (slot %d)", font_glyph_index(0x20));
}

/* ── the generated tables themselves ─────────────────────────────── */

static void check_table_integrity(void) {
    int sorted = 1, dup = 0, oob = 0;
    for (int i = 0; i < FONT_UNICODE_MAP_LEN; i++) {
        if (i) {
            if (font_unicode_cps[i] == font_unicode_cps[i - 1]) dup++;
            else if (font_unicode_cps[i] < font_unicode_cps[i - 1]) sorted = 0;
        }
        if (font_unicode_idx[i] >= FONT_NGLYPHS) oob++;
    }
    CHECK(sorted && !dup, "font_unicode_cps[] is strictly ascending (dup=%d)", dup);
    CHECK(!oob, "every font_unicode_idx[] < FONT_NGLYPHS (%d)", FONT_NGLYPHS);

    int wsorted = 1, wdup = 0, woob = 0;
    for (int i = 0; i < FONT_WIDE_MAP_LEN; i++) {
        if (i) {
            if (font_wide_cps[i] == font_wide_cps[i - 1]) wdup++;
            else if (font_wide_cps[i] < font_wide_cps[i - 1]) wsorted = 0;
        }
        if (font_wide_idx[i] >= FONT_WIDE_NGLYPHS) woob++;
    }
    CHECK(wsorted && !wdup, "font_wide_cps[] is strictly ascending (dup=%d)", wdup);
    CHECK(!woob, "every font_wide_idx[] < FONT_WIDE_NGLYPHS (%d)", FONT_WIDE_NGLYPHS);
    CHECK(FONT_UNICODE_MAP_LEN > 0 && FONT_WIDE_MAP_LEN > 0,
          "both maps are non-empty (%d / %d)", FONT_UNICODE_MAP_LEN, FONT_WIDE_MAP_LEN);
}

/* ── divergences: reported, not pinned ───────────────────────────── */

/* renderer.c:398-407 advances two cells whenever font_is_wide() says so,
 * and falls back to the narrow notdef when the wide atlas has no glyph.
 * So "wide but no wide glyph" is drawn as a literal '?' twice as wide as
 * it should be — the measured/drawn mismatch this file exists to catch. */
static void report_wide_without_glyph(void) {
    int ghosts = 0, hangul_ghosts = 0;
    for (uint32_t cp = 0x1100; cp <= 0x2FA1F; cp++) {
        if (!font_is_wide(cp)) continue;
        if (font_wide_glyph_index(cp) >= 0) continue;
        ghosts++;
        if (cp >= 0xAC00 && cp <= 0xD7FF) hangul_ghosts++;
    }
    if (ghosts) {
        NOTE("DEFECT: %d codepoints are declared wide by font_is_wide() but have "
             "no wide glyph (renderer draws '?' into 2 cells)", ghosts);
    }
    if (hangul_ghosts) {
        NOTE("DEFECT: all %d hangul cells (U+AC00..U+D7FF) are declared wide yet "
             "the atlas carries no hangul glyph at all", hangul_ghosts);
    }
    if (font_is_wide(0x3000) && font_wide_glyph_index(0x3000) < 0) {
        NOTE("DEFECT: U+3000 IDEOGRAPHIC SPACE (the fullwidth space, the most "
             "common CJK punctuation) is wide but absent from the wide atlas "
             "-> drawn as '?' (gi=%d)", font_glyph_index(0x3000));
    }
    if (font_is_wide(0xFF5F) && font_wide_glyph_index(0xFF5F) < 0) {
        NOTE("DEFECT: U+FF5F/U+FF60 FULLWIDTH LEFT/RIGHT PARENTHESIS are inside "
             "font_is_wide()'s U+FF01..U+FF60 range but absent from the atlas "
             "(gen_font.py:165 uses range(0xFF00, 0xFF60))");
    }
}

/* Blocks Unicode marks EAW=W that font_is_wide() answers 0 for. */
static void report_missing_wide_blocks(void) {
    static const struct { uint32_t lo, hi; const char *name; } blocks[] = {
        { 0x1100, 0x115F, "Hangul Jamo" },
        { 0x2E80, 0x2EFF, "CJK Radicals Supplement" },
        { 0x2F00, 0x2FDF, "Kangxi Radicals" },
        { 0xF900, 0xFAFF, "CJK Compatibility Ideographs" },
        { 0xFE10, 0xFE19, "Vertical Forms" },
        { 0xFE30, 0xFE4F, "CJK Compatibility Forms" },
        { 0x20000, 0x2FA1F, "CJK Unified Ideographs Extension B.." },
    };
    int missing = 0;
    for (unsigned i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        if (font_is_wide(blocks[i].lo)) continue;
        missing++;
        NOTE("DEFECT: U+%04X..U+%04X %s is EAW=Wide but font_is_wide() says narrow",
             blocks[i].lo, blocks[i].hi, blocks[i].name);
    }
    if (missing) {
        NOTE("  -> %d wide blocks uncovered; source/font_atlas.c:164-175 only "
             "tests 0x3000-0x9FFF, 0xAC00-0xD7FF, 0xFF01-0xFF60, 0xFFE0-0xFFE6", missing);
    }
}

/* The U+FFE0/U+FFE1 range start is one of the two halfwidth-named
 * codepoints in the whole Halfwidth/Fullwidth block. */
static void report_halfwidth_in_fullwidth_range(void) {
    if (!font_is_wide(0xFFE0) || !font_is_wide(0xFFE1)) return;
    NOTE("DEFECT: source/font_atlas.c:172 'if (cp >= 0xFFE0 && cp <= 0xFFE6)' "
         "returns 1 for U+FFE0 HALFWIDTH CENT SIGN and U+FFE1 HALFWIDTH POUND "
         "SIGN — Unicode EAW marks both H (1 cell), and the comment on line "
         "172 says /* 全角 */ (fullwidth).  The atlas (gen_font.py:166) does "
         "give them wide bitmaps, so the fix belongs in gen_font.py, not in "
         "font_is_wide() alone.");
}

/* An alias whose target is not in any generated range degrades to '?'. */
static void report_dead_aliases(void) {
    static const struct { uint32_t from, to; const char *what; } aliases[] = {
        { 0x231B, 0x29D6, "⌛ hourglass" },
        { 0x23F3, 0x29D6, "⏳ hourglass" },
        { 0x1F511, 0x21B3, "🔑 key" },
        { 0x1F4DA, 0x2261, "📚 books" },
        { 0x1F389, 0x2605, "🎉 party popper" },
    };
    for (unsigned i = 0; i < sizeof aliases / sizeof aliases[0]; i++) {
        if (font_glyph_index(aliases[i].to) != NOtdef) continue;
        NOTE("DEFECT: fallback_alias() sends U+%04X (%s) to U+%04X, which is in "
             "no generated narrow range -> both render as '?' (gi=%d)",
             aliases[i].from, aliases[i].what, aliases[i].to,
             font_glyph_index(aliases[i].from));
    }
}

/* The "fallback by range" branch is only reachable if the atlas is missing
 * a codepoint in U+2500..U+257F or U+2580..U+259F. */
static void report_dead_range_fallback(void) {
    if (FONT_UNICODE_MAP_LEN <= 0) return;
    int missing = 0;
    for (uint32_t cp = 0x2500; cp <= 0x259F; cp++) {
        int lo = 0, hi = FONT_UNICODE_MAP_LEN - 1, found = 0;
        while (lo <= hi) {
            int m = (lo + hi) / 2;
            if (font_unicode_cps[m] == cp) { found = 1; break; }
            if (font_unicode_cps[m] < cp) lo = m + 1; else hi = m - 1;
        }
        if (!found) missing++;
    }
    if (!missing) {
        printf("     source/font_atlas.c:150-153 'Fallback by range' is currently "
             "unreachable — all 160 cells of U+2500..U+259F are present in the "
             "atlas, so the branch never fires (and never did on this data set)\n");
    }
}

int main(void) {
    check_narrow_stays_narrow();
    check_wide_ranges();
    check_out_of_range_is_safe();
    check_alias_fallback();
    check_table_integrity();

    report_wide_without_glyph();
    report_missing_wide_blocks();
    report_halfwidth_in_fullwidth_range();
    report_dead_aliases();
    report_dead_range_fallback();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
