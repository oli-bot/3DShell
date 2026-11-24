#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/keychain_protocol.h"
#include "../source/terminal.h"

/* terminal.c only needs this one font-atlas query.  Protocol tests use ASCII. */
int font_is_wide(uint32_t codepoint) {
    (void)codepoint;
    return 0;
}

static int failures = 0;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        failures++; \
    } \
} while (0)

static char cell_char(terminal_t *term, int x, int y) {
    return (char)terminal_get_cell(term, x, y).codepoint;
}

static uint32_t cell_cp(terminal_t *term, int x, int y) {
    return terminal_get_cell(term, x, y).codepoint;
}

int main(void) {
    terminal_t *term = terminal_init(4, 3);
    CHECK(term != NULL, "terminal_init failed");
    if (!term) return 1;

    /* A fourth line must scroll the active screen, leaving input on the
     * physical bottom row instead of repainting at row 1. */
    terminal_write(term, "1\r\n2\r\n3\r\n4");
    CHECK(cell_char(term, 0, 0) == '2', "scroll row 1 should contain 2");
    CHECK(cell_char(term, 0, 1) == '3', "scroll row 2 should contain 3");
    CHECK(cell_char(term, 0, 2) == '4', "scroll row 3 should contain 4");
    CHECK(term->cur_y == 2 && term->cur_x == 1,
          "cursor should remain after 4 on the bottom row");

    /* fish uses CPR (CSI 6n) to decide where to redraw its input line. */
    terminal_write(term, "\x1b[6n");
    char reply[128] = {0};
    int n = terminal_take_response(term, reply, sizeof(reply));
    CHECK(n == 6 && memcmp(reply, "\x1b[3;2R", 6) == 0,
          "CSI 6n should report row 3, column 2");
    CHECK(terminal_take_response(term, reply, sizeof(reply)) == 0,
          "terminal response should be consumed");

    /* Local startup banners are discarded before the remote PTY begins; CPR
     * must then use the remote screen's clean origin, not the local banner's
     * former cursor row. */
    terminal_reset(term);
    terminal_write(term, "\x1b[6n");
    memset(reply, 0, sizeof(reply));
    n = terminal_take_response(term, reply, sizeof(reply));
    CHECK(n == 6 && memcmp(reply, "\x1b[1;1R", 6) == 0,
          "terminal reset should restore remote CPR origin");

    terminal_write(term, "\x1b[5n\x1b[c\x1b[>c");
    memset(reply, 0, sizeof(reply));
    n = terminal_take_response(term, reply, sizeof(reply));
    CHECK(n == 20 &&
          memcmp(reply, "\x1b[0n\x1b[?1;2c\x1b[>0;0;0c", 20) == 0,
          "status and device-attribute replies should be queued in order");

    /* Fish 4 enables xterm modifyOtherKeys and Kitty keyboard enhancement at
     * every prompt. CSI = 5 u is not CSI u (restore cursor): confusing the
     * two is what made fish repaint all typed input on terminal row 1. */
    terminal_write(term, "prompt\r\n> ");
    int fish_x = term->cur_x;
    int fish_y = term->cur_y;
    uint8_t fish_flags = term->cur_flags;
    terminal_write(term, "\x1b[>4;1m\x1b[=5u");
    CHECK(term->cur_x == fish_x && term->cur_y == fish_y,
          "fish keyboard protocol must not restore cursor to row 1");
    CHECK(term->cur_flags == fish_flags,
          "xterm modifyOtherKeys must not be parsed as SGR");

    /* Startup synchronization markers travel inside OSC so the raw SSH
     * bootstrap can detect them without leaking READY text onto the screen. */
    terminal_write(term, "\x1b]777;DSSH_READY_SHELL\x07");
    CHECK(term->cur_x == fish_x && term->cur_y == fish_y,
          "OSC readiness marker must not move the cursor");
    CHECK(cell_char(term, fish_x, fish_y) == ' ',
          "OSC readiness marker must not render visible text");

    /* An interactive shell echoes the printable command before security asks
     * for a password. Only the emitted ESC-prefixed OSC is a result marker. */
    const char echoed_result[] =
        "printf \"\\033]777;DSSH_KEYCHAIN_RESULT unlock=%d verify=%d\"";
    const char emitted_result[] =
        DSSH_KEYCHAIN_RESULT_MARKER " unlock=0 verify=0\x07";
    CHECK(strstr(echoed_result, DSSH_KEYCHAIN_RESULT_MARKER) == NULL,
          "echoed keychain command must not look like a result marker");
    CHECK(strstr(emitted_result, DSSH_KEYCHAIN_RESULT_MARKER) == emitted_result,
          "emitted keychain OSC must match the result marker");

    terminal_write(term, "\x1b[s\x1b[1;1H\x1b[u");
    CHECK(term->cur_x == fish_x && term->cur_y == fish_y,
          "parameterless CSI s/u should still save and restore cursor");

    /* ── terminal_resize (ZR dual-screen toggling) ──
     * Growing rows must keep the top-left overlap so the top screen layout
     * survives a ZR toggle; shrinking keeps only the top rows. */
    CHECK(term->rows == 3, "precondition: terminal is 3 rows");
    terminal_write(term, "\x1b[2J\x1b[HT\r\nO\r\nP");
    terminal_resize(term, 4, 6);
    CHECK(term->cols == 4 && term->rows == 6, "resize grows to 4×6");
    CHECK(cell_char(term, 0, 0) == 'T' && cell_char(term, 0, 1) == 'O' &&
          cell_char(term, 0, 2) == 'P',
          "resize preserves the top-left overlap");
    CHECK(cell_char(term, 0, 3) == ' ' && cell_char(term, 0, 5) == ' ',
          "resize clears the freshly added rows");
    terminal_write(term, "\x1b[10;1H");    /* row 10 doesn't exist → clamp */
    CHECK(term->cur_y == 5 && term->cur_x == 0,
          "resize clamps the cursor to the new grid");
    terminal_resize(term, 4, 3);
    CHECK(term->rows == 3, "resize shrinks back to 4×3");
    CHECK(cell_char(term, 0, 0) == 'T' && cell_char(term, 0, 1) == 'O' &&
          cell_char(term, 0, 2) == 'P',
          "shrink keeps the top overlap");

    /* A column-width change must drop the scrollback ring (its rows are
     * stored at the old width) and snap the view back to live. */
    terminal_write(term, "\x1b[2J\x1b[H");
    terminal_write(term, "1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7");
    CHECK(term->sb_size > 0, "precondition: scrollback has rows");
    terminal_scroll_view(term, 2);
    CHECK(term->sb_offset == 2, "precondition: scrolled back 2 rows");
    terminal_resize(term, 5, 3);
    CHECK(term->cols == 5 && term->rows == 3, "width change applied");
    CHECK(term->sb_size == 0, "width change clears scrollback");
    CHECK(term->sb_offset == 0, "resize snaps scroll offset back to live");

    /* A row-only resize (the real 66×20 ↔ 66×40 toggle) keeps scrollback. */
    terminal_write(term, "a\r\nb\r\nc\r\nd");
    CHECK(term->sb_size > 0, "precondition: scrollback repopulated");
    int sb_before = term->sb_size;
    terminal_scroll_view(term, 1);
    terminal_resize(term, 5, 4);
    CHECK(term->cols == 5 && term->rows == 4, "row-only resize applied");
    CHECK(term->sb_size == sb_before,
          "same-width resize preserves scrollback");
    CHECK(term->sb_offset == 0, "resize snaps scroll offset to live");

    /* The alternate screen (?1049) survives a resize, and swap still works. */
    terminal_write(term, "\x1b[?1049h");
    terminal_write(term, "X");
    terminal_resize(term, 5, 4);
    CHECK(term->use_alt == 1, "resize keeps the alternate screen active");
    CHECK(cell_char(term, 0, 0) == 'X',
          "alternate-screen content kept across resize");
    terminal_write(term, "\x1b[?1049l");
    CHECK(term->use_alt == 0, "leaving the alternate screen after resize");

    /* ── theme switching ────────────────────────────────────────────
     * Theme swaps must remap every cell that held the old default bg
     * (including scrollback and the alt screen) and must give newly
     * cleared cells the new bg.  The renderer skips drawing a cell's
     * bg when it equals terminal_get_bg, so a stale bg would either
     * over-draw or leave a visible seam. */
    {
        terminal_t *tm = terminal_init(4, 3);
        CHECK(tm != NULL, "theme fixture init failed");
        if (tm) {
            uint32_t dark_bg = terminal_get_bg(tm);
            CHECK(dark_bg == 0x1a1b26ff, "initial theme should be DARK");
            terminal_write(tm, "A");
            uint32_t cell_bg_dark = terminal_get_cell(tm, 0, 0).bg;
            CHECK(cell_bg_dark == dark_bg, "cell bg should be dark initially");
            /* Switch to MATRIX — existing cells should be remapped. */
            terminal_set_theme(tm, THEME_MATRIX);
            CHECK(terminal_get_theme(tm) == THEME_MATRIX, "theme should be MATRIX");
            uint32_t matrix_bg = terminal_get_bg(tm);
            CHECK(matrix_bg == 0x0d1117ff, "MATRIX bg value");
            CHECK(terminal_get_cell(tm, 0, 0).bg == matrix_bg,
                  "existing cell bg should be remapped to MATRIX");
            CHECK(tm->cur_bg == matrix_bg && tm->cur_fg == 0x38ef7dff,
                  "cursor colors should track MATRIX theme");
            /* Newly cleared rows should use the new bg. */
            terminal_write(tm, "\x1b[2J\x1b[H");
            CHECK(terminal_get_cell(tm, 0, 0).bg == matrix_bg,
                  "cleared cell should have MATRIX bg");
            CHECK(terminal_get_cell(tm, 3, 2).bg == matrix_bg,
                  "far cell should have MATRIX bg after clear");
            /* Scrollback created under DARK should be remapped too. */
            terminal_set_theme(tm, THEME_DARK);
            terminal_write(tm, "\x1b[2J\x1b[H");
            terminal_write(tm, "1\r\n2\r\n3\r\n4\r\n5\r\n");
            int had_scroll = tm->sb_size > 0;
            CHECK(had_scroll, "precondition: scrollback has rows for theme remap");
            terminal_set_theme(tm, THEME_MONO);
            uint32_t mono_bg = terminal_get_bg(tm);
            CHECK(mono_bg == 0x000000ff, "MONO bg value");
            if (had_scroll) {
                terminal_scroll_view(tm, 1);
                CHECK(terminal_get_cell(tm, 0, 0).bg == mono_bg,
                      "scrollback cell should be remapped to MONO");
                terminal_scroll_view(tm, -1);
            }
            /* Alt screen survives a theme switch and new alt cells are MONO. */
            terminal_write(tm, "\x1b[?1049h");
            terminal_write(tm, "Z");
            /* The X on entry kept MONO bg (it was remapped), new Z already MONO. */
            CHECK(terminal_get_cell(tm, 0, 0).bg == mono_bg,
                  "alt-screen cell should have MONO bg");
            terminal_set_theme(tm, THEME_SOLAR);
            uint32_t solar_bg = terminal_get_bg(tm);
            CHECK(solar_bg == 0x002b36ff, "SOLAR bg value");
            CHECK(terminal_get_cell(tm, 0, 0).bg == solar_bg,
                  "alt-screen cell should be remapped to SOLAR");
            terminal_write(tm, "\x1b[?1049l");
            CHECK(terminal_get_cell(tm, 0, 0).bg == solar_bg,
                  "main screen should be SOLAR after leaving alt");
            /* No-op theme switch should be a no-op. */
            terminal_set_theme(tm, THEME_SOLAR);
            CHECK(terminal_get_theme(tm) == THEME_SOLAR, "re-setting same theme should stick");
            terminal_free(tm);
        }
    }

    /* ── terminal_write_bytes: UTF-8 reassembly across SSH reads ──────
     * An SSH packet boundary can land in the middle of a multibyte
     * codepoint; the continuation stash rides on the terminal instance so
     * several shell channels multiplexed on one session can interleave
     * without cross-posting a fragment into the wrong window. */
    {
        terminal_t *t1 = terminal_init(20, 3);
        terminal_t *t2 = terminal_init(20, 3);
        CHECK(t1 != NULL && t2 != NULL, "utf8 fixture init failed");
        if (t1 && t2) {
            /* 你 = E4 BD A0 delivered as 2 bytes then 1. */
            terminal_write_bytes(t1, "\xe4\xbd", 2);
            CHECK(t1->utf8_frag_len == 2, "split codepoint should be stashed");
            CHECK(cell_cp(t1, 0, 0) == ' ',
                  "an incomplete codepoint must not render yet");
            terminal_write_bytes(t1, "\xa0", 1);
            CHECK(cell_cp(t1, 0, 0) == 0x4f60,
                  "stashed bytes plus the tail should decode to 你");
            CHECK(t1->utf8_frag_len == 0, "stash should drain on completion");

            /* A complete codepoint needs no stash at all. */
            terminal_write_bytes(t1, "\xe5\xa5\xbd", 3);
            CHECK(cell_cp(t1, 1, 0) == 0x597d, "whole codepoint renders");
            CHECK(t1->utf8_frag_len == 0, "complete feed must not stash");

            /* 4-byte codepoint split three ways. */
            terminal_write_bytes(t1, "\xf0", 1);
            terminal_write_bytes(t1, "\x9f\x98", 2);
            CHECK(cell_cp(t1, 2, 0) == ' ', "incomplete 4-byte stays hidden");
            terminal_write_bytes(t1, "\x80", 1);
            CHECK(cell_cp(t1, 2, 0) == 0x1f600,
                  "4-byte codepoint reassembled across three reads");

            /* Interleaving is the actual regression: with a process-wide
             * stash, t1's trailing E4 BD would be prepended to t2's ASCII
             * and both windows would show the wrong glyphs. */
            terminal_reset(t1);
            terminal_reset(t2);
            terminal_write_bytes(t1, "A\xe4\xbd", 3);
            terminal_write_bytes(t2, "BC", 2);
            terminal_write_bytes(t1, "\xa0Z", 2);
            CHECK(cell_cp(t1, 0, 0) == 'A' && cell_cp(t1, 1, 0) == 0x4f60 &&
                      cell_cp(t1, 2, 0) == 'Z' && cell_cp(t1, 3, 0) == ' ',
                  "t1 must show only its own reassembled text");
            CHECK(cell_cp(t2, 0, 0) == 'B' && cell_cp(t2, 1, 0) == 'C' &&
                      cell_cp(t2, 2, 0) == ' ',
                  "t2 must be unaffected by t1's pending fragment");

            /* Half a codepoint on a dead screen must not leak into the next. */
            terminal_reset(t1);
            terminal_write_bytes(t1, "x\xe5\xa5", 3);
            CHECK(t1->utf8_frag_len == 2, "precondition: fragment pending");
            terminal_reset(t1);
            CHECK(t1->utf8_frag_len == 0, "reset drops the pending fragment");
            terminal_write_bytes(t1, "\xe5\xa5\xbd", 3);
            CHECK(cell_cp(t1, 0, 0) == 0x597d && cell_cp(t1, 1, 0) == ' ',
                  "text after reset must not carry the dropped fragment");

            terminal_free(t1);
            terminal_free(t2);
        }
    }

    /* ── scrollback ring vs. a runtime column count ───────────────────
     * The ring is one flat cols×TERM_SCROLLBACK block.  It used to be
     * allocated from the initial width while both the writer and the reader
     * indexed it with the *current* cols — so a 33-column window later
     * resized to 66 wrote 66-cell rows into a 33-cell stride, walked past
     * the end of the allocation on every scroll and read past it on every
     * scrollback peek.  Unreachable only while cols is compile-time fixed. */
    {
        terminal_t *s = terminal_init(33, 10);
        CHECK(s != NULL, "33-col terminal_init failed");
        if (s) {
            char line[64];
            for (int i = 0; i < TERM_SCROLLBACK + 50; i++) {
                snprintf(line, sizeof(line), "l%04d\r\n", i);
                terminal_write(s, line);
            }
            CHECK(s->sb_size == TERM_SCROLLBACK, "ring filled at 33 cols");
            terminal_scroll_view(s, TERM_SCROLLBACK);
            CHECK(cell_char(s, 0, 0) == 'l', "scrollback readable at 33 cols");
            terminal_scroll_view(s, -TERM_SCROLLBACK);

            /* Widen: the allocation must follow the new stride, the stale
             * rows are dropped, and the next 500 lines land inside it. */
            terminal_resize(s, 66, 10);
            CHECK(s->sb_cols == 66, "widening updates the scrollback stride");
            CHECK(s->sb_size == 0, "width change drops the old rows");
            for (int i = 0; i < TERM_SCROLLBACK + 50; i++) {
                snprintf(line, sizeof(line), "w%04d\r\n", i);
                terminal_write(s, line);
            }
            CHECK(s->sb_size == TERM_SCROLLBACK, "ring refills at 66 cols");
            terminal_scroll_view(s, 10);
            CHECK(cell_char(s, 0, 0) == 'w', "scrollback readable at 66 cols");
            terminal_scroll_view(s, -10);

            /* Narrowing must shrink the stride too, and a rows-only change
             * must leave the ring completely alone. */
            terminal_resize(s, 33, 10);
            CHECK(s->sb_cols == 33, "narrowing updates the scrollback stride");
            for (int i = 0; i < 300; i++) {
                snprintf(line, sizeof(line), "k%04d\r\n", i);
                terminal_write(s, line);
            }
            int kept = s->sb_size;
            CHECK(kept > 0, "precondition: rows stored at 33 cols");
            terminal_resize(s, 33, 20);
            CHECK(s->sb_cols == 33 && s->sb_size == kept,
                  "rows-only resize keeps the ring");

            /* Enough churn to wrap the ring twice over. */
            for (int i = 0; i < 1200; i++) {
                snprintf(line, sizeof(line), "n%05d\r\n", i);
                terminal_write(s, line);
            }
            CHECK(s->sb_size == TERM_SCROLLBACK, "ring wraps without growing");
            terminal_scroll_view(s, 20);
            CHECK(cell_char(s, 0, 0) == 'n', "wrapped ring reads in bounds");
            terminal_free(s);
        }
    }

    /* ── Malformed CSI parameters ───────────────────────────────────────
     * '-' (0x2d) is swallowed by the 0x20-0x3f intermediate range but is
     * never a legal *parameter*, so strtol could hand back a negative count.
     * Each cursor case then moved the cursor off the grid — `ESC[-5B` drove
     * cur_y to -5 — and the next put_char indexed cells[cur_y*cols+cur_x]
     * outside the allocation.  ASan (see tools/test_terminal.sh) is what
     * turns these into failures; without it a corrupt heap keeps running.
     *
     * The resize tests above leave `term` at 5x4; pin it so the row
     * arithmetic below does not depend on that. */
    terminal_resize(term, 6, 4);
    static const char *const bad_csi[] = {
        "\x1b[-5A", "\x1b[-5B", "\x1b[-5C", "\x1b[-5D",
        "\x1b[-5E", "\x1b[-5F", "\x1b[-5G", "\x1b[-5d",
        "\x1b[-5;1H", "\x1b[1;-5H",
    };
    for (unsigned i = 0; i < sizeof(bad_csi) / sizeof(bad_csi[0]); i++) {
        terminal_reset(term);
        terminal_write(term, bad_csi[i]);
        CHECK(term->cur_x >= 0 && term->cur_x < term->cols &&
              term->cur_y >= 0 && term->cur_y < term->rows,
              "a negative CSI parameter must leave the cursor in the grid");
        terminal_write(term, "X");
        CHECK(term->cur_y >= 0 && term->cur_y < term->rows &&
              term->cur_x > 0 && term->cur_x <= term->cols,
              "writing after a negative CSI parameter stays in the grid");
    }

    /* Char ops with a negative count computed a negative memmove length and
     * walked off the row; a negative SGR index selected palette[-5]. */
    terminal_reset(term);
    terminal_write(term, "\x1b[-5P\x1b[-5@\x1b[-5X");
    terminal_write(term, "abc");
    CHECK(cell_char(term, 0, 0) == 'a' && cell_char(term, 1, 0) == 'b',
          "negative P/@/X must not disturb the line");
    terminal_reset(term);
    terminal_write(term, "\x1b[38;5;-5m\x1b[48;5;-5mZ");
    CHECK(cell_char(term, 0, 0) == 'Z',
          "a negative 256-colour index must stay in the palette");

    /* ── Scroll region (DECSTBM) ────────────────────────────────────────
     * scroll_up_one memmoves (bot - top) * cols cells, so an inverted or
     * out-of-range region produced a negative length — a huge size_t. */
    terminal_reset(term);
    terminal_write(term, "\x1b[9999;1r");     /* top past the last row */
    CHECK(term->scroll_top <= term->scroll_bottom,
          "an out-of-range DECSTBM must not invert the region");
    terminal_write(term, "a\r\nb\r\nc\r\n\x1b[S\x1b[T");
    CHECK(term->scroll_top <= term->scroll_bottom,
          "scrolling after an out-of-range DECSTBM stays ordered");

    terminal_reset(term);
    terminal_write(term, "\x1b[3;1r");        /* top 3, bottom 1: inverted */
    CHECK(term->scroll_top <= term->scroll_bottom,
          "an inverted DECSTBM must be ignored, not stored");
    terminal_write(term, "a\r\nb\r\nc\r\n\x1b[2S\x1b[2T\x1b[1L\x1b[1M");
    CHECK(term->scroll_top <= term->scroll_bottom,
          "line ops after an inverted DECSTBM stay ordered");

    /* ── L/M with the cursor at or below the region's last row ──────────
     * The cursor is clamped to the grid, not to the region, so cur_y can
     * sit below scroll_bottom; (bot - cur_y) then went negative, and at the
     * last row the source pointer was already one row past the end. */
    terminal_reset(term);
    terminal_write(term, "\x1b[1;1r");        /* region is row 0 only */
    terminal_write(term, "\x1b[3;1H");        /* cursor below it */
    terminal_write(term, "\x1b[1L\x1b[1M\x1b[9L\x1b[9M");
    CHECK(term->cur_y >= 0 && term->cur_y < term->rows,
          "L/M below the scroll region must leave the cursor in the grid");
    terminal_write(term, "Z");
    CHECK(term->cur_y >= 0 && term->cur_y < term->rows,
          "writing after L/M below the region stays in the grid");

    /* On the region's last row the pushed-out line leaves the region, so
     * that row ends up blank — the new guard must keep that behaviour. */
    terminal_reset(term);
    terminal_write(term, "abc\r\nxyz\r\npqr\r\nuvw");   /* rows 0..3 */
    terminal_write(term, "\x1b[4;1H");        /* == scroll_bottom */
    terminal_write(term, "\x1b[1L");
    CHECK(cell_char(term, 0, 3) == ' ', "insert-lines on the last row blanks it");
    CHECK(cell_char(term, 0, 2) == 'p', "insert-lines on the last row keeps the row above");
    terminal_write(term, "\x1b[4;1H\x1b[1M");
    CHECK(cell_char(term, 0, 3) == ' ', "delete-lines on the last row blanks it");
    CHECK(cell_char(term, 0, 2) == 'p', "delete-lines on the last row keeps the row above");

    /* ── Save/restore cursor across a resize ────────────────────────────
     * ESC 7 / ESC 8 (and CSI s/u, DEC 1048/1049) restore straight into
     * cur_x/cur_y with no clamp of their own, so a row saved in the tall
     * dual-screen grid was restored into the short one and the next
     * put_char indexed rows past the end of the new allocation.  The
     * START+L toggle is what makes this reachable in practice. */
    {
        terminal_t *tall = terminal_init(20, 12);
        CHECK(tall != NULL, "12-row terminal_init failed");
        if (tall) {
            terminal_write(tall, "\x1b[12;1H");   /* last row of the tall grid */
            terminal_write(tall, "\x1b" "7");     /* ESC 7 — save */
            terminal_resize(tall, 20, 4);         /* shrink, as START+L does */
            terminal_write(tall, "\x1b" "8");     /* ESC 8 — restore */
            CHECK(tall->cur_y >= 0 && tall->cur_y < tall->rows,
                  "ESC 8 after a shrink must land inside the new grid");
            terminal_write(tall, "X");
            CHECK(tall->cur_y >= 0 && tall->cur_y < tall->rows,
                  "writing after ESC 8 across a shrink stays in the grid");

            terminal_resize(tall, 20, 12);        /* back to the tall grid */
            terminal_write(tall, "\x1b[12;1H\x1b[s");
            terminal_resize(tall, 20, 4);
            terminal_write(tall, "\x1b[u");
            CHECK(tall->cur_y >= 0 && tall->cur_y < tall->rows,
                  "CSI u after a shrink must land inside the new grid");
            terminal_write(tall, "Y");
            CHECK(tall->cur_y >= 0 && tall->cur_y < tall->rows,
                  "writing after CSI u across a shrink stays in the grid");
            terminal_free(tall);
        }
    }

    /* ── SU / SD with a huge repeat count ────────────────────────────────
     * TERM_PARAM_MAX bounds the *value* of a parameter, not the *work*.
     * Each scroll iteration is a full-region memmove, so an unclamped
     * ESC[9999S was ~309 MB of memmove inside one frame on the 66x40
     * dual-screen grid — tens of seconds of frozen UI on the ARM11.
     * `printf '\e[9999S'` is all it takes to trigger.
     * The fix clamps the count to the region height, which is equivalent
     * (everything has already scrolled out) and caps the work at one region.
     * Asserted as "same result as an explicit full-height scroll" rather
     * than by timing, so the test cannot go flaky on a loaded machine. */
    {
        static const char *const seq[] = { "\x1b[9999S", "\x1b[4S" };
        terminal_t *grid[2];

        for (int k = 0; k < 2; k++) {
            terminal_t *s = terminal_init(6, 4);
            CHECK(s != NULL, "terminal_init failed for the scroll test");
            if (!s) return 1;
            terminal_write(s, "aaa\r\nbbb\r\nccc\r\nddd\x1b[H");
            terminal_write(s, seq[k]);

            /* Scrolling a 4-row region 4 (or 9999) times has moved every
             * line out, so the whole grid must be blank either way. */
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 6; x++) {
                    char m[64];
                    snprintf(m, sizeof(m), "huge scroll must clear row %d (seq %d)", y, k);
                    CHECK(cell_char(s, x, y) == ' ', m);
                }
            grid[k] = s;
        }

        /* And the two must agree cell for cell — that is what "the huge
         * count collapsed to the clamped one" actually means. */
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 6; x++)
                CHECK(cell_cp(grid[0], x, y) == cell_cp(grid[1], x, y),
                      "a huge SU count must equal a full-region SU");

        /* The blank-grid check above cannot tell 4 iterations from 9999 —
         * both end with everything scrolled out.  scroll_up_one() pushes one
         * row into the scrollback ring per iteration (top == 0 here), so the
         * ring's size is a *deterministic* count of how much work happened.
         * Region height is 4, so the clamped loop must push exactly 4 rows;
         * an unclamped one would push 9999 and saturate the ring at
         * TERM_SCROLLBACK.  Measured this way the test cannot go flaky. */
        CHECK(grid[0]->sb_size == 4,
              "a huge SU must do exactly one region's worth of work");
        CHECK(grid[1]->sb_size == 4,
              "an explicit full-height SU must push one row per line");

        terminal_free(grid[0]);
        terminal_free(grid[1]);

        /* Same for scroll-down, with a partial region so the clamp is
         * against the region height (2) and not the screen height (4):
         * rows 0 and 1 must be untouched. */
        terminal_t *d = terminal_init(6, 4);
        CHECK(d != NULL, "terminal_init failed for the SD test");
        if (!d) return 1;
        terminal_write(d, "aaa\r\nbbb\r\nccc\r\nddd");
        terminal_write(d, "\x1b[3;4r");      /* region = rows 3..4 */
        terminal_write(d, "\x1b[9999T");
        CHECK(cell_char(d, 0, 0) == 'a', "SD must not touch a row above the region");
        CHECK(cell_char(d, 0, 1) == 'b', "SD must not touch a row above the region");
        CHECK(cell_char(d, 0, 2) == ' ', "a huge SD must clear the region's top row");
        CHECK(cell_char(d, 0, 3) == ' ', "a huge SD must clear the region's bottom row");
        terminal_free(d);
    }

    /* ── Deterministic fuzz over the raw feed path ───────────────────────
     * Everything above is hand-written: it only covers the sequences I
     * thought to write.  This pass covers the ones I did not, by feeding
     * the parser bytes drawn from an alphabet weighted towards escape
     * sequences and multi-byte UTF-8, on a deliberately tiny grid so the
     * cursor and the scroll region sit on their boundaries most of the
     * time.
     *
     * It is a *seeded* fuzz on purpose: a stream that differs on every run
     * cannot be triaged and cannot be re-checked after a fix.  Under
     * AddressSanitizer (test_terminal.sh enables it, and degrades to
     * UBSan-only with a printed note if the runtime is missing) any
     * out-of-bounds write in the parser aborts the run right here. */
    {
        terminal_t *f = terminal_init(10, 5);
        CHECK(f != NULL, "terminal_init failed for the fuzz pass");
        if (!f) return 1;

        /* xorshift32 — reproducible, and good enough to walk the space. */
        uint32_t rng = 0x5eed1234u;
#define FRAND() (rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5)

        /* Bytes that reach the interesting code: ESC opens a sequence,
         * 0x20-0x3f are CSI parameters (and `-`, the one that used to
         * reach strtol), 0x40-0x7e are final bytes, and the UTF-8 leads
         * plus a continuation exercise the split-codepoint stash.  NUL and
         * CRLF are in there because a pty really does carry them. */
        static const unsigned char alphabet[] = {
            0x1b, '[', '?', '>', ';', '-', '0', '1', '5', '9',
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K',
            'L', 'M', 'P', 'S', 'T', 'X', 'b', 'd', 'f', 'h',
            'l', 'm', 'n', 'r', 's', 'u', ']', ')', '#',
            0xc3, 0xa9, 0xe4, 0xb8, 0xad, 0x80, 0x00, 'a', '\n', '\r'
        };
        const uint32_t na = (uint32_t)(sizeof(alphabet) / sizeof(alphabet[0]));

        for (int iter = 0; iter < 20000; iter++) {
            unsigned char buf[48];
            int len = 1 + (int)(FRAND() % (uint32_t)sizeof(buf));
            for (int i = 0; i < len; i++)
                buf[i] = alphabet[FRAND() % na];

            terminal_write_bytes(f, (const char *)buf, len);

            /* Reshape the grid underneath the parser now and then — the
             * START+L dual-screen toggle does exactly this mid-stream, and
             * the scroll region / saved cursor have to survive it. */
            if ((iter & 1023) == 0) {
                /* Two FRAND() calls in one expression would be unsequenced
                 * modifications of `rng` — real UB, and the compiler says so. */
                int fc = 4 + (int)(FRAND() % 20);
                int fr = 2 + (int)(FRAND() % 8);
                terminal_resize(f, fc, fr);
            }

            /* Drain query replies; otherwise response_buf fills and hides
             * everything the parser does afterwards. */
            if ((iter & 63) == 0) {
                char rep[256];
                while (terminal_take_response(f, rep, sizeof(rep)) > 0) { }
            }
            /* Home the cursor occasionally so the tiny grid keeps getting
             * exercised from the top rather than parking at the corner. */
            if ((iter & 255) == 0) terminal_write_bytes(f, "\x1b[H", 3);
        }
#undef FRAND
        terminal_free(f);
    }

    terminal_free(term);
    if (failures) {
        fprintf(stderr, "%d terminal test(s) failed\n", failures);
        return 1;
    }
    puts("terminal tests passed");
    return 0;
}
