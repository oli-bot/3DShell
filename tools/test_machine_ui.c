/*
 * Host-side tests for the SSH machine picker (source/machine_ui.c).
 *
 * The picker is the first screen of the whole program when more than one
 * machine is configured, so it is worth more than "it compiles":
 *
 *   - the state machine decides WHICH MACHINE GETS CONNECTED TO.  An
 *     off-by-one in the row wrap, a touch row mapped one slot out, or a
 *     confirm that reads the wrong field, all produce a working-looking
 *     picker that logs into the wrong host;
 *   - the page is themed like the project page, and the roles that were
 *     wrong there are wrong here in the same way: the selected row is a
 *     bright accent FILL (so `label` on it is 1.2:1 in DARK and MONO — the
 *     row goes blank), and the secondary tone cannot come from hint_fg
 *     (tuned against bottom_bg, 2.9:1 on panel_bg in SOLAR);
 *   - the row is now TWO lines when the machine has a name, and the name is
 *     editable from here, so the rename field is a second state machine
 *     living inside this one — one that writes to the user's config file.
 *
 * It compiles here because machine_ui.c names no C2D_/C3D_ symbol: every
 * rect and glyph goes through the renderer_draw_* wrappers, which
 * tools/stub/pui_host_stubs.c stands in for.  Two things the stub gives
 * us, used differently:
 *
 *   - the colour framebuffer answers "what colour is this pixel", which
 *     is how the row fills are checked;
 *   - the capture log answers "what would the screen say", which is how
 *     the top screen's per-machine fields are checked.
 *
 * font_atlas.c / font_data.c are linked for real (both are citro2d-free)
 * so the glyph boxes the ink check scans are the ones the device gets,
 * and utf8_next.c comes along because the row labels are clipped through
 * source/text_fit.h.
 *
 * Run via: ./tools/test_machine_ui.sh
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "machine_ui.h"
#include "pui_host_stubs.h"
#include "test_wcag.h"

/* ── harness ────────────────────────────────────────────────────────── */

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failures++; \
    } else { \
        fprintf(stderr, "ok   %s\n", msg); \
    } \
} while (0)

/* For the per-theme cases, which would otherwise be five silent passes —
 * a CHECKF that never prints leaves no way to tell "all five themes were
 * checked" from "the loop never ran".  The summary line counts them. */
#define CHECKF(cond, fmt, ...) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: " fmt "\n", __VA_ARGS__); \
        failures++; \
    } \
} while (0)

/* Geometry mirrored from machine_ui.c.  Deliberately re-typed rather than
 * shared: if someone moves a row, these tests must fail rather than
 * follow it. */
#define M_ROW_H   40
#define M_LIST_Y0 24
#define M_LEFT    12
#define M_W       296
/* The drawn row is M_ROW_H - 6 tall, with a 1px border line stamped on
 * its first and last row.  So the visible fill is rows y+1 .. y+h-2. */
#define M_ROW_H_INNER (M_ROW_H - 6)
/* The scroll window: how many rows are drawn, and where the footer hint
 * starts.  M_LIST_Y0 + M_VIS * M_ROW_H == M_FOOTER_Y is the whole point —
 * a sixth row would be drawn on the hint line. */
#define M_VIS      5
#define M_FOOTER_Y 224

/* The ✎ strip and the row's text box, also mirrored. */
#define M_EDIT_W  30
#define M_EDIT_X  (M_LEFT + M_W - M_EDIT_W)
#define M_TXT_X   (M_LEFT + 16)
#define M_TXT_W   (M_EDIT_X - 6 - M_TXT_X)
#define M_L1_DY   4
#define M_L2_DY   19
#define M_ONE_DY  ((M_ROW_H_INNER - 12) / 2)

/* Where the top screen's chrome sits, mirrored the same way. */
#define M_TOP_PAD  16
#define M_TOP_W    368
#define T_RULE_Y   68
#define T_RULE_X0  16
#define T_RULE_X1  384

/* The terminal's DARK ink.  Only used for the one discriminating assertion
 * that the top screen stopped wearing it (see t_top_screen_palette). */
#define TERM_DARK_FG 0xc0caf5ff

/* ── fixture ────────────────────────────────────────────────────────── */

static machine_ui_t *ui;
static ssh_config_t  cfg;

static void set_machine(int i, const char *host, const char *user,
                        int port, const char *key) {
    ssh_machine_t *m = &cfg.machines[i];
    memset(m, 0, sizeof(*m));
    snprintf(m->host, sizeof(m->host), "%s", host);
    snprintf(m->user, sizeof(m->user), "%s", user);
    m->port = port;
    snprintf(m->key_path, sizeof(m->key_path), "%s", key);
}

static void set_name(int i, const char *name) {
    snprintf(cfg.machines[i].name, sizeof(cfg.machines[i].name), "%s", name);
}

/* Give every machine a name.  Separate from reset() so the unnamed layout —
 * which is what every config.ini written before names existed produces —
 * stays the default fixture. */
static void name_all(int n) {
    static const char *names[] = {"alpha", "beta", "gamma", "delta",
                                  "epsilon", "zeta", "eta", "theta"};
    for (int i = 0; i < n && i < 8; i++) set_name(i, names[i]);
}

static void reset(int nmachines) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.machine_count = nmachines;
    for (int i = 0; i < nmachines; i++) {
        char host[32], user[32], key[64];
        snprintf(host, sizeof(host), "host%d.example", i);
        snprintf(user, sizeof(user), "user%d", i);
        snprintf(key,  sizeof(key),  "/keys/id_%d", i);
        set_machine(i, host, user, 22 + i, key);
    }

    if (ui) machine_ui_free(ui);
    ui = machine_ui_create();

    hstub_reset();
    hstub_paint_text = 0;
}

/* ── input helpers ──────────────────────────────────────────────────── */

/* One key-down edge, no typing.  `out` is deliberately NOT pre-set here:
 * several cases assert that the page left it alone, which a helper that
 * wrote to it first would hide.  Callers initialise it to whatever
 * sentinel they are checking for. */
static int key_kt_idx(uint32_t down, const char *kt, int *out) {
    return machine_ui_tick(ui, &cfg, down, 0, kt, -1, -1, 0, 0, out);
}

static int key_idx(uint32_t down, int *out) {
    return key_kt_idx(down, NULL, out);
}

static int key(uint32_t down) {
    int ignored = -1;
    return key_idx(down, &ignored);
}

/* One frame of typing: the soft keyboard's byte emission, no key edge. */
static int type(const char *kt) {
    int ignored = -1;
    return key_kt_idx(0, kt, &ignored);
}

/* A tap on the bottom screen: the press edge carries touch_down = 1, and
 * the release is a separate frame with touch_pressed = 0. */
static int tap(int x, int y, int *out) {
    int rc = machine_ui_tick(ui, &cfg, 0, 0, NULL, x, y, 1, 1, out);
    machine_ui_tick(ui, &cfg, 0, 0, NULL, -1, -1, 0, 0, out);
    return rc;
}

