/*
 * Host-side tests for source/keyboard.c — the physical-button input layer.
 *
 * Compile and run via tools/test_keyboard.sh.  The module is pure logic:
 * it references no C2D_/C3D_ symbol and its only 3DS dependency is the
 * `u32` typedef plus the KEY_* bits in source/keyboard.h, both of which
 * tools/stub/3ds.h supplies verbatim from libctru.  So the whole binding
 * table runs natively.
 *
 * The assertions below are driven by the key sequences a user actually
 * produces (press A, hold B, tap Y then tap a letter, …) and are checked
 * against the binding table documented at the top of source/keyboard.h —
 * not against whatever the code happens to do today.  Where the two
 * disagree, the divergence is reported as a note, never silently pinned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/keyboard.h"
#include "../source/ime_pinyin.h"
#include "stub/ime_test_dict.h"

#define IME_FIXTURE "build/kb_ime_fixture.bin"

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg, ...) do {                                    \
    checks++;                                                         \
    if (cond) printf("ok   " msg "\n", ##__VA_ARGS__);                \
    else { fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); failures++; } \
} while (0)

/* A divergence from the documented binding table.  Printed so it cannot be
 * overlooked, but not counted as a failure — fixing it is a source change
 * this test is not allowed to make. */
#define NOTE(msg, ...) do {                                           \
    printf("note: " msg "\n", ##__VA_ARGS__);                         \
} while (0)

/* One frame of physical input.  Returns the emitted bytes ("" if none). */
static const char *frame(keyboard_t *k, u32 down, u32 held) {
    const char *s = keyboard_handle_input(k, NULL, down, held, 0);
    return s ? s : "";
}

static void type(ime_t *ime, const char *s) {
    for (; *s; s++) ime_input_letter(ime, *s);
}

/* Feed the IME a buffer and wire it to the keyboard. */
static ime_t *fresh_ime(keyboard_t *k, const char *pinyin) {
    ime_t *ime = ime_init(IME_FIXTURE);
    if (!ime) {
        CHECK(0, "ime_init(build/kb_ime_fixture.bin) failed");
        return NULL;
    }
    type(ime, pinyin);
    keyboard_set_ime(k, ime);
    return ime;
}

/* ── documented single-key bindings ───────────────────────────────── */

static void check_plain_bindings(void) {
    keyboard_t *k = keyboard_init();
    CHECK(k != NULL, "keyboard_init returns a state block");
    if (!k) return;

    /* keyboard.h: "SELECT → emit \x1b (Esc)" */
    CHECK(strcmp(frame(k, KEY_SELECT, KEY_SELECT), "\x1b") == 0 && k->out_len == 1,
          "SELECT emits a lone 0x1b (Esc), nothing else");

    /* keyboard.h: "A → emit \r (Enter)" — and the status chip flashes ENT. */
    CHECK(strcmp(frame(k, KEY_A, KEY_A), "\r") == 0,
          "A emits \\r (Enter)");
    CHECK(strcmp(keyboard_status_label(k), "ENT") == 0,
          "the status chip reads ENT on the frame A lands");

    keyboard_free(k);
}

/* ── B = backspace, with the accelerating repeat ──────────────────── */

static void check_backspace_repeat(void) {
    keyboard_t *k = keyboard_init();
    int first = 0, repeats = 0;

    /* Press: exactly one 0x7f on the down edge. */
    const char *s = frame(k, KEY_B, KEY_B);
    first = (strcmp(s, "\x7f") == 0 && k->out_len == 1);
    CHECK(first, "B emits a lone 0x7f (Backspace) on the down edge");

    /* Hold: keyboard.h promises "D-pad → arrow keys with accelerating
     * auto-repeat" and keyboard.c puts B on the same ramp, so a 60-frame
     * hold (1 s at 60 fps) must delete more than the one we already got. */
    for (int i = 0; i < 60; i++) {
        const char *r = frame(k, 0, KEY_B);
        if (r[0] == 0x7f && k->out_len == 1) repeats++;
    }
    CHECK(repeats >= 5, "holding B for 60 frames repeats the delete %d more times",
          repeats);

    keyboard_free(k);
}

/* ── D-pad → arrow keys ──────────────────────────────────────────── */

