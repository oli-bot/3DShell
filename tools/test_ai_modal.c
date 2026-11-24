/*
 * Host-side tests for the AI-ask modal (source/ai_modal.c).
 *
 * The module is pure logic plus a handful of renderer calls: a fade
 * state machine, a markdown→(text, style) transform, a soft-wrap walk
 * and a single-line ellipsis.  None of that needs the 3DS, and all of
 * it is the kind of thing that breaks silently — a wrap that drops a
 * codepoint, an ellipsis that forgets to reserve room for itself, a
 * fade whose alpha never reaches full.
 *
 * It compiles here because ai_modal.c names no C2D_/C3D_ symbol: every
 * rect and every glyph goes through the renderer_draw_* wrappers, which
 * tools/stub/pui_host_stubs.c stands in for.  The two things the stub
 * gives us are used differently:
 *
 *   - the capture log answers "what would the screen say".  ai_modal
 *     draws the answer ONE CODEPOINT PER CALL (adjacent codepoints can
 *     carry different styles), so the log has a newline between every
 *     character; saw_flat() below strips those before searching.
 *   - the colour framebuffer answers "what colour would this pixel be",
 *     which is the only way to check the modal's layer geometry — an
 *     overlay drawn 1 px off, or a border on the wrong edge, renders
 *     perfectly well and looks almost right.
 *
 * font_atlas.c/font_data.c are linked for real (both are citro2d-free)
 * so the glyph metrics the wrap uses are the ones the device gets.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "ai_modal.h"
#include "pui_host_stubs.h"
#include "test_wcag.h"

/* The modal wears the live keyboard palette, so the colours the geometry
 * assertions below compare against are read out of it rather than typed
 * as literals — a literal would pin the old Tokyo Night values and turn
 * every "is the border there" check into a theme-regression check.
 * t_theme_colours() is the one test that walks all five themes. */
static kb_pal_t g_pal;

/* ── harness ────────────────────────────────────────────────────────── */

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                   \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

/* Geometry mirrored from ai_modal.c.  Deliberately re-typed rather than
 * shared: if someone moves the modal, these tests must fail rather than
 * follow it. */
#define M_X      6
#define M_Y      6
#define M_W      308
#define M_H      228
#define M_PAD    10
#define M_INNER_X (M_X + M_PAD)
#define M_SEP_Y   28

#define C_OVERLAY 0x00000099u
#define C_BG      (g_pal.panel_bg)
#define C_BORDER  (g_pal.key_top)
#define C_SEP     (g_pal.key_top)

/* ── log helper ─────────────────────────────────────────────────────── */

/* The answer walk emits one codepoint per renderer call, so the capture
 * log is one character per line.  Flatten it, then search. */
static int saw_flat(const char *needle) {
    static char flat[HSTUB_LOG_MAX];
    int n = 0;
    for (int i = 0; i < hstub_log_len && n < HSTUB_LOG_MAX - 1; i++)
        if (hstub_log[i] != '\n') flat[n++] = hstub_log[i];
    flat[n] = 0;
    return strstr(flat, needle) != NULL;
}

/* Open a modal with the given Q/A and run it to the settled phase, with
 * the capture buffers cleared and one frame drawn.  Returns the modal. */
static ai_modal_t *settled(const char *q, const char *a) {
    ai_modal_t *m = ai_modal_init();
    ai_modal_open(m, q, a);
    for (int i = 0; i < 40; i++) ai_modal_tick(m);   /* past FADE_IN */
    hstub_reset();
    ai_modal_draw(m, &g_pal);
    return m;
}

/* ── tests ──────────────────────────────────────────────────────────── */

/* The fade state machine: invisible until opened, visible through the
 * whole fade-out (main.c keys its input suppression off visible()), and
 * close() idempotent so a second A press mid-fade is harmless. */
