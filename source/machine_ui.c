/*
 * The machine picker — a pre-connect screen that lets the user choose which
 * SSH machine (from config.ini's host_0 / host_1 / ...) to connect to before
 * the project cards for that machine are loaded.
 *
 * It mirrors the project page's split: top screen shows the highlighted
 * machine's details, bottom screen lists the configured machines (a five-row
 * window of them — see M_VIS).
 *
 * Theming follows project_ui.c exactly, because this page is the one the
 * project page opens behind — a different palette here would read as a
 * different program:
 *
 *   both screens  the live keyboard palette (softkb_palette()).  The top
 *                 screen used to take the terminal's ink instead, on the
 *                 theory that the page should not look bolted onto the
 *                 session.  It is the wrong trade: the two tables are not
 *                 the same table, so a theme.ini could colour the top half
 *                 of this page differently from the bottom half, and a page
 *                 that has borrowed the top panel should not wear the
 *                 colour of the terminal it is standing in front of.
 *                 render_frame() clears the top target to that palette for
 *                 the same reason.
 *
 * Three rules are load-bearing and are pinned by tools/test_machine_ui.c:
 *
 *   - The selected row is filled with key_accent, which is a BRIGHT surface,
 *     so its text must come from the palette's dark-ink family (ink_dark).
 *     label on key_accent measures 1.2:1 in DARK and MONO.
 *   - The secondary tone is derived from the surface's own ink at reduced
 *     alpha, never taken from hint_fg: hint_fg is tuned against bottom_bg
 *     and falls to 2.9:1 on panel_bg in SOLAR.
 *   - Nothing on this page is drawn in key_accent as TEXT.  It measures
 *     4.08:1 on bottom_bg in SOLAR, under the 4.5:1 floor every ink here
 *     holds — it is a surface colour on this page, not an ink.
 *
 * A fourth rule is about reach, not colour: only M_VIS rows are drawn and the
 * cursor can walk past them, so the window has to follow it.  With
 * DSSH_MACHINES_MAX = 8 and five rows fitting above the footer, the naive
 * "draw all n rows" version left machines 6-8 with no pixels at all while
 * still being selectable.
 *
 * Like project_ui it draws with the public renderer_draw_* API only (no C2D_/
 * C3D_ symbols of its own) so it stays host-testable.
 *
 * Ownership model: main.c passes the live ssh_config_t (file-scope g_cfg) in
 * on every tick/draw; this module never copies or persists the machine list.
 * A rename is a draft held here and handed OUT through the accessors below —
 * main.c is what writes config.ini.
 */

#include <3ds.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "kb_theme.h"
#include "renderer.h"
#include "text_fit.h"
#include "utf8_fit.h"
#include "machine_ui.h"

#define M_ROW_H   40
#define M_LIST_Y0 24
#define M_LEFT    12
#define M_W       296
#define M_H       34             /* row strip height; the rest is the gap */
/* Rows that fit between the list origin and the footer hint.  M_LIST_Y0 +
 * M_VIS * M_ROW_H = 224, which is exactly where the hint line is drawn, so
 * a sixth row would start on top of it — and a seventh and eighth would
 * fall off the 240-px screen entirely.  DSSH_MACHINES_MAX is 8, so this is
 * a real ceiling and not a theoretical one: the list scrolls, it does not
 * overflow.  tools/test_machine_ui.c pins the arithmetic. */
#define M_VIS     5
#define M_FOOTER_Y 224

/* The ✎ strip: the rightmost M_EDIT_W px of every row.  A tap there opens
 * the rename field for THAT row instead of connecting to it — the row body
 * already means "connect", so a touch-only user has nowhere else to put
 * this.  Drawn on every row rather than only the highlighted one, so the
 * affordance is visible before you go looking for it. */
#define M_EDIT_W  30
#define M_EDIT_X  (M_LEFT + M_W - M_EDIT_W)   /* 278 */

