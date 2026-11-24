#include "project_ui.h"
#include "project_presets.h"
#include "ssh_client.h"
#include "utf8_fit.h"
#include "text_fit.h"

#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Geometry ─────────────────────────────────────────────────────────
 *
 * Bottom screen 320×240, top screen 400×240, both drawn in pixels with
 * the 6×12 glyph atlas (renderer_draw_text_px_z).
 *
 * LIST mode — the whole bottom screen is the card list:
 *
 *   y   0.. 19   title bar: page name + "3/16"
 *   y  20..207   five cards, 36 px each with a 2 px gutter
 *   y 208..239   action bar: 进入 / 编辑 / 新增 / 删除
 *
 * EDIT mode — four tabs, one section each:
 *
 *   y   0.. 19   title bar: which card, plus 保存 / 取消
 *   y  20.. 41   tab strip: 目录 / 工具 / 参数 / 自定义
 *   y  42..213   the active section
 *   y 214..239   key legend
 *
 * The "+ 新增项目" card is a real row at index project_count, so D-pad ↓
 * reaches it the same way it reaches any other card.
 *
 * Marks — a card and a tool chip both lead with the tool's 16×16 mark
 * (proj_logo_t in project_presets.h), so the list says which agent CLI a
 * project runs before you read a word of it.  That is what pushes the
 * card's text column out to CARD_TXT_X and the chip's to TOOL_NAME_X;
 * both marks sit in a 16 px square whose top-left corner is the constant
 * below it, and the tests mirror those constants so a moved mark fails
 * loudly instead of silently overlapping the text.
 */
#define SCR_W        320
#define SCR_H        240
#define PAD          6
#define INNER_X      PAD
#define INNER_W      (SCR_W - 2 * PAD)              /* 308 */

#define HDR_H        20
#define TAB_Y        20
#define TAB_H        22
#define TAB_N        4
#define TAB_W        75
#define TAB_PITCH    77
#define TAB_X0       7

#define BODY_Y0      42
#define BODY_Y1      213
#define FTR_Y        214
#define FTR_H        26

#define CARD_Y0      HDR_H
#define CARD_H       36
#define CARD_GAP     2
#define CARD_PITCH   (CARD_H + CARD_GAP)
#define CARD_VIS     5
/* A card leads with its tool's 16 px mark, centred vertically in the
 * 36 px face; the text column starts to the right of it. */
#define CARD_LOGO_X  (INNER_X + 9)
#define CARD_LOGO_Y  10
#define CARD_TXT_X   (INNER_X + 30)
#define CARD_TXT_R   (INNER_X + INNER_W - 8)
#define LIST_FTR_Y   208
#define LIST_FTR_H   32
#define LIST_BTN_N   4
#define LIST_BTN_GAP 4

/* Tool chooser chip: 2 columns of 151 px inside the 308 px body.  The
 * mark takes the left 16 px of the top line and the name sits beside it;
 * the description keeps its own full-width line underneath, so adding
 * the mark costs the text nothing. */
#define TOOL_CHIP_W  151
#define TOOL_CHIP_H  34
#define TOOL_CHIP_X0 INNER_X
#define TOOL_CHIP_Y0 46
#define TOOL_LOGO_X  5
#define TOOL_LOGO_Y  2
#define TOOL_NAME_X  26

#define TOP_W        400
#define TOP_H        240
#define TOP_PAD      10
#define TOP_INNER_W  (TOP_W - 2 * TOP_PAD)          /* 380 */
#define LINE_H       13

/* z layers.  The page owns both screens while it is up, so it only has to
 * out-rank itself; the values match the softkb / ai_modal convention so
 * the page can sit under the AI modal if one ever arrives on top. */
#define Z_PANEL      0.10f
#define Z_CARD       0.20f
#define Z_LOGO       0.25f   /* above the card face, below its border */
#define Z_BORDER     0.30f
#define Z_RULE       0.35f
#define Z_TEXT       0.50f
#define Z_TEXT_HI    0.55f

#define PUI_PATH_MAX 160

/* The probe buffer must never be able to hold a path the card cannot
 * store.  "使用当前目录" copies ui->cwd straight into draft.path, so if
 * this ever failed the user would get a card whose `cd` target is a
 * silent prefix of the directory they were standing in.  copy_str()
 * returns void and nothing checks copy_clamped()'s return, so the only
 * thing standing between that and a shipped bug is this line. */
_Static_assert(DSSH_PROJECT_PATH_MAX >= PUI_PATH_MAX,
               "PUI_PATH_MAX must not exceed DSSH_PROJECT_PATH_MAX");

/* ── Hit-test result codes ──────────────────────────────────────────── */
#define HIT_NONE    (-1)
#define HIT_RUN     (-10)
#define HIT_EDIT    (-11)
#define HIT_DELETE  (-12)
#define HIT_ADD     (-13)
#define HIT_SAVE    (-14)
#define HIT_CANCEL  (-15)
#define HIT_TAB(i)  (-20 - (i))
#define HIT_ROW(i)  (-40 - (i))
#define HIT_BTN(i)  (-80 - (i))
#define HIT_TOOL(i) (-120 - (i))
#define HIT_FLAG(i) (-160 - (i))

#define HIT_IS_TAB(h, i)   ((h) == HIT_TAB(i))
#define HIT_TAB_IDX(h)     (HIT_TAB(0) - (h))
#define HIT_ROW_IDX(h)     (HIT_ROW(0) - (h))
#define HIT_BTN_IDX(h)     (HIT_BTN(0) - (h))
#define HIT_TOOL_IDX(h)    (HIT_TOOL(0) - (h))
#define HIT_FLAG_IDX(h)    (HIT_FLAG(0) - (h))
#define HIT_IS_TAB_KIND(h) ((h) <= HIT_TAB(0) && (h) > HIT_TAB(0) - 16)
#define HIT_IS_ROW_KIND(h) ((h) <= HIT_ROW(0) && (h) > HIT_ROW(0) - 16)
#define HIT_IS_BTN_KIND(h) ((h) <= HIT_BTN(0) && (h) > HIT_BTN(0) - 16)
#define HIT_IS_TOOL_KIND(h)((h) <= HIT_TOOL(0) && (h) > HIT_TOOL(0) - 32)
#define HIT_IS_FLAG_KIND(h)((h) <= HIT_FLAG(0) && (h) > HIT_FLAG(0) - 32)

typedef enum { L_ZH = 0, L_EN = 1 } pui_lang_t;

typedef enum {
    TAB_PATH = 0,
    TAB_TOOL,
    TAB_ARGS,
    TAB_CUSTOM,
} pui_tab_t;

typedef enum {
    FIELD_NONE = 0,
    FIELD_NAME,
    FIELD_PATH,
    FIELD_NOTE,
    FIELD_TOOL,     /* the custom tool name */
    FIELD_ARGS,     /* free-form extra args */
    FIELD_CMD,      /* the fully-custom long command */
} pui_field_t;

typedef enum {
    CWD_IDLE = 0,
    CWD_PROBING,
    CWD_OK,
    CWD_FAILED,
} pui_cwd_state_t;

struct project_ui_t {
    int         open;
    pui_entry_t entry;
    pui_lang_t  lang;

    int         tab;
    int         in_editor;      /* 1 while the form owns the bottom screen */
    int         list_cursor;    /* 0..project_count inclusive */
    int         list_first;     /* first visible card */
    int         path_cursor;    /* selected row of the 目录 tab (0..3) */
    int         delete_armed;   /* row index, -1 = none */
    int         tool_cursor;
    int         args_cursor;
    int         args_first;

    /* The card being edited.  Edits land here first so 取消 can drop them;
     * 保存 copies the whole struct back. */
    dssh_project_t draft;
    int         draft_row;      /* -1 while browsing the list */

    /* Text entry.  While `field` is set the soft keyboard owns the bottom
     * screen and its bytes land in the draft field named here. */
    pui_field_t field;
    int         kbd_armed;

    int         dirty;
    /* What `dirty` was when the editor opened.  Editing the draft sets
     * `dirty` so the top screen can show unsaved state, but 取消 must put
     * it back — otherwise cancelling a card would make main.c rewrite the
     * settings file with nothing changed in it. */
    int         dirty_before;

    /* `pwd` probe over an auxiliary channel. */
    ssh_aux_channel_t *aux;
    char        aux_buf[256];
    int         aux_len;
    pui_cwd_state_t cwd_state;
    char        cwd[PUI_PATH_MAX];
    char        window_cwd[PUI_PATH_MAX];
};

/* ── Drawing helpers ────────────────────────────────────────────────── */
static void fill(int x, int y, int w, int h, float z, uint32_t c) {
    renderer_draw_rect_px(x, y, w, h, z, c);
}

/* 1-px outline, four rects — there is no stroke primitive. */
static void stroke(int x, int y, int w, int h, float z, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    fill(x, y, w, 1, z, c);
    fill(x, y + h - 1, w, 1, z, c);
    fill(x, y, 1, h, z, c);
    fill(x + w - 1, y, 1, h, z, c);
}

static uint32_t dim_to(uint32_t rgba, unsigned alpha) {
    return (rgba & 0xFFFFFF00u) | alpha;
}

/* text_clip() used to live here.  It moved to source/text_fit.h as
 * tf_text_clip() when the machine picker needed the same ellipsis: two
 * copies of a clip rule means two copies of the wide-cell arithmetic, and
 * the wide-cell arithmetic has to agree with font_atlas.c.  The call
 * sites below keep the same shape, one letter longer. */

/* Hard-wrap `s` at max_w pixels, at most max_lines lines.  Returns the
 * number of lines drawn.  A word is never broken mid-codepoint; there is
 * no hyphenation and no word-boundary search, because paths and command
 * lines have no spaces to break on anyway. */