static void t_phase_machine(void) {
    ai_modal_t *m = ai_modal_init();
    CHECK(m != NULL, "ai_modal_init returned NULL");
    CHECK(!ai_modal_visible(m), "a fresh modal must not be visible");

    ai_modal_open(m, "q", "a");
    CHECK(ai_modal_visible(m), "open() must make the modal visible");

    for (int i = 0; i < 30; i++) ai_modal_tick(m);
    CHECK(ai_modal_visible(m), "modal must stay visible after fade-in");

    ai_modal_close(m);
    CHECK(ai_modal_visible(m), "modal must stay visible while fading out");

    ai_modal_close(m);   /* idempotent */
    for (int i = 0; i < 12; i++) ai_modal_tick(m);
    CHECK(!ai_modal_visible(m), "modal must be gone after fade-out");

    /* Ticking a closed modal must not resurrect it. */
    for (int i = 0; i < 5; i++) ai_modal_tick(m);
    CHECK(!ai_modal_visible(m), "ticking a closed modal must not reopen it");

    ai_modal_free(m);
}

/* The layer stack.  This is the test that pins the renderer-wrapper
 * refactor: same seven quads, same places, same colours. */
static void t_layer_geometry(void) {
    ai_modal_t *m = settled("q", "a");

    /* overlay + body + 4 border edges + separator */
    CHECK(hstub_rect_calls == 7,
          "expected 7 rects, drew %d", hstub_rect_calls);

    CHECK(hstub_fb_at(0, 0) == C_OVERLAY,
          "top-left corner should be the dimming overlay, got %08x",
          hstub_fb_at(0, 0));
    CHECK(hstub_fb_at(319, 239) == C_OVERLAY,
          "bottom-right corner should be the dimming overlay, got %08x",
          hstub_fb_at(319, 239));

    /* The 6 px margin is what stands in for rounded corners: just outside
     * the modal must still be overlay, not body. */
    CHECK(hstub_fb_at(M_X - 1, M_Y + 5) == C_OVERLAY,
          "the margin left of the modal must be overlay");
    CHECK(hstub_fb_at(M_X + M_W, M_Y + 5) == C_OVERLAY,
          "the margin right of the modal must be overlay");

    CHECK(hstub_fb_at(M_X + 5, M_Y + 5) == C_BG,
          "modal interior should be the modal background, got %08x",
          hstub_fb_at(M_X + 5, M_Y + 5));

    /* All four border edges, sampled at their midpoints. */
    CHECK(hstub_fb_at(M_X + 20, M_Y) == C_BORDER, "top border missing");
    CHECK(hstub_fb_at(M_X + 20, M_Y + M_H - 1) == C_BORDER,
          "bottom border missing");
    CHECK(hstub_fb_at(M_X, M_Y + 50) == C_BORDER, "left border missing");
    CHECK(hstub_fb_at(M_X + M_W - 1, M_Y + 50) == C_BORDER,
          "right border missing");

    CHECK(hstub_fb_at(M_INNER_X + 4, M_SEP_Y) == C_SEP, "separator missing");
    CHECK(hstub_fb_at(M_INNER_X - 1, M_SEP_Y) == C_BG,
          "separator must not bleed into the modal padding");

    ai_modal_free(m);
}

/* Mid-fade the layers must be translucent, and at the same ratio the
 * module's own alpha_scale() promises.  A fade that snapped to opaque
 * would still pass every geometry check above. */
static void t_fade_is_translucent(void) {
    ai_modal_t *m = ai_modal_init();
    ai_modal_open(m, "q", "a");

    /* Frame 0 eases to alpha 0, so the first frame paints nothing. */
    hstub_reset();
    ai_modal_draw(m, &g_pal);
    CHECK(hstub_rect_calls == 0,
          "frame 0 of the fade should paint nothing, painted %d rects",
          hstub_rect_calls);

    /* Halfway: t = 128/256, eased = 256 - (128*128 >> 8) = 192. */
    for (int i = 0; i < 15; i++) ai_modal_tick(m);
    hstub_reset();
    ai_modal_draw(m, &g_pal);

    uint32_t ov = hstub_fb_at(0, 0);
    uint32_t bg = hstub_fb_at(M_X + 5, M_Y + 5);
    CHECK((ov & 0xffu) == 114u,
          "halfway overlay alpha should be 114, got %u", ov & 0xffu);
    CHECK((bg & 0xffu) == 191u,
          "halfway body alpha should be 191, got %u", bg & 0xffu);
    /* The hue must survive the fade untouched.  Read the reference out of
     * the palette rather than typing 0x24 — that literal was the old
     * hard-coded Tokyo Night body and pinned the test to one theme. */
    CHECK((bg >> 24) == (C_BG >> 24),
          "fading must scale alpha only, not the hue: got %08x, want %08x",
          bg, C_BG);

    ai_modal_free(m);
}