static void draw_bot(const kb_pal_t *pal) {
    hstub_reset();
    machine_ui_draw_bot(ui, &cfg, NULL, pal);
}

static void draw_top(const kb_pal_t *pal) {
    hstub_reset();
    machine_ui_draw_top(ui, &cfg, NULL, pal);
}

static const kb_pal_t *dark(void) { return kb_pal_for(THEME_DARK); }

/* The fill colour of row `i`, sampled inside the drawn strip and clear of
 * the label's text box (which starts at M_TXT_X) and of the ✎ strip. */
static uint32_t row_fill(int i) {
    return hstub_fb_at(M_LEFT + 5, M_LIST_Y0 + i * M_ROW_H + 10);
}

/* The 1px line stamped on the row's top and bottom edge. */
static uint32_t row_edge(int i, int bottom) {
    int y = M_LIST_Y0 + i * M_ROW_H + (bottom ? M_ROW_H_INNER - 1 : 0);
    return hstub_fb_at(M_LEFT + 5, y);
}

/* The darkest pixel in row `i`'s text box over the 12-px band starting at
 * `dy`, expressed as its contrast against that row's fill.
 *
 * This is the only check that turns text painting on.  Glyph edges are
 * anti-aliased toward the row's fill, so the useful reading is the
 * DARKEST pixel: a fully covered pixel is the ink itself and every other
 * pixel is that ink blended toward the fill, so the darkest pixel's
 * contrast against the fill is the ink's, or better.  It is what makes
 * "the selected row is drawn in the dark ink" a real assertion rather
 * than a restatement of the palette.
 *
 * The band matters on a two-line row: the name and the address are
 * different tones on a resting row, and a window straddling both would
 * only ever measure the darker of the two. */
static double band_contrast(int i, int dy) {
    uint32_t fill = row_fill(i);
    int y0 = M_LIST_Y0 + i * M_ROW_H + dy;
    double best = 1e9;
    uint32_t darkest = fill;
    for (int y = y0; y < y0 + 12; y++) {
        for (int x = M_TXT_X; x < M_TXT_X + 60; x++) {
            uint32_t c = hstub_fb_at(x, y);
            if (c == fill) continue;
            double l = wcag_lum(c);
            if (l < best) { best = l; darkest = c; }
        }
    }
    return wcag_contrast(darkest, fill);
}

/* ── state machine ──────────────────────────────────────────────────── */

static void t_open_close(void) {
    reset(3);

    CHECK(!machine_ui_visible(ui), "a fresh picker is not visible");

    CHECK(machine_ui_open(ui, &cfg, 0, 0) == 1, "open() reports success");
    CHECK(machine_ui_visible(ui), "open() makes the picker visible");
    CHECK(!machine_ui_wants_keyboard(ui),
          "the picker does not want the keyboard until a rename starts");
    CHECK(machine_ui_renamed_index(ui) == -1,
          "a fresh picker has no committed rename");

    machine_ui_close(ui);
    CHECK(!machine_ui_visible(ui), "close() hides it");

    /* Closing twice, and closing a fresh handle, must be harmless. */
    machine_ui_close(ui);
    CHECK(!machine_ui_visible(ui), "close() is idempotent");

    /* NULL everywhere the API allows it. */
    CHECK(!machine_ui_visible(NULL), "a NULL picker is never visible");
    CHECK(machine_ui_open(NULL, &cfg, 0, 0) == 0, "open(NULL) reports failure");
    CHECK(!machine_ui_wants_keyboard(NULL), "a NULL picker wants no keyboard");
    CHECK(machine_ui_renamed_index(NULL) == -1, "a NULL picker renamed nothing");
    CHECK(machine_ui_renamed_name(NULL) != NULL,
          "renamed_name(NULL) is an empty string, not a crash");
    machine_ui_close(NULL);
    machine_ui_free(NULL);

    /* Ticking a closed picker must not resurrect it, and must not write
     * an index for main.c to connect to. */
    int out = 7;
    CHECK(key_idx(KEY_A, &out) == MUI_ACT_NONE, "A on a closed picker is a no-op");
    CHECK(out == 7, "and it writes no index");
    CHECK(!machine_ui_visible(ui), "and the picker stays closed");
}

/* The pre-highlight is clamped, not trusted: it comes from
 * settings.ini's selected_machine, which a hand-edited file can set past
 * the end of the list. */
static void t_open_clamps_initial_index(void) {
    reset(3);

    machine_ui_open(ui, &cfg, 2, 0);
    draw_top(dark());
    CHECK(hstub_saw("主机: host2.example"),
          "initial_index 2 pre-highlights the third machine");

    machine_ui_open(ui, &cfg, 99, 0);
    draw_top(dark());
    CHECK(hstub_saw("主机: host0.example"),
          "an out-of-range initial_index falls back to the first machine");

    machine_ui_open(ui, &cfg, -1, 0);
    draw_top(dark());
    CHECK(hstub_saw("主机: host0.example"),
          "a negative initial_index falls back to the first machine");

    /* No machines configured: the page still opens on a single row. */
    reset(0);
    machine_ui_open(ui, &cfg, 0, 0);
    CHECK(machine_ui_visible(ui), "the picker opens with no machines configured");
    int out = -1;
    CHECK(key_idx(KEY_DOWN, &out) == MUI_ACT_NONE, "Down with one row is not a confirm");
    CHECK(key_idx(KEY_UP, &out) == MUI_ACT_NONE, "Up with one row is not a confirm");
    draw_bot(dark());
    CHECK(hstub_rect_calls > 0, "the empty list still paints its row");
}

static void t_navigate_wraps(void) {
    reset(3);
    machine_ui_open(ui, &cfg, 0, 0);

    int out = -1;
    CHECK(key_idx(KEY_DOWN, &out) == MUI_ACT_NONE && out == -1,
          "Down is not a confirm");
    draw_top(dark());
    CHECK(hstub_saw("主机: host1.example"), "Down moves to the second machine");

    key(KEY_DOWN);
    draw_top(dark());
    CHECK(hstub_saw("主机: host2.example"), "Down moves to the third machine");

    /* Past the end: back to the first, not stuck and not out of range. */
    key(KEY_DOWN);
    draw_top(dark());
    CHECK(hstub_saw("主机: host0.example"), "Down wraps from the last to the first");

    key(KEY_UP);
    draw_top(dark());
    CHECK(hstub_saw("主机: host2.example"), "Up wraps from the first to the last");

    /* The counter is 1-based and names the total. */
    draw_top(dark());
    CHECK(hstub_saw("3 / 3"), "the counter is 1-based over the machine count");
}

