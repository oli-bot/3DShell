/* Host-side tests for the project page's state machine.
 *
 * source/project_ui.c is the one part of the project-card feature that
 * cannot be checked by reading the preset table or the row parser: it
 * decides which tab a card opens on, whether switching tabs re-derives
 * the "the hand-written string is what runs" flag, whether a delete
 * needs its second press, and what the page does when the `pwd` probe
 * comes back empty.  All of those are invisible until a card runs the
 * wrong command on a real console.
 *
 * The drawing is stubbed out into a capture log plus a colour
 * framebuffer.  The log asserts on *what the screen says* — the only way
 * to catch the tab/raw mismatch, since the page is told which tab is
 * active and the two can disagree.  The framebuffer asserts on *where
 * each pixel was painted*, which is the only way to check the tool
 * marks: a mark drawn one grid row low, or at the wrong scale, renders
 * perfectly well and overlaps the text underneath it.
 *
 * Run via: ./tools/test_project_ui.sh
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/project_ui.h"
#include "../source/project_presets.h"
#include "stub/pui_host_stubs.h"
#include "test_wcag.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failures++; \
    } else { \
        fprintf(stderr, "ok   %s\n", msg); \
    } \
} while (0)

/* Same, but prints both sides — the difference between "the field is
 * empty" and "the field has the wrong bytes in it" is the whole
 * diagnosis, and a bare FAIL hides it. */
#define CHECK_STR(got, want, msg) do { \
    if (strcmp((got), (want)) != 0) { \
        fprintf(stderr, "FAIL: %s\n      got \"%s\", want \"%s\"\n", \
                msg, (got), (want)); \
        failures++; \
    } else { \
        fprintf(stderr, "ok   %s\n", msg); \
    } \
} while (0)

/* ── Fixture ────────────────────────────────────────────────────────── */

static project_ui_t   *ui;
static dssh_settings_t st;
static kb_pal_t        pal;
/* Both screens take the live keyboard palette now, so there is no terminal
 * stand-in here any more.  There used to be one: project_ui_draw_top() took
 * a terminal_t and read its ink, and the stub answered terminal_get_fg/bg
 * with constants.  The top screen's ink is pal.label, which is what the
 * mark assertions below check against. */

/* project_ui_probe_cwd() short-circuits on a NULL ssh, so the probe
 * cases need a non-NULL handle.  The stub ignores what it is given. */
static char ssh_dummy[8];
#define SSH ((ssh_client_t *)ssh_dummy)

static void set_proj(int i, const char *name, const char *path,
                     const char *tool, const char *args,
                     const char *cmd, int raw) {
    dssh_project_t *p = &st.projects[i];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    snprintf(p->path, sizeof(p->path), "%s", path);
    snprintf(p->tool, sizeof(p->tool), "%s", tool);
    snprintf(p->args, sizeof(p->args), "%s", args);
    snprintf(p->cmd,  sizeof(p->cmd),  "%s", cmd);
    p->raw = raw;
}

static void reset(int nprojects) {
    memset(&st, 0, sizeof(st));
    st.ui_language     = 1;            /* English, so this file stays ASCII */
    st.project_count   = nprojects;
    st.selected_project = 0;

    if (ui) project_ui_free(ui);
    ui = project_ui_create();

    hstub_reset();
    hstub_connected  = 1;
    hstub_pwd        = "/home/me/work/dssh\n";
    hstub_exec_fail  = 0;
    hstub_probe_count = 0;
    hstub_last_cmd[0] = 0;
    hstub_read_chunk = 4096;

    const kb_pal_t *p = kb_pal_for(THEME_DARK);
    if (p) pal = *p;
    else   memset(&pal, 0, sizeof(pal));
}

/* ── Input helpers ──────────────────────────────────────────────────── */

/* A one-frame key-down edge.  The page reads `down`, not `held`, for
 * everything except the (unused) chord bits. */
static pui_act_t key(uint32_t k, int *out) {
    return project_ui_tick(ui, &st, SSH, k, 0, NULL, -1, -1, 0, 0, out);
}

/* An idle frame: lets the aux channel be pumped without new input. */
static pui_act_t idle(void) {
    return project_ui_tick(ui, &st, SSH, 0, 0, NULL, -1, -1, 0, 0, NULL);
}

/* A tap on the bottom screen (down-edge, then released). */
static pui_act_t tap(int x, int y, int *out) {
    pui_act_t a = project_ui_tick(ui, &st, SSH, 0, 0, NULL, x, y, 1, 1, out);
    project_ui_tick(ui, &st, SSH, 0, 0, NULL, -1, -1, 0, 0, NULL);
    return a;
}

static void draw_top(void) {
    hstub_reset();
    project_ui_draw_top(ui, &st, NULL, &pal);
}

static void draw_bot(void) {
    hstub_reset();
    project_ui_draw_bot(ui, &st, NULL, &pal);
}

/* hstub_saw() matches substrings, and a card's second line is its whole
 * path — which contains its own basename.  So "the row shows the
 * directory's name" has to be matched on a whole log line, or it would
 * pass on the path alone with the name never drawn at all.  One append
 * per line, each terminated by '\n'. */
static int hstub_saw_line(const char *line) {
    char needle[512];
    if (!line || !*line) return 0;
    snprintf(needle, sizeof(needle), "\n%s\n", line);
    if (strstr(hstub_log, needle)) return 1;
    snprintf(needle, sizeof(needle), "%s\n", line);
    return strncmp(hstub_log, needle, strlen(needle)) == 0;
}

/* Which tab is active, read off the bottom screen.
 *
 * Not the tab strip — it draws all four labels whatever the tab is, and
 * not the top screen's help paragraph, which is word-wrapped and so can
 * split the phrase being looked for across two lines.  These are the
 * labels each tab draws in its *body*, and nothing else draws them:
 *
 *   Dir     "Directory"              the path field's label
 *   Tool    "opencode"               a tool name in the grid
 *   Args    "claude flags"           the "<tool> flags" heading
 *   Custom  "Edit this command line" the button under the command box
 *
 * None of the preset descriptions contain any of them (checked), so a
 * hit means the tab really is up. */