static void check_dpad(void) {
    keyboard_t *k = keyboard_init();
    u32 dirs[4]  = { KEY_DUP, KEY_DDOWN, KEY_DRIGHT, KEY_DLEFT };
    const char *want[4] = { "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D" };
    const char *name[4] = { "Up", "Down", "Right", "Left" };
    int all = 1;

    for (int i = 0; i < 4; i++) {
        keyboard_t *kk = keyboard_init();
        const char *got = frame(kk, dirs[i], dirs[i]);
        if (strcmp(got, want[i]) != 0) {
            all = 0;
            fprintf(stderr, "  D-pad %s gave %s, expected %s\n",
                    name[i], got, want[i]);
        }
        keyboard_free(kk);
    }
    CHECK(all, "D-pad Up/Down/Right/Left emit ESC [ A / B / C / D");

    /* Two directions at once: keyboard.c documents "priority Up > Down >
     * Right > Left", so a diagonal Up+Down must read as Up. */
    const char *diag = frame(k, KEY_DUP | KEY_DDOWN, KEY_DUP | KEY_DDOWN);
    CHECK(strcmp(diag, "\x1b[A") == 0,
          "D-pad Up+Down together resolve to Up (documented priority)");

    keyboard_free(k);
}

/* ── Y = Ctrl, X = Alt: hold semantics ───────────────────────────── */

static void check_modifier_hold(void) {
    keyboard_t *k = keyboard_init();

    /* "The modifier keys (Y/X) don't produce any byte themselves." */
    CHECK(frame(k, KEY_Y, KEY_Y)[0] == 0 && keyboard_ctrl_active(k) == 1,
          "Y on its own emits nothing but arms Ctrl");

    /* Release: Ctrl must drop with the button (Hold mode is the default). */
    frame(k, 0, 0);
    CHECK(keyboard_ctrl_active(k) == 0,
          "releasing Y in Hold mode disarms Ctrl");

    /* keyboard.h: "Y + letter → Ctrl-letter (a → 0x01 …)", and
     * "Y + special → Ctrl-form where applicable (Ctrl-[ = 0x1b, etc.)". */
    frame(k, KEY_Y, KEY_Y);
    struct { char base; char want; const char *what; } ctl[] = {
        { 'c', 0x03, "Ctrl-C is 0x03 (SIGINT)" },
        { '[', 0x1b, "Ctrl-[ is 0x1b (Esc)" },
        { '?', 0x7f, "Ctrl-? is 0x7f (Rubout)" },
    };
    for (size_t i = 0; i < sizeof ctl / sizeof ctl[0]; i++) {
        const char *s = keyboard_emit_for_ex(k, ctl[i].base, 0);
        CHECK(s && s[0] == ctl[i].want && k->out_len == 1, "%s", ctl[i].what);
        frame(k, 0, KEY_Y);   /* keep Y held across the emissions */
    }

    /* keyboard.h: "X + char → Alt-char = ESC then char (2-byte sequence)". */
    frame(k, 0, 0);
    frame(k, KEY_X, KEY_X);
    const char *alt = keyboard_emit_for_ex(k, 'f', 0);
    CHECK(alt && strcmp(alt, "\x1b" "f") == 0 && k->out_len == 2,
          "X + f emits ESC f (Alt-f), a 2-byte sequence");
    CHECK(strcmp(keyboard_status_label(k), "ALT") == 0,
          "the status chip reads ALT while X is held");

    /* Shift (the on-screen key) uppercases letters only. */
    keyboard_t *k2 = keyboard_init();
    CHECK(strcmp(keyboard_emit_for_ex(k2, 'a', 1), "A") == 0 &&
          strcmp(keyboard_emit_for_ex(k2, 'a', 0), "a") == 0,
          "shifted 'a' emits 'A'; unshifted emits 'a'");
    keyboard_free(k2);

    keyboard_free(k);
}

/* ── sticky (latch) modifiers ────────────────────────────────────── */

static void check_sticky(void) {
    keyboard_t *k = keyboard_init();
    keyboard_set_sticky_mode(k, 1);

    /* Tap Y once → Ctrl latched, and it survives the release. */
    frame(k, KEY_Y, KEY_Y);
    frame(k, 0, 0);
    CHECK(keyboard_ctrl_active(k) == 1,
          "sticky mode: one tap of Y latches Ctrl across the release");

    /* One character consumes the latch — that is what "sticky" means. */
    const char *s = keyboard_emit_for_ex(k, 'z', 0);
    CHECK(s && s[0] == 0x1a, "sticky Ctrl + z emits 0x1a");
    CHECK(keyboard_ctrl_active(k) == 0,
          "the latched Ctrl is consumed by the first character");
    CHECK(strcmp(keyboard_emit_for_ex(k, 'z', 0), "z") == 0,
          "the next character is plain 'z' again");

    /* A second tap of Y toggles the latch back off. */
    frame(k, KEY_Y, KEY_Y);
    frame(k, 0, 0);
    CHECK(keyboard_ctrl_active(k) == 1, "a second tap re-latches Ctrl");
    frame(k, KEY_Y, KEY_Y);
    frame(k, 0, 0);
    CHECK(keyboard_ctrl_active(k) == 0, "a third tap clears the latch again");

    keyboard_free(k);
}