static int text_wrap(int x, int y, float z, const char *s, uint32_t c,
                     int max_w, int max_lines) {
    if (!s || !*s || max_lines <= 0) return 0;
    char buf[384];
    int o = 0, w = 0, lines = 1;
    int cx = x, cy = y;
    const char *p = s;
    while (*p) {
        uint32_t cp;
        int adv = tf_u8_next(p, &cp);
        int cw  = tf_cp_w(cp);
        if (cp == '\n' || w + cw > max_w) {
            buf[o] = 0;
            renderer_draw_text_px_z(cx, cy, z, buf, c);
            o = 0; w = 0;
            if (++lines > max_lines) return lines - 1;
            cy += LINE_H;
            if (cp == '\n') { p += adv; continue; }
        }
        if (o + adv >= (int)sizeof(buf)) {
            buf[o] = 0;
            renderer_draw_text_px_z(cx, cy, z, buf, c);
            return lines;
        }
        memcpy(buf + o, p, (size_t)adv);
        o += adv;
        p += adv;
        w += cw;
    }
    buf[o] = 0;
    renderer_draw_text_px_z(cx, cy, z, buf, c);
    return lines;
}

/* A filled rounded-ish chip: the corner pixels are skipped rather than
 * anti-aliased, which is all a 1-px radius needs at this size. */
static void chip(int x, int y, int w, int h, uint32_t bg, uint32_t border,
                 int selected) {
    if (w <= 0 || h <= 0) return;
    fill(x, y, w, h, Z_CARD, bg);
    stroke(x, y, w, h, Z_BORDER, border);
    if (selected) {
        /* An inner hairline doubles the border's weight without needing a
         * second colour role. */
        stroke(x + 1, y + 1, w - 2, h - 2, Z_BORDER, border);
    }
}

/* Draw a tool's pixel mark.  `scale` blows each grid cell up to
 * scale×scale px, so the same 16×16 data serves the 1× chip and the 2×
 * top screen.
 *
 * Cells are merged into horizontal runs first.  Adjacent 1-px quads at
 * the same depth would otherwise show hairline seams where the coverage
 * rules meet, and a run is one quad instead of up to sixteen — the tool
 * row draws seven marks and the card list five more.
 *
 * '#' takes `ink`; '+' takes the same ink at half alpha.  That second
 * tone is deliberately derived rather than per-tool: a brand colour per
 * tool would have to be re-checked against every keyboard theme (several
 * are light), and the one that reads on all of them is the surface's own
 * ink, which is what the mark is drawn in anyway. */
static void draw_logo(int x, int y, const proj_logo_t *lg, int scale,
                      uint32_t ink) {
    if (!lg || !lg->rows || scale <= 0) return;
    uint32_t soft = dim_to(ink, 0x80);
    for (int r = 0; r < lg->h; r++) {
        const char *row = lg->rows[r];
        if (!row) continue;
        int c = 0;
        while (c < lg->w) {
            char ch = row[c];
            if (ch != '#' && ch != '+') { c++; continue; }
            int run = 1;
            while (c + run < lg->w && row[c + run] == ch) run++;
            fill(x + c * scale, y + r * scale, run * scale, scale, Z_LOGO,
                 ch == '#' ? ink : soft);
            c += run;
        }
    }
}

static void chip_label(int x, int y, int w, int h, const char *label,
                       uint32_t ink) {
    int tw = renderer_utf8_text_width_px(label);
    int tx = x + (w - tw) / 2;
    if (tx < x + 2) tx = x + 2;
    renderer_draw_text_px_z(tx, y + (h - 12) / 2, Z_TEXT_HI, label, ink);
}

/* ── Theming ──────────────────────────────────────────────────────────
 * The bottom screen takes the live keyboard palette; the top screen keeps
 * the terminal's own ink so the page does not read as a second program.
 */
typedef struct {
    uint32_t bg, panel, card, card_sel, border, ink, dim, accent, warn;
    /* Text drawn ON `card_sel`.  Not a nuance of ink/dim: card_sel is a
     * bright accent FILL, so the colour that reads on it belongs to the
     * palette's other family (ink_dark).  See bot_cols(). */
    uint32_t sel_ink, sel_dim;
} pui_col_t;

static pui_col_t bot_cols(const kb_pal_t *p) {
    pui_col_t c;
    c.bg       = p->bottom_bg;
    c.panel    = p->panel_bg;
    c.card     = p->key_body;
    c.card_sel = p->key_accent;
    c.border   = p->key_border;
    c.ink      = p->label;
    /* The secondary tone is derived from the surface's OWN ink rather
     * than taken from hint_fg.  hint_fg is tuned against bottom_bg and
     * measures 2.1-2.6:1 once it sits on key_body (SOLAR/DARK/MONO),
     * while label@0xB3 holds >= 5.0 there.  Same argument draw_logo()
     * makes for its second tone. */
    c.dim      = dim_to(p->label, 0xB3);
    c.accent   = p->key_accent;
    c.warn     = p->batt_warn;
    /* test_kb_theme.c pins ink_dark/key_accent at >= 4.5:1 in every
     * theme; label/key_accent measures 1.2-3.0 and is unreadable.  So
     * the selected surface takes the dark ink, at full strength for the
     * name and at 0xC0 for the sub-line (still >= 3.4:1). */
    c.sel_ink  = p->ink_dark;
    c.sel_dim  = dim_to(p->ink_dark, 0xC0);
    return c;
}

/* The top screen wears the SAME palette as the bottom screen.
 *
 * It used to take the terminal's ink (terminal_get_fg/bg), on the theory
 * that the page should not look bolted onto the session.  It is the wrong
 * trade: those are two different tables, so a theme.ini could colour the
 * two halves of one page differently, and the derivation below is what
 * actually carried the look — a full-strength ink, a hairline at 0x55, a
 * secondary tone at 0x99 — not the source of the ink.  Only the source
 * moved, so the page reads exactly as it did in the three themes where the
 * two tables happen to agree, and correctly in the two where they do not. */
static pui_col_t top_cols(const kb_pal_t *p) {
    uint32_t fg = p->label;
    uint32_t bg = p->bottom_bg;
    pui_col_t c;
    c.bg       = bg;
    c.panel    = (bg & 0xFFFFFF00u) | 0x30;   /* translucent lift */
    c.card     = bg;
    c.card_sel = bg;
    c.border   = dim_to(fg, 0x55);
    c.ink      = fg;
    c.dim      = dim_to(fg, 0x99);
    c.accent   = dim_to(fg, 0xFF);
    c.warn     = dim_to(fg, 0xCC);
    /* Nothing up here is filled with card_sel — selection is a stroke
     * plus the accent rail — so the ordinary inks already apply. */
    c.sel_ink  = c.ink;
    c.sel_dim  = c.dim;
    return c;
}

/* ── Small utilities ────────────────────────────────────────────────── */
static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Every cap this file applies counts bytes (PUI_PATH_MAX, the
 * DSSH_PROJECT_*_MAX field sizes) while the text is usually Chinese, so
 * copy_str / path_basename / append_bytes all run their result through
 * utf8_fit() — see utf8_fit.h. */