#define SAW_DIR    "Directory"
#define SAW_TOOL   "opencode"
#define SAW_ARGS   "claude flags"
#define SAW_CUSTOM "Edit this command line"

/* The top screen's only unwrapped, tab-dependent string. */
#define SAW_RAWMODE "prepended"

/* Geometry, mirrored from the comment block at the top of project_ui.c.
 * If the layout moves, these tests should fail loudly rather than
 * silently tap the wrong control. */
#define CARD_CY(i)  (20 + (i) * 38 + 18)
#define BTN_RUN_X   43
#define BTN_EDIT_X  121
#define BTN_ADD_X   199
#define BTN_DEL_X   277
#define BAR_Y       220
#define TAB_CY      31
#define TAB_CX(i)   (7 + (i) * 77 + 37)

/* Where the tool marks sit, mirrored from the CARD_LOGO_* / TOOL_LOGO_*
 * constants in project_ui.c.  These are what the framebuffer checks
 * sample, so a moved mark fails here instead of quietly sampling the
 * gap between two chips. */
#define TOOL_CHIP_X(i) (6 + ((i) % 2) * (151 + 6))
#define TOOL_CHIP_Y(i) (46 + ((i) / 2) * 36)
#define TOOL_LOGO_X(i) (TOOL_CHIP_X(i) + 5)
#define TOOL_LOGO_Y(i) (TOOL_CHIP_Y(i) + 2)
#define CARD_LOGO_X  15                       /* INNER_X + 9 */
#define CARD_LOGO_Y(i) (20 + (i) * 38 + 10)   /* CARD_Y0 + i*38 + 10 */
#define TOP_LOGO_X   10                       /* TOP_PAD */
#define TOP_LOGO_Y   4                        /* the title block starts at 6 */
#define TOP_LOGO_SCALE 2
#define TOP_W        400
#define TOP_PAD      10

/* ── Mark verification ──────────────────────────────────────────────── */

/* Assert that the mark for `tool` is really painted at (x,y), one grid
 * cell per `scale` px, in `ink` ('+' cells at half alpha) over `surface`.
 *
 * A whole-mark walk rather than a spot check: every lit cell has to land
 * in the right pixel and every empty cell has to still show the surface
 * underneath.  That is what catches a mark drawn one row low, at the
 * wrong scale, or with its tones swapped — all of which the text log
 * cannot see and all of which look like a plausible icon.
 *
 * Returns the number of lit cells found, so a caller can tell "the mark
 * verified" from "nothing was drawn and the walk was vacuous".
 *
 * The sample is the centre of each cell, so the check does not depend on
 * which corner the drawer's run starts from. */
static int check_mark_drawn(const char *tool, int x, int y, int scale,
                            uint32_t ink, uint32_t surface, const char *msg) {
    const proj_logo_t *lg = proj_tool_logo(tool);
    char buf[128];
    int lit = 0, bad = 0;
    uint32_t soft = (ink & 0xFFFFFF00u) | 0x80u;

    if (!lg) {
        fprintf(stderr, "FAIL: %s (no mark for \"%s\")\n", msg, tool);
        failures++;
        return 0;
    }

    for (int r = 0; r < lg->h; r++) {
        for (int c = 0; c < lg->w; c++) {
            char ch = lg->rows[r][c];
            uint32_t got = hstub_fb_at(x + c * scale + scale / 2,
                                       y + r * scale + scale / 2);
            if (ch == '#')      { lit++; if (got != ink)    bad++; }
            else if (ch == '+') { lit++; if (got != soft)   bad++; }
            else if (ch != '.') { bad++; }
            else                { if (got != surface) bad++; }
        }
    }
    snprintf(buf, sizeof(buf), "%s (%d lit cells)", msg, lit);
    CHECK(bad == 0 && lit > 0, buf);
    return lit;
}

/* ── Cases ──────────────────────────────────────────────────────────── */

static void t_open_close(void) {
    reset(2);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    set_proj(1, "b", "/srv/b", "codex", "", "", 0);

    CHECK(project_ui_visible(ui) == 0, "starts closed");
    CHECK(project_ui_open(ui, PUI_ENTRY_RUN, 0, "/srv/a") == 1, "open returns 1");
    CHECK(project_ui_visible(ui) == 1, "open makes it visible");

    key(KEY_B, NULL);
    CHECK(project_ui_visible(ui) == 0, "B closes from the list");

    project_ui_open(ui, PUI_ENTRY_RUN, 0, "");
    key(KEY_START, NULL);
    CHECK(project_ui_visible(ui) == 0, "START closes from the list");
}

static void t_navigate_and_run(void) {
    int out = -1;
    reset(3);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    set_proj(1, "b", "/srv/b", "codex",  "", "", 0);
    set_proj(2, "c", "/srv/c", "agy",    "", "", 0);
    project_ui_open(ui, PUI_ENTRY_RUN, 0, "");

    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);
    pui_act_t a = key(KEY_A, &out);
    CHECK(a == PUI_ACT_RUN && out == 2, "A runs the third card");

    /* Down past the last card lands on the trailing "+ New project" row,
     * which must not run anything. */
    out = -1;
    key(KEY_DDOWN, NULL);
    a = key(KEY_A, &out);
    CHECK(a == PUI_ACT_NONE && out == -1,
          "A on the add row does not run");
    CHECK(project_ui_visible(ui) == 1, "add row opened the editor instead");
    CHECK(project_ui_wants_keyboard(ui) == 0, "editor is not a text field");

    key(KEY_B, NULL);                 /* save + back to the list */
    CHECK(st.project_count == 4, "saving a blank new card appends one");

    /* Saving selects the card it just made, so the cursor sits on the new
     * card with the add row one press below it.  Two more downs carry the
     * wrap all the way back to the first card — pressed in this order
     * because A on the add row opens the editor, which would swallow the
     * presses that follow. */
    out = -1;
    key(KEY_DDOWN, NULL);             /* new card → the add row */
    key(KEY_DDOWN, NULL);             /* add row → wraps to card 0 */
    a = key(KEY_A, &out);
    CHECK(a == PUI_ACT_RUN && out == 0, "down from the add row wraps to card 0");

    /* And up from the first card wraps back to the add row, where A opens
     * the editor instead of running anything. */
    out = -1;
    key(KEY_DUP, NULL);
    a = key(KEY_A, &out);
    CHECK(a == PUI_ACT_NONE && out == -1,
          "up from card 0 wraps to the add row, which does not run");
    CHECK(project_ui_visible(ui) == 1, "the add row opened the editor");
}

