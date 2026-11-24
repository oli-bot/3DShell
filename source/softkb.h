#pragma once
#include "renderer.h"
#include "keyboard.h"
#include "ime_pinyin.h"
#include "voice.h"
#include "theme_ini.h"   /* theme_ini_report_t, for softkb_set_theme_report() */
#include "kb_theme.h"    /* kb_pal_t, for softkb_palette() */

/*
 * On-screen touch keyboard for the 3DS bottom screen (M4 + M7 IME).
 *
 * Three pages (iOS-style, CN/EN symbol variants selected by R mode):
 *   PAGE_LETTERS   qwerty / asdf / [⇧] zxcvb nm [⌫] / [123][tab][space][↑]
 *   PAGE_SYMBOLS   1234567890 + common punct (+ 。、￥"" in CN) /
 *                  [#+=] flips to PAGE_SYMBOLS2; [拼音/ABC] returns here→letters
 *   PAGE_SYMBOLS2  brackets/programming set:
 *                     CN 【】{}#%^*+= / _—\|~《》$&· / …：[]<>
 *                     EN []{}#%^*+= / _\|~<>€£¥• / .?!'
 *
 * On-screen ⇧ is a tap-toggle one-shot shift (long-press = CapsLock);
 * it uppercases LETTERS-page faces only and is disarmed on any page
 * flip (CapsLock persists).  Physical L is now free.  Symbol pages are
 * shift-independent (键面即所得 — every face equals the emitted bytes).
 * ⌫ = backspace (IME buffer chew / DEL), ↑ = commit candidate + Enter
 * or plain Enter.  The old emoji-slot key sends Tab (essential in a shell).
 *
 * Top row of the bottom screen is reserved for:
 *   [STA]                  candidate-area (IME, M7)              [⚙]
 *    └ keyboard_status_label()                                └ settings
 */

typedef enum {
    PAGE_LETTERS = 0,
    PAGE_SYMBOLS,
    PAGE_SYMBOLS2,     /* #+= programming/CJK-punct page (iOS-style) */
} softkb_page_t;

typedef struct softkb_t softkb_t;

/* `ime` is the loaded pinyin IME engine (may be NULL if the dict
 * failed to load — softkb degrades CN mode to direct passthrough).
 * Pass NULL at startup and call softkb_set_ime(...) once the dict
 * has finished loading; this lets main.c show a "loading..." banner
 * during the ~5-second dict read. */
softkb_t *softkb_init(ime_t *ime);
void      softkb_set_ime(softkb_t *kb, ime_t *ime);
void      softkb_free(softkb_t *kb);

/* Optional voice-input handle.  When non-NULL, softkb_draw renders
 * RECORDING/TRANSCRIBING/ERROR badges in the top-left status slot,
 * preempting the modifier indicator.  Pass NULL or call with NULL to
 * disable. */
void      softkb_set_voice(softkb_t *kb, const voice_t *v);

/* Render the current page + status row to the bottom screen.  Must be
 * called inside C2D_SceneBegin(bottom_target). */
void softkb_draw(softkb_t *kb, renderer_t *r, const keyboard_t *kbd);

/* Dual-screen settings page, top half: the full item list.  Called from
 * main.c INSTEAD of the terminal while softkb_settings_open() is true.
 * Must be called inside C2D_SceneBegin(top_target). */
void softkb_draw_settings(softkb_t *kb, renderer_t *r);

/* Dual-screen overlay: draw just the voice status badge (REC /
 * spinner / ERR) floating in the top-left corner over the bottom-screen
 * terminal, where softkb_draw is not called.  No-op when voice is IDLE.
 * Must be called inside C2D_SceneBegin(bottom_target). */
void softkb_draw_voice_badge(const softkb_t *kb);

/* Process a touch event for the current frame.
 *   pressed=1 means the touch has just gone down (keys_down & KEY_TOUCH).
 *   pressed=0 means the touch is held or just released — we use this only
 *   to clear the visual "pressed" highlight; we don't fire on release.
 *
 * If a printable key was tapped, returns the byte sequence to send to
 * SSH (already passed through keyboard_emit_for so modifiers apply).
 * If a control key was tapped (page switch, etc.), returns NULL but the
 * internal state has been updated and the next softkb_draw will reflect
 * the change.
 *
 * Pass tx=ty=-1 when there is no current touch.  The function should be
 * called on every frame so the visual highlight follows touch state. */
const char *softkb_touch(softkb_t *kb,
                         keyboard_t *kbd,
                         int tx, int ty,
                         int pressed);

/* Currently displayed page (for diagnostic / layout testing). */
softkb_page_t softkb_current_page(const softkb_t *kb);

/* ── Debug page ─────────────────────────────────────────────────────
 *
 * Triggered via the 调试页面 item in the ⚙ settings list.  It shows
 * the last 32 received bytes in hex and every physical-key binding.
 * Display-only: the old mascot / bottom-layout toggle buttons moved
 * into the settings list (吉祥物 and 下屏布局 items).  Tap the
 * top-right badge once to leave debug mode.
 */