static void t_confirm_and_cancel(void) {
    reset(3);
    machine_ui_open(ui, &cfg, 0, 0);

    int out = -1;
    key(KEY_DOWN);                             /* -> machine 1 */
    CHECK(key_idx(KEY_A, &out) == MUI_ACT_CONNECT, "A reports a confirm");
    CHECK(out == 1, "A reports the highlighted machine");

    /* B cancels without touching the out-parameter — main.c must not see
     * a stale index it could connect to. */
    out = 7;
    CHECK(key_idx(KEY_B, &out) == MUI_ACT_CANCEL, "B reports a cancel");
    CHECK(out == 7, "B leaves the out-parameter alone");

    /* A and B in the same frame: B is checked first, so a cancel wins. */
    reset(3);
    machine_ui_open(ui, &cfg, 1, 0);
    out = -1;
    CHECK(key_idx(KEY_B | KEY_A, &out) == MUI_ACT_CANCEL,
          "a frame carrying both A and B cancels");
    CHECK(out == -1, "the cancelled frame writes no index");

    /* A confirm on a closed picker must not fire. */
    machine_ui_close(ui);
    out = -1;
    CHECK(key_idx(KEY_A, &out) == MUI_ACT_NONE, "A on a closed picker does nothing");
    CHECK(out == -1, "a closed picker writes no index");
}

static void t_touch_rows(void) {
    reset(3);
    machine_ui_open(ui, &cfg, 0, 0);

    int out = -1;
    /* The row index is (ty - M_LIST_Y0) / M_ROW_H. */
    CHECK(tap(M_LEFT + 40, M_LIST_Y0 + 1 * M_ROW_H + 5, &out) == MUI_ACT_CONNECT,
          "a tap on row 1 confirms");
    CHECK(out == 1, "a tap on row 1 confirms machine 1");

    CHECK(tap(M_LEFT + 40, M_LIST_Y0 + 2 * M_ROW_H + 5, &out) == MUI_ACT_CONNECT,
          "a tap on the last row confirms");
    CHECK(out == 2, "a tap on the last row confirms the last machine");

    /* Inside the row's own strip, not just anywhere in the pitch: the strip
     * is M_ROW_H_INNER tall and the remaining 6 px of the pitch are the gap
     * between rows, which is deliberately inert. */
    CHECK(tap(M_LEFT + 40, M_LIST_Y0 + 1 * M_ROW_H + M_ROW_H_INNER - 1, &out)
              == MUI_ACT_CONNECT,
          "the last pixel of a row's strip still maps to that row");
    CHECK(out == 1, "a tap on the strip's bottom edge is still row 1");
    out = -1;
    CHECK(tap(M_LEFT + 40, M_LIST_Y0 + 1 * M_ROW_H + M_ROW_H_INNER, &out)
              == MUI_ACT_NONE,
          "a tap in the gap under a row is ignored, not given to the row above");
    CHECK(tap(M_LEFT + 40, M_LIST_Y0 + 1 * M_ROW_H + M_ROW_H - 1, &out)
              == MUI_ACT_NONE,
          "nor at the bottom of the gap");
    CHECK(out == -1, "the gap taps wrote no index");

    /* Left of the list, and above it, are not row hits. */
    out = -1;
    CHECK(tap(M_LEFT - 1, M_LIST_Y0 + 5, &out) == MUI_ACT_NONE,
          "a tap left of the list is ignored");
    CHECK(tap(M_LEFT + M_W + 1, M_LIST_Y0 + 5, &out) == MUI_ACT_NONE,
          "a tap right of the list is ignored");
    CHECK(tap(M_LEFT + 40, M_LIST_Y0 - 1, &out) == MUI_ACT_NONE,
          "a tap above the list is ignored");
    CHECK(out == -1, "the ignored taps wrote no index");

    /* A slot inside the window but past the last machine is not selectable. */
    CHECK(tap(M_LEFT + 40, M_LIST_Y0 + 3 * M_ROW_H + 5, &out) == MUI_ACT_NONE,
          "a tap on an empty slot is ignored");
    CHECK(out == -1, "the empty-slot tap wrote no index");

    /* The footer band is help text, not a row.  It used to fall through to
     * slot 5 — so with six or more machines configured, tapping the
     * "Up/Down select - A confirm" line connected to machine 6. */
    CHECK(tap(M_LEFT + 40, M_FOOTER_Y, &out) == MUI_ACT_NONE,
          "a tap on the footer hint line is ignored");
    CHECK(tap(M_LEFT + 40, 239, &out) == MUI_ACT_NONE,
          "and at the very bottom of the screen");
    CHECK(out == -1, "the footer taps wrote no index");

    /* A drag across the rows moves the highlight but must NOT confirm —
     * confirming on every move would connect on the way past. */
    machine_ui_open(ui, &cfg, 0, 0);
    out = -1;
    int rc = machine_ui_tick(ui, &cfg, 0, 0, NULL, M_LEFT + 40,
                             M_LIST_Y0 + 2 * M_ROW_H + 5, 1, 0, &out);
    CHECK(rc == MUI_ACT_NONE, "the press edge without touch_down does not confirm");
    CHECK(out == -1, "the press edge without touch_down writes no index");
    draw_top(dark());
    CHECK(hstub_saw("主机: host2.example"),
          "the press edge still moves the highlight");
}

/* ── the ✎ strip ────────────────────────────────────────────────────── */

/* The strip is a rename hit, not a connect hit — and it is the ONLY route
 * to the rename field for a touch-only user, since the row body already
 * means "connect".  Both halves are asserted, because the failure mode is
 * silent: a strip that connected would look like the feature was missing,
 * and a row body that renamed would look like the picker had stopped
 * working. */
static void t_edit_strip(void) {
    reset(3);
    name_all(3);
    machine_ui_open(ui, &cfg, 0, 0);

    int out = 7;
    CHECK(tap(M_EDIT_X + 4, M_LIST_Y0 + 1 * M_ROW_H + 5, &out) == MUI_ACT_NONE,
          "a tap on the ✎ strip is not a connect");
    CHECK(out == 7, "and it writes no index to connect to");
    CHECK(machine_ui_wants_keyboard(ui),
          "a tap on the ✎ strip opens the rename field");
    CHECK(machine_ui_renamed_index(ui) == -1,
          "and opening the field is not yet a commit");

    /* The highlight followed the tap, so the top screen describes the
     * machine being renamed. */
    draw_top(dark());
    CHECK(hstub_saw("主机: host1.example"),
          "the ✎ tap moves the highlight to the row it was on");

    /* Right up to the divider is still the row body. */
    machine_ui_open(ui, &cfg, 0, 0);
    out = -1;
    CHECK(tap(M_EDIT_X - 1, M_LIST_Y0 + 1 * M_ROW_H + 5, &out) == MUI_ACT_CONNECT,
          "one pixel left of the divider is still the row body");
    CHECK(out == 1, "and it connects to that row");
    CHECK(!machine_ui_wants_keyboard(ui), "without opening the field");

    /* The strip is drawn on every row, not just the highlighted one — the
     * affordance has to be visible before you go looking for it. */
    machine_ui_open(ui, &cfg, 0, 0);
    draw_bot(dark());
    CHECK(hstub_saw("✎"), "the ✎ glyph is drawn");
    for (int slot = 0; slot < 3; slot++)
        CHECKF(hstub_fb_at(M_EDIT_X, M_LIST_Y0 + slot * M_ROW_H + 17)
                   == dark()->key_border,
               "slot %d carries the strip's divider", slot);

    /* A strip tap below the last machine is not a hit at all. */
    machine_ui_open(ui, &cfg, 0, 0);
    out = 7;
    CHECK(tap(M_EDIT_X + 4, M_LIST_Y0 + 4 * M_ROW_H + 5, &out) == MUI_ACT_NONE,
          "a ✎ tap on an empty slot is ignored");
    CHECK(!machine_ui_wants_keyboard(ui), "and opens nothing");
}