/* ── documented behaviour this build does not deliver ─────────────── */

static void check_ctrl_space(void) {
    keyboard_t *k = keyboard_init();
    frame(k, KEY_Y, KEY_Y);
    const char *s = keyboard_emit_for_ex(k, ' ', 0);

    /* to_ctrl() maps ' ' and '@' to 0x00, and it used to return 0 for
     * "no sensible mapping" as well — so keyboard_emit_for_ex() read the
     * NUL as the sentinel and fell back to the literal, and Ctrl-Space
     * quietly sent 0x20.  to_ctrl() now says -1 for "no mapping"; this
     * case is the one that proves the distinction is live. */
    CHECK(s && s[0] == 0x00 && k->out_len == 1,
          "Ctrl-Space emits NUL, and reports one byte even though "
          "strlen(out) is 0 — which is why the send path counts bytes");

    frame(k, KEY_Y, KEY_Y);
    s = keyboard_emit_for_ex(k, '@', 0);
    CHECK(s && s[0] == 0x00 && k->out_len == 1,
          "Ctrl-@ is the same NUL (the other key that maps to 0x00)");

    keyboard_free(k);
}

/* ── IME branches: A dumps the buffer, B chews it ────────────────── */

static void check_ime_branches(void) {
    keyboard_t *k = keyboard_init();
    ime_t *ime;

    /* "A in IME mode … dumps the pinyin buffer to SSH as raw ASCII
     * letters and clears the buffer." */
    ime = fresh_ime(k, "wo");
    if (ime) {
        const char *s = frame(k, KEY_A, KEY_A);
        CHECK(strcmp(s, "wo") == 0,
              "A with a pinyin buffer sends the raw letters ('wo'), not \\r");
        CHECK(ime_buffer_len(ime) == 0,
              "A with a pinyin buffer clears that buffer");
        ime_free(ime);
    }

    /* "In IME mode, B chews the pinyin buffer instead." — no 0x7f reaches
     * the SSH pipe while a pinyin buffer is live. */
    ime = fresh_ime(k, "wo");
    if (ime) {
        const char *s = frame(k, KEY_B, KEY_B);
        CHECK(s[0] == 0 && ime_buffer_len(ime) == 1,
              "B with a pinyin buffer backspaces it and emits no 0x7f");
        ime_free(ime);
    }

    /* Empty buffer → B is the ordinary Backspace again. */
    ime = fresh_ime(k, "");
    if (ime) {
        CHECK(strcmp(frame(k, KEY_B, KEY_B), "\x7f") == 0,
              "B with an empty pinyin buffer emits 0x7f again");
        ime_free(ime);
    }

    keyboard_free(k);
}

/* ── the on-screen ↑ key ─────────────────────────────────────────── */

static void check_enter_keys(void) {
    keyboard_t *k = keyboard_init();

    CHECK(strcmp(keyboard_enter_key(k), "\r") == 0 && k->out_len == 1,
          "the on-screen Enter key emits a lone \\r");

    /* "↑ with an active pinyin buffer: commit the currently-selected
     * candidate and append '\r' as ONE atomic emission." */
    ime_t *ime = fresh_ime(k, "wo");
    if (ime) {
        const char *s = keyboard_commit_enter(k);
        CHECK(s && strcmp(s, "\xe6\x88\x91\r") == 0 && k->out_len == 4,
              "Enter with a pinyin buffer commits 我 + \\r as one emission");
        ime_free(ime);
    }

    keyboard_free(k);
}

int main(void) {
    if (ime_test_dict_write(IME_FIXTURE) != 0) {
        fprintf(stderr, "FAIL: cannot write %s\n", IME_FIXTURE);
        return 1;
    }

    check_plain_bindings();
    check_backspace_repeat();
    check_dpad();
    check_modifier_hold();
    check_sticky();
    check_ctrl_space();
    check_ime_branches();
    check_enter_keys();

    printf("\n%d check(s), %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
