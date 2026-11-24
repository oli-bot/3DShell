#include "keyboard.h"
#include "keyrepeat.h"
#include "terminal.h"
#include <stdlib.h>
#include <string.h>

/* ──────────────────────────────────────────────────────────────────────
 * D-pad / B auto-repeat tuning lives in keyrepeat.h — the touch soft
 * keyboard repeats on the same ramp, and a held key has to feel the same
 * on both surfaces for the 按键重复速度 setting to mean anything.
 * kbd->repeat_style selects one of three ramps (3DS runs at 60 fps, so
 * 1 frame ≈ 16.7 ms).
 * ────────────────────────────────────────────────────────────────────── */

keyboard_t *keyboard_init(void) {
    keyboard_t *k = calloc(1, sizeof(*k));
    if (!k) return NULL;
    k->mode = MODE_EN;
    k->sticky_mode   = 0;
    k->repeat_style  = KEYREPEAT_NORMAL;   /* = the ramp every build shipped */
    k->shift_latched = 0;
    k->ctrl_latched  = 0;
    k->alt_latched   = 0;
    k->shift_press_f = -1;
    k->ctrl_press_f  = -1;
    k->alt_press_f   = -1;
    return k;
}

void keyboard_free(keyboard_t *kbd) { free(kbd); }

void keyboard_set_ime(keyboard_t *kbd, ime_t *ime) {
    if (!kbd) return;
    kbd->ime = ime;
}

/* ── byte emission helpers ─────────────────────────────────────────── */

static const char *emit_seq(keyboard_t *k, const char *seq) {
    int n = (int)strlen(seq);
    if (n >= (int)sizeof(k->out_buf)) n = sizeof(k->out_buf) - 1;
    memcpy(k->out_buf, seq, n);
    k->out_buf[n] = 0;
    k->out_len    = n;
    return k->out_buf;
}

static const char *emit_byte(keyboard_t *k, char c) {
    k->out_buf[0] = c;
    k->out_buf[1] = 0;
    k->out_len    = 1;
    return k->out_buf;
}

static void mark_event(keyboard_t *k, const char *label) {
    k->last_event_label = label;
    k->last_event_frame = k->frame;
}

void keyboard_notify_event(keyboard_t *kbd, const char *label) {
    if (!kbd || !label) return;
    mark_event(kbd, label);
}

/* Byte length of the sequence the last emit_* call left in out_buf.
 * Callers must use this rather than strlen(): Ctrl-Space and Ctrl-@
 * emit a single NUL, and strlen() of a buffer whose only byte is 0x00
 * is 0, so measuring it that way drops the keystroke on the floor. */
int keyboard_out_len(const keyboard_t *kbd) {
    return kbd ? kbd->out_len : 0;
}

/* Convert a printable ASCII char to its Ctrl- equivalent, or -1 if there
 * is no sensible mapping.
 *
 * The "no mapping" answer is -1 and not 0 on purpose: Ctrl-Space and
 * Ctrl-@ really do map to NUL, and a caller that reads 0 as "no mapping"
 * turns Ctrl-Space into a literal space.  That is not hypothetical — it
 * is what this function's own documented contract used to invite. */
static int to_ctrl(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 1;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c == ' ' || c == '@') return 0x00;     /* Ctrl-Space = NUL */
    if (c == '[')  return 0x1b;                /* Ctrl-[ = Esc */
    if (c == '\\') return 0x1c;
    if (c == ']')  return 0x1d;
    if (c == '^')  return 0x1e;
    if (c == '_')  return 0x1f;
    if (c == '?')  return 0x7f;
    return -1;
}

/* ── physical-key handler ──────────────────────────────────────────── */