/* ── drawing: top screen ────────────────────────────────────────────── */

static void t_top_screen(void) {
    reset(3);
    set_name(1, "工作机");
    machine_ui_open(ui, &cfg, 1, 0);
    draw_top(dark());

    /* The page must NOT paint its own background: render_frame() has
     * already cleared this target to the palette's background, and a
     * second fill here would pin the top screen to one theme's colour —
     * the exact thing taking the palette is meant to avoid.  A pixel the
     * page never touches reads 0 in this framebuffer. */
    CHECK(hstub_fb_at(T_RULE_X1 - 1, 230) == 0,
          "the top screen paints no background of its own");
    CHECK(hstub_fb_at(0, 0) == 0, "not even at the origin");

    /* The one piece of chrome: a hairline rule in the border tone, one
     * pixel tall, so it stays a hairline in every theme. */
    uint32_t fg = dark()->label;
    CHECK(hstub_fb_at(T_RULE_X0 + 4, T_RULE_Y) == wcag_alpha(fg, 0x55),
          "the header rule is one row of the border tone");
    CHECK(hstub_fb_at(T_RULE_X0 + 4, T_RULE_Y - 1) == 0,
          "the rule is exactly one pixel tall");
    CHECK(hstub_fb_at(T_RULE_X0 + 4, T_RULE_Y + 1) == 0,
          "the rule has nothing under it either");
    CHECK(hstub_fb_at(T_RULE_X1, T_RULE_Y) == 0,
          "the rule stops before the screen edge");

    /* The fields describe the highlighted machine, not the first one. */
    CHECK(hstub_saw("名称: 工作机"),      "the top screen shows the name");
    CHECK(hstub_saw("主机: host1.example"), "the top screen shows the host");
    CHECK(hstub_saw("用户: user1"),         "the top screen shows the user");
    CHECK(hstub_saw("端口: 23"),            "the top screen shows the port");
    CHECK(hstub_saw("密钥: /keys/id_1"),    "the top screen shows the key path");
    CHECK(hstub_saw("2 / 3"),               "the top screen shows the position");

    /* An unnamed machine says so rather than showing an empty field — an
     * empty line reads as a rendering bug. */
    set_name(1, "");
    draw_top(dark());
    CHECK(hstub_saw("名称: （未命名）"),
          "an unnamed machine says so on the top screen");

    /* Every value is clipped to the panel.  A 255-byte host used to run off
     * the right edge of a 400-px screen with nothing to say it had.  The
     * clip keeps the longest prefix that leaves room for the 3-cell
     * ellipsis, so the run of h's is long — 40 cells is 240 px, well inside
     * the 368-px rule minus the "主机: " label — and 60 of them is not. */
    memset(cfg.machines[1].host, 'h', sizeof(cfg.machines[1].host) - 1);
    cfg.machines[1].host[sizeof(cfg.machines[1].host) - 1] = 0;
    draw_top(dark());
    {
        char run[80];
        memset(run, 'h', 40); run[40] = 0; strcat(run, "...");
        CHECK(hstub_saw(run),
              "an over-long host is ellipsised, not run off the panel");
        memset(run, 'h', 60); run[60] = 0;
        CHECK(!hstub_saw(run), "and it is clipped rather than drawn whole");
    }

    /* NULL-tolerance.  main.c always passes a live palette, but a page that
     * segfaults on a dropped handle is not worth the risk. */
    hstub_reset();
    machine_ui_draw_top(NULL, &cfg, NULL, dark());
    machine_ui_draw_top(ui, NULL, NULL, dark());
    CHECK(hstub_rect_calls == 0, "a NULL argument paints nothing");
    CHECK(hstub_text_calls == 0, "a NULL argument draws no text");

    /* A NULL palette falls back to THEME_DARK, as on the bottom screen. */
    hstub_reset();
    machine_ui_draw_top(ui, &cfg, NULL, NULL);
    CHECK(hstub_fb_at(T_RULE_X0 + 4, T_RULE_Y) ==
              wcag_alpha(kb_pal_for(THEME_DARK)->label, 0x55),
          "a NULL palette falls back to THEME_DARK");
}

/* The top screen follows the keyboard palette, and this is the assertion
 * that says so.  Before the change it took the terminal's ink, which is a
 * DIFFERENT table: the two agree in MONO and SOLAR, but DARK's terminal ink
 * is 0xc0caf5 against the palette's 0xcdd6f4, so DARK is the one theme
 * where "it came from the palette" is a discriminating claim rather than a
 * restatement of the code.
 *
 * TWO bands are scanned, not one.  The first version of this test only
 * looked at the title (y 16..27), and that turned out to pin nothing about
 * the body: the title is drawn in `title` while the five field values are
 * drawn in `ink`/`dim`, so pointing THOSE at the terminal's ink passed the
 * suite.  A reverse control caught it (the probe was written, run, and came
 * back green — the assertion was weaker than its own message claimed).
 * Both bands are therefore asserted, and the message says which inks they
 * cover. */
static void t_top_screen_palette(void) {
    kb_pal_t p = *kb_pal_for(THEME_DARK);
    reset(1);
    set_name(0, "alpha");
    machine_ui_open(ui, &cfg, 0, 0);

    hstub_reset();
    hstub_paint_text = 1;
    machine_ui_draw_top(ui, &cfg, NULL, &p);
    hstub_paint_text = 0;

    /* Header band: the title.  Body band: the five field values. */
    static const int bands[][2] = { { 16, 28 }, { 96, 220 } };
    for (unsigned b = 0; b < 2; b++) {
        int saw_pal = 0, saw_term = 0;
        for (int y = bands[b][0]; y < bands[b][1]; y++) {
            for (int x = M_TOP_PAD; x < M_TOP_PAD + 140; x++) {
                uint32_t c = hstub_fb_at(x, y);
                if (c == p.label)     saw_pal  = 1;
                if (c == TERM_DARK_FG) saw_term = 1;
            }
        }
        CHECKF(saw_pal,
               "the DARK top screen's band %u is drawn in the palette's label",
               b);
        CHECKF(!saw_term,
               "and band %u is not drawn in the terminal's DARK ink "
               "(0xc0caf5) — the two tables differ, so this is the assertion "
               "that pins the source", b);
    }

    /* The same, per theme, for the rule and the absence of a background. */
    static const term_theme_t themes[] = {
        THEME_DARK, THEME_MATRIX, THEME_MONO, THEME_SOLAR, THEME_CUSTOM,
    };
    for (unsigned t = 0; t < sizeof(themes) / sizeof(themes[0]); t++) {
        kb_pal_t tp = *kb_pal_for(themes[t]);
        const char *n = wcag_theme_name(themes[t]);
        reset(1);
        set_name(0, "alpha");
        machine_ui_open(ui, &cfg, 0, 0);
        hstub_reset();
        machine_ui_draw_top(ui, &cfg, NULL, &tp);

        CHECKF(hstub_fb_at(T_RULE_X0 + 4, T_RULE_Y) ==
                   wcag_alpha(tp.label, 0x55),
               "%s: the header rule is the palette's ink at 0x55", n);
        CHECKF(hstub_fb_at(T_RULE_X1 - 1, 230) == 0,
               "%s: the top screen still paints no background", n);
    }
}