/* The row's text box: from the left padding to the strip's divider, less a
 * 6-px gutter so a long name never touches the ✎. */
#define M_TXT_X   (M_LEFT + 16)               /* 28  */
#define M_TXT_W   (M_EDIT_X - 6 - M_TXT_X)    /* 244 */

/* Line offsets inside the drawn strip.  Two 12-px lines at 4 and 19 (the
 * project page's card convention), and the single-line fallback centred
 * exactly where it always was. */
#define M_L1_DY   4
#define M_L2_DY   19
#define M_ONE_DY  ((M_H - 12) / 2)            /* 11 */

/* Top screen.  M_TOP_W is the rule's width, not the panel's: the panel is
 * 400 px and the rule stops short of the edge, as it always has. */
#define M_TOP_PAD 16
#define M_TOP_W   368

/* The ✎ glyph, U+270E PENCIL LOWER RIGHT.  Present in the shipped 6x12
 * atlas (tools/gen_font.py's symbol range), so it needs no new asset. */
#define M_EDIT_GLYPH "\xe2\x9c\x8e"

/* ── Draw order, low to high ──────────────────────────────────────────
 *
 * citro2d DEPTH-TESTS.  A glyph whose z is below the z of the surface it
 * sits on is not "drawn slightly behind" — it is not drawn at all, because
 * the surface has already written a nearer depth at those pixels.  So the
 * order below is load-bearing, and it has to be strictly increasing along
 * the paint order: page → surface → edge → glyph → text.
 *
 * This was a real device-only defect.  The two-line rows were drawn with
 * their name at z 0.55 and their address at 0.50 while the row fill went
 * down at 0.60, so on the 3DS a NAMED machine was two empty bars and the
 * single-line (unnamed) fallback — the only one drawn at 0.70 — was the
 * only row you could read.  Nothing on the host could catch it:
 * tools/stub/pui_host_stubs.c throws z away (`(void)z`) and composites in
 * call order, which is what makes the page host-testable at all.  It took
 * a run in Azahar, where `analyze_picker2.py` sampled the row's text box
 * and found every pixel still equal to the fill.
 *
 * Named constants rather than literals for the same reason project_ui.c
 * has them: a number that only means something in relation to its
 * neighbours cannot be reviewed when it is written as `0.55f` five
 * different places. */
#define MZ_PAGE     0.10f   /* the page background fill (bottom screen only) */
#define MZ_SURFACE  0.20f   /* row strips, the rename field's chip */
#define MZ_EDGE     0.30f   /* the 1px row borders, the ✎ divider, the rule */
#define MZ_GLYPH    0.40f   /* the ✎ mark, above the divider it sits beside */
#define MZ_TEXT     0.50f   /* every label drawn on a surface */
#define MZ_TEXT_HI  0.55f   /* the single line a resting row is read by */

/* Per-frame colour set.  Two families, as in project_ui: `ink`/`dim` read on
 * a raised surface, `sel_ink` on the accent fill.
 *
 * One type for both screens, because they draw the same kinds of thing and
 * share the derivation.  The roles each painter actually reads:
 *
 *   bottom  bg, panel, sel, border, ink, dim, sel_ink
 *   top     panel (the rename field's chip), border, ink, dim, title
 *
 * `sel`/`sel_ink` are set on the top screen and `title` on the bottom
 * anyway: a role that is read nowhere today is a role that rots, but a
 * struct returned with a garbage field is worse — the next painter to read
 * one would get transparent black. */
typedef struct {
    uint32_t bg, panel, sel, border;
    uint32_t ink, dim, sel_ink;
    uint32_t title;
} mui_col_t;

static uint32_t dim_to(uint32_t rgba, unsigned alpha) {
    return (rgba & 0xFFFFFF00u) | alpha;
}

/* Bottom screen — the live keyboard palette.  See the file comment for why
 * dim is derived rather than copied from hint_fg. */