/* Append received SSH bytes to the debug recv ring (last 32 bytes
 * shown).  Safe to call every frame; bytes < 1 are no-ops. */
void softkb_record_recv(softkb_t *kb, const char *bytes, int n);

/* First row of the bottom bar main.c owns (clock / battery / mascot).
 * softkb stops its key rows and the settings card at this line, and main.c
 * uses the same number as the top of the mascot's touch strip — one value
 * instead of 214 spelled on both sides. */
#define SOFTKB_BOTTOM_ROW_Y 214

#include "mascot.h"

/* True iff the keyboard is currently rendering the debug overlay
 * instead of the key layout.  When this returns 1, main.c should also
 * suppress the clock + mascot in the bottom row — the debug page
 * occupies the full bottom screen. */
int  softkb_in_debug(const softkb_t *kb);

/* ── Settings page — dual-screen, 3DS System-Settings style ─────────
 *
 * Opened and closed by the ⚙ badge in the status row, or closed by
 * physical B.  While open it owns BOTH screens: the top screen shows
 * all items in one D-pad-navigated list (↑/↓ select, ←/→ or A adjust
 * the selected value), the bottom screen shows the selected item's
 * detail card with every option as a tappable button.  The page is
 * modal — while it is up, main.c must suppress the bottom bar/mascot
 * and the START+L dual-screen toggle. */

/* True while the settings page replaces the terminal + key layout. */
int softkb_settings_open(const softkb_t *kb);

/* Force the settings page closed.  main.c calls this every frame while
 * some other page has rented the soft keyboard (the project page, the
 * AI-ask modal), because those pages reach the keyboard either through
 * softkb_touch() or by drawing softkb_draw() themselves — which is
 * exactly what puts the ⚙ badge on screen — while neither of them
 * draws the settings page.  Left alone, a tap on that badge would open
 * a page that owns the keypad but paints nothing.
 *
 * Deliberately does NOT touch settings_dirty: that flag means "a value
 * changed and has not reached settings.ini yet", and main.c's save
 * block reads it independently of settings_open.  Clearing it here
 * would drop an edit the user already made; keeping it lets the change
 * land on the next successful write.  debug_mode is left alone too —
 * it is a different page with its own exit. */
void softkb_leave_settings(softkb_t *kb);

/* True while the project-bookmarks editor replaces the terminal.  Like
 * softkb_settings_open(), main.c must read this every frame and route
 * the keypad to its own handler when it flips to 1. */
int softkb_projects_open(const softkb_t *kb);

/* Clear the projects-open flag from main.c after the editor has done
 * its own B-exit handling.  Symmetric with how main.c also reads the
 * settings_open flag, but settings' own B handler flips the flag —
 * here we drive both sides because the editor lives outside the
 * settings framework. */
void softkb_projects_close(softkb_t *kb);

/* Handles every physical key the settings page owns (the whole keypad:
 * D-pad, A/B, X/Y, L/R, ZL, START, SELECT) and returns that owned
 * mask — 0 when the page is closed.  Callers must strip the returned
 * bits from BOTH the down-edge and the held mask before any other
 * consumer sees them: R otherwise starts hold-to-talk, the D-pad
 * otherwise emits arrows, START otherwise starts an AI-ask take (when
 * R is held) and START+L otherwise flips the dual view.  Do not call
 * while the modal is animating.
 * AI modal is up — the modal owns A/B. */
u32 softkb_settings_keys(softkb_t *kb, u32 down, u32 held);

/* Mascot enable and species selector — seven modes, 0..6.
 *
 * Modes 1..6 are indices into mascot_species_t (mascot.h) after the -1 in
 * softkb_mascot_species(), and they are also the seven opts[] of the
 * SID_MASCOT row in softkb.c — the two must stay the same length:
 *
 *   mode 0 = OFF    (关)
 *   mode 1 = CRAB   (屑屑)
 *   mode 2 = CHICK  (坤坤)
 *   mode 3 = PHAGE  (菌菌)
 *   mode 4 = ALIEN  (大眼)
 *   mode 5 = NIULAI (牛来)
 *   mode 6 = DOG    (刀盾狗)
 *
 * Adding a species means adding an option to that row as well; see
 * SOFTKB_OPTS_MAX and the width asserts on settings_item_t. */
int              softkb_mascot_mode(const softkb_t *kb);
int              softkb_mascot_enabled(const softkb_t *kb);
mascot_species_t softkb_mascot_species(const softkb_t *kb);

/* Dual-screen bottom-panel render mode: 0 = AUTO (full-screen TUI →
 * center-crop, shell → squeeze), 1 = SQUEEZE (always squeeze all 66
 * columns), 2 = CENTER (always center-crop to the middle columns at full
 * 6 px), 3 = TRIM (crop 2 cols left + 3 right, squeeze the remaining 61
 * at ~0.87×).  Set from the 下屏布局 settings item; main.c reads it
 * every frame to pick the bottom-screen renderer. */