/* ── drawing: bottom screen, layout ─────────────────────────────────── */

static void t_bottom_screen_layout(void) {
    kb_pal_t p = *kb_pal_for(THEME_DARK);
    reset(3);
    machine_ui_open(ui, &cfg, 1, 0);
    draw_bot(&p);

    CHECK(hstub_fb_at(0, 0) == p.bottom_bg,
          "the bottom screen is cleared to the keyboard background");
    CHECK(hstub_fb_at(319, 239) == p.bottom_bg, "and to its far corner");

    CHECK(row_fill(1) == p.key_accent, "the highlighted row is the accent fill");
    CHECK(row_fill(0) == p.panel_bg,   "a resting row is the panel fill");
    CHECK(row_fill(2) == p.panel_bg,   "and so is the one after it");

    CHECK(row_edge(1, 0) == p.key_border, "the row's top edge is the border tone");
    CHECK(row_edge(1, 1) == p.key_border, "the row's bottom edge too");

    /* The rows are separated by a gap of the page background, so the
     * highlight is legible even in the two themes where key_border
     * equals panel_bg and a resting row's edge line is a no-op. */
    int gap_y = M_LIST_Y0 + M_ROW_H - 2;
    CHECK(hstub_fb_at(M_LEFT + 5, gap_y) == p.bottom_bg,
          "there is a background gap between two rows");

    /* The unnamed row: one centred line, exactly as before names existed. */
    CHECK(hstub_saw("1. host0.example"), "an unnamed row is labelled with its host");
    CHECK(hstub_saw("3. host2.example:24"),
          "and carries its port when it is not 22 — five machines on one "
          "host are otherwise indistinguishable");
    /* 1-based, because the top screen's counter is: a list numbered from 0
     * next to "2 / 3" named a different machine than the one it pointed at. */
    CHECK(!hstub_saw("0. host0.example"),
          "and the list is not numbered from zero");
    CHECK(!hstub_saw("1. host0.example:22"),
          "a default port is not spelled out");

    /* NULL palette: the header promises a palette, but a NULL must fall
     * back rather than dereference. */
    hstub_reset();
    machine_ui_draw_bot(ui, &cfg, NULL, NULL);
    CHECK(hstub_fb_at(0, 0) == kb_pal_for(THEME_DARK)->bottom_bg,
          "a NULL palette falls back to THEME_DARK");
    CHECK(row_fill(1) == kb_pal_for(THEME_DARK)->key_accent,
          "and the highlight follows it");

    hstub_reset();
    machine_ui_draw_bot(NULL, &cfg, NULL, &p);
    machine_ui_draw_bot(ui, NULL, NULL, &p);
    CHECK(hstub_rect_calls == 0, "a NULL picker or config paints nothing");
}

/* A named machine gets two lines: what it is called, then how to reach it.
 * The two are different tones and different y bands, which is what makes
 * the row scannable rather than one long label. */
static void t_named_rows_are_two_lines(void) {
    kb_pal_t p = *kb_pal_for(THEME_DARK);
    reset(3);
    set_name(0, "家里");
    set_name(1, "build-box");
    machine_ui_open(ui, &cfg, 0, 0);
    draw_bot(&p);

    CHECK(hstub_saw("1. 家里"), "a named row shows its name");
    CHECK(hstub_saw("host0.example"), "and its address");
    CHECK(hstub_saw("2. build-box"), "the second named row too");
    CHECK(hstub_saw("host1.example:23"),
          "with the port appended when it is not 22");
    CHECK(hstub_saw("3. host2.example:24"),
          "an unnamed row still shows host:port on its one line");
    CHECK(!hstub_saw("2. host1.example:23"),
          "and a named row does NOT repeat its address on the name line — "
          "the two are separate lines, not one concatenation");

    /* The name is above the address, not beside it — the whole point of the
     * two-line layout is that a long name and a long host do not compete
     * for one line's worth of width. */
    hstub_reset();
    hstub_paint_text = 1;
    machine_ui_draw_bot(ui, &cfg, NULL, &p);
    hstub_paint_text = 0;
    int name_band_has_ink = 0, addr_band_has_ink = 0;
    {
        int y = M_LIST_Y0;
        for (int x = M_TXT_X; x < M_TXT_X + 60; x++) {
            for (int k = 0; k < 12; k++) {
                if (hstub_fb_at(x, y + M_L1_DY + k) != p.key_accent)
                    name_band_has_ink = 1;
                if (hstub_fb_at(x, y + M_L2_DY + k) != p.key_accent)
                    addr_band_has_ink = 1;
            }
        }
    }
    CHECK(name_band_has_ink, "the name is drawn in the upper band");
    CHECK(addr_band_has_ink, "the address is drawn in the lower band");

    /* The two lines do not overlap: the 3 px between the bands is clear on
     * the highlighted row (the fill shows through). */
    {
        int y = M_LIST_Y0;
        int clear = 1;
        for (int x = M_TXT_X; x < M_TXT_X + 60; x++)
            for (int k = M_L1_DY + 12; k < M_L2_DY; k++)
                if (hstub_fb_at(x, y + k) != p.key_accent) clear = 0;
        CHECK(clear, "there is a gap between the name and the address");
    }

    /* An over-long ADDRESS is ellipsised.  The name cannot be (it is capped
     * at 31 bytes, 204 px of a 244 px box), but the host is
     * CONFIG_STR_MAX wide. */
    memset(cfg.machines[0].host, 'h', sizeof(cfg.machines[0].host) - 1);
    cfg.machines[0].host[sizeof(cfg.machines[0].host) - 1] = 0;
    draw_bot(&p);
    CHECK(hstub_saw("hhhhhhhh..."),
          "an over-long address is ellipsised inside the row's text box");

    /* The row's right-hand end is the ✎ strip, so the text box must stop
     * before it: nothing is drawn under the strip's divider. */
    hstub_reset();
    hstub_paint_text = 1;
    machine_ui_draw_bot(ui, &cfg, NULL, &p);
    hstub_paint_text = 0;
    int ink_under_strip = 0;
    for (int y = M_LIST_Y0; y < M_LIST_Y0 + M_ROW_H_INNER; y++)
        for (int x = M_EDIT_X + 1; x < M_EDIT_X + 5; x++)
            if (hstub_fb_at(x, y) != p.key_accent &&
                hstub_fb_at(x, y) != p.key_border) ink_under_strip = 1;
    CHECK(!ink_under_strip, "no text is drawn under the ✎ strip");
}