static mui_col_t mui_bot_cols(const kb_pal_t *p) {
    mui_col_t c;
    c.bg      = p->bottom_bg;
    c.panel   = p->panel_bg;          /* resting row strip */
    c.sel     = p->key_accent;        /* the selected row's fill */
    c.border  = p->key_border;        /* the 1px top/bottom edge, as in chip() */
    c.ink     = p->label;
    c.dim     = dim_to(p->label, 0xB3);
    c.sel_ink = p->ink_dark;          /* test_kb_theme.c: >= 4.5 on key_accent */
    c.title   = p->label;
    return c;
}

/* Top screen — the same palette, and the same derivation the terminal ink
 * used to get (a full-strength ink, a hairline at 0x55, a secondary tone at
 * 0x99).  Only the source table moved. */
static mui_col_t mui_top_cols(const kb_pal_t *p) {
    mui_col_t c;
    c.bg      = p->bottom_bg;
    c.panel   = p->panel_bg;          /* the rename field's chip */
    c.sel     = p->bottom_bg;
    c.border  = dim_to(p->label, 0x55);
    c.ink     = p->label;
    c.dim     = dim_to(p->label, 0x99);
    c.sel_ink = p->label;
    c.title   = p->label;             /* NOT key_accent — 4.08:1 in SOLAR */
    return c;
}

struct machine_ui_t {
    int open;
    int cursor;
    int first;   /* index of the first visible row (scroll window) */
    int lang;    /* 0 = Chinese, anything else = English */

    /* The rename field.  While `renaming` is set the soft keyboard owns the
     * bottom screen and its bytes land in `draft`; `rename_row` is the
     * machine being edited. */
    int  renaming;
    int  rename_row;
    int  kbd_armed;
    char draft[DSSH_MACHINE_NAME_MAX];

    /* The last committed rename, for main.c to persist.  Held here rather
     * than written into the caller's ssh_config_t because this module does
     * not own the machine list — and because a test can then assert on the
     * name without a config file. */
    int  result_row;
    char result[DSSH_MACHINE_NAME_MAX];
};

/* Interface-language pick, same shape as project_ui.c's T() macro: the six
 * languages softkb.c knows all land here, and everything but Chinese reads
 * English — which is what the rest of the app does too. */
static const char *mui_t(const machine_ui_t *ui, const char *zh,
                         const char *en) {
    return (ui && ui->lang == 0) ? zh : en;
}

/* Slide the window so the cursor is always inside it, and never past the
 * last full page.  Mirrors project_ui.c's list_first clamp (nav_clamp):
 * without it the picker highlighted rows it never drew — with 8 machines
 * configured, 7 x D-pad landed on a row that was 64 px below the screen
 * and the highlight simply was not there. */
static void mui_clamp(machine_ui_t *ui, int n) {
    if (n <= 0) { ui->cursor = 0; ui->first = 0; return; }
    if (ui->cursor < 0)      ui->cursor = 0;
    if (ui->cursor >= n)     ui->cursor = n - 1;
    if (ui->first > ui->cursor)
        ui->first = ui->cursor;
    if (ui->cursor >= ui->first + M_VIS)
        ui->first = ui->cursor - M_VIS + 1;
    int max_first = n - M_VIS;
    if (max_first < 0) max_first = 0;
    if (ui->first < 0)          ui->first = 0;
    if (ui->first > max_first)  ui->first = max_first;
}

/* Drop the last codepoint.  Byte-at-a-time backspace would leave a
 * half-written glyph behind, which the atlas then draws as U+FFFD — and
 * which would be written to config.ini as a torn sequence. */
static void draft_backspace(char *buf) {
    size_t len = strlen(buf);
    if (!len) return;
    size_t i = len - 1;
    while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80) i--;
    buf[i] = 0;
}