int  softkb_dual_bottom_mode(const softkb_t *kb);

/* Extended settings getters */
int          softkb_sticky_mode(const softkb_t *kb);
int          softkb_key_sound_enabled(const softkb_t *kb);
/* Auto-repeat ramp for held keys — a keyrepeat_style_t from keyrepeat.h
 * (0 slow, 1 normal, 2 fast).  main.c pushes it into keyboard.c. */
int          softkb_repeat_style(const softkb_t *kb);
/* Language of this program's own text: 0 = Chinese, 1 = English.  Not the
 * IME typing mode (that is the on-screen 拼/EN key).  main.c gates its
 * terminal banner on it. */
int          softkb_ui_lang(const softkb_t *kb);
term_theme_t softkb_theme(const softkb_t *kb);
int          softkb_voice_max_sec(const softkb_t *kb);
int          softkb_bottom_info_mode(const softkb_t *kb);

/* Bottom-screen palette, owned by softkb.c / kb_theme.h but needed by
 * main.c, which fills the background and draws the info bar.  All but
 * softkb_bottom_c2d_bg return RGBA 0xRRGGBBAA; that one is already
 * C2D_Color32()-ordered and ready for C2D_TargetClear().  The theme does
 * NOT reach the voice badge's runtime status tints, the AI answer modal or
 * the mascot sprites — those keep their own fixed colours.
 *
 * softkb_bottom_c2d_bg() is also what main.c clears the TOP panel to while
 * a page owns both screens (the project page, the machine picker).  The
 * name says "bottom" because the palette is the bottom screen's, not
 * because the colour is only good there: a page that has borrowed the top
 * panel must be cleared to the same surface as its own bottom half, and
 * the terminal's background is a different table.  render_frame() is the
 * only caller that decides, and it decides for both panels at once. */
uint32_t softkb_bottom_c2d_bg(const softkb_t *kb);
uint32_t softkb_info_dim(const softkb_t *kb);
uint32_t softkb_batt_warn_color(const softkb_t *kb);
uint32_t softkb_batt_chg_color(const softkb_t *kb);
uint32_t softkb_batt_crit_color(const softkb_t *kb);
/* Window strip (main.c): the focused tab number takes the palette's primary
 * text role — it has to out-shout the dim tab numbers beside it in every
 * theme, not just in Tokyo Night.  The unread-output dot borrows the alarm
 * role (same value as the critical-battery digits): it is the one colour each
 * theme owns for "look here", and MATRIX/MONO express it as brightness, not
 * red. */
uint32_t softkb_tab_active_color(const softkb_t *kb);
uint32_t softkb_unread_color(const softkb_t *kb);
/* Background of the chip the floating window strip sits on in dual-screen
 * mode — the same role the status-row EN/CHN chips use. */
uint32_t softkb_chip_bg(const softkb_t *kb);

/* The live bottom-screen palette, for a page that draws its own layout
 * there instead of the key grid (the project card page).  Never NULL;
 * re-derived from the current theme on every call, so it is safe to read
 * on the same frame the theme changed. */
const kb_pal_t *softkb_palette(const softkb_t *kb);

/* Settings persistence — main.c loads at startup and saves when dirty. */
struct dssh_settings_t;
void softkb_apply_settings(softkb_t *kb, const struct dssh_settings_t *st);

/* Overlay the soft-keyboard-owned fields onto a caller-populated struct.
 *
 * Contract: `st` must be filled in by the caller first — typically via
 *   settings_defaults(&st); settings_load(&st, SETTINGS_PATH);
 * This function then overwrites only the fields the soft keyboard
 * owns (mascot_mode, theme_mode, voice_max_sec, etc.) and leaves
 * everything else untouched.  An uninitialised struct passed here
 * will leak stack garbage through the subsequent settings_save(),
 * silently clobbering whatever was on disk — so the caller is
 * responsible for seeding the struct before calling.
 *
 * The old behaviour of seeding from settings_defaults() before
 * overlaying was a footgun: any new field added to dssh_settings_t
 * (e.g. the [projects] section) would be wiped to its default the
 * moment the user tweaked any UI option, because the export
 * function did not know about that field.  Always overlay, never
 * reset. */
void softkb_export_settings(const softkb_t *kb, struct dssh_settings_t *st);

/* Tell the on-screen card what sdmc:/3ds/3dssh/theme.ini did at startup.
 * main.c calls this once, right after theme_ini_load(); the 自定义 option's
 * tip line is drawn from it.  Passing NULL restores the "never read a file"
 * report, which is what the row says before anyone has told it otherwise. */
void softkb_set_theme_report(softkb_t *kb, const theme_ini_report_t *report);
int  softkb_settings_dirty(const softkb_t *kb);
void softkb_clear_settings_dirty(softkb_t *kb);