/* ── drawing: bottom screen, per theme ──────────────────────────────── */

/* Mirrors machine_ui.c's mui_bot_cols().  Re-derived rather than shared:
 * if someone re-points a role at a different palette field, this test has
 * to fail rather than quietly follow it. */
static uint32_t mui_ink(const kb_pal_t *p)     { return p->label; }
static uint32_t mui_dim(const kb_pal_t *p)     { return wcag_alpha(p->label, 0xB3); }
static uint32_t mui_sel_ink(const kb_pal_t *p) { return p->ink_dark; }

/* The picker has to hold up on all five keyboard palettes.  Four things per
 * theme, each of which caught a real defect in the page family:
 *
 *   1. the fills come from the palette at all;
 *   2. the highlighted row is the accent fill AND its label is painted in
 *      the palette's dark-ink family — label on key_accent measures
 *      1.31:1 in DARK, 1.20:1 in MONO and 3.00:1 in SOLAR, so a row drawn
 *      in `label` is a row you cannot read;
 *   3. the resting rows keep the light ink, and it clears 4.5:1 on the
 *      panel fill;
 *   4. the address line clears 4.5:1 too — on BOTH fills.  It is a second
 *      tone, so it is the one most likely to be dimmed past legibility,
 *      and dimming it on the accent fill was measured and rejected:
 *      ink_dark at 0xC0 falls to 3.52:1 in SOLAR.
 */
static void t_theme_colours(void) {
    static const term_theme_t themes[] = {
        THEME_DARK, THEME_MATRIX, THEME_MONO, THEME_SOLAR, THEME_CUSTOM,
    };

    for (unsigned t = 0; t < sizeof(themes) / sizeof(themes[0]); t++) {
        kb_pal_t p = *kb_pal_for(themes[t]);
        const char *n = wcag_theme_name(themes[t]);

        reset(3);
        name_all(3);
        machine_ui_open(ui, &cfg, 1, 0);
        draw_bot(&p);

        CHECKF(row_fill(1) == p.key_accent,
               "%s: the highlighted row is key_accent", n);
        CHECKF(row_fill(0) == p.panel_bg,
               "%s: a resting row is panel_bg", n);

        /* The inks, read off the pixels rather than the palette. */
        hstub_reset();
        hstub_paint_text = 1;
        machine_ui_draw_bot(ui, &cfg, NULL, &p);
        hstub_paint_text = 0;

        double rest_name = band_contrast(0, M_L1_DY);
        double rest_addr = band_contrast(0, M_L2_DY);
        double sel_name  = band_contrast(1, M_L1_DY);
        double sel_addr  = band_contrast(1, M_L2_DY);

        CHECKF(sel_name >= 4.5,
               "%s: the highlighted row's name is %.2f:1 on the accent fill "
               "— it must use ink_dark (%.2f:1), not label (%.2f:1)",
               n, sel_name, wcag_contrast(p.ink_dark, p.key_accent),
               wcag_contrast(p.label, p.key_accent));
        CHECKF(sel_addr >= 4.5,
               "%s: the highlighted row's address line is %.2f:1 — dimming "
               "ink_dark on the accent fill drops it to %.2f:1, so both "
               "lines take the full dark ink",
               n, sel_addr, wcag_contrast_over(wcag_alpha(p.ink_dark, 0xC0),
                                               p.key_accent));
        CHECKF(rest_name >= 4.5,
               "%s: a resting row's name is only %.2f:1 on panel_bg", n, rest_name);
        CHECKF(rest_addr >= 4.5,
               "%s: a resting row's address line is only %.2f:1 on panel_bg",
               n, rest_addr);

        /* The mapping itself, so a palette change cannot make the two
         * checks above agree by accident. */
        CHECKF(mui_sel_ink(&p) != mui_ink(&p),
               "%s: ink_dark == label, so the two rows cannot be told apart", n);
        CHECKF(mui_dim(&p) != mui_ink(&p),
               "%s: the address tone is the name tone, so the two-line row "
               "is one line twice", n);
    }
}

/* ── eight machines: the ceiling DSSH_MACHINES_MAX allows ───────────── */

/* Five rows fit above the footer and DSSH_MACHINES_MAX is 8, so the picker
 * has to scroll — and it did not.  The rows were laid out from a fixed
 * origin with no window, so machines 6-8 were drawn at y=264..337, past the
 * bottom of a 240-px screen, while the D-pad still walked onto them: the
 * highlight was simply not on the display.  Worse, row 5 started exactly on
 * the footer hint line, and the touch mapper had no upper bound, so tapping
 * that line of help text *confirmed machine 6*.
 *
 * These are the assertions that would have caught all of it.  The suite only
 * ever built three-machine fixtures before, which is why none of them
 * existed. */
static void t_eight_machines_scroll(void) {
    kb_pal_t p = *kb_pal_for(THEME_DARK);
    reset(8);
    machine_ui_open(ui, &cfg, 0, 0);
    draw_bot(&p);

    /* Exactly the window is painted... */
    for (int slot = 0; slot < M_VIS; slot++)
        CHECKF(row_fill(slot) == (slot == 0 ? p.key_accent : p.panel_bg),
               "8 machines: slot %d carries a row", slot);
    /* ...and not one pixel of a row lands on the footer band. */
    for (int y = M_FOOTER_Y; y < 240; y++)
        CHECKF(hstub_fb_at(M_LEFT + 5, y) == p.bottom_bg,
               "8 machines: the footer band stays clear at y=%d", y);

    CHECK(!hstub_saw("8. host7.example"),
          "8 machines: the last machine is outside the window at the top");
    CHECK(hstub_saw("1. host0.example"), "8 machines: the window starts at 1");

    /* Walk to the bottom.  The window has to follow, or the highlight is
     * off-screen — the original bug, stated as an assertion. */
    for (int i = 0; i < 7; i++) key(KEY_DOWN);
    draw_bot(&p);
    CHECK(hstub_saw("8. host7.example"),
          "8 machines: after seven D-pad steps the last machine is drawn");
    CHECK(hstub_saw("4. host3.example"),
          "8 machines: the window slid exactly far enough");
    CHECK(!hstub_saw("3. host2.example"),
          "8 machines: and the rows above it scrolled out");
    int sel_on_screen = 0;
    for (int y = 0; y < M_FOOTER_Y; y++)
        if (hstub_fb_at(M_LEFT + 5, y) == p.key_accent) sel_on_screen = 1;
    CHECK(sel_on_screen, "8 machines: the highlighted row is on screen");

    draw_top(dark());
    CHECK(hstub_saw("8 / 8"), "8 machines: the counter agrees with the cursor");

    /* Touch maps through the window: with the window slid to machine 4, the
     * fifth slot is machine 8, so the row index must be first + slot. */
    machine_ui_open(ui, &cfg, 0, 0);
    for (int i = 0; i < 7; i++) key(KEY_DOWN);
    int out = -1;
    CHECK(tap(M_LEFT + 40, M_LIST_Y0 + 4 * M_ROW_H + 5, &out) == MUI_ACT_CONNECT,
          "8 machines: a tap on the fifth slot confirms");
    CHECK(out == 7, "8 machines: and it confirms machine 8, not machine 5");

    /* The footer regression itself.  With three machines a tap on the hint
     * line mapped to slot 5, which was past the end and therefore harmless —
     * which is exactly why the old three-machine fixture never caught it.
     * With six or more, slot 5 was a real machine: a tap on a line of help
     * text connected to a host. */
    machine_ui_open(ui, &cfg, 0, 0);
    out = -1;
    CHECK(tap(M_LEFT + 40, M_FOOTER_Y, &out) == MUI_ACT_NONE,
          "8 machines: a tap on the footer hint line is ignored");
    CHECK(tap(M_LEFT + 40, 239, &out) == MUI_ACT_NONE,
          "8 machines: and at the very bottom of the screen");
    CHECK(out == -1, "8 machines: the footer taps confirmed no machine");

    /* Up from the top wraps to the last machine and drags the window with
     * it — the other direction of the same clamp. */
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_UP);
    draw_bot(&p);
    CHECK(hstub_saw("8. host7.example"),
          "8 machines: Up from the first row wraps to the last and is drawn");
}