const char *keyboard_handle_input(keyboard_t *kbd,
                                  struct terminal_t *term,
                                  u32 keys_down, u32 keys_held,
                                  int circle_dy) {
    if (!kbd) return NULL;
    kbd->out_len = 0;
    kbd->frame++;

    /* Update modifier hold state from keys_held.
     * Shift is now soft-only (on-screen ⇧); L is free.  Y/X remain
     * physical Ctrl/Alt. */
    kbd->shift_held = 0;
    kbd->shift_latched = 0;
    kbd->ctrl_held  = (keys_held & KEY_Y) != 0;
    kbd->alt_held   = (keys_held & KEY_X) != 0;

    /* Sticky/Latch mode: down-edge toggles the latch on/off.
     * Shift latch removed — only Ctrl/Alt remain sticky. */
    if (kbd->sticky_mode) {
        if (keys_down & KEY_Y) kbd->ctrl_latched  = !kbd->ctrl_latched;
        if (keys_down & KEY_X) kbd->alt_latched   = !kbd->alt_latched;
    } else {
        kbd->ctrl_latched  = 0;
        kbd->alt_latched   = 0;
    }

    /* Stamp press frame on transition not-held → held or when latched. */
    if ((keys_down & KEY_Y) || kbd->ctrl_latched)  kbd->ctrl_press_f  = kbd->frame;
    if ((keys_down & KEY_X) || kbd->alt_latched)   kbd->alt_press_f   = kbd->frame;

    /* SELECT → Esc */
    if (keys_down & KEY_SELECT) {
        mark_event(kbd, "ESC");
        return emit_byte(kbd, '\x1b');
    }

    /* IME mode toggle moved off R (now reserved for hold-to-talk voice)
     * and off L (now free as the START+L dual-screen toggle modifier).
     * The sole entry point is the on-screen "拼/EN" soft key on every
     * keyboard page — physical START is *not* an IME toggle either; it
     * is solely the AI-ask chord modifier (held R + START).  Status
     * chip for the toggle is written by softkb.c's KIND_IME_TOGGLE
     * handler, not from here.
     */

    /* Circle Pad scroll — same throttling as M3 polish:
     *   deadband 80, sustained-push 6 frames before first event,
     *   then one event per 5 frames.
     *
     * Two very different modes, decided by whether the remote is in a
     * full-screen app (alternate screen, use_alt):
     *
     *  • use_alt == 0 → normal shell.  Real lines scroll off the top and
     *    DSSH captures them into its local scrollback, so the stick
     *    scrolls that buffer.  Simple and correct for shells, less, git
     *    log, etc.  (The old pre-v1.2 code instead forwarded the wheel as
     *    an SGR mouse sequence — `ESC[<64;1;1M` / `<65;…` at a hardcoded
     *    (1,1) — because it assumed "remote mouse tracking == tmux".)
     *
     *  • use_alt == 1 → full-screen TUI (opencode, htop, vim, …).  These
     *    apps redraw the whole screen in place with cursor-positioning
     *    every frame, so NOTHING ever scrolls off the terminal top —
     *    DSSH's local scrollback stays empty and scrolling it would be a
     *    no-op.  They scroll with their own keybindings instead: opencode
     *    binds pageup/pagedown to its transcript (session.page.up/down).
     *    So here the stick is translated into those keys and sent to the
     *    remote — the same effect as pressing them on a real keyboard.
     *    (The desktop mouse wheel "just works" for the same reason: the
     *    pointer lands inside opencode's transcript box; a 3DS stick has
     *    no pointer position, so keys are the reliable analogue.) */
    int scroll_active = (circle_dy > 80 || circle_dy < -80);
    if (scroll_active) {
        kbd->scroll_timer++;
        int armed = (kbd->scroll_timer == 6) ||
                    (kbd->scroll_timer > 6 && (kbd->scroll_timer - 6) % 5 == 0);
        if (armed && term) {
            if (term->use_alt) {
                /* Full-screen TUI: PageUp / PageDown (ESC[5~ / ESC[6~).
                 * Pushing the stick up = scroll back = page up. */
                return emit_seq(kbd, circle_dy > 0 ? "\x1b[5~" : "\x1b[6~");
            }
            terminal_scroll_view(term, circle_dy > 0 ? 1 : -1);
        }
    } else {
        kbd->scroll_timer = 0;
    }

    /* D-pad auto-repeat (priority Up > Down > Right > Left). */
    u32 dpad = keys_held & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT);
    u32 dpad_active = 0;
    if      (dpad & KEY_DUP)    dpad_active = KEY_DUP;
    else if (dpad & KEY_DDOWN)  dpad_active = KEY_DDOWN;
    else if (dpad & KEY_DRIGHT) dpad_active = KEY_DRIGHT;
    else if (dpad & KEY_DLEFT)  dpad_active = KEY_DLEFT;

    if (dpad_active != kbd->dpad_last) {
        kbd->dpad_last   = dpad_active;
        kbd->dpad_frames = 0;
    }
    if (dpad_active) {
        int fire = (kbd->dpad_frames == 0) ||
                   keyrepeat_fires(kbd->repeat_style, kbd->dpad_frames);
        kbd->dpad_frames++;
        if (fire) {
            /* IME mode swaps the D-pad onto candidate navigation:
             *   Up    → previous page
             *   Down  → next page
             *   Left  → selection cursor left within page
             *   Right → selection cursor right within page
             * Arrow keys still go through normally when no buffer. */
            if (kbd->ime && ime_active(kbd->ime)) {
                if      (dpad_active == KEY_DUP)    ime_page_prev(kbd->ime);
                else if (dpad_active == KEY_DDOWN)  ime_page_next(kbd->ime);
                else if (dpad_active == KEY_DLEFT)  ime_selection_left(kbd->ime);
                else if (dpad_active == KEY_DRIGHT) ime_selection_right(kbd->ime);
                return NULL;
            }
            const char *seq;
            if      (dpad_active == KEY_DUP)    seq = "\x1b[A";
            else if (dpad_active == KEY_DDOWN)  seq = "\x1b[B";
            else if (dpad_active == KEY_DRIGHT) seq = "\x1b[C";
            else                                 seq = "\x1b[D";
            return emit_seq(kbd, seq);
        }
    }

    /* A in IME mode is the "oops, I meant English" escape hatch:
     * dumps the pinyin buffer to SSH as raw ASCII letters and clears
     * the buffer.  This way you don't have to backspace + toggle
     * mode + retype if you accidentally typed in CN.
     *
     * Space remains the "commit candidate" key (handled by softkb).
     * A in EN mode (or with empty buffer) sends the literal Enter. */
    if (keys_down & KEY_A) {
        if (kbd->ime && ime_active(kbd->ime)) {
            const char *result = emit_seq(kbd, ime_buffer(kbd->ime));
            ime_clear(kbd->ime);
            mark_event(kbd, "ENT");
            return result;
        }
        mark_event(kbd, "ENT");
        if (term && term->sb_offset) terminal_scroll_view(term, -term->sb_offset);
        return emit_byte(kbd, '\r');
    }
    /* B → Backspace, with the same auto-repeat ramp as D-pad so holding
     * B for a couple seconds rapid-deletes (super useful in Claude Code's
     * input editor).  In IME mode, B chews the pinyin buffer instead. */
    if (keys_held & KEY_B) {
        int b_pressed_now = (keys_down & KEY_B) != 0;
        int fire = 0;
        if (b_pressed_now) {
            kbd->b_held_frames = 0;
            mark_event(kbd, "BSP");
            fire = 1;
        } else {
            kbd->b_held_frames++;
            fire = keyrepeat_fires(kbd->repeat_style, kbd->b_held_frames);
        }
        if (fire) {
            if (kbd->ime && ime_active(kbd->ime)) {
                ime_backspace(kbd->ime);
                return NULL;
            }
            return emit_byte(kbd, 0x7f);
        }
    } else {
        kbd->b_held_frames = 0;
    }
    /* X / Y / L are modifiers — no byte on their own; flags already updated. */
    return NULL;
}