/* The markdown transform.  Asserted through the capture log, so this
 * covers the walk end to end rather than a private helper. */
static void t_markdown_transform(void) {
    ai_modal_t *m;

    m = settled("q", "# Heading");
    CHECK(saw_flat("Heading"), "heading text must survive");
    CHECK(!saw_flat("# Heading"), "the '#' marker must be stripped");
    ai_modal_free(m);

    m = settled("q", "- item");
    CHECK(saw_flat("item"), "bullet text must survive");
    CHECK(saw_flat("\xe2\x80\xa2 "), "a bullet must become U+2022");
    CHECK(!saw_flat("- item"), "the '-' marker must be stripped");
    ai_modal_free(m);

    m = settled("q", "**bold** and *italic*");
    CHECK(saw_flat("bold"), "bold text must survive");
    CHECK(saw_flat("italic"), "italic text must survive");
    CHECK(!saw_flat("**"), "bold markers must be stripped");
    ai_modal_free(m);

    m = settled("q", "use `code` here");
    CHECK(saw_flat("code"), "inline code must survive");
    CHECK(!saw_flat("`"), "inline-code backticks must be stripped");
    ai_modal_free(m);

    m = settled("q", "[text](http://example.com)");
    CHECK(saw_flat("text"), "link text must survive");
    CHECK(!saw_flat("http"), "the link target must be dropped");
    ai_modal_free(m);

    m = settled("q", "```\nfenced\n```");
    CHECK(saw_flat("fenced"), "fenced block content must survive");
    CHECK(!saw_flat("```"), "the fence markers must not be rendered");
    ai_modal_free(m);

    /* Plain text is passed through untouched. */
    m = settled("q", "nothing to see");
    CHECK(saw_flat("nothing to see"), "plain text must pass through");
    ai_modal_free(m);
}

/* A question wider than the inner width gets an ellipsis instead of
 * running under the modal's right edge. */
static void t_question_ellipsis(void) {
    /* INNER_W is 288 px = 48 ASCII cells, so 80 chars cannot fit. */
    const char *longq =
        "0123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789";
    ai_modal_t *m = settled(longq, "a");

    CHECK(saw_flat("..."), "an over-wide question must be ellipsised");
    CHECK(!saw_flat(longq), "the whole over-wide question must not be drawn");

    /* A question that fits is drawn whole, with no ellipsis. */
    ai_modal_free(m);
    m = settled("short?", "a");
    CHECK(saw_flat("short?"), "a short question must be drawn whole");
    CHECK(!saw_flat("..."), "a short question must not be ellipsised");
    ai_modal_free(m);
}

/* An answer taller than the answer area is cut off with a trailing
 * "...".  The budget is geometric (14 rows at 13 px from y=32 to y=224),
 * so 20 lines must truncate. */
static void t_answer_truncation(void) {
    char body[256];
    int o = 0;
    for (int i = 1; i <= 20; i++)
        o += snprintf(body + o, sizeof(body) - (size_t)o, "line%d\n", i);

    ai_modal_t *m = settled("q", body);
    CHECK(saw_flat("line1"), "the first answer line must be drawn");
    CHECK(!saw_flat("line20"), "the last line must be past the cutoff");
    CHECK(saw_flat("..."), "a truncated answer must end with an ellipsis");
    ai_modal_free(m);

    /* A short answer fits, so no ellipsis is appended. */
    m = settled("q", "one\ntwo");
    CHECK(saw_flat("two"), "a short answer must be drawn whole");
    CHECK(!saw_flat("..."), "a short answer must not be ellipsised");
    ai_modal_free(m);
}