static void t_settings_entry_never_runs(void) {
    int out = -1;
    reset(2);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    set_proj(1, "b", "/srv/b", "codex",  "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");

    pui_act_t a = key(KEY_A, &out);
    CHECK(a == PUI_ACT_NONE && out == -1, "A never runs from the settings entry");

    /* X still edits, so the settings page can manage the cards. */
    a = key(KEY_X, NULL);
    CHECK(a == PUI_ACT_NONE && project_ui_visible(ui) == 1, "X opens the editor");
}

static void t_delete_needs_two_presses(void) {
    reset(3);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    set_proj(1, "b", "/srv/b", "codex",  "", "", 0);
    set_proj(2, "c", "/srv/c", "agy",    "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 1, "");

    key(KEY_Y, NULL);
    CHECK(st.project_count == 3, "first Y only arms the delete");
    CHECK(project_ui_dirty(ui) == 0, "arming does not mark the file dirty");

    key(KEY_Y, NULL);
    CHECK(st.project_count == 2, "second Y deletes");
    CHECK(project_ui_dirty(ui) == 1, "deleting marks the settings dirty");

    project_ui_clear_dirty(ui);
    CHECK(project_ui_dirty(ui) == 0, "clear_dirty clears it");
}

static void t_delete_via_touch(void) {
    reset(2);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    set_proj(1, "b", "/srv/b", "codex",  "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");

    tap(BTN_DEL_X, BAR_Y, NULL);
    CHECK(st.project_count == 2, "tap on Delete arms");
    tap(BTN_DEL_X, BAR_Y, NULL);
    CHECK(st.project_count == 1, "second tap deletes");
}

static void t_touch_run_and_edit(void) {
    int out = -1;
    reset(3);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    set_proj(1, "b", "/srv/b", "codex",  "", "", 0);
    set_proj(2, "c", "/srv/c", "agy",    "", "", 0);
    project_ui_open(ui, PUI_ENTRY_RUN, 0, "");

    /* One tap selects, a second tap on the same card runs it. */
    pui_act_t a = tap(BTN_RUN_X, CARD_CY(2), &out);
    CHECK(a == PUI_ACT_NONE, "first tap on a card only selects it");

    a = tap(BTN_RUN_X, CARD_CY(2), &out);
    CHECK(a == PUI_ACT_RUN && out == 2, "second tap runs the selected card");

    /* 编辑 button. */
    out = -1;
    tap(BTN_EDIT_X, BAR_Y, NULL);
    CHECK(project_ui_visible(ui) == 1, "Edit button stays on the page");
    CHECK(project_ui_wants_keyboard(ui) == 0, "Edit opens the form, not a field");
}

/* The regression that motivated this whole file: a card saved as a
 * hand-written command must open on the 自定义 tab, and a card saved as
 * three parts must open on the 目录 tab.  The page carries both a `tab`
 * and a `raw` flag and they have to agree, or the form shows one thing
 * and the card runs another. */
static void t_raw_card_opens_on_custom_tab(void) {
    reset(1);
    set_proj(0, "raw", "/srv/r", "claude", "--continue",
             "source .venv/bin/activate && claude", 1);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    draw_top();
    CHECK(hstub_saw(SAW_RAWMODE), "raw card shows the no-cd warning");
    CHECK(hstub_saw("source .venv/bin/activate && claude"),
          "raw card's top screen previews the hand-written command");

    draw_bot();
    CHECK(hstub_saw(SAW_CUSTOM), "raw card opens on the Custom tab");
    CHECK(!hstub_saw(SAW_DIR), "raw card is not showing the Dir tab's body");

    /* Save untouched: the card must come back byte-identical. */
    key(KEY_B, NULL);
    CHECK(st.projects[0].raw == 1, "saving an untouched raw card keeps raw=1");
    char cmd[256];
    dssh_project_command(&st.projects[0], cmd, (int)sizeof(cmd));
    CHECK(strcmp(cmd, "source .venv/bin/activate && claude") == 0,
          "saving an untouched raw card keeps its command");
}

static void t_part_card_opens_on_dir_tab(void) {
    reset(1);
    set_proj(0, "part", "/srv/p", "claude", "--continue", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    draw_bot();
    CHECK(hstub_saw(SAW_DIR), "part card opens on the Directory tab");
    CHECK(!hstub_saw(SAW_CUSTOM), "part card is not on the Custom tab");
    draw_top();
    CHECK(!hstub_saw(SAW_RAWMODE), "part card does not claim custom mode");

    key(KEY_B, NULL);
    CHECK(st.projects[0].raw == 0, "saving an untouched part card keeps raw=0");
}

/* Switching away from 自定义 must re-derive the command from the three
 * parts, and switching to it must keep the hand-written string. */
static void t_tab_switch_rederives_raw(void) {
    reset(1);
    set_proj(0, "both", "/srv/b", "claude", "--continue",
             "source .venv/bin/activate && claude", 1);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    /* D-pad left three times: Custom → Args → Tool → Dir. */
    key(KEY_DLEFT, NULL);
    draw_bot();
    CHECK(hstub_saw(SAW_ARGS), "D-pad left once reaches the Args tab");

    key(KEY_DLEFT, NULL);
    draw_bot();
    CHECK(hstub_saw(SAW_TOOL), "D-pad left twice reaches the Tool tab");

    key(KEY_DLEFT, NULL);
    draw_bot();
    CHECK(hstub_saw(SAW_DIR), "D-pad left three times reaches the Dir tab");
    draw_top();
    CHECK(!hstub_saw(SAW_RAWMODE), "Dir tab is no longer custom mode");

    key(KEY_B, NULL);
    CHECK(st.projects[0].raw == 0, "saving from Directory drops raw");
    char cmd[256];
    dssh_project_command(&st.projects[0], cmd, (int)sizeof(cmd));
    CHECK(strcmp(cmd, "cd /srv/b && claude --continue") == 0,
          "Directory tab composes the command from the three parts");
}

/* The same thing via L/R, which is the other way to change tab. */
static void t_tab_switch_via_shoulder(void) {
    reset(1);
    set_proj(0, "part", "/srv/p", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    key(KEY_R, NULL);
    draw_bot();
    CHECK(hstub_saw(SAW_TOOL), "R steps Dir → Tool");

    key(KEY_L, NULL);
    draw_bot();
    CHECK(hstub_saw(SAW_DIR), "L steps back to Dir");
}

/* Tapping the tab strip must go through the same derivation as the
 * buttons do — this was a real bug: the tap path assigned the tab
 * directly and left `raw` pointing at the other mode. */
static void t_tab_switch_via_touch(void) {
    reset(1);
    set_proj(0, "raw", "/srv/r", "claude", "--continue",
             "source .venv/bin/activate && claude", 1);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    draw_bot();
    CHECK(hstub_saw(SAW_CUSTOM), "starts on Custom");

    tap(TAB_CX(0), TAB_CY, NULL);      /* the Dir tab */
    draw_bot();
    CHECK(hstub_saw(SAW_DIR), "tapping Dir switches the tab");
    draw_top();
    CHECK(!hstub_saw(SAW_RAWMODE), "tapping Dir leaves custom mode");

    key(KEY_B, NULL);
    CHECK(st.projects[0].raw == 0, "tapping a part tab re-derives raw");

    char cmd[256];
    dssh_project_command(&st.projects[0], cmd, (int)sizeof(cmd));
    CHECK(strcmp(cmd, "cd /srv/r && claude --continue") == 0,
          "the composed command replaces the hand-written one");
}

/* Tapping the Custom tab must set raw, or the card would keep running
 * the composed command while the form claimed otherwise. */
static void t_touch_custom_tab_sets_raw(void) {
    reset(1);
    set_proj(0, "part", "/srv/p", "claude", "--continue", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    tap(TAB_CX(3), TAB_CY, NULL);      /* the Custom tab */
    draw_bot();
    CHECK(hstub_saw(SAW_CUSTOM), "tapping Custom switches the tab");
    draw_top();
    CHECK(hstub_saw(SAW_RAWMODE), "tapping Custom enters custom mode");

    key(KEY_B, NULL);
    CHECK(st.projects[0].raw == 1, "tapping Custom sets raw");
}

static void t_cancel_discards(void) {
    reset(1);
    set_proj(0, "keep", "/srv/k", "claude", "--continue", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    /* Take the current directory, which overwrites the path, then cancel. */
    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);              /* cursor 3 = the probe button */
    key(KEY_A, NULL);
    for (int i = 0; i < 8; i++) idle();

    tap(123, BAR_Y, NULL);             /* the 取消 button */
    CHECK(strcmp(st.projects[0].path, "/srv/k") == 0,
          "cancel leaves the stored card alone");
    CHECK(project_ui_dirty(ui) == 0, "cancel does not mark the file dirty");
}

static void t_cwd_probe(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");

    /* Not connected: the probe must fail rather than hang. */
    hstub_connected = 0;
    project_ui_probe_cwd(ui, SSH);
    CHECK(project_ui_cwd_state(ui) == 3, "probe on a dead session fails");

    /* Connected but the exec fails. */
    hstub_connected = 1;
    hstub_exec_fail = 1;
    project_ui_probe_cwd(ui, SSH);
    CHECK(project_ui_cwd_state(ui) == 3, "probe with a failed exec fails");

    /* A real answer. */
    hstub_exec_fail = 0;
    hstub_probe_count = 0;
    project_ui_probe_cwd(ui, SSH);
    CHECK(hstub_probe_count == 1, "probe ran exactly one command");
    CHECK(strcmp(hstub_last_cmd, "pwd") == 0, "probe runs pwd");

    for (int i = 0; i < 8 && project_ui_cwd_state(ui) == 1; i++) idle();
    CHECK(project_ui_cwd_state(ui) == 2, "probe with an answer succeeds");
    CHECK(strcmp(project_ui_cwd(ui), "/home/me/work/dssh") == 0,
          "probe trims the trailing newline");

    /* A non-absolute answer is not a directory. */
    hstub_pwd = "bash: pwd: command not found\n";
    project_ui_probe_cwd(ui, SSH);
    for (int i = 0; i < 8 && project_ui_cwd_state(ui) == 1; i++) idle();
    CHECK(project_ui_cwd_state(ui) == 3, "a non-path answer is a failure");

    /* An empty answer is a failure too, not a silent success. */
    hstub_pwd = "";
    project_ui_probe_cwd(ui, SSH);
    for (int i = 0; i < 8 && project_ui_cwd_state(ui) == 1; i++) idle();
    CHECK(project_ui_cwd_state(ui) == 3, "an empty answer is a failure");
}

/* The probe is a round trip; the answer arriving one byte per frame must
 * not be mistaken for an error, and must not be re-read as EOF. */
static void t_cwd_probe_dribbles(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");

    hstub_pwd = "/very/long/path\n";
    hstub_read_chunk = 1;
    project_ui_probe_cwd(ui, SSH);
    CHECK(project_ui_cwd_state(ui) == 1, "starts probing");

    for (int i = 0; i < 64 && project_ui_cwd_state(ui) == 1; i++) idle();
    CHECK(project_ui_cwd_state(ui) == 2, "a byte-per-frame answer still lands");
    CHECK(strcmp(project_ui_cwd(ui), "/very/long/path") == 0,
          "the dribbled answer is assembled intact");
}

/* The window's own cd wins over the probe: a second SSH channel starts
 * in the login directory, so `pwd` there cannot know where the focused
 * shell has wandered to. */
static void t_window_cwd_beats_probe(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    hstub_pwd = "/home/me\n";
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "/srv/wandered");
    key(KEY_X, NULL);

    for (int i = 0; i < 8 && project_ui_cwd_state(ui) == 1; i++) idle();

    /* Row 3 of the Dir tab is the "use the current directory" button. */
    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);
    key(KEY_A, NULL);

    draw_bot();
    CHECK(hstub_saw("/srv/wandered"),
          "the window's own cd is what the button offers");
    CHECK(!hstub_saw("/home/me\n"),
          "the login-dir probe answer is not what it offers");

    key(KEY_B, NULL);                  /* save */
    CHECK(strcmp(st.projects[0].path, "/srv/wandered") == 0,
          "the window's cd is what got stored, not the probe's answer");
}

/* ── Byte caps against codepoint boundaries ───────────────────────────
 *
 * Everything the "use the current directory" button touches is capped in
 * bytes: cwd[160] → draft.path[128] → basename → draft.name[24].  A
 * Chinese directory costs three bytes a glyph, so a byte cap lands
 * inside a glyph unless the prefix happens to be a multiple of three.
 * These two cases are the only ones in this file carrying non-ASCII
 * literals, because the non-ASCII is what is under test. */

#define CJK_GLYPH "\xe6\xb1\x89"   /* 汉, three bytes */

static int utf8_valid(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        size_t seq;
        if      ((*p & 0xE0) == 0xC0) seq = 2;
        else if ((*p & 0xF0) == 0xE0) seq = 3;
        else if ((*p & 0xF8) == 0xF0) seq = 4;
        else                          return 0;
        for (size_t i = 1; i < seq; i++)
            if ((p[i] & 0xC0) != 0x80) return 0;
        p += seq;
    }
    return 1;
}

/* "/a/" + 60 glyphs = 183 bytes: longer than every buffer it passes
 * through, and the 3-byte prefix is what makes the 127-byte field cap
 * fall one byte *into* a glyph rather than on its edge. */
static void long_cjk_path(char *out, size_t out_sz) {
    size_t o = 0;
    const char *pre = "/a/";
    while (*pre && o + 1 < out_sz) out[o++] = *pre++;
    for (int i = 0; i < 60; i++) {
        const char *g = CJK_GLYPH;
        while (*g && o + 1 < out_sz) out[o++] = *g++;
    }
    out[o] = 0;
}

static void t_cjk_path_cap_does_not_split(void) {
    static char pwd[256];
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);

    /* No trailing newline on purpose: the truncation is what is under
     * test here, not the newline trim.  No window_cwd either, so the
     * probe is the only source. */
    long_cjk_path(pwd, sizeof(pwd));
    hstub_pwd = pwd;
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);
    for (int i = 0; i < 8 && project_ui_cwd_state(ui) == 1; i++) idle();
    CHECK(project_ui_cwd_state(ui) == 2, "a long CJK login dir still probes");

    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);
    key(KEY_A, NULL);
    key(KEY_B, NULL);                  /* save */

    /* The cap counts bytes and a CJK glyph costs three, so where the cut
     * falls is a function of DSSH_PROJECT_PATH_MAX — derive it rather
     * than writing 126, which is precisely the number that went stale
     * when the cap moved to match the probe buffer.  (At the current cap
     * the cut lands on a glyph boundary, so this fixture now asserts the
     * "valid UTF-8, whole prefix" half; the backing-off half is pinned
     * by test_theme_ini's 16-byte slot and test_config's fields, where
     * the caps do not divide evenly.) */
    size_t room = (size_t)DSSH_PROJECT_PATH_MAX - 1;
    size_t want = 3 + ((room - 3) / 3) * 3;   /* "/a/" + whole glyphs */
    CHECK(utf8_valid(st.projects[0].path),
          "a CJK path cut by the byte cap stays valid UTF-8");
    CHECK(strlen(st.projects[0].path) == want,
          "the cut backs off to the last whole glyph, not to the byte cap");
    CHECK(strncmp(st.projects[0].path, pwd, want) == 0,
          "what is kept is a prefix of the real directory");
}