/* ── modifier queries for softkb ────────────────────────────────────── */

int keyboard_shift_held(const keyboard_t *k) { return k && k->shift_held; }
int keyboard_ctrl_held (const keyboard_t *k) { return k && k->ctrl_held;  }
int keyboard_alt_held  (const keyboard_t *k) { return k && k->alt_held;   }

int keyboard_shift_active(const keyboard_t *k) {
    return k && (k->shift_held || k->shift_latched);
}
int keyboard_ctrl_active(const keyboard_t *k) {
    return k && (k->ctrl_held || k->ctrl_latched);
}
int keyboard_alt_active(const keyboard_t *k) {
    return k && (k->alt_held || k->alt_latched);
}

void keyboard_set_sticky_mode(keyboard_t *k, int sticky) {
    if (!k) return;
    k->sticky_mode = sticky;
    if (!sticky) {
        k->shift_latched = 0;
        k->ctrl_latched  = 0;
        k->alt_latched   = 0;
    }
}

int keyboard_get_sticky_mode(const keyboard_t *k) {
    return k ? k->sticky_mode : 0;
}

void keyboard_set_repeat_style(keyboard_t *k, int style) {
    if (!k) return;
    k->repeat_style = style;
}

void keyboard_consume_latched(keyboard_t *k) {
    if (!k) return;
    k->shift_latched = 0;
    k->ctrl_latched  = 0;
    k->alt_latched   = 0;
}

