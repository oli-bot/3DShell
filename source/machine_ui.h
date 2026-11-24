#ifndef MACHINE_UI_H
#define MACHINE_UI_H

#include <stdint.h>
#include "config.h"
#include "kb_theme.h"
#include "renderer.h"

typedef struct machine_ui_t machine_ui_t;

/* What one machine_ui_tick() frame asked for.  CONNECT and CANCEL are the
 * pre-existing pair; RENAMED was added with the display name and is the
 * only one that carries a string. */
#define MUI_ACT_NONE     0
#define MUI_ACT_CONNECT  1    /* *out_index = the chosen machine */
#define MUI_ACT_CANCEL  (-1)  /* B: no session, the page closes */
#define MUI_ACT_RENAMED  2    /* the user committed a new name */

machine_ui_t *machine_ui_create(void);
void          machine_ui_free(machine_ui_t *ui);

/* Open the picker.  `initial_index` pre-highlights a machine (e.g. the last
 * chosen one); it is clamped into range.
 *
 * `lang` is the interface-language index main.c already booted with
 * (softkb_ui_lang()): 0 = Chinese, anything else = English.  Same
 * two-language convention as project_ui.c — the six-language table in
 * softkb.c covers the settings page, and every other page falls back to
 * English for ja/ko/de/fr.  This page used to print Chinese *and* English
 * stacked, which read as neither. */
int machine_ui_open(machine_ui_t *ui, const ssh_config_t *cfg,
                    int initial_index, int lang);

void machine_ui_close(machine_ui_t *ui);
int  machine_ui_visible(const machine_ui_t *ui);

/* Per-frame input.
 *
 * `kt` is the soft keyboard's byte emission for this frame (NULL when
 * nothing was typed).  It is only consulted while the rename field is up —
 * which is the only time main.c lends this page the keyboard, see
 * machine_ui_wants_keyboard().  tx/ty/touch_* describe the raw bottom-screen
 * touch, which the page reads directly whenever the keyboard is not armed.
 *
 * Returns MUI_ACT_CONNECT with *out_index set, MUI_ACT_CANCEL, or
 * MUI_ACT_RENAMED — in which case the new name is read with
 * machine_ui_renamed_index()/machine_ui_renamed_name() rather than through
 * *out_index, because a rename does not choose a machine. */
int machine_ui_tick(machine_ui_t *ui, const ssh_config_t *cfg,
                    uint32_t down, uint32_t held, const char *kt,
                    int tx, int ty, int touch_pressed, int touch_down,
                    int *out_index);

/* ── The rename field ─────────────────────────────────────────────────
 *
 * Renaming is a text field borrowed from the soft keyboard, exactly as
 * project_ui.c borrows it for a card's path.  X on the picker opens it for
 * the highlighted machine; so does a tap on a row's ✎ strip, which is the
 * only route open to a touch-only user (tapping the row body connects, so
 * the affordance cannot share it).
 *
 * The draft is committed by the keyboard's ✓ (which emits "\r") or by A,
 * matching the picker's own "A confirms" reading; B and START leave
 * without writing.  Nothing is written to disk here — a commit hands the
 * name to main.c, which owns the config file. */

/* 1 while the soft keyboard should own the bottom screen. */
int machine_ui_wants_keyboard(const machine_ui_t *ui);

/* 0 for the frame(s) right after the field opened.  main.c passes
 * (-1,-1) to softkb_touch while this is 0 — that still updates
 * prev_pressed, which is the whole point: otherwise the tap that opened
 * the field also presses whatever key sits under the finger. */
int machine_ui_keyboard_armed(const machine_ui_t *ui);

/* The machine whose name was committed, or -1 when there is nothing to
 * persist.  Stays valid until the next rename starts or the page reopens. */
int         machine_ui_renamed_index(const machine_ui_t *ui);
const char *machine_ui_renamed_name(const machine_ui_t *ui);

/* Render.  Each must be called inside C2D_SceneBegin() of its own target.
 *
 * Both screens take the live keyboard palette (softkb_palette()), so the
 * page is one surface split in two.  The top screen used to take the
 * terminal's ink instead; that is a different table, so a theme.ini could
 * colour the two halves of this page differently.  Passing NULL falls back
 * to THEME_DARK's palette, same as project_ui_draw_bot(). */
void machine_ui_draw_top(const machine_ui_t *ui, const ssh_config_t *cfg,
                         renderer_t *r, const kb_pal_t *pal);
void machine_ui_draw_bot(const machine_ui_t *ui, const ssh_config_t *cfg,
                         renderer_t *r, const kb_pal_t *pal);

#endif /* MACHINE_UI_H */