/* Append the soft keyboard's bytes to the draft, keeping it inside `cap`,
 * NUL-terminated and on a codepoint boundary.
 *
 * Control bytes are dropped even though the keyboard cannot produce one: a
 * name is written into config.ini as a single line, so a stray \n or \r
 * reaching the file would break it for every future load.  The boundary
 * rule is the shared utf8_fit() (source/utf8_fit.h) — the same one every
 * other byte cap in this app uses. */
static void draft_append(char *buf, size_t cap, const char *kt) {
    size_t len = strlen(buf);
    for (const char *p = kt; *p && len + 1 < cap; p++) {
        unsigned char b = (unsigned char)*p;
        if (b < 0x20 || b == 0x7F) continue;
        buf[len++] = (char)b;
    }
    buf[len] = 0;
    buf[utf8_fit(buf, len)] = 0;
}

/* Open the field on machine `idx`, pre-filled with its current name so a
 * rename can be an edit rather than a retype. */
static void enter_rename(machine_ui_t *ui, const ssh_config_t *cfg, int idx,
                         int touch_pressed) {
    ui->renaming   = 1;
    ui->rename_row = idx;
    ui->result_row = -1;
    ui->draft[0]   = 0;
    if (cfg && idx >= 0 && idx < cfg->machine_count)
        snprintf(ui->draft, sizeof(ui->draft), "%s", cfg->machines[idx].name);
    /* The tap that opened the field must not also press whatever key sits
     * under the finger — same rule, and the same flag, as project_ui.c. */
    ui->kbd_armed = touch_pressed ? 0 : 1;
}

/* Hand the draft to main.c.  The draft is already NUL-terminated and inside
 * the field width (draft_append keeps it so), which is why this is a plain
 * copy and not another capped snprintf. */
static void commit_rename(machine_ui_t *ui) {
    ui->result_row = ui->rename_row;
    memcpy(ui->result, ui->draft, sizeof(ui->result));
    ui->renaming = 0;
}

machine_ui_t *machine_ui_create(void) {
    machine_ui_t *ui = (machine_ui_t *)calloc(1, sizeof(*ui));
    if (ui) ui->result_row = -1;
    return ui;
}

void machine_ui_free(machine_ui_t *ui) {
    free(ui);
}

int machine_ui_open(machine_ui_t *ui, const ssh_config_t *cfg,
                    int initial_index, int lang) {
    if (!ui) return 0;
    int n = (cfg && cfg->machine_count > 0) ? cfg->machine_count : 1;
    if (initial_index < 0 || initial_index >= n) initial_index = 0;
    ui->cursor = initial_index;
    ui->first  = 0;
    ui->lang   = lang;
    ui->open   = 1;
    ui->renaming   = 0;
    ui->kbd_armed  = 1;
    ui->result_row = -1;
    mui_clamp(ui, n);
    return 1;
}

void machine_ui_close(machine_ui_t *ui) {
    if (!ui) return;
    ui->open = 0;
    /* Dropping the field with the page: leaving `renaming` set would keep
     * main.c handing the soft keyboard to a page that is not drawing. */
    ui->renaming  = 0;
    ui->kbd_armed = 1;
}

int machine_ui_visible(const machine_ui_t *ui) {
    return ui && ui->open;
}

int machine_ui_wants_keyboard(const machine_ui_t *ui) {
    return ui && ui->open && ui->renaming;
}

int machine_ui_keyboard_armed(const machine_ui_t *ui) {
    return ui && ui->open && ui->renaming && ui->kbd_armed;
}

int machine_ui_renamed_index(const machine_ui_t *ui) {
    return ui ? ui->result_row : -1;
}

const char *machine_ui_renamed_name(const machine_ui_t *ui) {
    return ui ? ui->result : "";
}