ime_mode_t keyboard_get_mode(const keyboard_t *k) {
    return k ? k->mode : MODE_EN;
}

void keyboard_toggle_mode(keyboard_t *k) {
    if (!k) return;
    k->mode = (k->mode == MODE_EN) ? MODE_CN : MODE_EN;
}

/* ── status-indicator label ────────────────────────────────────────── */

#define EVENT_LIFETIME_FRAMES 12

const char *keyboard_status_label(const keyboard_t *k) {
    if (!k) return "   ";
    /* 1) Held or latched modifier (most recent press wins). */
    int s_f = (k->shift_held || k->shift_latched) ? k->shift_press_f : -1;
    int c_f = (k->ctrl_held  || k->ctrl_latched)  ? k->ctrl_press_f  : -1;
    int a_f = (k->alt_held   || k->alt_latched)   ? k->alt_press_f   : -1;
    if (s_f >= 0 || c_f >= 0 || a_f >= 0) {
        int max = -1;
        const char *lbl = "   ";
        if (s_f > max) { max = s_f; lbl = "SFT"; }
        if (c_f > max) { max = c_f; lbl = "CTL"; }
        if (a_f > max) { max = a_f; lbl = "ALT"; }
        return lbl;
    }
    /* 2) Recent transient event. */
    if (k->last_event_label &&
        (k->frame - k->last_event_frame) < EVENT_LIFETIME_FRAMES) {
        return k->last_event_label;
    }
    /* 3) Idle. */
    return "   ";
}

/* ── softkb dispatch helper ────────────────────────────────────────── */

const char *keyboard_emit_for_ex(keyboard_t *k, char base, int shifted) {
    if (!k || base == 0) return NULL;

    int is_shift = shifted || k->shift_held || k->shift_latched;
    int is_ctrl  = k->ctrl_held  || k->ctrl_latched;
    int is_alt   = k->alt_held   || k->alt_latched;

    char c = base;
    if (is_shift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';

    /* Consume latched modifiers once a character is emitted. */
    if (k->sticky_mode) {
        keyboard_consume_latched(k);
    }

    /* Ctrl: emit Ctrl-X form. */
    if (is_ctrl) {
        int x = to_ctrl(c);
        if (x < 0) return emit_byte(k, c);   /* fallback: literal */
        return emit_byte(k, (char)x);
    }

    /* Alt: ESC + char (Linux/xterm convention). */
    if (is_alt) {
        k->out_buf[0] = '\x1b';
        k->out_buf[1] = c;
        k->out_buf[2] = 0;
        k->out_len    = 2;
        return k->out_buf;
    }

    return emit_byte(k, c);
}

/* Physical-modifier-only wrapper (B button etc. still route here). */
const char *keyboard_emit_for(keyboard_t *k, char base) {
    return keyboard_emit_for_ex(k, base, k ? (k->shift_held || k->shift_latched) : 0);
}

/* ── soft keyboard ↑ send-key support ──────────────────────────────── */

/* Plain terminal Enter (\r).  Used by the on-screen ↑ key when no
 * pinyin buffer is active. */
const char *keyboard_enter_key(keyboard_t *k) {
    if (!k) return NULL;
    mark_event(k, "ENT");
    return emit_byte(k, '\r');
}

/* ↑ with an active pinyin buffer: commit the currently-selected
 * candidate and append '\r' as ONE atomic emission — a single ssh
 * write, so server echo can't interleave between the text and the
 * newline. */
const char *keyboard_commit_enter(keyboard_t *k) {
    if (!k || !k->ime || !ime_active(k->ime)) return keyboard_enter_key(k);
    const char *txt = ime_select_current(k->ime);   /* commits + clears */
    if (!txt || !*txt) return emit_byte(k, '\r');
    size_t n = strlen(txt);
    if (n + 2 > sizeof(k->out_buf)) {               /* never expected */
        return emit_byte(k, '\r');
    }
    memcpy(k->out_buf, txt, n);
    k->out_buf[n]     = '\r';
    k->out_buf[n + 1] = '\0';
    k->out_len        = (int)n + 1;
    mark_event(k, "ENT");
    return k->out_buf;
}
