#pragma once
#include <3ds.h>
#include "ime_pinyin.h"

/*
 * Physical-button input layer for 3dssh M4.
 *
 * Final key bindings (M4 design, Shift now soft-only):
 *   START   quit program
 *   SELECT  → emit \x1b (Esc)
 *   A       → emit \r  (Enter)
 *   B       → emit 0x7f (Backspace)
 *   Y       → Ctrl modifier  (hold-style: only updates flag, no direct emit)
 *   X       → Alt  modifier  (hold-style)
 *   L       → free (was Shift; now on-screen ⇧ only.  L+START = dual-screen
 *             toggle modifier, see main.c)
 *   R       → hold-to-talk voice (main.c owns the press/release; this TU
 *             no longer toggles IME on R — that lives on the on-screen
 *             拼/EN soft key since the M11 redesign)
 *   D-pad   → arrow keys with accelerating auto-repeat
 *   Circle  → tmux mouse-wheel forwarding / local scrollback
 *
 * The modifier keys (Y/X) don't produce any byte themselves — they
 * just update flags that the soft keyboard (softkb.c) queries when
 * dispatching a tapped letter.  That's why pressing Y on its own
 * sends nothing; you have to press Y AND tap a screen letter.
 * Shift is now solely the on-screen ⇧ (one-shot tap, long-press = CapsLock).
 */

typedef enum {
    MODE_EN = 0,    /* English direct typing */
    MODE_CN,        /* Chinese pinyin (M7 will route through IME; M4 = passthrough) */
} ime_mode_t;

typedef struct keyboard_t {
    /* D-pad auto-repeat state */
    u32 dpad_last;
    int dpad_frames;
    /* Backspace (B) auto-repeat state — same ramp as D-pad. */
    int b_held_frames;
    /* Which keyrepeat.h ramp the two above follow: a keyrepeat_style_t.
     * Owned by the settings page; main.c pushes it down every frame. */
    int repeat_style;
    /* Circle Pad scroll throttling */
    int scroll_timer;

    /* IME mode toggled by R */
    ime_mode_t mode;

    /* Monotonic frame counter (incremented each handle_input call) */
    int frame;

    /* Modifier "is currently held" — mirrors keys_held bits */
    int shift_held, ctrl_held, alt_held;
    /* Sticky modifier toggle & latched state */
    int sticky_mode;
    int shift_latched, ctrl_latched, alt_latched;
    /* Frame stamp of most-recent press for each modifier (so we can
     * pick the "most recent" modifier when several are held). */
    int shift_press_f, ctrl_press_f, alt_press_f;

    /* Last transient event for the status indicator (Enter/BS/Esc/IME toggle).
     * Shown for ~12 frames after the press, then fades.
     *
     * IME mode toggles now come from the on-screen 拼/EN key (softkb.c,
     * KIND_IME_TOGGLE).  The status chip shows just "C" / "E" — the
     * destination mode — rather than "L→C" / "L→E", because the chip
     * is too narrow for a directional arrow and the source key isn't
     * relevant to the user once the press has landed. */
    const char *last_event_label;   /* "ENT", "BSP", "ESC", "C", "E", NULL */
    int         last_event_frame;

    /* Output byte buffer for one frame's worth of emission.  Sized
     * to fit the longest single response: the A-key "emit IME buffer
     * as English" path can dump up to IME_BUFFER_MAX (31) ASCII
     * letters in one go. */
    char out_buf[64];
    int  out_len;

    /* M7: pinyin IME pointer (may be NULL).  When set and active, the
     * B key does ime_backspace instead of emitting 0x7f, and D-pad
     * left/right page through candidates instead of sending arrow keys. */
    ime_t *ime;
} keyboard_t;

keyboard_t *keyboard_init(void);
void        keyboard_free(keyboard_t *kbd);

/* Wire the pinyin IME to the keyboard.  Pass NULL to disable.  Until
 * this is called the keyboard runs in pure-passthrough M4 behavior. */
void        keyboard_set_ime(keyboard_t *kbd, ime_t *ime);

/* Per-frame physical-key handling.  Emits bytes for SELECT/A/B/D-pad and
 * sustained Circle Pad scroll/wheel events.  Modifier keys (L/Y/X) and
 * R only update internal state and return NULL.
 *
 * Returns a NUL-terminated byte sequence to send to SSH, or NULL if
 * nothing to send this frame. */