/* Same class one funnel later: an unnamed card is named after its
 * directory, and the name field is 24 bytes — eight glyphs, so a nine-
 * glyph directory name is cut mid-glyph. */
static void t_cjk_basename_does_not_split(void) {
    static char pwd[256];
    reset(1);
    set_proj(0, "", "/srv/a", "claude", "", "", 0);   /* unnamed on purpose */

    long_cjk_path(pwd, sizeof(pwd));
    hstub_pwd = pwd;
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);
    for (int i = 0; i < 8 && project_ui_cwd_state(ui) == 1; i++) idle();

    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);
    key(KEY_DDOWN, NULL);
    key(KEY_A, NULL);
    key(KEY_B, NULL);

    CHECK(utf8_valid(st.projects[0].name),
          "a card named after a CJK directory stays valid UTF-8");
    CHECK(strlen(st.projects[0].name) == 21,
          "the name is seven whole glyphs, not seven and a fragment");
}

/* A card with no name in it.
 *
 * Reachable even though commit_draft() always names a card it saves:
 * settings.ini is plain text and parse_project_row() accepts an empty
 * first field, so a hand-edited row (or one written by something else)
 * puts a nameless card in the list.  Both screens have to say something,
 * and what they say is the directory's last segment — the string
 * commit_draft() would have stored — so the row reads the same before
 * and after a save. */