/* Degenerate input must not crash: main.c can hand us an empty question
 * and an empty answer if the shim returns nothing. */
static void t_empty_input(void) {
    ai_modal_t *m = ai_modal_init();
    ai_modal_open(m, NULL, NULL);
    for (int i = 0; i < 40; i++) ai_modal_tick(m);
    hstub_reset();
    ai_modal_draw(m, &g_pal);
    CHECK(hstub_rect_calls == 7, "the frame must still draw with no text");

    /* open() restarts the fade, so settle again before reading the frame. */
    ai_modal_open(m, "", "");
    for (int i = 0; i < 40; i++) ai_modal_tick(m);
    hstub_reset();
    ai_modal_draw(m, &g_pal);
    CHECK(hstub_rect_calls == 7, "empty strings must not break the draw");
    ai_modal_free(m);

    /* NULL must be tolerated everywhere the API allows it. */
    ai_modal_draw(NULL, &g_pal);
    ai_modal_draw(NULL, NULL);
    ai_modal_tick(NULL);
    ai_modal_close(NULL);
    ai_modal_free(NULL);
    CHECK(!ai_modal_visible(NULL), "a NULL modal is never visible");
}

/* Closing *during* the fade-in must dim out from the alpha the panel had
 * actually reached.  close() used to reset frame to 0 unconditionally,
 * and the fade-out curve is `256 - f*256/12`, so its first frame was
 * 256 — the panel flashed to fully opaque and only then started to fade.
 * Caught here because the jump is one frame wide and looks like nothing
 * in a screenshot. */
static void t_close_mid_fadein_does_not_flash(void) {
    ai_modal_t *m = ai_modal_init();
    ai_modal_open(m, "q", "a");

    /* Part-way into the 30-frame fade-in. */
    for (int i = 0; i < 5; i++) ai_modal_tick(m);
    hstub_reset();
    ai_modal_draw(m, &g_pal);
    uint32_t before = hstub_fb_at(0, 0) & 0xffu;

    /* The fade-in at frame 5 eases to 78/256, so the overlay is 153*78/256
     * = 46.  Asserted as a range rather than a magic constant so a future
     * change to FADE_IN_FRAMES does not silently invalidate the test. */
    CHECK(before > 0 && before < 153u,
          "mid-fade-in the overlay must be translucent, got %u", before);

    ai_modal_close(m);
    hstub_reset();
    ai_modal_draw(m, &g_pal);
    uint32_t first_out = hstub_fb_at(0, 0) & 0xffu;

    CHECK(first_out != 0xffu,
          "the first fade-out frame must not snap to fully opaque");
    CHECK(first_out == before,
          "fade-out must start where fade-in left off: %u -> %u",
          before, first_out);

    /* And the rest of the fade-out is monotonically non-increasing to 0. */
    uint32_t prev = first_out;
    int reached_zero = 0;
    for (int i = 0; i < 12; i++) {
        ai_modal_tick(m);
        hstub_reset();
        ai_modal_draw(m, &g_pal);
        uint32_t now = hstub_fb_at(0, 0) & 0xffu;
        CHECK(now <= prev, "fade-out must not brighten: %u -> %u", prev, now);
        prev = now;
        if (now == 0) reached_zero = 1;
    }
    CHECK(reached_zero, "the fade-out must reach fully transparent");

    /* Closing from the settled phase keeps the original full-strength
     * fade: out_from is 256 there. */
    ai_modal_free(m);

    m = settled("q", "a");
    ai_modal_close(m);
    hstub_reset();
    ai_modal_draw(m, &g_pal);
    CHECK((hstub_fb_at(0, 0) & 0xffu) == 153u,
          "closing from OPEN must still start at full overlay alpha, got %u",
          hstub_fb_at(0, 0) & 0xffu);
    ai_modal_free(m);
}

/* The two predicates must disagree for exactly one phase.  main.c routes
 * input off captures_input() and layout precedence off visible(), so if
 * they ever collapse back into one flag the 12-frame fade-out becomes an
 * input blackout again — every key, the soft keyboard and all touch dead
 * for ~0.2 s after the user dismissed the panel. */