/* ── the rename field ───────────────────────────────────────────────── */

static void t_rename_opens_and_prefills(void) {
    reset(3);
    set_name(1, "工作机");
    machine_ui_open(ui, &cfg, 1, 0);

    int out = 7;
    CHECK(key_idx(KEY_X, &out) == MUI_ACT_NONE, "X is not a connect");
    CHECK(out == 7, "X writes no index");
    CHECK(machine_ui_wants_keyboard(ui), "X opens the rename field");
    CHECK(machine_ui_keyboard_armed(ui),
          "a key press arms the keyboard immediately — there is no finger to "
          "wait for");
    CHECK(machine_ui_renamed_index(ui) == -1, "opening the field is not a commit");

    draw_top(dark());
    CHECK(hstub_saw("给机器改名"), "the top screen says what is happening");
    CHECK(hstub_saw("工作机"), "and echoes the current name, so a rename can "
                               "be an edit rather than a retype");

    /* The ✎ tap is the other route in, and it must wait for the finger to
     * lift before the keyboard sees the touch — otherwise the tap that
     * opened the field also presses whatever key is under it. */
    reset(3);
    machine_ui_open(ui, &cfg, 0, 0);
    int ignored = -1;
    machine_ui_tick(ui, &cfg, 0, 0, NULL, M_EDIT_X + 4,
                    M_LIST_Y0 + 5, 1, 1, &ignored);
    CHECK(machine_ui_wants_keyboard(ui), "the ✎ tap opens the field");
    CHECK(!machine_ui_keyboard_armed(ui),
          "and leaves the keyboard disarmed until the finger lifts");
    machine_ui_tick(ui, &cfg, 0, 0, NULL, -1, -1, 0, 0, &ignored);
    CHECK(machine_ui_keyboard_armed(ui), "the release arms it");
}

static void t_rename_types_and_commits(void) {
    reset(3);
    machine_ui_open(ui, &cfg, 1, 0);

    CHECK(key(KEY_X) == MUI_ACT_NONE, "X opens the field");
    type("abc");
    draw_top(dark());
    CHECK(hstub_saw("abc"), "typed bytes land in the draft");

    type("\x7f");                              /* the keyboard's ⌫ */
    draw_top(dark());
    CHECK(hstub_saw("ab"), "backspace drops the last character");
    CHECK(!hstub_saw("abc"), "and the dropped one is gone");

    /* Commit with the keyboard's ✓, which emits "\r". */
    CHECK(type("\r") == MUI_ACT_RENAMED, "the ✓ key commits");
    CHECK(!machine_ui_wants_keyboard(ui), "and closes the field");
    CHECK(machine_ui_renamed_index(ui) == 1, "the commit names the machine");
    CHECK(strcmp(machine_ui_renamed_name(ui), "ab") == 0,
          "and carries the draft out to main.c");

    /* A commits too — the picker's own "A confirms" reading, so the key you
     * reach for is the key you expect. */
    reset(3);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    type("lab");
    int out = -1;
    CHECK(key_idx(KEY_A, &out) == MUI_ACT_RENAMED, "A commits the rename");
    CHECK(out == -1, "and writes no connect index");
    CHECK(machine_ui_renamed_index(ui) == 0, "the commit names machine 0");
    CHECK(strcmp(machine_ui_renamed_name(ui), "lab") == 0, "with the new name");

    /* "\t" is the other commit key the keyboard can send. */
    reset(3);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    type("x");
    CHECK(type("\t") == MUI_ACT_RENAMED, "tab also commits");

    /* B backs out and discards.  Nothing is written, and main.c is told
     * nothing — the point of a cancel. */
    reset(3);
    set_name(0, "keep-me");
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    type("zzz");
    CHECK(key(KEY_B) == MUI_ACT_NONE, "B leaves the field");
    CHECK(!machine_ui_wants_keyboard(ui), "and closes it");
    CHECK(machine_ui_renamed_index(ui) == -1,
          "a cancel reports no rename for main.c to persist");
    draw_bot(dark());
    CHECK(hstub_saw("1. keep-me"),
          "and the machine keeps the name it had");
    CHECK(!hstub_saw("zzz"), "the discarded draft is not on screen");

    /* START and SELECT also back out. */
    for (int i = 0; i < 2; i++) {
        uint32_t k = i ? KEY_SELECT : KEY_START;
        reset(3);
        machine_ui_open(ui, &cfg, 0, 0);
        key(KEY_X);
        CHECK(key(k) == MUI_ACT_NONE, i ? "SELECT backs out" : "START backs out");
        CHECK(machine_ui_renamed_index(ui) == -1, "and reports no rename");
    }

    /* While the field is up, nothing connects and nothing closes. */
    reset(3);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    int out2 = 7;
    CHECK(key_idx(KEY_DOWN, &out2) == MUI_ACT_NONE,
          "the D-pad does not move the highlight while typing");
    CHECK(out2 == 7, "and writes no index");
    CHECK(machine_ui_wants_keyboard(ui), "the field is still up");
}