static void copy_str(char *dst, size_t sz, const char *src) {
    if (!dst || sz == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t i = 0;
    for (; i + 1 < sz && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
    size_t fit = utf8_fit(dst, i);
    if (fit != i) dst[fit] = 0;
}

/* Basename of a path, used to name a card the user has not named yet. */
static void path_basename(const char *path, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = 0;
    if (!path || !*path) return;
    const char *end = path + strlen(path);
    while (end > path && end[-1] == '/') end--;
    const char *start = end;
    while (start > path && start[-1] != '/') start--;
    size_t n = (size_t)(end - start);
    if (n == 0) { copy_str(out, out_sz, path); return; }
    if (n > out_sz - 1) n = out_sz - 1;
    n = utf8_fit(start, n);
    memcpy(out, start, n);
    out[n] = 0;
}

/* One codepoint from the soft keyboard.  A three-byte glyph arriving when
 * only two bytes are left is written in full and then backed off, rather
 * than leaving half of it in the field. */
static void append_bytes(char *dst, size_t sz, const char *bytes) {
    if (!dst || sz == 0 || !bytes) return;
    size_t cur = strlen(dst);
    for (size_t i = 0; bytes[i] && cur + 1 < sz; i++) dst[cur++] = bytes[i];
    dst[cur] = 0;
    size_t fit = utf8_fit(dst, cur);
    if (fit != cur) dst[fit] = 0;
}

static void backspace_cp(char *dst) {
    if (!dst) return;
    size_t len = strlen(dst);
    if (!len) return;
    size_t i = len - 1;
    while (i > 0 && ((unsigned char)dst[i] & 0xC0) == 0x80) i--;
    dst[i] = 0;
}

static void fmt_ago(long then, char *out, int out_sz, pui_lang_t lang) {
    if (!out || out_sz <= 0) return;
    if (then <= 0) {
        snprintf(out, (size_t)out_sz, "%s",
                 lang == L_ZH ? "从未使用" : "never used");
        return;
    }
    long d = (long)time(NULL) - then;
    if (d < 0) d = 0;
    if (d < 60) {
        snprintf(out, (size_t)out_sz, "%s", lang == L_ZH ? "刚刚" : "just now");
    } else if (d < 3600) {
        snprintf(out, (size_t)out_sz, lang == L_ZH ? "%ld 分钟前" : "%ld min ago",
                 d / 60);
    } else if (d < 86400) {
        snprintf(out, (size_t)out_sz, lang == L_ZH ? "%ld 小时前" : "%ld h ago",
                 d / 3600);
    } else if (d < 86400L * 30) {
        snprintf(out, (size_t)out_sz, lang == L_ZH ? "%ld 天前" : "%ld d ago",
                 d / 86400);
    } else {
        snprintf(out, (size_t)out_sz, lang == L_ZH ? "%ld 个月前" : "%ld mo ago",
                 d / (86400L * 30));
    }
}

/* The card as the list shows it: composed command, or the raw one. */
static void project_command_of(const dssh_project_t *p, char *out, int out_sz) {
    if (!out || out_sz <= 0) return;
    if (dssh_project_command(p, out, out_sz) <= 0)
        copy_str(out, (size_t)out_sz, "");
}

static const char *project_tool_label(const dssh_project_t *p,
                                      char *buf, int buf_sz, pui_lang_t lang) {
    if (p->raw) {
        snprintf(buf, (size_t)buf_sz, "%s",
                 lang == L_ZH ? "自定义" : "custom");
        return buf;
    }
    if (p->tool[0]) { copy_str(buf, (size_t)buf_sz, p->tool); return buf; }
    snprintf(buf, (size_t)buf_sz, "%s", lang == L_ZH ? "仅 cd" : "cd only");
    return buf;
}

/* What a card is called on screen — the one place that decides it, so the
 * list row and the top screen's title cannot drift apart.
 *
 * A card saved through the editor is never nameless: commit_draft() falls
 * back to the directory's last segment and then to "card".  But
 * settings.ini is plain text and parse_project_row() accepts an empty
 * first field, so a hand-written or third-party row can put a nameless
 * card in the list.  Falling back to the directory's last segment is what
 * commit_draft() would have stored anyway, so the row reads the same
 * before and after a save; the placeholder is for a card with neither,
 * which is otherwise a row of pure background the user cannot tell is
 * there. */
static const char *card_display_name(const project_ui_t *ui,
                                     const dssh_project_t *p,
                                     char *buf, size_t buf_sz) {
    if (p->name[0]) return p->name;
    if (p->path[0]) {
        path_basename(p->path, buf, buf_sz);
        if (buf[0]) return buf;
    }
    return ui->lang == L_ZH ? "(未命名)" : "(unnamed)";
}

/* The mark a card shows.  A raw card reads as "custom" everywhere else on
 * the page, and its tool field may still hold whatever name the card was
 * built from — drawing that tool's mark beside the word "custom" would
 * contradict the label, so it gets the pencil instead. */
static const proj_logo_t *card_logo(const dssh_project_t *p) {
    return proj_tool_logo(p->raw ? "" : p->tool);
}

/* ── Language tables ────────────────────────────────────────────────── */
#define T(zh, en) ((ui->lang == L_ZH) ? (zh) : (en))

static const char *const k_tab_label[2][TAB_N] = {
    { "目录", "工具", "参数", "自定义" },
    { "Dir",  "Tool", "Args", "Custom" },
};

static const char *const k_tab_help[2][TAB_N] = {
    { "第一段：这张卡片在哪个目录下运行。可以手动输入，也可以直接取用"
      "当前窗口所在（或新建窗口时登录到）的目录。",
      "第二段：选择要启动的终端 agent 工具。预设里没有的，选「自己输入"
      "命令」再敲命令名。",
      "第三段：给上面那个工具加可选参数。勾选即可，也可以手动追加。"
      "参数会随所选工具变化。",
      "不用分段，整行命令完全由你写。适合需要先 source 虚拟环境、"
      "导出环境变量、或串联两条命令的场景。" },
    { "Part 1: the directory this card runs in. Type it, or take the "
      "directory the current window is in.",
      "Part 2: which terminal agent CLI to launch. Not listed? Pick "
      "\"Type your own command\" and enter it.",
      "Part 3: flags for that tool. Tick the presets, or append your own. "
      "The list changes with the tool.",
      "Skip the three parts and write the whole line yourself — for "
      "sourcing a venv, exporting vars, or chaining commands." },
};

static const char *const k_new_card[2] = { "+ 新增项目", "+ New project" };

/* ── Selection helpers ──────────────────────────────────────────────── */
static int nav_count(const dssh_settings_t *st) {
    return st->project_count + 1;      /* + the 新增 card */
}

static int add_allowed(const dssh_settings_t *st) {
    return st->project_count < DSSH_PROJECTS_MAX;
}

static void list_keep_visible(project_ui_t *ui, const dssh_settings_t *st) {
    int n = nav_count(st);
    if (ui->list_cursor < 0) ui->list_cursor = 0;
    if (ui->list_cursor >= n) ui->list_cursor = n - 1;
    if (ui->list_first > ui->list_cursor)
        ui->list_first = ui->list_cursor;
    if (ui->list_cursor >= ui->list_first + CARD_VIS)
        ui->list_first = ui->list_cursor - CARD_VIS + 1;
    int max_first = n - CARD_VIS;
    if (max_first < 0) max_first = 0;
    ui->list_first = clampi(ui->list_first, 0, max_first);
}

static int tool_index_of(const dssh_project_t *p) {
    int n = 0;
    const proj_tool_t *tools = proj_tools(&n);
    for (int i = 0; i < n; i++)
        if (tools[i].tool[0] && strcmp(tools[i].tool, p->tool) == 0) return i;
    return n - 1;                     /* the custom slot is last */
}

static const proj_tool_t *draft_tool(const project_ui_t *ui) {
    return proj_tool_by_name(ui->draft.tool);
}

/* How many rows the 参数 tab shows: one per flag, plus "append manually". */
static int args_row_count(const project_ui_t *ui) {
    const proj_tool_t *t = draft_tool(ui);
    return (t ? t->nflags : 0) + 1;
}

/* Switching tabs is what decides between the three-part form and the
 * fully-custom command, so the raw flag is derived here rather than at
 * each of the four places a tab can change (D-pad ◀/▶, L/R, a tap on the
 * strip).  Switching away from 自定义 is deliberately non-destructive: the
 * typed command stays in `cmd` and comes back when the tab is re-entered. */
static void set_tab(project_ui_t *ui, int tab) {
    ui->tab = tab;
    ui->draft.raw = (tab == TAB_CUSTOM) ? 1 : 0;
}

static void args_keep_visible(project_ui_t *ui) {
    int n = args_row_count(ui);
    if (ui->args_cursor < 0) ui->args_cursor = 0;
    if (ui->args_cursor >= n) ui->args_cursor = n - 1;
    if (ui->args_first > ui->args_cursor) ui->args_first = ui->args_cursor;
    if (ui->args_cursor >= ui->args_first + 5)
        ui->args_first = ui->args_cursor - 4;
    if (ui->args_first < 0) ui->args_first = 0;
}

/* ── Text entry ─────────────────────────────────────────────────────── */
static const char *field_buf_c(const project_ui_t *ui) {
    switch (ui->field) {
    case FIELD_NAME: return ui->draft.name;
    case FIELD_PATH: return ui->draft.path;
    case FIELD_NOTE: return ui->draft.note;
    case FIELD_TOOL: return ui->draft.tool;
    case FIELD_ARGS: return ui->draft.args;
    case FIELD_CMD:  return ui->draft.cmd;
    default:         return NULL;
    }
}

static char *field_buf(project_ui_t *ui) {
    return (char *)field_buf_c(ui);
}

static int field_cap(pui_field_t f) {
    switch (f) {
    case FIELD_NAME: return DSSH_PROJECT_NAME_MAX;
    case FIELD_PATH: return DSSH_PROJECT_PATH_MAX;
    case FIELD_NOTE: return DSSH_PROJECT_NOTE_MAX;
    case FIELD_TOOL: return DSSH_PROJECT_TOOL_MAX;
    case FIELD_ARGS: return DSSH_PROJECT_ARGS_MAX;
    case FIELD_CMD:  return DSSH_PROJECT_CMD_MAX;
    default:         return 0;
    }
}

static void enter_field(project_ui_t *ui, pui_field_t f, int touch_pressed) {
    ui->field = f;
    /* The press that got us here must not also reach the soft keyboard:
     * softkb derives its down-edge from prev_pressed, so the very next
     * frame would look like a fresh tap on whatever key is under the
     * finger.  main.c passes (-1,-1) to softkb_touch until this clears. */
    ui->kbd_armed = touch_pressed ? 0 : 1;
}

/* ── cwd probe ──────────────────────────────────────────────────────── */
void project_ui_probe_cwd(project_ui_t *ui, ssh_client_t *ssh) {
    if (!ui) return;
    if (ui->aux) { ui->cwd_state = CWD_PROBING; return; }
    if (!ssh || !ssh_is_connected(ssh)) { ui->cwd_state = CWD_FAILED; return; }
    ui->cwd_state = CWD_PROBING;
    ui->aux_len = 0;
    ui->aux_buf[0] = 0;
    ui->aux = ssh_aux_exec(ssh, "pwd", NULL, 0);
    if (!ui->aux) ui->cwd_state = CWD_FAILED;
}

void project_ui_poll_cwd(project_ui_t *ui, ssh_client_t *ssh) {
    (void)ssh;
    if (!ui || !ui->aux) return;

    int failed = 0;
    for (int i = 0; i < 4; i++) {
        if (ui->aux_len >= (int)sizeof(ui->aux_buf) - 1) break;
        int n = ssh_aux_read(ui->aux, ui->aux_buf + ui->aux_len,
                             (int)sizeof(ui->aux_buf) - 1 - ui->aux_len);
        if (n > 0) {
            ui->aux_len += n;
            ui->aux_buf[ui->aux_len] = 0;
            continue;
        }
        if (n < 0) { failed = 1; break; }
        if (!ssh_aux_eof(ui->aux)) return;   /* more to come next frame */
        break;
    }

    /* Falling out of the loop because the per-frame read budget ran out
     * is NOT the same thing as the remote closing stdout.  Treating them
     * alike truncated a slow answer to its first four chunks and then
     * reported that fragment as the directory.  Only EOF, a hard error,
     * or a genuinely full buffer ends the probe. */
    int done = failed;
    if (!done) {
        if (ui->aux_len >= (int)sizeof(ui->aux_buf) - 1) done = 1;
        else if (ssh_aux_eof(ui->aux))                  done = 1;
    }
    if (!done) return;

    /* Done: trim the trailing newline and stash the answer. */
    while (ui->aux_len > 0 &&
           (ui->aux_buf[ui->aux_len - 1] == '\n' ||
            ui->aux_buf[ui->aux_len - 1] == '\r'))
        ui->aux_buf[--ui->aux_len] = 0;
    if (!failed && ui->aux_len > 0 && ui->aux_buf[0] == '/') {
        copy_str(ui->cwd, sizeof(ui->cwd), ui->aux_buf);
        ui->cwd_state = CWD_OK;
    } else {
        ui->cwd_state = CWD_FAILED;
    }
    ssh_aux_close(ui->aux);
    ui->aux = NULL;
}

int project_ui_cwd_state(const project_ui_t *ui) {
    return ui ? (int)ui->cwd_state : CWD_IDLE;
}

const char *project_ui_cwd(const project_ui_t *ui) {
    return ui ? ui->cwd : "";
}

/* What "use the current directory" would fill in, and where it came from.
 * The window's own cd wins: DSSH typed it, so it is exact — a second SSH
 * channel's `pwd` only knows where a *new* shell would start. */
static const char *best_cwd(const project_ui_t *ui, const char **source) {
    if (ui->window_cwd[0]) {
        if (source) *source = ui->lang == L_ZH ? "本窗口" : "this window";
        return ui->window_cwd;
    }
    if (ui->cwd_state == CWD_OK && ui->cwd[0]) {
        if (source) *source = ui->lang == L_ZH ? "登录目录" : "login dir";
        return ui->cwd;
    }
    if (source) *source = "";
    return "";
}

/* ── Draft lifecycle ────────────────────────────────────────────────── */
static void draft_from(const project_ui_t *ui, const dssh_settings_t *st,
                       dssh_project_t *out) {
    if (ui->draft_row >= 0 && ui->draft_row < st->project_count)
        *out = st->projects[ui->draft_row];
    else
        memset(out, 0, sizeof(*out));
}

static void open_editor(project_ui_t *ui, dssh_settings_t *st, int row,
                        ssh_client_t *ssh, int touch_pressed) {
    ui->draft_row = row;
    draft_from(ui, st, &ui->draft);
    if (row < 0) memset(&ui->draft, 0, sizeof(ui->draft));
    ui->in_editor    = 1;
    ui->dirty_before = ui->dirty;
    /* Land on the tab that matches how the card is actually stored: a card
     * saved as a fully-custom command must open on 自定义, otherwise the
     * form would show the three-part view while draft.raw still said the
     * hand-written string is what runs. */
    set_tab(ui, ui->draft.raw ? TAB_CUSTOM : TAB_PATH);
    ui->path_cursor  = 0;
    ui->tool_cursor  = tool_index_of(&ui->draft);
    ui->args_cursor  = 0;
    ui->args_first   = 0;
    ui->field        = FIELD_NONE;
    ui->kbd_armed    = 1;
    ui->delete_armed = -1;
    /* Kick the directory probe now: it is one round trip on an auxiliary
     * channel, and having the answer ready is what makes the "use the
     * current directory" button a single tap instead of a wait. */
    if (ui->cwd_state != CWD_PROBING) project_ui_probe_cwd(ui, ssh);
    (void)touch_pressed;
}

static void commit_draft(project_ui_t *ui, dssh_settings_t *st) {
    if (!ui->draft.name[0]) {
        /* A card with no name is unfindable in the list; fall back to the
         * directory's last segment, then to the tool name. */
        path_basename(ui->draft.path, ui->draft.name, sizeof(ui->draft.name));
        if (!ui->draft.name[0])
            copy_str(ui->draft.name, sizeof(ui->draft.name),
                     ui->draft.tool[0] ? ui->draft.tool : "card");
    }
    if (ui->draft_row >= 0 && ui->draft_row < st->project_count) {
        st->projects[ui->draft_row] = ui->draft;
    } else if (add_allowed(st)) {
        st->projects[st->project_count] = ui->draft;
        ui->list_cursor = st->project_count;
        st->project_count++;
    } else {
        return;                       /* full — nothing to commit into */
    }
    ui->dirty = 1;
}

static void leave_editor(project_ui_t *ui, dssh_settings_t *st, int save) {
    if (save) commit_draft(ui, st);
    else      ui->dirty = ui->dirty_before;
    ui->in_editor = 0;
    ui->draft_row = -1;
    ui->field     = FIELD_NONE;
    list_keep_visible(ui, st);
}

static void delete_row(project_ui_t *ui, dssh_settings_t *st, int idx) {
    if (idx < 0 || idx >= st->project_count) return;
    for (int i = idx; i < st->project_count - 1; i++)
        st->projects[i] = st->projects[i + 1];
    memset(&st->projects[st->project_count - 1], 0,
           sizeof(st->projects[0]));
    st->project_count--;
    if (st->selected_project >= st->project_count)
        st->selected_project = DSSH_PROJECT_NONE;
    if (ui->list_cursor >= nav_count(st)) ui->list_cursor = nav_count(st) - 1;
    if (ui->list_cursor < 0) ui->list_cursor = 0;
    ui->dirty = 1;
}

/* ── Hit testing ────────────────────────────────────────────────────── */
static int list_hit(const project_ui_t *ui, const dssh_settings_t *st,
                    int tx, int ty) {
    if (ty >= LIST_FTR_Y && ty < LIST_FTR_Y + LIST_FTR_H) {
        int bw = (INNER_W - (LIST_BTN_N - 1) * LIST_BTN_GAP) / LIST_BTN_N;
        for (int i = 0; i < LIST_BTN_N; i++) {
            int bx = INNER_X + i * (bw + LIST_BTN_GAP);
            if (tx >= bx && tx < bx + bw) {
                switch (i) {
                case 0: return HIT_RUN;
                case 1: return HIT_EDIT;
                case 2: return HIT_ADD;
                default: return HIT_DELETE;
                }
            }
        }
        return HIT_NONE;
    }
    if (ty < CARD_Y0) return HIT_NONE;
    int slot = (ty - CARD_Y0) / CARD_PITCH;
    if (slot < 0 || slot >= CARD_VIS) return HIT_NONE;
    int row = ui->list_first + slot;
    /* Reject the gutter between cards so a tap there does nothing rather
     * than picking the neighbour. */
    if ((ty - CARD_Y0) - slot * CARD_PITCH >= CARD_H) return HIT_NONE;
    if (row >= nav_count(st)) return HIT_NONE;
    return HIT_ROW(row);
}

static int form_hit(const project_ui_t *ui, int tx, int ty) {
    if (ty >= TAB_Y && ty < TAB_Y + TAB_H) {
        for (int i = 0; i < TAB_N; i++) {
            int x = TAB_X0 + i * TAB_PITCH;
            if (tx >= x && tx < x + TAB_W) return HIT_TAB(i);
        }
        return HIT_NONE;
    }
    if (ty >= FTR_Y) {
        int bw = 74;
        if (tx >= INNER_X && tx < INNER_X + bw) return HIT_SAVE;
        if (tx >= INNER_X + bw + 6 && tx < INNER_X + 2 * bw + 6)
            return HIT_CANCEL;
        return HIT_NONE;
    }
    switch (ui->tab) {
    case TAB_PATH: {
        /* three 32-px rows, then the probe button */
        for (int i = 0; i < 3; i++) {
            int y = BODY_Y0 + 4 + i * 32;
            if (ty >= y && ty < y + 30) return HIT_ROW(i);
        }
        int by = BODY_Y0 + 4 + 3 * 32 + 4;
        if (ty >= by && ty < by + 28) return HIT_BTN(0);
        return HIT_NONE;
    }
    case TAB_TOOL: {
        int n = 0;
        proj_tools(&n);
        for (int i = 0; i < n; i++) {
            int col = i % 2, row = i / 2;
            int x = INNER_X + col * (151 + 6);
            int y = 46 + row * 36;
            if (tx >= x && tx < x + 151 && ty >= y && ty < y + 34)
                return HIT_TOOL(i);
        }
        return HIT_NONE;
    }
    case TAB_ARGS: {
        int n = args_row_count(ui);
        for (int slot = 0; slot < 5; slot++) {
            int idx = ui->args_first + slot;
            if (idx >= n) break;
            int y = 62 + slot * 28;
            if (ty >= y && ty < y + 26) return HIT_FLAG(idx);
        }
        return HIT_NONE;
    }
    case TAB_CUSTOM: {
        int by = 176;
        if (ty >= by && ty < by + 28) return HIT_BTN(1);
        return HIT_NONE;
    }
    default: return HIT_NONE;
    }
}

/* ── Row activation ─────────────────────────────────────────────────── */
/* One place that knows what "activate row N of tab T" means, so the D-pad
 * and a tap cannot drift apart. */
static void activate_path_row(project_ui_t *ui, int row, ssh_client_t *ssh,
                              int touch_pressed) {
    switch (row) {
    case 0: enter_field(ui, FIELD_NAME, touch_pressed); break;
    case 1: enter_field(ui, FIELD_PATH, touch_pressed); break;
    case 2: enter_field(ui, FIELD_NOTE, touch_pressed); break;
    default: break;
    }
    (void)ssh;
}

static void activate_tool(project_ui_t *ui, int idx, int touch_pressed) {
    int n = 0;
    const proj_tool_t *tools = proj_tools(&n);
    if (idx < 0 || idx >= n) return;
    ui->tool_cursor = idx;
    if (!tools[idx].tool[0]) {
        /* The custom slot: the command name is whatever the user types. */
        ui->draft.tool[0] = 0;
        enter_field(ui, FIELD_TOOL, touch_pressed);
        return;
    }
    if (strcmp(ui->draft.tool, tools[idx].tool) != 0) {
        copy_str(ui->draft.tool, sizeof(ui->draft.tool), tools[idx].tool);
        /* Presets belong to a tool; carrying claude's --continue over to
         * codex would produce a command that silently does nothing. */
        ui->draft.args[0] = 0;
    }
    ui->args_cursor = 0;
    ui->args_first  = 0;
}

static void activate_args_row(project_ui_t *ui, int idx, int touch_pressed) {
    const proj_tool_t *t = draft_tool(ui);
    int nf = t ? t->nflags : 0;
    if (idx < nf) {
        proj_args_toggle_flag(ui->draft.args, DSSH_PROJECT_ARGS_MAX,
                              t->flags[idx].arg);
        return;
    }
    enter_field(ui, FIELD_ARGS, touch_pressed);
}

/* ── Per-frame input ────────────────────────────────────────────────── */
pui_act_t project_ui_tick(project_ui_t *ui, dssh_settings_t *st,
                          ssh_client_t *ssh,
                          uint32_t down, uint32_t held,
                          const char *kt,
                          int tx, int ty, int touch_pressed, int touch_down,
                          int *out_index) {
    (void)held;
    if (out_index) *out_index = -1;
    if (!ui || !ui->open || !st) return PUI_ACT_NONE;

    /* Follow 设置 → 界面语言, live.  The two enums are NOT the same shape:
     * pui_lang_t is two-valued (L_ZH / L_EN) while settings' ui_language is
     * 0..5 (softkb.c's LANG_*: zh / en / ja / ko / de / fr).  The comparison
     * below is what this page means by that — Chinese is Chinese and every
     * other language takes the English column, because a zh/en table is all
     * this page has.  (It used to be a plain cast with a comment claiming
     * the values "line up on purpose"; they stopped lining up when the
     * settings row grew from 2 languages to 6.)  Done here rather than in
     * project_ui_open() because open() is not handed the settings, and
     * because tick() runs before the draw in the same frame, so a language
     * flip lands on the very next repaint. */
    ui->lang = (st->ui_language == L_ZH) ? L_ZH : L_EN;

    project_ui_poll_cwd(ui, ssh);

    /* ── Text entry owns the frame while it is up ── */
    if (ui->field != FIELD_NONE) {
        if (!touch_pressed) ui->kbd_armed = 1;
        char *buf = field_buf(ui);
        int cap = field_cap(ui->field);
        if (buf) {
            if (kt && strcmp(kt, "\x7f") == 0) {
                backspace_cp(buf);
                ui->dirty = 1;
            } else if (kt && (strcmp(kt, "\r") == 0 || strcmp(kt, "\t") == 0)) {
                ui->field = FIELD_NONE;       /* commit and go back */
            } else if (kt && kt[0] == '\x1b') {
                /* Escape sequences (arrows, function keys) have no meaning
                 * in a one-line field. */
            } else if (kt && *kt) {
                append_bytes(buf, (size_t)cap, kt);
                ui->dirty = 1;
            }
        }
        if (down & (KEY_A | KEY_B | KEY_START | KEY_SELECT))
            ui->field = FIELD_NONE;
        return PUI_ACT_NONE;
    }

    /* ── Editor ── */
    if (ui->in_editor) {
        int n = 0;
        proj_tools(&n);

        if (down & KEY_B) {
            leave_editor(ui, st, 1);
            return PUI_ACT_NONE;
        }
        if (down & KEY_START) {
            leave_editor(ui, st, 1);
            ui->open = 0;
            return PUI_ACT_CLOSED;
        }
        if (down & KEY_DLEFT) set_tab(ui, (ui->tab + TAB_N - 1) % TAB_N);
        if (down & KEY_DRIGHT) set_tab(ui, (ui->tab + 1) % TAB_N);
        if (down & KEY_L) set_tab(ui, (ui->tab + TAB_N - 1) % TAB_N);
        if (down & KEY_R) set_tab(ui, (ui->tab + 1) % TAB_N);

        if (down & KEY_DUP) {
            switch (ui->tab) {
            case TAB_PATH:
                /* Rows 0..2 are the text fields, 3 is the probe button —
                 * all four are on the D-pad ring so the whole tab is
                 * reachable without the stylus. */
                ui->path_cursor = (ui->path_cursor + 3) % 4;
                break;
            case TAB_TOOL: ui->tool_cursor = (ui->tool_cursor + n - 1) % n; break;
            case TAB_ARGS:
                ui->args_cursor--;
                if (ui->args_cursor < 0) ui->args_cursor = args_row_count(ui) - 1;
                args_keep_visible(ui);
                break;
            default: break;
            }
        }
        if (down & KEY_DDOWN) {
            switch (ui->tab) {
            case TAB_PATH: ui->path_cursor = (ui->path_cursor + 1) % 4; break;
            case TAB_TOOL: ui->tool_cursor = (ui->tool_cursor + 1) % n; break;
            case TAB_ARGS:
                ui->args_cursor++;
                if (ui->args_cursor >= args_row_count(ui)) ui->args_cursor = 0;
                args_keep_visible(ui);
                break;
            default: break;
            }
        }
        if (down & KEY_A) {
            switch (ui->tab) {
            case TAB_PATH:
                if (ui->path_cursor == 3) {
                    const char *src = NULL;
                    const char *c = best_cwd(ui, &src);
                    if (c && *c) {
                        copy_str(ui->draft.path, sizeof(ui->draft.path), c);
                        if (!ui->draft.name[0])
                            path_basename(ui->draft.path, ui->draft.name,
                                          sizeof(ui->draft.name));
                        ui->dirty = 1;
                    } else {
                        project_ui_probe_cwd(ui, ssh);
                    }
                } else {
                    activate_path_row(ui, ui->path_cursor, ssh, 0);
                }
                break;
            case TAB_TOOL: activate_tool(ui, ui->tool_cursor, 0); break;
            case TAB_ARGS: activate_args_row(ui, ui->args_cursor, 0); break;
            default: enter_field(ui, FIELD_CMD, 0); break;
            }
        }

        /* Touch.  Only the down-edge acts; a drag across the form must not
         * fire a row it happens to pass over. */
        if (touch_down) {
            int h = form_hit(ui, tx, ty);
            if (h == HIT_SAVE)            { leave_editor(ui, st, 1); }
            else if (h == HIT_CANCEL)     { leave_editor(ui, st, 0); }
            else if (HIT_IS_TAB_KIND(h))  { set_tab(ui, HIT_TAB_IDX(h)); }
            else if (HIT_IS_ROW_KIND(h))  { ui->path_cursor = HIT_ROW_IDX(h);
                                            activate_path_row(ui, HIT_ROW_IDX(h),
                                                              ssh, 1); }
            else if (HIT_IS_BTN_KIND(h) && HIT_BTN_IDX(h) == 0) {
                const char *src = NULL;
                const char *c = best_cwd(ui, &src);
                if (c && *c) {
                    copy_str(ui->draft.path, sizeof(ui->draft.path), c);
                    if (!ui->draft.name[0])
                        path_basename(ui->draft.path, ui->draft.name,
                                      sizeof(ui->draft.name));
                    ui->dirty = 1;
                } else {
                    project_ui_probe_cwd(ui, ssh);
                }
            } else if (HIT_IS_BTN_KIND(h) && HIT_BTN_IDX(h) == 1) {
                enter_field(ui, FIELD_CMD, 1);
            } else if (HIT_IS_TOOL_KIND(h)) {
                activate_tool(ui, HIT_TOOL_IDX(h), 1);
            } else if (HIT_IS_FLAG_KIND(h)) {
                ui->args_cursor = HIT_FLAG_IDX(h);
                activate_args_row(ui, HIT_FLAG_IDX(h), 1);
            }
        }
        return PUI_ACT_NONE;
    }

    /* ── Card list ── */
    /* Wrap, like every cursor in the editor below.  The wrap lives here
     * and not in list_keep_visible(), which is also the "a card just
     * disappeared" repair — clamping is right there and wrong here. */
    list_keep_visible(ui, st);
    if (down & KEY_DUP) {
        ui->list_cursor--;
        if (ui->list_cursor < 0) ui->list_cursor = nav_count(st) - 1;
        ui->delete_armed = -1;
    }
    if (down & KEY_DDOWN) {
        ui->list_cursor++;
        if (ui->list_cursor >= nav_count(st)) ui->list_cursor = 0;
        ui->delete_armed = -1;
    }
    list_keep_visible(ui, st);

    if (down & KEY_B) {
        ui->open = 0;
        return PUI_ACT_CLOSED;
    }
    /* START means "done with this page" everywhere else in DSSH, and in
     * the editor it saves and closes; here there is nothing to save, so
     * it just closes. */
    if (down & KEY_START) {
        ui->open = 0;
        return PUI_ACT_CLOSED;
    }
    if (down & KEY_X) {
        if (ui->list_cursor < st->project_count)
            open_editor(ui, st, ui->list_cursor, ssh, 0);
        return PUI_ACT_NONE;
    }
    if (down & KEY_Y) {
        if (ui->list_cursor < st->project_count) {
            if (ui->delete_armed == ui->list_cursor) {
                delete_row(ui, st, ui->list_cursor);
                ui->delete_armed = -1;
            } else {
                ui->delete_armed = ui->list_cursor;
            }
        }
        return PUI_ACT_NONE;
    }
    if (down & KEY_A) {
        if (ui->list_cursor >= st->project_count) {
            if (add_allowed(st)) open_editor(ui, st, -1, ssh, 0);
        } else if (ui->entry != PUI_ENTRY_SETTINGS) {
            if (out_index) *out_index = ui->list_cursor;
            return PUI_ACT_RUN;
        }
        return PUI_ACT_NONE;
    }

    if (touch_down) {
        int h = list_hit(ui, st, tx, ty);
        if (h == HIT_RUN && ui->entry != PUI_ENTRY_SETTINGS &&
            ui->list_cursor < st->project_count) {
            if (out_index) *out_index = ui->list_cursor;
            return PUI_ACT_RUN;
        }
        if (h == HIT_EDIT && ui->list_cursor < st->project_count)
            open_editor(ui, st, ui->list_cursor, ssh, 1);
        else if (h == HIT_ADD && add_allowed(st))
            open_editor(ui, st, -1, ssh, 1);
        else if (h == HIT_DELETE && ui->list_cursor < st->project_count) {
            if (ui->delete_armed == ui->list_cursor) {
                delete_row(ui, st, ui->list_cursor);
                ui->delete_armed = -1;
            } else {
                ui->delete_armed = ui->list_cursor;
            }
        } else if (HIT_IS_ROW_KIND(h)) {
            int row = HIT_ROW_IDX(h);
            if (row == ui->list_cursor && row < st->project_count &&
                ui->entry != PUI_ENTRY_SETTINGS) {
                /* Second tap on the already-selected card runs it — the
                 * same gesture the list has everywhere else on the 3DS. */
                if (out_index) *out_index = row;
                return PUI_ACT_RUN;
            }
            ui->list_cursor = row;
            ui->delete_armed = -1;
        }
    }
    return PUI_ACT_NONE;
}

/* ── Drawing: bottom screen, list mode ──────────────────────────────── */
static void draw_header(const project_ui_t *ui, const dssh_settings_t *st,
                        const pui_col_t *c, const char *title,
                        const char *right) {
    fill(0, 0, SCR_W, HDR_H, Z_PANEL, c->panel);
    fill(0, HDR_H - 1, SCR_W, 1, Z_BORDER, c->border);
    renderer_draw_text_px_z(INNER_X, 4, Z_TEXT_HI, title, c->ink);
    if (right && *right) {
        int w = renderer_utf8_text_width_px(right);
        renderer_draw_text_px_z(SCR_W - INNER_X - w, 4, Z_TEXT, right, c->dim);
    }
    (void)ui; (void)st;
}

static void draw_list(const project_ui_t *ui, const dssh_settings_t *st,
                      const pui_col_t *c) {
    char right[32];
    snprintf(right, sizeof(right), "%d/%d", st->project_count,
             DSSH_PROJECTS_MAX);
    draw_header(ui, st, c, T("项目", "Projects"), right);

    int n = nav_count(st);
    for (int slot = 0; slot < CARD_VIS; slot++) {
        int idx = ui->list_first + slot;
        if (idx >= n) break;
        int y = CARD_Y0 + slot * CARD_PITCH;
        int sel = (idx == ui->list_cursor);

        if (idx >= st->project_count) {
            /* The 新增 card.  Dashed-looking (a single hairline) and
             * centred so it reads as an action, not a record. */
            int dis = !add_allowed(st);
            chip(INNER_X, y, INNER_W, CARD_H,
                 c->card, dis ? c->border : c->border, 0);
            const char *lbl = k_new_card[ui->lang == L_ZH ? 0 : 1];
            int w = renderer_utf8_text_width_px(lbl);
            renderer_draw_text_px_z((SCR_W - w) / 2, y + (CARD_H - 12) / 2,
                                    Z_TEXT_HI, lbl, dis ? c->dim : c->ink);
            if (sel)
                stroke(INNER_X - 1, y - 1, INNER_W + 2, CARD_H + 2,
                       Z_BORDER, c->accent);
            continue;
        }

        const dssh_project_t *p = &st->projects[idx];
        uint32_t surf = sel ? c->card_sel : c->card;
        /* The two inks travel with the surface.  card_sel is a bright
         * accent FILL, so a card that keeps the light ink loses its own
         * name (label on key_accent measures 1.2:1 in DARK and 1.2:1 in
         * MONO — see the floors in test_kb_theme.c). */
        uint32_t ink = sel ? c->sel_ink : c->ink;
        uint32_t dim = sel ? c->sel_dim : c->dim;
        chip(INNER_X, y, INNER_W, CARD_H, surf, c->border, sel);

        /* The tool's mark, so the list is scannable by CLI rather than by
         * reading every name.  Always in the card's own ink — a mark
         * dimmed to the secondary tone is not worth 16 px of a card. */
        draw_logo(CARD_LOGO_X, y + CARD_LOGO_Y, card_logo(p), 1, ink);

        char tbuf[40];
        const char *tool = project_tool_label(p, tbuf, sizeof(tbuf), ui->lang);
        int tw = renderer_utf8_text_width_px(tool);
        int name_w = CARD_TXT_R - 8 - tw - CARD_TXT_X;

        char nbuf[DSSH_PROJECT_NAME_MAX];
        tf_text_clip(CARD_TXT_X, y + 4, Z_TEXT_HI,
                  card_display_name(ui, p, nbuf, sizeof(nbuf)),
                  ink, name_w);
        renderer_draw_text_px_z(CARD_TXT_R - tw, y + 4, Z_TEXT,
                                tool, sel ? ink : dim);

        const char *sub = p->path[0] ? p->path
                                     : (p->raw ? p->cmd
                                               : (ui->lang == L_ZH ? "(未设置目录)"
                                                                   : "(no directory)"));
        tf_text_clip(CARD_TXT_X, y + 19, Z_TEXT, sub, dim,
                  CARD_TXT_R - CARD_TXT_X);

        if (ui->delete_armed == idx) {
            const char *warn = ui->lang == L_ZH ? "再按一次 Y 删除" : "Y again to delete";
            int w = renderer_utf8_text_width_px(warn);
            fill(INNER_X + INNER_W - w - 10, y + 18, w + 6, 14, Z_TEXT_HI,
                 c->warn);
            renderer_draw_text_px_z(INNER_X + INNER_W - w - 7, y + 19,
                                    Z_TEXT_HI, warn, c->bg);
        }
    }

    /* Action bar. */
    fill(0, LIST_FTR_Y, SCR_W, LIST_FTR_H, Z_PANEL, c->panel);
    fill(0, LIST_FTR_Y, SCR_W, 1, Z_BORDER, c->border);
    int bw = (INNER_W - (LIST_BTN_N - 1) * LIST_BTN_GAP) / LIST_BTN_N;
    static const char *const lbl_zh[LIST_BTN_N] = { "进入", "编辑", "新增", "删除" };
    static const char *const lbl_en[LIST_BTN_N] = { "Run", "Edit", "Add", "Del" };
    for (int i = 0; i < LIST_BTN_N; i++) {
        int bx = INNER_X + i * (bw + LIST_BTN_GAP);
        int by = LIST_FTR_Y + 4;
        int enabled = 1;
        if ((i == 0 || i == 1 || i == 3) &&
            ui->list_cursor >= st->project_count)
            enabled = 0;
        if (i == 0 && ui->entry == PUI_ENTRY_SETTINGS) enabled = 0;
        if (i == 2) enabled = add_allowed(st);
        chip(bx, by, bw, 24, c->card, c->border, 0);
        chip_label(bx, by, bw, 24,
                   ui->lang == L_ZH ? lbl_zh[i] : lbl_en[i],
                   enabled ? c->ink : c->dim);
    }
}

/* ── Drawing: bottom screen, edit mode ──────────────────────────────── */
static void draw_tabs(const project_ui_t *ui, const pui_col_t *c) {
    for (int i = 0; i < TAB_N; i++) {
        int x = TAB_X0 + i * TAB_PITCH;
        int sel = (i == ui->tab);
        chip(x, TAB_Y, TAB_W, TAB_H,
             sel ? c->card_sel : c->card, c->border, sel);
        chip_label(x, TAB_Y, TAB_W, TAB_H,
                   k_tab_label[ui->lang == L_ZH ? 0 : 1][i],
                   sel ? c->sel_ink : c->dim);
    }
}

static void draw_path_tab(const project_ui_t *ui, const pui_col_t *c) {
    static const char *const lbl_zh[3] = { "名称", "工作目录", "备注" };
    static const char *const lbl_en[3] = { "Name", "Directory", "Note" };
    const char *const *lbl = ui->lang == L_ZH ? lbl_zh : lbl_en;
    const char *vals[3] = { ui->draft.name, ui->draft.path, ui->draft.note };

    for (int i = 0; i < 3; i++) {
        int y = BODY_Y0 + 4 + i * 32;
        int sel = (ui->path_cursor == i);
        chip(INNER_X, y, INNER_W, 30, c->card, sel ? c->accent : c->border, sel);
        renderer_draw_text_px_z(INNER_X + 8, y + 2, Z_TEXT, lbl[i], c->dim);
        const char *v = vals[i][0] ? vals[i]
                                   : (ui->lang == L_ZH ? "（点击输入）"
                                                       : "(tap to type)");
        tf_text_clip(INNER_X + 8, y + 15, Z_TEXT_HI, v,
                  vals[i][0] ? c->ink : c->dim, INNER_W - 16);
    }

    /* The probe button doubles as the probe's status line. */
    int by = BODY_Y0 + 4 + 3 * 32 + 4;
    chip(INNER_X, by, INNER_W, 28, c->card, c->border, 0);
    const char *src = NULL;
    const char *cwd = best_cwd(ui, &src);
    char line[128];
    if (cwd && *cwd) {
        snprintf(line, sizeof(line),
                 ui->lang == L_ZH ? "用当前目录（%s）: %s"
                                  : "Use current dir (%s): %s",
                 src, cwd);
    } else if (ui->cwd_state == CWD_PROBING) {
        snprintf(line, sizeof(line), "%s",
                 ui->lang == L_ZH ? "正在探测当前目录…" : "Probing current dir…");
    } else if (ui->cwd_state == CWD_FAILED) {
        snprintf(line, sizeof(line), "%s",
                 ui->lang == L_ZH ? "探测失败，点此重试" : "Probe failed, tap to retry");
    } else {
        snprintf(line, sizeof(line), "%s",
                 ui->lang == L_ZH ? "取用当前目录" : "Use current directory");
    }
    int tw = renderer_utf8_text_width_px(line);
    renderer_draw_text_px_z(INNER_X + (INNER_W - tw) / 2, by + 8, Z_TEXT_HI,
                            line, (cwd && *cwd) ? c->ink : c->dim);

    int hy = by + 34;
    text_wrap(INNER_X, hy, Z_TEXT,
              ui->lang == L_ZH
                ? "「当前目录」优先取本窗口被 cd 到的位置，"
                  "取不到时用新建 shell 会登录到的目录。"
                : "Uses this window's cd target when known, "
                  "otherwise the login directory of a new shell.",
              c->dim, INNER_W, 3);
}

static void draw_tool_tab(const project_ui_t *ui, const pui_col_t *c) {
    int n = 0;
    const proj_tool_t *tools = proj_tools(&n);
    for (int i = 0; i < n; i++) {
        int col = i % 2, row = i / 2;
        int x = TOOL_CHIP_X0 + col * (TOOL_CHIP_W + 6);
        int y = TOOL_CHIP_Y0 + row * (TOOL_CHIP_H + 2);
        int sel = (i == ui->tool_cursor);
        int used = (tools[i].tool[0] &&
                    strcmp(tools[i].tool, ui->draft.tool) == 0);
        uint32_t ink = sel ? c->sel_ink : c->ink;
        uint32_t dim = sel ? c->sel_dim : c->dim;
        chip(x, y, TOOL_CHIP_W, TOOL_CHIP_H, sel ? c->card_sel : c->card,
             (sel || used) ? c->accent : c->border, sel);
        draw_logo(x + TOOL_LOGO_X, y + TOOL_LOGO_Y, tools[i].logo, 1, ink);
        renderer_draw_text_px_z(x + TOOL_NAME_X, y + 4, Z_TEXT_HI,
                                tools[i].tool[0]
                                    ? tools[i].tool
                                    : (ui->lang == L_ZH ? "自定义" : "custom"),
                                ink);
        tf_text_clip(x + 6, y + 19, Z_TEXT,
                  ui->lang == L_ZH ? tools[i].desc_zh : tools[i].desc_en,
                  dim, 139);
    }
    text_wrap(INNER_X, 194, Z_TEXT,
              ui->lang == L_ZH
                ? "选中即替换命令名；换工具会清空已勾选的参数。"
                : "Picking a tool rewrites the command name; "
                  "switching clears the ticked flags.",
              c->dim, INNER_W, 2);
}

static void draw_args_tab(const project_ui_t *ui, const pui_col_t *c) {
    const proj_tool_t *t = draft_tool(ui);
    int nf = t ? t->nflags : 0;
    char head[128];
    if (t) {
        snprintf(head, sizeof(head), ui->lang == L_ZH ? "%s 的可选参数" : "%s flags",
                 t->tool);
    } else {
        snprintf(head, sizeof(head), "%s",
                 ui->lang == L_ZH ? "自定义命令：没有预设参数"
                                  : "Custom command: no presets");
    }
    renderer_draw_text_px_z(INNER_X, 46, Z_TEXT, head, c->dim);

    int n = nf + 1;
    for (int slot = 0; slot < 5; slot++) {
        int idx = ui->args_first + slot;
        if (idx >= n) break;
        int y = 62 + slot * 28;
        int sel = (idx == ui->args_cursor);
        chip(INNER_X, y, INNER_W, 26, c->card, sel ? c->accent : c->border, sel);

        if (idx < nf) {
            const proj_flag_t *f = &t->flags[idx];
            int on = proj_args_has_flag(ui->draft.args, f->arg);
            uint32_t ink = f->dangerous ? c->warn : c->ink;
            renderer_draw_text_px_z(INNER_X + 8, y + 2, Z_TEXT_HI,
                                    on ? "[x]" : "[ ]", on ? c->accent : c->dim);
            tf_text_clip(INNER_X + 34, y + 2, Z_TEXT_HI, f->arg, ink,
                      INNER_W - 44);
            tf_text_clip(INNER_X + 34, y + 14, Z_TEXT,
                      ui->lang == L_ZH ? f->desc_zh : f->desc_en,
                      c->dim, INNER_W - 44);
        } else {
            renderer_draw_text_px_z(INNER_X + 8, y + 2, Z_TEXT_HI, "[+]",
                                    c->dim);
            renderer_draw_text_px_z(INNER_X + 34, y + 2, Z_TEXT_HI,
                                    ui->lang == L_ZH ? "手动追加参数…"
                                                     : "Append args manually…",
                                    c->ink);
            tf_text_clip(INNER_X + 34, y + 14, Z_TEXT, ui->draft.args,
                      c->dim, INNER_W - 44);
        }
    }

    if (ui->args_first + 5 < n) {
        char more[32];
        snprintf(more, sizeof(more), "%s %d",
                 ui->lang == L_ZH ? "还有" : "more:",
                 n - ui->args_first - 5);
        int w = renderer_utf8_text_width_px(more);
        renderer_draw_text_px_z(SCR_W - INNER_X - w, 199, Z_TEXT, more, c->dim);
    }
}

static void draw_custom_tab(const project_ui_t *ui, const pui_col_t *c) {
    text_wrap(INNER_X, BODY_Y0 + 4, Z_TEXT,
              ui->lang == L_ZH
                ? "整行原样发送：不会自动加 cd，也不做任何改写。"
                : "Sent verbatim: no cd is prepended and nothing is rewritten.",
              c->dim, INNER_W, 2);

    int by = 74;
    chip(INNER_X, by, INNER_W, 96, c->card, c->border, 0);
    if (ui->draft.cmd[0]) {
        text_wrap(INNER_X + 8, by + 8, Z_TEXT_HI, ui->draft.cmd, c->ink,
                  INNER_W - 16, 6);
    } else {
        renderer_draw_text_px_z(INNER_X + 8, by + 8, Z_TEXT,
                                ui->lang == L_ZH ? "（还没有内容）"
                                                 : "(empty)",
                                c->dim);
    }

    int gy = 176;
    chip(INNER_X, gy, INNER_W, 28, c->card, c->border, 0);
    chip_label(INNER_X, gy, INNER_W, 28,
               ui->lang == L_ZH ? "编辑这一整行命令" : "Edit this command line",
               c->ink);
}

static void draw_edit(const project_ui_t *ui, const dssh_settings_t *st,
                      const pui_col_t *c) {
    char title[80];
    const char *name = ui->draft.name[0]
                     ? ui->draft.name
                     : (ui->lang == L_ZH ? "未命名" : "unnamed");
    snprintf(title, sizeof(title), "%s · %s",
             ui->draft_row >= 0 ? T("编辑项目", "Edit project")
                                : T("新建项目", "New project"),
             name);
    draw_header(ui, st, c, title, "");

    draw_tabs(ui, c);

    switch (ui->tab) {
    case TAB_PATH:   draw_path_tab(ui, c);   break;
    case TAB_TOOL:   draw_tool_tab(ui, c);   break;
    case TAB_ARGS:   draw_args_tab(ui, c);   break;
    default:         draw_custom_tab(ui, c); break;
    }

    fill(0, FTR_Y, SCR_W, FTR_H, Z_PANEL, c->panel);
    fill(0, FTR_Y, SCR_W, 1, Z_BORDER, c->border);
    chip(INNER_X, FTR_Y + 1, 74, 24, c->card, c->border, 0);
    chip_label(INNER_X, FTR_Y + 1, 74, 24, T("保存", "Save"), c->ink);
    chip(INNER_X + 80, FTR_Y + 1, 74, 24, c->card, c->border, 0);
    chip_label(INNER_X + 80, FTR_Y + 1, 74, 24, T("取消", "Cancel"), c->dim);
    renderer_draw_text_px_z(INNER_X + 166, FTR_Y + 7, Z_TEXT,
                            T("←→ 换段 · B 保存", "←→ section · B save"),
                            c->dim);
}

/* ── Drawing: top screen ────────────────────────────────────────────── */
static void draw_top_list(const project_ui_t *ui, const dssh_settings_t *st,
                          const pui_col_t *c) {
    int y = 6;
    if (ui->list_cursor >= st->project_count) {
        /* The 新增 card has no intro to show — say what the page is for
         * instead of leaving the top screen blank. */
        renderer_draw_text_px_scaled(TOP_PAD, y, T("新增项目", "New project"),
                                     c->ink, 2);
        y += 34;
        fill(TOP_PAD, y, TOP_INNER_W, 1, Z_RULE, c->border);
        y += 10;
        text_wrap(TOP_PAD, y, Z_TEXT,
                  ui->lang == L_ZH
                    ? "按 A 或点「新增」建立一张卡片：先选目录，再选终端 agent 工具，"
                      "然后挑几个可选参数。三项都会写进下面这张卡片的启动命令。"
                    : "Press A (or tap Add) to create a card: pick a directory, "
                      "pick a terminal agent CLI, then tick a few flags. All three "
                      "are folded into the command shown below.",
                  c->dim, TOP_INNER_W, 6);
        return;
    }

    const dssh_project_t *p = &st->projects[ui->list_cursor];

    /* Title block, 34 px tall so the rule below stays where the rest of
     * the page expects it: the tool's mark at 2×, the name at 2× beside
     * it, and the tool's display label under the name.  The label cannot
     * share the name's line any more — at 2× a long name would push it
     * off the 400 px panel — and stacking it also keeps the mark's 32 px
     * square from crowding the text. */
    {
        char nbuf[DSSH_PROJECT_NAME_MAX];
        const proj_logo_t *lg = card_logo(p);
        int lw = lg ? 2 * lg->w : 0;
        int tx = TOP_PAD + (lw ? lw + 12 : 0);
        draw_logo(TOP_PAD, y - 2, lg, 2, c->ink);
        renderer_draw_text_px_scaled(tx, y - 2,
                                     card_display_name(ui, p, nbuf,
                                                       sizeof(nbuf)),
                                     c->ink, 2);
        char tbuf[40];
        renderer_draw_text_px_z(tx, y + 22, Z_TEXT,
                                project_tool_label(p, tbuf, sizeof(tbuf),
                                                   ui->lang),
                                c->dim);
    }
    y += 34;
    fill(TOP_PAD, y, TOP_INNER_W, 1, Z_RULE, c->border);
    y += 10;

    char cmd[DSSH_PROJECT_PATH_MAX + DSSH_PROJECT_CMD_MAX + 32];
    project_command_of(p, cmd, (int)sizeof(cmd));

    renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT, T("工作目录", "Directory"), c->dim);
    y += 14;
    {
        int lines = text_wrap(TOP_PAD, y, Z_TEXT_HI, p->path[0] ? p->path : "-",
                              c->ink, TOP_INNER_W, 2);
        y += 14 * (lines > 0 ? lines : 1);
    }
    y += 8;

    renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT,
                            p->raw ? T("完整命令（自定义）", "Full command (custom)")
                                   : T("启动命令", "Command to run"), c->dim);
    y += 14;
    {
        int lines = text_wrap(TOP_PAD, y, Z_TEXT_HI, cmd[0] ? cmd : "(empty)",
                              c->ink, TOP_INNER_W, 3);
        y += 14 * (lines > 0 ? lines : 1);
    }
    y += 8;

    renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT, T("备注", "Note"), c->dim);
    y += 14;
    {
        int lines = text_wrap(TOP_PAD, y, Z_TEXT,
                              p->note[0] ? p->note
                                         : T("（未填写）", "(none)"),
                              p->note[0] ? c->dim : dim_to(c->ink, 0x66),
                              TOP_INNER_W, 2);
        y += 14 * (lines > 0 ? lines : 1);
    }
    y += 10;

    fill(TOP_PAD, y, TOP_INNER_W, 1, Z_RULE, c->border);
    y += 8;

    char ago[48], stats[160];
    fmt_ago(p->last_used, ago, (int)sizeof(ago), ui->lang);
    snprintf(stats, sizeof(stats),
             ui->lang == L_ZH ? "使用 %d 次 · 最近 %s" : "used %d times · %s",
             p->uses, ago);
    renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT, stats, c->dim);
    y += 16;

    renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT,
                            ui->entry == PUI_ENTRY_SETTINGS
                              ? T("A 编辑 · Y 删除 · B 返回设置",
                                  "A edit · Y delete · B back to settings")
                              : T("A 在此窗口运行 · X 编辑 · B 跳过",
                                  "A run in this window · X edit · B skip"),
                            c->dim);
}