int machine_ui_tick(machine_ui_t *ui, const ssh_config_t *cfg,
                    uint32_t down, uint32_t held, const char *kt,
                    int tx, int ty, int touch_pressed, int touch_down,
                    int *out_index) {
    (void)held;
    /* *out_index is written ONLY on the connect paths below.  Clearing it
     * up front would be friendlier to a caller that reads it without
     * checking the return code, but it is also a write the page makes on
     * every frame it is merely open — and main.c keys off the return value,
     * so "the page did not choose anything" has to leave the caller's
     * variable exactly as it found it.  tools/test_machine_ui.c asserts
     * that for the cancel, the ✎ tap, the closed-page and the typing
     * cases. */
    if (!ui || !ui->open) return MUI_ACT_NONE;
    int n = (cfg && cfg->machine_count > 0) ? cfg->machine_count : 1;

    /* ── The rename field owns the frame while it is up ──
     *
     * Same byte protocol as project_ui.c's text entry: "\x7f" is the
     * keyboard's ⌫, "\r" (and "\t") is its ✓, "\x1b..." is an arrow or
     * function key and means nothing in a one-line field. */
    if (ui->renaming) {
        if (!touch_pressed) ui->kbd_armed = 1;
        if (kt && strcmp(kt, "\x7f") == 0) {
            draft_backspace(ui->draft);
        } else if (kt && (strcmp(kt, "\r") == 0 || strcmp(kt, "\t") == 0)) {
            commit_rename(ui);
            return MUI_ACT_RENAMED;
        } else if (kt && kt[0] == '\x1b') {
            /* Escape sequences carry no meaning here. */
        } else if (kt && *kt) {
            draft_append(ui->draft, sizeof(ui->draft), kt);
        }
        /* A confirms, B backs out — the same two meanings the picker's own
         * list gives them, so the key you reach for is the key you expect.
         * Neither one connects while the field is up. */
        if (down & KEY_A) {
            commit_rename(ui);
            return MUI_ACT_RENAMED;
        }
        if (down & (KEY_B | KEY_START | KEY_SELECT)) {
            ui->renaming = 0;
            return MUI_ACT_NONE;
        }
        return MUI_ACT_NONE;
    }

    if (down & KEY_UP)   ui->cursor = (ui->cursor - 1 + n) % n;
    if (down & KEY_DOWN) ui->cursor = (ui->cursor + 1) % n;
    mui_clamp(ui, n);
    if (down & KEY_B)    return MUI_ACT_CANCEL;

    /* X renames the highlighted machine — the same key that edits the
     * selected card on the project page, which is the page this one hands
     * off to. */
    if (down & KEY_X) {
        enter_rename(ui, cfg, ui->cursor, touch_pressed);
        return MUI_ACT_NONE;
    }
    if (down & KEY_A)    { if (out_index) *out_index = ui->cursor;
                           return MUI_ACT_CONNECT; }

    /* Touch: map the y coordinate to a visible row; confirm on the press
     * edge.  Three things are deliberately not hits:
     *
     *   - anything above the list or left/right of it;
     *   - the 6-px gap between two rows, so a tap there does nothing
     *     instead of silently picking the row above (project_ui.c rejects
     *     its card gutter for the same reason);
     *   - the footer band at y >= M_FOOTER_Y.  It used to fall through to
     *     slot 5, so tapping the "Up/Down select - A confirm" hint line
     *     *confirmed machine 6* — a tap on a line of help text connected
     *     to a host.  The two bounds below coincide for the geometry above
     *     (M_LIST_Y0 + M_VIS * M_ROW_H == M_FOOTER_Y), so either one alone
     *     would do today; they are both written out because they guard
     *     different edits — M_VIS guards "how many rows are drawn", and
     *     this one keeps holding if the pitch or the window size changes.
     *
     * The ✎ strip is checked first because it sits inside the row's own
     * bounds, and it must not connect. */
    if (touch_pressed && tx >= M_LEFT && tx <= M_LEFT + M_W &&
        ty >= M_LIST_Y0 && ty < M_FOOTER_Y) {
        int slot = (ty - M_LIST_Y0) / M_ROW_H;
        int within = (ty - M_LIST_Y0) - slot * M_ROW_H;
        if (slot >= 0 && slot < M_VIS && within < M_H) {
            int i = ui->first + slot;
            if (i >= 0 && i < n) {
                ui->cursor = i;
                if (tx >= M_EDIT_X) {
                    enter_rename(ui, cfg, i, touch_pressed);
                    return MUI_ACT_NONE;
                }
                if (touch_down) { if (out_index) *out_index = i;
                                  return MUI_ACT_CONNECT; }
            }
        }
    }
    return MUI_ACT_NONE;
}