static void t_rename_caps_and_sanitises(void) {
    reset(2);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);

    /* Fill past the field width.  The draft must stop at the cap, not
     * overflow it — and must still be valid UTF-8. */
    for (int i = 0; i < 60; i++) type("a");
    CHECK(type("\r") == MUI_ACT_RENAMED, "an over-long draft still commits");
    CHECK(strlen(machine_ui_renamed_name(ui)) == DSSH_MACHINE_NAME_MAX - 1,
          "the draft is capped at the field width");

    /* A CJK glyph costs three bytes, so the cap has to be applied on a
     * codepoint boundary or the name written to config.ini is torn. */
    reset(2);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    for (int i = 0; i < 20; i++) type("中");
    CHECK(type("\r") == MUI_ACT_RENAMED, "a CJK draft commits");
    {
        const char *n = machine_ui_renamed_name(ui);
        size_t len = strlen(n);
        CHECK(len > 0 && len <= DSSH_MACHINE_NAME_MAX - 1,
              "the CJK draft is inside the cap");
        CHECK(len % 3 == 0,
              "and it is a whole number of 3-byte glyphs, not a torn one");
    }

    /* Backspace on a CJK draft drops the whole glyph, not one byte. */
    reset(2);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    type("中中");
    type("\x7f");
    CHECK(type("\r") == MUI_ACT_RENAMED, "commit after a CJK backspace");
    CHECK(strcmp(machine_ui_renamed_name(ui), "中") == 0,
          "backspace drops one codepoint, not one byte");

    /* Control bytes are dropped: the keyboard cannot produce one, but a
     * name goes into config.ini as a single line and a stray \n would break
     * the file for every future load. */
    reset(2);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    type("a\nb\rc\td");
    CHECK(type("\r") == MUI_ACT_RENAMED, "commit after control bytes");
    CHECK(strcmp(machine_ui_renamed_name(ui), "abcd") == 0,
          "control bytes are stripped from the draft");

    /* Escape sequences (arrows, function keys) carry no meaning here. */
    reset(2);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    type("q");
    type("\x1b[A");
    CHECK(type("\r") == MUI_ACT_RENAMED, "commit after an escape sequence");
    CHECK(strcmp(machine_ui_renamed_name(ui), "q") == 0,
          "an escape sequence is ignored, not typed");
}

static void t_rename_clears_and_resets(void) {
    reset(2);
    set_name(0, "old");
    machine_ui_open(ui, &cfg, 0, 0);

    /* An empty draft is a legitimate commit: it clears the name and the
     * picker goes back to showing the host. */
    key(KEY_X);
    type("\x7f");  type("\x7f");  type("\x7f");
    CHECK(type("\r") == MUI_ACT_RENAMED, "an emptied draft commits");
    CHECK(machine_ui_renamed_name(ui)[0] == 0,
          "and carries an empty name, which is how a name is cleared");

    /* Closing the page with the field open must not leave main.c lending
     * the keyboard to a page that is not drawing. */
    reset(2);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    CHECK(machine_ui_wants_keyboard(ui), "the field is up");
    machine_ui_close(ui);
    CHECK(!machine_ui_wants_keyboard(ui),
          "closing the page drops the field with it");

    /* Reopening resets the committed result, so a stale name cannot be
     * persisted twice. */
    reset(2);
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    type("once");
    type("\r");
    CHECK(machine_ui_renamed_index(ui) == 0, "the rename is pending");
    machine_ui_open(ui, &cfg, 0, 0);
    CHECK(machine_ui_renamed_index(ui) == -1,
          "reopening the page clears the pending rename");
}

/* ── language ───────────────────────────────────────────────────────── */

/* The page used to print Chinese and English at once, whatever the interface
 * language was.  It now follows softkb_ui_lang() with the same two-language
 * convention as project_ui.c — including the field labels, which used to be
 * hardcoded English under a Chinese title. */
static void t_language(void) {
    reset(3);

    machine_ui_open(ui, &cfg, 0, 0);
    draw_top(dark());
    CHECK(hstub_saw("选择 SSH 机器"), "language 0 draws Chinese");
    CHECK(hstub_saw("名称:"), "language 0 localises the field labels");
    CHECK(hstub_saw("主机:"), "including the host");
    CHECK(!hstub_saw("Select SSH machine"), "language 0 is not bilingual");
    CHECK(!hstub_saw("Host:"), "and has no English field labels either");

    machine_ui_open(ui, &cfg, 0, 1);
    draw_top(dark());
    CHECK(hstub_saw("Select SSH machine"), "language 1 draws English");
    CHECK(hstub_saw("Name:"), "language 1 uses English field labels");
    CHECK(hstub_saw("Host:"), "including the host");
    CHECK(!hstub_saw("选择 SSH 机器"), "language 1 is not bilingual either");

    /* ja / ko / de / fr (2..5) take the English column, as on every other
     * page that only has a zh/en table. */
    machine_ui_open(ui, &cfg, 0, 3);
    draw_top(dark());
    CHECK(hstub_saw("Select SSH machine"),
          "a third language falls back to English");

    /* The rename field is localised too. */
    machine_ui_open(ui, &cfg, 0, 0);
    key(KEY_X);
    draw_top(dark());
    CHECK(hstub_saw("给机器改名"), "the rename field is Chinese at lang 0");
    CHECK(hstub_saw("（空 = 用主机名）"), "and explains an empty draft");

    machine_ui_open(ui, &cfg, 0, 1);
    key(KEY_X);
    draw_top(dark());
    CHECK(hstub_saw("Rename machine"), "the rename field is English at lang 1");
    CHECK(hstub_saw("(empty = use the host)"), "with the same explanation");

    /* The footer switches too. */
    reset(3);
    machine_ui_open(ui, &cfg, 0, 1);
    draw_bot(dark());
    CHECK(hstub_saw("✎ rename"), "the footer is English at lang 1");
    machine_ui_open(ui, &cfg, 0, 0);
    draw_bot(dark());
    CHECK(hstub_saw("✎ 改名"), "and Chinese at lang 0");
}

/* A hint line that runs off the 320-px panel is invisible help.  The footer
 * grew an entry with the rename feature, so the width is asserted rather
 * than eyeballed. */
static void t_footer_fits(void) {
    static const char *zh = "↑↓ 选择  -  A 连接  -  ✎ 改名  -  B 取消";
    static const char *en = "Up/Down  -  A connect  -  ✎ rename  -  B cancel";

    reset(3);
    machine_ui_open(ui, &cfg, 0, 0);
    draw_bot(dark());
    CHECK(hstub_saw(zh), "the Chinese footer is drawn whole");
    CHECK(renderer_utf8_text_width_px(zh) + M_LEFT <= 320,
          "and it fits the bottom screen");

    machine_ui_open(ui, &cfg, 0, 1);
    draw_bot(dark());
    CHECK(hstub_saw(en), "the English footer is drawn whole");
    CHECK(renderer_utf8_text_width_px(en) + M_LEFT <= 320,
          "and it fits the bottom screen");
}

int main(void) {
    printf("machine_ui host tests\n");

    t_open_close();
    t_open_clamps_initial_index();
    t_navigate_wraps();
    t_confirm_and_cancel();
    t_touch_rows();
    t_edit_strip();
    t_top_screen();
    t_top_screen_palette();
    t_bottom_screen_layout();
    t_named_rows_are_two_lines();
    t_theme_colours();
    t_eight_machines_scroll();
    t_rename_opens_and_prefills();
    t_rename_types_and_commits();
    t_rename_caps_and_sanitises();
    t_rename_clears_and_resets();
    t_language();
    t_footer_fits();

    if (failures) {
        fprintf(stderr, "\n%d of %d check(s) failed\n", failures, checks);
        return 1;
    }
    printf("\nall machine-picker checks passed (%d checks)\n", checks);
    return 0;
}