static void t_nameless_card_is_legible(void) {
    reset(1);
    set_proj(0, "", "/srv/widgets", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_RUN, 0, "");
    /* project_ui_create() defaults to Chinese and only tick() carries
     * st.ui_language over, so a draw before any tick is 中文 whatever
     * the settings say. */
    idle();

    draw_bot();
    CHECK(hstub_saw_line("widgets"),
          "a nameless card's row shows its directory's name");
    CHECK(!hstub_saw("(unnamed)"), "and no placeholder while it has one");

    draw_top();
    CHECK(hstub_saw_line("widgets"),
          "the top screen names a nameless card the same way");

    /* Neither name nor directory: the placeholder is all that is left,
     * and it still has to be drawn — a row of pure background is a row
     * the user cannot tell is there. */
    set_proj(0, "", "", "claude", "", "", 0);
    draw_bot();
    CHECK(hstub_saw("(unnamed)"),
          "a card with neither name nor directory still says something");
    draw_top();
    CHECK(hstub_saw("(unnamed)"), "the top screen falls back the same way");

    /* The top screen's placeholder was hardcoded English, so a 中文 page
     * read "(unnamed)" between 工作目录 and 备注.  Switching 界面语言 has
     * to take it with it. */
    st.ui_language = 0;
    idle();                            /* tick() is what carries lang over */
    draw_top();
    CHECK(!hstub_saw("(unnamed)"),
          "the top screen's placeholder follows 界面语言");
}