static void draw_top_edit(const project_ui_t *ui, const dssh_settings_t *st,
                          const pui_col_t *c) {
    (void)st;
    int y = 6;
    const char *title = ui->field != FIELD_NONE
                      ? T("输入中", "Typing")
                      : T("编辑项目", "Edit project");
    renderer_draw_text_px_scaled(TOP_PAD, y, title, c->ink, 2);
    /* The mark of whatever the draft currently runs, in the corner the
     * title block leaves free.  This is what says which tool's flags the
     * 参数 tab is listing, and it changes the moment a chip is picked. */
    {
        const proj_logo_t *lg =
            proj_tool_logo(ui->draft.raw ? "" : ui->draft.tool);
        int lw = lg ? 2 * lg->w : 0;
        if (lw) draw_logo(TOP_W - TOP_PAD - lw, y - 2, lg, 2, c->ink);
    }
    y += 34;
    fill(TOP_PAD, y, TOP_INNER_W, 1, Z_RULE, c->border);
    y += 10;

    if (ui->field != FIELD_NONE) {
        static const char *const fz[7] = {
            "", "名称", "工作目录", "备注", "工具命令", "附加参数", "完整命令"
        };
        static const char *const fe[7] = {
            "", "Name", "Directory", "Note", "Tool command", "Extra args",
            "Full command"
        };
        int fi = (int)ui->field;
        renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT,
                                ui->lang == L_ZH ? fz[fi] : fe[fi], c->dim);
        y += 14;
        const char *buf = field_buf_c(ui);
        int lines = text_wrap(TOP_PAD, y, Z_TEXT_HI,
                              (buf && buf[0]) ? buf : T("（空）", "(empty)"),
                              c->ink, TOP_INNER_W, 4);        y += 14 * (lines > 0 ? lines : 1);
        y += 10;
        fill(TOP_PAD, y, TOP_INNER_W, 1, Z_RULE, c->border);
        y += 8;
        text_wrap(TOP_PAD, y, Z_TEXT,
                  T("用下屏软键盘输入。⌫ 删一个字，↑ 或 A/B 结束输入。",
                    "Type on the bottom-screen keyboard. "
                    "⌫ deletes a character; ↑ or A/B ends input."),
                  c->dim, TOP_INNER_W, 3);
        y += 42;

        char cmd[DSSH_PROJECT_PATH_MAX + DSSH_PROJECT_CMD_MAX + 32];
        project_command_of(&ui->draft, cmd, (int)sizeof(cmd));
        renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT, T("最终命令", "Resulting command"),
                                c->dim);
        y += 14;
        text_wrap(TOP_PAD, y, Z_TEXT_HI, cmd[0] ? cmd : "(empty)", c->ink,
                  TOP_INNER_W, 3);
        return;
    }

    renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT,
                            k_tab_label[ui->lang == L_ZH ? 0 : 1][ui->tab],
                            c->dim);
    y += 16;
    {
        int lines = text_wrap(TOP_PAD, y, Z_TEXT,
                              k_tab_help[ui->lang == L_ZH ? 0 : 1][ui->tab],
                              c->ink, TOP_INNER_W, 4);
        y += 14 * (lines > 0 ? lines : 1);
    }
    y += 10;
    fill(TOP_PAD, y, TOP_INNER_W, 1, Z_RULE, c->border);
    y += 8;

    char cmd[DSSH_PROJECT_PATH_MAX + DSSH_PROJECT_CMD_MAX + 32];
    project_command_of(&ui->draft, cmd, (int)sizeof(cmd));
    renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT, T("最终命令", "Resulting command"),
                            c->dim);
    y += 14;
    {
        int lines = text_wrap(TOP_PAD, y, Z_TEXT_HI, cmd[0] ? cmd : "(empty)",
                              c->ink, TOP_INNER_W, 3);
        y += 14 * (lines > 0 ? lines : 1);
    }
    y += 8;

    const proj_tool_t *t = draft_tool(ui);
    const char *note = NULL;
    if (t) note = ui->lang == L_ZH ? t->note_zh : t->note_en;
    if (note) {
        renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT, T("注意", "Note"), c->warn);
        y += 14;
        text_wrap(TOP_PAD, y, Z_TEXT, note, c->warn, TOP_INNER_W, 3);
        y += 14 * 3;
    }
    if (ui->tab == TAB_CUSTOM || ui->draft.raw) {
        y = y > 200 ? 200 : y;
        renderer_draw_text_px_z(TOP_PAD, y, Z_TEXT,
                                T("自定义模式：不会自动加 cd",
                                  "Custom mode: no cd is prepended"),
                                c->warn);
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */
project_ui_t *project_ui_create(void) {
    project_ui_t *ui = (project_ui_t *)calloc(1, sizeof(project_ui_t));
    if (!ui) return NULL;
    ui->lang = L_ZH;
    return ui;
}

void project_ui_free(project_ui_t *ui) {
    if (!ui) return;
    if (ui->aux) ssh_aux_close(ui->aux);
    free(ui);
}

int project_ui_open(project_ui_t *ui, pui_entry_t entry, int initial_index,
                    const char *window_cwd) {
    if (!ui) return 0;
    ui->open         = 1;
    ui->entry        = entry;
    ui->in_editor    = 0;
    ui->draft_row    = -1;
    ui->field        = FIELD_NONE;
    ui->kbd_armed    = 1;
    ui->delete_armed = -1;
    ui->tab          = TAB_PATH;
    ui->path_cursor  = 0;
    ui->args_cursor  = 0;
    ui->args_first   = 0;
    copy_str(ui->window_cwd, sizeof(ui->window_cwd),
             window_cwd ? window_cwd : "");
    ui->list_cursor = (initial_index >= 0) ? initial_index : 0;
    /* A probe left over from a previous session answers a question about a
     * different connection. */
    ui->cwd_state = CWD_IDLE;
    ui->cwd[0]    = 0;
    return 1;
}

void project_ui_close(project_ui_t *ui) {
    if (!ui) return;
    ui->open      = 0;
    ui->in_editor = 0;
    ui->draft_row = -1;
    ui->field     = FIELD_NONE;
    /* A probe still in flight would otherwise hold an aux channel open on
     * the session — and its answer would be stale by the time the page is
     * reopened. */
    if (ui->aux) {
        ssh_aux_close(ui->aux);
        ui->aux = NULL;
    }
    ui->cwd_state = CWD_IDLE;
}

int project_ui_visible(const project_ui_t *ui) {
    return ui && ui->open;
}

int project_ui_wants_keyboard(const project_ui_t *ui) {
    return ui && ui->open && ui->field != FIELD_NONE;
}

int project_ui_keyboard_armed(const project_ui_t *ui) {
    return ui && ui->open && ui->field != FIELD_NONE && ui->kbd_armed;
}

int project_ui_dirty(const project_ui_t *ui) {
    return ui && ui->dirty;
}

void project_ui_clear_dirty(project_ui_t *ui) {
    if (ui) ui->dirty = 0;
}

void project_ui_draw_top(const project_ui_t *ui, const dssh_settings_t *st,
                         renderer_t *r, const kb_pal_t *pal) {
    if (!ui || !ui->open || !st) return;
    (void)r;
    /* softkb_palette() can never return NULL (kb_pal_for() clamps), but a
     * NULL here must not be a crash — same fallback as draw_bot below. */
    if (!pal) pal = kb_pal_for(THEME_DARK);
    /* The top screen takes the same palette as the bottom one, and
     * render_frame() has cleared the target to its background, so this only
     * paints the page on top of it.  See top_cols() for why the terminal's
     * ink is no longer the source. */
    pui_col_t c = top_cols(pal);
    if (ui->in_editor) draw_top_edit(ui, st, &c);
    else               draw_top_list(ui, st, &c);
}

void project_ui_draw_bot(const project_ui_t *ui, const dssh_settings_t *st,
                         renderer_t *r, const kb_pal_t *pal) {
    if (!ui || !ui->open || !st) return;
    (void)r;
    /* softkb_palette() can never return NULL (kb_pal_for() clamps), but a
     * NULL here must not be a crash — same fallback as ai_modal.c. */
    if (!pal) pal = kb_pal_for(THEME_DARK);
    pui_col_t c = bot_cols(pal);
    if (ui->in_editor) draw_edit(ui, st, &c);
    else               draw_list(ui, st, &c);
}