struct terminal_t;
const char *keyboard_handle_input(keyboard_t *kbd,
                                  struct terminal_t *term,
                                  u32 keys_down, u32 keys_held,
                                  int circle_dy);

/* Byte count of the sequence keyboard_handle_input() just handed back.
 * Use this, not strlen(): Ctrl-Space and Ctrl-@ emit a NUL, and strlen()
 * of a buffer whose only byte is 0x00 is 0, so the keystroke would be
 * measured as nothing and never reach the shell. */
int keyboard_out_len(const keyboard_t *kbd);

/* Modifier-state queries used by the soft keyboard. */
int        keyboard_shift_held(const keyboard_t *kbd);
int        keyboard_ctrl_held(const keyboard_t *kbd);
int        keyboard_alt_held (const keyboard_t *kbd);
int        keyboard_shift_active(const keyboard_t *kbd);
int        keyboard_ctrl_active(const keyboard_t *kbd);
int        keyboard_alt_active (const keyboard_t *kbd);
ime_mode_t keyboard_get_mode (const keyboard_t *kbd);

/* Sticky modifier toggle (0 = Hold, 1 = Sticky/Latch). */
void       keyboard_set_sticky_mode(keyboard_t *kbd, int sticky);
int        keyboard_get_sticky_mode(const keyboard_t *kbd);
void       keyboard_consume_latched(keyboard_t *kbd);

/* On-screen mode switch (softkb.c, KIND_IME_TOGGLE) — identical effect
 * to what the physical R key used to do, before R was re-purposed as
 * the voice hold-to-talk key.  No physical-key equivalent exists now:
 * IME mode flips happen from the on-screen 拼/EN key only. */
void       keyboard_toggle_mode(keyboard_t *kbd);

/* Status-indicator label.  Returns a NUL-terminated UTF-8 string that
 * renders to exactly 3 display cells in the leftmost slot of the soft
 * keyboard's top row.  Priority:
 *   1) any held modifier        →  "SFT" / "CTL" / "ALT" (most recent)
 *   2) transient event < 12 frames ago →  "ENT" / "BSP" / "ESC" / "C" / "E"
 *   3) idle                      →  "   " (3 spaces) */
const char *keyboard_status_label(const keyboard_t *kbd);

/* Soft-keyboard transient event — mirrors the physical B/A/R flash.
 * `label` must be a static string like "SFT"/"BSP"/"ENT" that lives
 * for the lifetime of the program (the pointer is stored, not copied). */
void keyboard_notify_event(keyboard_t *kbd, const char *label);

/* Select the auto-repeat ramp (keyrepeat.h) for the held D-pad and held B.
 * Takes effect on the next repeated emission; it cannot retract a repeat that
 * already fired.  Out-of-range values behave as KEYREPEAT_NORMAL. */
void keyboard_set_repeat_style(keyboard_t *kbd, int style);

/* Combine the currently-held physical modifiers with a base ASCII char
 * (typically from a soft-keyboard tap) and return the byte sequence to
 * send to SSH.
 *
 * Mappings:
 *   L + letter   → uppercase (a→A …)
 *   L + digit    → no special handling (use page 2 for symbols)
 *   Y + letter   → Ctrl-letter (a → 0x01 …)
 *   Y + special  → Ctrl-form where applicable (Ctrl-[ = 0x1b, etc.)
 *   X + char     → Alt-char = ESC then char (2-byte sequence)
 *   none         → the literal char
 *
 * Returns pointer to internal buffer, NULL on nonsensical combination. */
const char *keyboard_emit_for(keyboard_t *kbd, char base);

/* Variant with an explicit shift state — the letters page passes
 * (physical L held || on-screen ⇧ armed) so sticky-shift uppercases;
 * symbol/number pages always pass 0 (键面即所得: the face equals the
 * emitted bytes, iOS-style).  Ctrl/Alt transforms apply regardless. */
const char *keyboard_emit_for_ex(keyboard_t *kbd, char base, int shifted);

/* On-screen ↑ send key: plain Enter, or commit-current-candidate +
 * Enter as one atomic emission when a pinyin buffer is active. */
const char *keyboard_enter_key(keyboard_t *kbd);
const char *keyboard_commit_enter(keyboard_t *kbd);