static void t_keyboard_arming(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    /* Row 0 of the Dir tab is the name field. */
    CHECK(project_ui_wants_keyboard(ui) == 0, "no field yet");

    /* Entered by button: nothing is under a finger, so it is armed at
     * once and the very next frame may type. */
    key(KEY_A, NULL);
    CHECK(project_ui_wants_keyboard(ui) == 1, "A on a text row starts typing");
    CHECK(project_ui_keyboard_armed(ui) == 1,
          "a button press needs no arming delay");

    /* Bytes the soft keyboard emits come back in as `kt`.  The field is
     * pre-filled with the card's current name, so typing appends rather
     * than replaces — "a" + "x" + "y" - ⌫ = "ax". */
    project_ui_tick(ui, &st, SSH, 0, 0, "x", -1, -1, 0, 0, NULL);
    project_ui_tick(ui, &st, SSH, 0, 0, "y", -1, -1, 0, 0, NULL);
    project_ui_tick(ui, &st, SSH, 0, 0, "\x7f", -1, -1, 0, 0, NULL);  /* ⌫ */
    key(KEY_B, NULL);                  /* commit the field */
    CHECK(project_ui_wants_keyboard(ui) == 0, "B leaves the field");

    key(KEY_B, NULL);                  /* save the card */
    CHECK_STR(st.projects[0].name, "ax",
              "typing appends to the pre-filled field; backspace removes one");
}

/* The touch path is the one that needs the arming guard: softkb derives
 * its down-edge from prev_pressed, so handing it the press that opened
 * the field would fire whatever key sits under the finger. */
static void t_keyboard_arming_on_touch(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);

    /* Row 0 of the Dir tab spans y 46..76. */
    project_ui_tick(ui, &st, SSH, 0, 0, NULL, 26, 60, 1, 1, NULL);
    CHECK(project_ui_wants_keyboard(ui) == 1, "tap on a text row starts typing");
    CHECK(project_ui_keyboard_armed(ui) == 0,
          "the tap that opened the field is not handed to the keyboard");

    /* Still disarmed while the finger stays down... */
    project_ui_tick(ui, &st, SSH, 0, 0, NULL, 26, 60, 1, 0, NULL);
    CHECK(project_ui_keyboard_armed(ui) == 0, "still disarmed while held");

    /* ...and armed the moment it lifts, because prev_pressed has now
     * seen the press and the release. */
    project_ui_tick(ui, &st, SSH, 0, 0, NULL, -1, -1, 0, 0, NULL);
    CHECK(project_ui_keyboard_armed(ui) == 1, "arms once the finger lifts");
}

/* ── The ⚙ badge while the project page is typing ───────────────────
 *
 * While a text field is open the project page has rented the soft
 * keyboard: main.c hands the raw bottom-screen touch to softkb_touch()
 * and the bottom screen is painted by softkb_draw(), status row and ⚙
 * badge included.  A tap inside the badge rect therefore used to flip
 * settings_open — and the settings page it opens is drawn on NEITHER
 * screen (render_frame's pui_up branches win both), while still owning
 * the whole keypad and still writing its dirty flag to settings.ini.
 *
 * The fix is an invariant main.c holds every frame: while another page
 * has rented the keyboard, settings_open is 0.  source/softkb.c cannot
 * link on the host (it draws through citro2d), so the flag below is a
 * model of that ordering and of softkb.c's badge rect — it pins the
 * contract main.c has to keep, it does not run softkb.c.  The two
 * assertions about project_ui_wants_keyboard() and the saved field ARE
 * about real code. */

/* softkb.c:686-689 — BADGE_X = 320 - (3*6+4) - 2, BADGE_H = STATUS_H-4. */
#define BADGE_X 296
#define BADGE_Y 2
#define BADGE_W 22
#define BADGE_H 30

static int model_settings_open;

static void model_softkb_touch(int x, int y) {
    if (x >= BADGE_X && x < BADGE_X + BADGE_W &&
        y >= BADGE_Y && y < BADGE_Y + BADGE_H)
        model_settings_open = !model_settings_open;
}

/* What main.c:1661 does at the top of every frame while pui is up.
 * Everything that would draw the page or route the keypad runs after
 * it, so a badge tap can only ever raise the flag for the remainder of
 * its own frame — the frame that draws and the frame that reads keys
 * both see 0. */
static void model_frame_start(void) {
    if (project_ui_visible(ui)) model_settings_open = 0;
}

/* One frame of main.c with a finger on the badge. */
static void frame_tap_badge(int x, int y) {
    model_frame_start();
    model_softkb_touch(x, y);
    project_ui_tick(ui, &st, SSH, 0, 0, NULL, x, y, 1, 1, NULL);
}

static void t_badge_does_not_open_settings_while_typing(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);
    model_settings_open = 0;

    /* Row 0 of the Dir tab is the name field. */
    key(KEY_A, NULL);
    CHECK(project_ui_wants_keyboard(ui) == 1, "A on a text row starts typing");

    /* Prove the coordinates really do land on the badge — otherwise the
     * check below could only pass by accident. */
    model_settings_open = 0;
    model_softkb_touch(300, 10);
    CHECK(model_settings_open == 1, "self-check: (300,10) is on the badge");
    model_settings_open = 0;

    /* x 300 / y 10 is inside the badge rect, and while the keyboard is
     * borrowed it sits over the card's own title bar. */
    frame_tap_badge(300, 10);
    model_frame_start();               /* the frame that would draw it */
    CHECK(model_settings_open == 0,
          "the ⚙ badge does not open the settings page mid-field");
    CHECK(project_ui_wants_keyboard(ui) == 1,
          "that tap does not end the project page's field either");

    /* The field is still what receives input: the next keyboard byte has
     * to land in the name, not on some page nobody can see. */
    project_ui_tick(ui, &st, SSH, 0, 0, "z", -1, -1, 0, 0, NULL);
    key(KEY_B, NULL);                  /* commit the field */
    key(KEY_B, NULL);                  /* save the card */
    CHECK_STR(st.projects[0].name, "az",
              "the field kept receiving bytes after the badge tap");
}