static void t_input_capture_window(void) {
    ai_modal_t *m = ai_modal_init();

    CHECK(!ai_modal_visible(m), "a fresh modal is not visible");
    CHECK(!ai_modal_captures_input(m), "a fresh modal captures nothing");

    /* FADE_IN: painted *and* capturing — the modal is arriving. */
    ai_modal_open(m, "q", "a");
    CHECK(ai_modal_visible(m), "fade-in must be visible");
    CHECK(ai_modal_captures_input(m), "fade-in must capture input");

    for (int i = 0; i < 15; i++) ai_modal_tick(m);
    CHECK(ai_modal_captures_input(m), "mid fade-in must still capture");

    /* OPEN: both true. */
    for (int i = 0; i < 20; i++) ai_modal_tick(m);
    CHECK(ai_modal_visible(m), "the open modal must be visible");
    CHECK(ai_modal_captures_input(m), "the open modal must capture input");

    /* FADE_OUT: the one phase where they must differ. */
    ai_modal_close(m);
    CHECK(ai_modal_visible(m),
          "fade-out must stay visible (nothing may pop up over it)");
    CHECK(!ai_modal_captures_input(m),
          "fade-out must NOT capture input — the close already happened");

    for (int i = 0; i < 5; i++) ai_modal_tick(m);
    CHECK(!ai_modal_captures_input(m), "mid fade-out must not capture");

    /* CLOSED: both false. */
    for (int i = 0; i < 12; i++) ai_modal_tick(m);
    CHECK(!ai_modal_visible(m), "a finished fade-out must not be visible");
    CHECK(!ai_modal_captures_input(m), "a closed modal captures nothing");

    /* Re-opening must capture again (the phase machine is not latched). */
    ai_modal_open(m, "q", "a");
    CHECK(ai_modal_captures_input(m), "re-opening must capture input again");

    ai_modal_free(m);

    CHECK(!ai_modal_captures_input(NULL), "a NULL modal captures nothing");
}

/* ── theming ────────────────────────────────────────────────────────── */

/* Mirrors ai_modal.c's am_cols().  Re-derived rather than shared: if
 * someone re-points a role at a different palette field, this test has to
 * fail rather than quietly follow it.  The maths underneath is shared —
 * see tools/test_wcag.h for why the byte order must live in one place. */
static uint32_t am_question(const kb_pal_t *p) { return wcag_alpha(p->label,     0xB3); }
static uint32_t am_answer  (const kb_pal_t *p) { return p->label; }
static uint32_t am_header  (const kb_pal_t *p) { return p->batt_warn; }
static uint32_t am_code    (const kb_pal_t *p) { return p->pinyin_fg; }
static uint32_t am_bullet  (const kb_pal_t *p) { return wcag_alpha(p->label,     0x99); }

/* The modal is a raised panel on the bottom screen, so it has to look
 * right on all five keyboard palettes.  Three things are checked per
 * theme, and each one caught a real defect when this test was written:
 *
 *   1. the body and the frame come from the palette at all;
 *   2. the frame is VISIBLE — key_border is what the modal used to reach
 *      for by analogy, and it equals panel_bg in DARK, so the frame would
 *      have disappeared in the default theme;
 *   3. every text role clears its contrast floor ON THE BODY SURFACE.
 *      hint_fg is the palette's dim role, but it is tuned against
 *      bottom_bg: on panel_bg it measures 2.92:1 in SOLAR.
 */