/* ── Drawing: top screen ────────────────────────────────────────────── */

/* The rename field.  This is the only place the text being typed is
 * visible — the soft keyboard owns the bottom screen — so it echoes the
 * draft, shows where the next glyph lands, and says how much of the byte
 * budget is gone. */
static void draw_top_rename(const machine_ui_t *ui, const ssh_config_t *cfg,
                            const mui_col_t *c) {
    int idx = ui->rename_row;
    if (idx < 0 || idx >= cfg->machine_count) idx = 0;
    const ssh_machine_t *m = &cfg->machines[idx];

    renderer_draw_text_px_z(M_TOP_PAD, 16, MZ_TEXT_HI,
                            mui_t(ui, "给机器改名", "Rename machine"),
                            c->title);
    char buf[CONFIG_STR_MAX + 40];
    snprintf(buf, sizeof(buf), "%d / %d", idx + 1,
             cfg->machine_count > 0 ? cfg->machine_count : 1);
    renderer_draw_text_px_z(320, 16, MZ_TEXT, buf, c->dim);

    snprintf(buf, sizeof(buf), "%s: %s", mui_t(ui, "主机", "Host"), m->host);
    tf_text_clip(M_TOP_PAD, 44, MZ_TEXT, buf, c->dim, M_TOP_W);

    renderer_draw_rect_px(M_TOP_PAD, 68, M_TOP_W, 1, MZ_EDGE, c->border);

    /* The field: a chip on the palette's panel_bg — which is what makes it
     * a surface at all, since bottom_bg is what the caller cleared to. */
    const int fx = M_TOP_PAD, fy = 104, fw = M_TOP_W, fh = 36;
    renderer_draw_rect_px(fx, fy, fw, fh, MZ_SURFACE, c->panel);
    renderer_draw_rect_px(fx, fy, fw, 1, MZ_EDGE, c->border);
    renderer_draw_rect_px(fx, fy + fh - 1, fw, 1, MZ_EDGE, c->border);

    const int tx = fx + 10;
    const int ty = fy + (fh - 12) / 2;
    int used = tf_text_clip(tx, ty, MZ_TEXT_HI,
                            ui->draft[0] ? ui->draft
                                         : mui_t(ui, "（空 = 用主机名）",
                                                      "(empty = use the host)"),
                            ui->draft[0] ? c->ink : c->dim,
                            fw - 20 - 8);
    /* Caret, one cell wide, right where the next glyph goes. */
    renderer_draw_rect_px(tx + used + 1, ty, FA_CELL_W, 12, MZ_TEXT_HI, c->ink);

    renderer_draw_text_px_z(M_TOP_PAD, 158, MZ_TEXT,
                            mui_t(ui, "A / ✓ 保存  -  B 取消",
                                     "A / ✓ save  -  B cancel"), c->dim);
    snprintf(buf, sizeof(buf), "%d / %d", (int)strlen(ui->draft),
             DSSH_MACHINE_NAME_MAX - 1);
    renderer_draw_text_px_z(400 - M_TOP_PAD -
                                renderer_utf8_text_width_px(buf),
                            158, MZ_TEXT, buf, c->dim);
}

/* The machine list's detail card: five fields, one per line.  The name goes
 * first because it is what the list below is keyed on, and every value is
 * clipped to the panel — a long host or key path used to run off the right
 * edge of a 400-px screen with nothing to say it had. */