static void t_edit_then_add_then_run(void) {
    int out = -1;
    reset(1);
    set_proj(0, "one", "/srv/one", "claude", "--continue", "", 0);
    project_ui_open(ui, PUI_ENTRY_RUN, 0, "");

    /* Add a second card through the 新增 button. */
    tap(BTN_ADD_X, BAR_Y, NULL);
    CHECK(project_ui_wants_keyboard(ui) == 0, "Add opens the form");

    /* Walk to the Tool tab and pick a different tool by touch. */
    tap(TAB_CX(1), TAB_CY, NULL);
    tap(6 + 20, 46 + 20, NULL);        /* first tool cell */
    key(KEY_B, NULL);
    CHECK(st.project_count == 2, "the new card was appended");

    /* It is the selected row now, so A runs it. */
    pui_act_t a = key(KEY_A, &out);
    CHECK(a == PUI_ACT_RUN && out == 1, "A runs the card just created");

    /* The new card has a tool but no directory, so the command must name
     * the tool and must NOT start with a bare `cd` — `cd && claude` is a
     * shell syntax error, and the composer has to know that. */
    char cmd[256];
    dssh_project_command(&st.projects[1], cmd, (int)sizeof(cmd));
    CHECK(strcmp(cmd, "claude") == 0,
          "a card with no directory composes just the tool, no bare cd");
}

/* ── Marks on screen ────────────────────────────────────────────────── */

/* The mark checker has to be able to fail, or every case below it is
 * vacuous.  Draw the tool tab once and then verify the same mark shifted
 * a pixel, doubled in scale, and replaced by another tool's — all three
 * must be rejected.  `failures` is put back afterwards so the deliberate
 * misses do not count against the run; the banner is there so the FAIL
 * lines above the result are not read as a real regression. */
static void t_mark_checker_can_fail(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);
    tap(TAB_CX(1), TAB_CY, NULL);
    draw_bot();

    int saved = failures, before;
    fprintf(stderr, "-- three deliberate failures follow --\n");

    /* Chip 0 is the ticked one, so it sits on the accent fill and its
     * mark is drawn in ink_dark — the ink that reads on a bright
     * surface.  label on key_accent is 1.2:1 in DARK. */
    before = failures;
    check_mark_drawn("claude", TOOL_LOGO_X(0) + 1, TOOL_LOGO_Y(0), 1,
                     pal.ink_dark, pal.key_accent, "self-check: off by one");
    CHECK(failures > before, "self-check: an off-by-one mark is rejected");

    before = failures;
    check_mark_drawn("claude", TOOL_LOGO_X(0), TOOL_LOGO_Y(0), 2,
                     pal.ink_dark, pal.key_accent, "self-check: wrong scale");
    CHECK(failures > before, "self-check: a wrong-scale mark is rejected");

    before = failures;
    check_mark_drawn("codex", TOOL_LOGO_X(0), TOOL_LOGO_Y(0), 1,
                     pal.ink_dark, pal.key_accent, "self-check: wrong tool");
    CHECK(failures > before, "self-check: another tool's mark is rejected");

    /* And it accepts the real thing, so the three rejections above are
     * not just "this checker always fails". */
    before = failures;
    check_mark_drawn("claude", TOOL_LOGO_X(0), TOOL_LOGO_Y(0), 1,
                     pal.ink_dark, pal.key_accent, "self-check: the real mark");
    CHECK(failures == before, "self-check: the real mark is accepted");

    failures = saved;                  /* the misses were the point */
    fprintf(stderr, "-- deliberate failures end --\n");
}

/* The tool row: one mark per chip, in the chip's own ink, over whatever
 * surface that chip got.  The card being edited runs claude, so chip 0
 * is the ticked one and takes the selected surface; the rest do not. */
static void t_tool_tab_marks(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);
    tap(TAB_CX(1), TAB_CY, NULL);        /* the Tool tab */
    draw_bot();

    int n = 0;
    const proj_tool_t *tools = proj_tools(&n);
    int lit = 0;
    for (int i = 0; i < n; i++) {
        uint32_t surface = (i == 0) ? pal.key_accent : pal.key_body;
        /* The ticked chip is filled with the accent, so its mark takes
         * the dark-ink family; the unticked ones keep the light ink. */
        uint32_t ink = (i == 0) ? pal.ink_dark : pal.label;
        char msg[96];
        snprintf(msg, sizeof(msg), "tool tab: %s's mark is on its own chip",
                 tools[i].tool[0] ? tools[i].tool : "custom");
        lit += check_mark_drawn(tools[i].tool, TOOL_LOGO_X(i), TOOL_LOGO_Y(i),
                                1, ink, surface, msg);
    }
    /* A net under the per-chip checks: each mark has far more than ten
     * lit cells, so a total this low means chips went blank. */
    CHECK(lit >= n * 10, "tool tab: every chip carries real ink");
}

/* The card list: the mark leads each card, so which CLI a project runs
 * is legible before its name is read.  Card 0 is the selected one. */
static void t_card_list_marks(void) {
    reset(4);
    set_proj(0, "a", "/srv/a", "claude", "", "", 0);
    set_proj(1, "b", "/srv/b", "codex",  "", "", 0);
    set_proj(2, "c", "/srv/c", "opencode", "", "", 0);
    /* A raw card still holds "claude" in its tool field, but every label
     * on the page calls it custom — so its mark has to be the pencil. */
    set_proj(3, "raw", "/srv/r", "claude", "--continue",
             "source .venv/bin/activate && claude", 1);
    project_ui_open(ui, PUI_ENTRY_RUN, 0, "");
    draw_bot();

    static const char *const tools[4] = { "claude", "codex", "opencode", "" };
    for (int i = 0; i < 4; i++) {
        uint32_t surface = (i == 0) ? pal.key_accent : pal.key_body;
        /* Card 0 is selected: accent fill, so dark ink. */
        uint32_t ink = (i == 0) ? pal.ink_dark : pal.label;
        char msg[96];
        snprintf(msg, sizeof(msg), "card list: card %d shows its mark", i);
        check_mark_drawn(tools[i], CARD_LOGO_X, CARD_LOGO_Y(i), 1,
                         ink, surface, msg);
    }
}