static void t_theme_colours(void) {
    static const term_theme_t themes[] = {
        THEME_DARK, THEME_MATRIX, THEME_MONO, THEME_SOLAR, THEME_CUSTOM,
    };
    static const char *const names[] = {
        "DARK", "MATRIX", "MONO", "SOLAR", "CUSTOM",
    };

    for (unsigned t = 0; t < sizeof(themes) / sizeof(themes[0]); t++) {
        kb_pal_t p = *kb_pal_for(themes[t]);
        const char *n = names[t];

        ai_modal_t *m = ai_modal_init();
        ai_modal_open(m, "q", "a");
        for (int i = 0; i < 40; i++) ai_modal_tick(m);   /* past FADE_IN */
        hstub_reset();
        ai_modal_draw(m, &p);

        CHECK(hstub_fb_at(M_X + 5, M_Y + 5) == p.panel_bg,
              "%s: modal body is %08x, want panel_bg %08x",
              n, hstub_fb_at(M_X + 5, M_Y + 5), p.panel_bg);
        CHECK(hstub_fb_at(M_X + 20, M_Y) == p.key_top,
              "%s: top border is not key_top", n);
        CHECK(hstub_fb_at(M_INNER_X + 4, M_SEP_Y) == p.key_top,
              "%s: separator is not key_top", n);

        CHECK(wcag_contrast(p.key_top, p.panel_bg) >= 1.5,
              "%s: hairline only %.2f:1 on panel_bg — the frame is invisible",
              n, wcag_contrast(p.key_top, p.panel_bg));

        CHECK(wcag_contrast(am_answer(&p), p.panel_bg) >= 4.5,
              "%s: answer %.2f:1 < 4.5", n, wcag_contrast(am_answer(&p), p.panel_bg));
        CHECK(wcag_contrast_over(am_question(&p), p.panel_bg) >= 4.5,
              "%s: question %.2f:1 < 4.5", n,
              wcag_contrast_over(am_question(&p), p.panel_bg));
        CHECK(wcag_contrast_over(am_bullet(&p), p.panel_bg) >= 4.5,
              "%s: bullet %.2f:1 < 4.5", n,
              wcag_contrast_over(am_bullet(&p), p.panel_bg));
        /* Header and code are accent runs, not body prose — the 3:1 tier
         * test_kb_theme.c already uses for tertiary text. */
        CHECK(wcag_contrast(am_header(&p), p.panel_bg) >= 3.0,
              "%s: header %.2f:1 < 3", n, wcag_contrast(am_header(&p), p.panel_bg));
        CHECK(wcag_contrast(am_code(&p), p.panel_bg) >= 3.0,
              "%s: code %.2f:1 < 3", n, wcag_contrast(am_code(&p), p.panel_bg));

        /* Header and code must stay distinguishable from each other and
         * from the answer: an inline code span that matches the prose is
         * not a code span.  MONO deliberately feeds six greys to 23 roles
         * and has batt_warn == pinyin_fg, so a collision there is the
         * palette's own — the check is that the MAPPING never invents one
         * on top of it. */
        CHECK(am_code(&p) != am_answer(&p),
              "%s: code and answer are the same colour", n);
        CHECK(am_header(&p) != am_code(&p) || p.batt_warn == p.pinyin_fg,
              "%s: header and code collide, and not because the palette "
              "says so", n);

        ai_modal_free(m);
    }

    /* A NULL palette must fall back to THEME_DARK rather than to zeroes —
     * a black-on-black panel is worse than a wrong theme. */
    ai_modal_t *m = ai_modal_init();
    ai_modal_open(m, "q", "a");
    for (int i = 0; i < 40; i++) ai_modal_tick(m);
    hstub_reset();
    ai_modal_draw(m, NULL);
    CHECK(hstub_fb_at(M_X + 5, M_Y + 5) == kb_pal_for(THEME_DARK)->panel_bg,
          "a NULL palette must fall back to THEME_DARK, got %08x",
          hstub_fb_at(M_X + 5, M_Y + 5));
    ai_modal_free(m);
}

int main(void) {
    printf("ai_modal host tests\n");

    g_pal = *kb_pal_for(THEME_DARK);

    t_phase_machine();
    t_layer_geometry();
    t_fade_is_translucent();
    t_close_mid_fadein_does_not_flash();
    t_input_capture_window();
    t_markdown_transform();
    t_question_ellipsis();
    t_answer_truncation();
    t_empty_input();
    t_theme_colours();

    printf("  %d check(s) run\n", checks);
    if (failures) {
        printf("  %d check(s) FAILED\n", failures);
        return 1;
    }
    printf("all ai-modal checks passed\n");
    return 0;
}