static void draw_top_list(const machine_ui_t *ui, const ssh_config_t *cfg,
                          const mui_col_t *c) {
    int idx = ui->cursor;
    if (idx < 0 || idx >= cfg->machine_count) idx = 0;
    const ssh_machine_t *m = &cfg->machines[idx];

    /* No full-screen fill: render_frame() cleared this target to the
     * keyboard palette's background, and painting it again would only hide
     * that. */
    renderer_draw_text_px_z(M_TOP_PAD, 16, MZ_TEXT_HI,
                            mui_t(ui, "选择 SSH 机器", "Select SSH machine"),
                            c->title);
    renderer_draw_text_px_z(M_TOP_PAD, 44, MZ_TEXT,
                            mui_t(ui, "连上后再选项目卡片",
                                     "the project cards come next"),
                            c->dim);
    char buf[CONFIG_STR_MAX + 40];
    snprintf(buf, sizeof(buf), "%d / %d", idx + 1,
             cfg->machine_count > 0 ? cfg->machine_count : 1);
    renderer_draw_text_px_z(320, 16, MZ_TEXT, buf, c->dim);

    /* Header rule — the one piece of chrome, drawn in the border tone so it
     * stays a hairline in every theme. */
    renderer_draw_rect_px(M_TOP_PAD, 68, M_TOP_W, 1, MZ_EDGE, c->border);

    int y = 96;
    snprintf(buf, sizeof(buf), "%s: %s", mui_t(ui, "名称", "Name"),
             m->name[0] ? m->name : mui_t(ui, "（未命名）", "(unnamed)"));
    tf_text_clip(M_TOP_PAD, y, MZ_TEXT, buf, c->ink, M_TOP_W);
    y += 28;

    snprintf(buf, sizeof(buf), "%s: %s", mui_t(ui, "主机", "Host"), m->host);
    tf_text_clip(M_TOP_PAD, y, MZ_TEXT, buf, c->ink, M_TOP_W);
    y += 28;

    snprintf(buf, sizeof(buf), "%s: %s", mui_t(ui, "用户", "User"), m->user);
    tf_text_clip(M_TOP_PAD, y, MZ_TEXT, buf, c->ink, M_TOP_W);
    y += 28;

    snprintf(buf, sizeof(buf), "%s: %d", mui_t(ui, "端口", "Port"), m->port);
    tf_text_clip(M_TOP_PAD, y, MZ_TEXT, buf, c->ink, M_TOP_W);
    y += 28;

    snprintf(buf, sizeof(buf), "%s: %s", mui_t(ui, "密钥", "Key"), m->key_path);
    tf_text_clip(M_TOP_PAD, y, MZ_TEXT, buf, c->dim, M_TOP_W);
}

void machine_ui_draw_top(const machine_ui_t *ui, const ssh_config_t *cfg,
                         renderer_t *r, const kb_pal_t *pal) {
    (void)r;
    if (!ui || !cfg) return;
    /* softkb_palette() can never return NULL (kb_pal_for() clamps), but a
     * NULL here must not be a crash — same fallback as ai_modal.c. */
    if (!pal) pal = kb_pal_for(THEME_DARK);
    mui_col_t c = mui_top_cols(pal);
    if (ui->renaming) draw_top_rename(ui, cfg, &c);
    else              draw_top_list(ui, cfg, &c);
}