/* The top screen carries the same mark at 2×.  Nothing has painted that
 * panel's background — render_frame() clears it with C2D, which the stub
 * does not model — so an empty cell reads as 0, not as the palette bg.
 *
 * The ink is pal.label: the top screen takes the same palette as the
 * bottom one, so `fg` is no longer a terminal constant. */
static void t_top_screen_mark(void) {
    reset(2);
    set_proj(0, "a", "/srv/a", "gemini", "", "", 0);
    set_proj(1, "raw", "/srv/r", "claude", "--continue",
             "source .venv/bin/activate && claude", 1);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    uint32_t fg = pal.label;

    draw_top();
    check_mark_drawn("gemini", TOP_LOGO_X, TOP_LOGO_Y, TOP_LOGO_SCALE, fg, 0,
                     "top screen: the selected card's mark is drawn at 2x");

    /* Down to the raw card: the mark has to follow the label. */
    key(KEY_DDOWN, NULL);
    draw_top();
    check_mark_drawn("", TOP_LOGO_X, TOP_LOGO_Y, TOP_LOGO_SCALE, fg, 0,
                     "top screen: a raw card shows the pencil, not claude");
}

/* The editor puts the draft's mark in the corner the title block leaves
 * free — that is what says whose flags the 参数 tab is listing. */
static void t_editor_corner_mark(void) {
    reset(1);
    set_proj(0, "a", "/srv/a", "aider", "--continue", "", 0);
    project_ui_open(ui, PUI_ENTRY_SETTINGS, 0, "");
    key(KEY_X, NULL);
    uint32_t fg = pal.label;

    draw_top();
    check_mark_drawn("aider", TOP_W - TOP_PAD - 2 * 16, TOP_LOGO_Y,
                     TOP_LOGO_SCALE, fg, 0,
                     "editor: the draft's mark sits in the top-right corner");

    /* Onto the Args tab — the mark must still be the same tool's. */
    key(KEY_R, NULL);
    key(KEY_R, NULL);
    draw_top();
    check_mark_drawn("aider", TOP_W - TOP_PAD - 2 * 16, TOP_LOGO_Y,
                     TOP_LOGO_SCALE, fg, 0,
                     "editor: the mark follows onto the Args tab");
}

/* ── Theming ────────────────────────────────────────────────────────── */

/* The page wears the live keyboard palette, so it has to hold up on all
 * five.  What is checked is the MAPPING — which role ends up as the card
 * fill and which as the ink on it — not the palette's own floors, which
 * test_kb_theme.c owns.
 *
 * The two roles that were wrong before this existed, and why they were
 * invisible in a screenshot:
 *
 *   - the selected card drew its name in `label`.  label on key_accent
 *     is 1.2:1 in DARK and MONO, so the name was still there in the
 *     layout, still in the capture log, and simply not readable;
 *   - the secondary tone came from hint_fg, which is tuned against
 *     bottom_bg: on key_body it measures 2.1-2.6:1.
 *
 * Both are caught here by re-using the mark walker, which asserts the
 * fill AND the ink at once: the mark is one grid cell per pixel over the
 * card's surface, so "the mark is in ink_dark on key_accent" is also
 * "the card is filled with key_accent and its ink is ink_dark". */
static void t_theme_colours(void) {
    static const term_theme_t themes[] = {
        THEME_DARK, THEME_MATRIX, THEME_MONO, THEME_SOLAR, THEME_CUSTOM,
    };

    for (unsigned t = 0; t < sizeof(themes) / sizeof(themes[0]); t++) {
        kb_pal_t p = *kb_pal_for(themes[t]);
        const char *n = wcag_theme_name(themes[t]);
        char msg[128];

        reset(2);
        set_proj(0, "a", "/srv/a", "claude", "", "", 0);
        set_proj(1, "b", "/srv/b", "codex",  "", "", 0);
        project_ui_open(ui, PUI_ENTRY_RUN, 0, "");

        hstub_reset();
        project_ui_draw_bot(ui, &st, NULL, &p);

        /* Card 0 is selected: the accent fill, with the dark ink on it. */
        snprintf(msg, sizeof(msg),
                 "%s: the selected card is key_accent with ink_dark on it", n);
        check_mark_drawn("claude", CARD_LOGO_X, CARD_LOGO_Y(0), 1,
                         p.ink_dark, p.key_accent, msg);

        /* Card 1 is not: the resting keycap surface, with the light ink. */
        snprintf(msg, sizeof(msg),
                 "%s: a resting card is key_body with label on it", n);
        check_mark_drawn("codex", CARD_LOGO_X, CARD_LOGO_Y(1), 1,
                         p.label, p.key_body, msg);

        /* The two inks must not be interchangeable — if the page ever
         * collapsed sel_ink back into ink, the selected card would still
         * pass the check above in the two themes where the fill happens
         * to be dark enough, and go blank in the other three. */
        if (p.ink_dark == p.label) {
            snprintf(msg, sizeof(msg), "%s: ink_dark == label, nothing to map", n);
            CHECK(0, msg);
        }
    }
}

int main(void) {
    t_open_close();
    t_navigate_and_run();
    t_settings_entry_never_runs();
    t_delete_needs_two_presses();
    t_delete_via_touch();
    t_touch_run_and_edit();
    t_raw_card_opens_on_custom_tab();
    t_part_card_opens_on_dir_tab();
    t_tab_switch_rederives_raw();
    t_tab_switch_via_shoulder();
    t_tab_switch_via_touch();
    t_touch_custom_tab_sets_raw();
    t_cancel_discards();
    t_cwd_probe();
    t_cwd_probe_dribbles();
    t_window_cwd_beats_probe();
    t_cjk_path_cap_does_not_split();
    t_cjk_basename_does_not_split();
    t_nameless_card_is_legible();
    t_keyboard_arming();
    t_keyboard_arming_on_touch();
    t_badge_does_not_open_settings_while_typing();
    t_edit_then_add_then_run();
    t_mark_checker_can_fail();
    t_tool_tab_marks();
    t_card_list_marks();
    t_top_screen_mark();
    t_editor_corner_mark();
    t_theme_colours();

    if (failures) {
        fprintf(stderr, "\n%d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stderr, "\nall project-page checks passed\n");
    return 0;
}
