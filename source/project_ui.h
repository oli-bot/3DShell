#ifndef PROJECT_UI_H
#define PROJECT_UI_H

/*
 * The project page — a dual-screen card picker and card editor.
 *
 *   top screen (400×240)   the selected card's intro: directory, the exact
 *                          command it will run, the user's own note, and
 *                          how often / how recently it was used.
 *   bottom screen (320×240) the card list itself (touch or D-pad), and in
 *                          edit mode a four-tab form.
 *
 * Three entry points share this one object and this one code path:
 *   1. after SSH authentication succeeds  (pick a card, then run it)
 *   2. when a new terminal window opens   (same, for that window's shell)
 *   3. from 设置 → 项目                   (manage the cards; run disabled)
 *
 * Entry points 1 and 2 are indistinguishable from in here, and pui_entry_t
 * says so rather than inventing a difference.
 *
 * Text entry reuses the soft keyboard rather than reimplementing one:
 * while a field is being typed into, project_ui_wants_keyboard() goes true
 * and main.c draws softkb on the bottom screen, feeding the bytes it
 * emits back through project_ui_tick().  The keyboard is never drawn and
 * touch never reaches it outside that sub-mode, so a tap on a card cannot
 * also press the key that happens to be underneath it.
 *
 * This TU draws with citro2d, but it calls no C2D_* / C3D_* symbol of its
 * own — every draw goes through the renderer_draw_* wrappers — so it does
 * compile and run on the host once tools/stub/ supplies C3D_RenderTarget
 * and the KEY_* bits.  tools/test_project_ui.sh uses exactly that to
 * cover the state machine (which tab a card opens on, tab ↔ raw
 * derivation, the delete arming, the probe's failure modes) with the
 * drawing captured as text rather than rasterised.  Keep it that way:
 * reaching for a C2D_* call here would cost that test.
 */

#include <stdint.h>

#include "config.h"
#include "kb_theme.h"
#include "renderer.h"

typedef struct project_ui_t project_ui_t;
typedef struct ssh_client_t ssh_client_t;

/* Why the page was opened.  Exactly one distinction is ever acted on —
 * whether a card may be run at all — so there are two values, not three.
 * There used to be a PUI_ENTRY_CONNECT alongside PUI_ENTRY_NEW_WINDOW, but
 * nothing branched on the difference: the page is opened after the SSH
 * handshake, and after a fresh shell channel has been opened, and for
 * window 0 those are the same moment.  A third value that changes nothing
 * only invites the next reader to guess what it changes. */
typedef enum {
    PUI_ENTRY_RUN = 0,       /* pick a card and run it in the focused window */
    PUI_ENTRY_SETTINGS,      /* opened from the settings list — no run */
} pui_entry_t;

/* What the caller should do about this frame's input. */
typedef enum {
    PUI_ACT_NONE = 0,
    PUI_ACT_RUN,        /* run projects[out_index] in the focused window */
    PUI_ACT_CLOSED,     /* the page closed on this press */
} pui_act_t;

/* Note on PUI_ACT_CLOSED: it is informational.  Closing is equally
 * visible as project_ui_visible() going 1 → 0, and main.c watches that
 * transition instead — because a card being run closes the page from
 * inside project_run() without ever returning PUI_ACT_CLOSED, and any
 * per-exit-path bookkeeping would miss that one. */

project_ui_t *project_ui_create(void);
void          project_ui_free(project_ui_t *ui);

/* Open the page.  `window_cwd` is where the focused window's shell is
 * believed to be — main.c tracks what it injected, so it hands the value
 * in rather than this module guessing.  Pass "" when unknown.  Returns 1
 * if the page opened, 0 only for a NULL handle: there is no "refused"
 * state, because an empty book still has the + 新增项目 row to fill it
 * with. */
int project_ui_open(project_ui_t *ui, pui_entry_t entry, int initial_index,
                    const char *window_cwd);

void project_ui_close(project_ui_t *ui);
int  project_ui_visible(const project_ui_t *ui);

/* 1 while the soft keyboard must be drawn on the bottom screen instead of
 * the form.  main.c picks the bottom-screen renderer off this. */
int project_ui_wants_keyboard(const project_ui_t *ui);

/* 1 when the current touch gesture may be handed to softkb_touch().  It
 * goes 0 for the gesture that *entered* the keyboard sub-mode: softkb
 * derives its down-edge from prev_pressed, so letting the same press
 * through would fire whatever key sits under the finger.  main.c passes
 * (-1,-1) to softkb_touch while this is 0 — that still updates
 * prev_pressed, which is the whole point. */
int project_ui_keyboard_armed(const project_ui_t *ui);

/* Per-frame input.  `kt` is the soft keyboard's byte emission for this
 * frame (NULL when nothing was typed); tx/ty/touch_* describe the raw
 * bottom-screen touch, which the page reads directly whenever the
 * keyboard is not armed.  Returns PUI_ACT_RUN with *out_index set when
 * the user chose a card to run.
 *
 * Never returns PUI_ACT_RUN for a PUI_ENTRY_SETTINGS session. */
pui_act_t project_ui_tick(project_ui_t *ui, dssh_settings_t *st,
                          ssh_client_t *ssh,
                          uint32_t down, uint32_t held,
                          const char *kt,
                          int tx, int ty, int touch_pressed, int touch_down,
                          int *out_index);

/* Render.  Each must be called inside C2D_SceneBegin() of its own target.
 *
 * Both screens take the live keyboard palette (softkb_palette()), so the
 * page is one surface split in two.  The top screen used to take the
 * terminal instead, which is a different colour table — a theme.ini could
 * then colour the two halves of the page differently.  Passing NULL falls
 * back to THEME_DARK's palette, as in project_ui_draw_bot(). */
void project_ui_draw_top(const project_ui_t *ui, const dssh_settings_t *st,
                         renderer_t *r, const kb_pal_t *pal);
void project_ui_draw_bot(const project_ui_t *ui, const dssh_settings_t *st,
                         renderer_t *r, const kb_pal_t *pal);

/* ── "Use the current directory" ──────────────────────────────────────
 * Two sources, in this order:
 *
 *   1. what main.c says the focused window's shell was last told to cd
 *      into — ground truth, because DSSH is what typed the cd;
 *   2. a `pwd` run over an auxiliary exec channel.
 *
 * (2) needs the caveat spelled out: a second SSH channel starts its own
 * process in the login directory, so `pwd` there answers "where does a
 * new shell land", not "where has this window's shell wandered to".  That
 * is exactly the right answer for a window that was just opened (entry
 * point 2 above) and a wrong one for a shell the user has been cd-ing
 * around in — which is why (1) is consulted first.
 */
void project_ui_probe_cwd(project_ui_t *ui, ssh_client_t *ssh);
void project_ui_poll_cwd(project_ui_t *ui, ssh_client_t *ssh);

/* 0 idle, 1 probing, 2 ok, 3 failed. */
int         project_ui_cwd_state(const project_ui_t *ui);
const char *project_ui_cwd(const project_ui_t *ui);

/* 1 when the settings file needs writing (a card was added, edited or
 * deleted).  main.c saves and then clears this.  Editing the draft raises
 * it so the top screen can show unsaved state, but 取消 puts it back to
 * whatever it was when the editor opened — cancelling must not make
 * main.c rewrite an unchanged file. */
int  project_ui_dirty(const project_ui_t *ui);
void project_ui_clear_dirty(project_ui_t *ui);

#endif /* PROJECT_UI_H */