/* ── Drawing: bottom screen ─────────────────────────────────────────── */
void machine_ui_draw_bot(const machine_ui_t *ui, const ssh_config_t *cfg,
                         renderer_t *r, const kb_pal_t *pal) {
    (void)r;
    if (!ui || !cfg) return;
    if (!pal) pal = kb_pal_for(THEME_DARK);
    mui_col_t c = mui_bot_cols(pal);
    int n = cfg->machine_count > 0 ? cfg->machine_count : 1;
    int first = ui->first;
    if (first > n - M_VIS) first = n - M_VIS;
    if (first < 0) first = 0;

    renderer_draw_rect_px(0, 0, 320, 240, MZ_PAGE, c.bg);

    /* Only the window is drawn.  Rows past M_VIS have no pixels — the
     * cursor can still reach them, mui_clamp() just keeps the window
     * following it. */
    for (int slot = 0; slot < M_VIS; slot++) {
        int i = first + slot;
        if (i >= n) break;
        int y = M_LIST_Y0 + slot * M_ROW_H;
        int sel = (i == ui->cursor);
        const ssh_machine_t *m = &cfg->machines[i];

        /* The selected row is a solid accent fill; the resting rows are the
         * raised strip tone.  Both are opaque, so the ink below is picked to
         * match the surface rather than the page.
         *
         * On the accent fill BOTH lines take the full dark ink.  Dimming the
         * address line there was measured and rejected: ink_dark at 0xC0
         * falls to 3.52:1 in SOLAR and 4.00:1 in MATRIX, under the 4.5:1
         * floor every other ink on this page holds.  The hierarchy on a
         * highlighted row comes from the fill, not from a second tone. */
        uint32_t ink = sel ? c.sel_ink : c.ink;
        uint32_t dim = sel ? c.sel_ink : c.dim;

        renderer_draw_rect_px(M_LEFT, y, M_W, M_H, MZ_SURFACE, sel ? c.sel : c.panel);
        renderer_draw_rect_px(M_LEFT, y, M_W, 1, MZ_EDGE, c.border);
        renderer_draw_rect_px(M_LEFT, y + M_H - 1, M_W, 1, MZ_EDGE, c.border);

        /* The ✎ strip: a hairline divider and the glyph, in the secondary
         * tone so it reads as an affordance rather than as content. */
        renderer_draw_rect_px(M_EDIT_X, y + 6, 1, M_H - 12, MZ_EDGE, c.border);
        renderer_draw_text_px_z(M_EDIT_X + (M_EDIT_W - FA_CELL_W) / 2,
                                y + M_ONE_DY, MZ_GLYPH, M_EDIT_GLYPH, dim);

        /* The address is the host, with the port appended when it is not 22:
         * several machines on one host are otherwise indistinguishable in
         * the list, which is exactly what the eight-machine fixture is.  The
         * named and unnamed rows share this string, so the two paths cannot
         * disagree about what a machine is called. */
        char addr[CONFIG_STR_MAX + 16];
        if (m->port == 22) snprintf(addr, sizeof(addr), "%s", m->host);
        else snprintf(addr, sizeof(addr), "%s:%d", m->host, m->port);

        char line[CONFIG_STR_MAX + 32];
        /* 1-based: the top screen says "3 / 8", and a list numbered from 0
         * next to it read as a different machine. */
        snprintf(line, sizeof(line), "%d. %s", i + 1,
                 m->name[0] ? m->name : addr);

        if (m->name[0]) {
            /* Two lines: what the machine is called, then how to reach it. */
            tf_text_clip(M_TXT_X, y + M_L1_DY, MZ_TEXT, line, ink, M_TXT_W);
            tf_text_clip(M_TXT_X, y + M_L2_DY, MZ_TEXT, addr, dim, M_TXT_W);
        } else {
            /* Unnamed: one centred line — exactly the row this page drew
             * before names existed, so a config.ini with no name_ keys
             * looks and reads the way it always did. */
            tf_text_clip(M_TXT_X, y + M_ONE_DY, MZ_TEXT_HI, line, ink, M_TXT_W);
        }
    }

    renderer_draw_text_px_z(M_LEFT, M_FOOTER_Y, MZ_TEXT,
                            mui_t(ui, "↑↓ 选择  -  A 连接  -  ✎ 改名  -  B 取消",
                                      "Up/Down  -  A connect  -  ✎ rename  -  "
                                      "B cancel"),
                            c.dim);
}
