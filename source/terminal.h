/*
 * Terminal emulation and ANSI/VT100 definitions.
 * Derived from skmtrd/3dssh (MIT License, Copyright (c) 2026 skmtrd).
 */

#pragma once
#include <stdint.h>

#define TERM_MAX_COLS   80
/* Maximum terminal height.  The single-screen top is 20 rows; the
 * dual-screen mode doubles it to 40 (top 20 + bottom 20).  The PTY is
 * renegotiated to 40 rows while that mode is active. */
#define TERM_MAX_ROWS   48
/* Scrollback rows kept in memory.  Kept small (500) on purpose — for
 * a "native SSH" experience the real history lives on the server side
 * (tmux's `set -g history-limit` typically 2000–50000).  This client
 * scrollback only matters when the user is NOT in tmux and pipes
 * something long like `cat largefile`; for those cases 500 rows is
 * plenty.  At 80 cols × 12 bytes/cell that's ~480 KB. */
#define TERM_SCROLLBACK 500
#define TERM_RESPONSE_MAX 128

typedef struct {
    uint32_t codepoint;
    uint32_t fg, bg;
    uint8_t  flags;
} term_cell_t;

#define CELL_FLAG_BOLD        (1<<0)
#define CELL_FLAG_UNDERLINE   (1<<1)
#define CELL_FLAG_REVERSE     (1<<2)
#define CELL_FLAG_BLINK       (1<<3)
#define CELL_FLAG_WIDE        (1<<4)  /* 全角文字の先頭セル */
#define CELL_FLAG_WIDE_CONT   (1<<5)  /* 全角文字の後続セル（描画スキップ） */
/* The cell's fg is the *current* theme's default foreground
 * (theme_fg[term->theme]).  Set at write time so terminal_set_theme
 * can migrate every "use the theme default" cell — including
 * non-space ones like a shell prompt's "~ %" — without ever
 * clobbering cells whose color came from an explicit ANSI SGR. */
#define CELL_FLAG_FG_DEFAULT  (1<<6)

typedef enum {
    THEME_DARK = 0,    /* Tokyo Night (深蓝暗色) */
    THEME_MATRIX,      /* Matrix Green (黑客绿) */
    THEME_MONO,        /* Monochrome (复古黑白) */
    THEME_SOLAR,       /* Solarized Dark (深海：base03 是青蓝，不是暖色) */
    /* Colours read from sdmc:/3ds/3dssh/theme.ini.  Joining the enum rather
     * than riding alongside it is what lets every existing range check in
     * the program — here, in kb_theme.c and in softkb — accept index 4
     * untouched.  The slot ships seeded with THEME_DARK's values, so a
     * missing or useless theme.ini costs nothing. */
    THEME_CUSTOM,
    THEME_COUNT,
} term_theme_t;

/* A partial override of the THEME_CUSTOM terminal slot: the setter starts
 * from THEME_DARK every time, then writes only the members whose have_*
 * flag is set.  theme.ini lists colours one at a time, so "which keys were
 * present" has to survive the trip — and terminal.c owns the seed. */
typedef struct {
    uint32_t bg, fg, ansi[16];
    uint8_t  have_bg, have_fg, have_ansi[16];
} term_custom_pal_t;

typedef struct terminal_t {
    int cols, rows;
    int cur_x, cur_y;
    int scroll_top, scroll_bottom;

    term_cell_t *cells;      // active screen
    term_cell_t *alt_cells;  // alternate screen (?1047/?1049)
    int use_alt;

    /* active theme */
    term_theme_t theme;

    /* saved cursor (main screen) */
    int      saved_x, saved_y;
    uint32_t saved_fg, saved_bg;
    uint8_t  saved_flags;

    /* scrollback */
    term_cell_t *scrollback;
    int sb_head, sb_size, sb_offset;
    /* Row stride the scrollback allocation was actually made for.  The
     * ring math must use this, never `cols`: cols is a runtime value that
     * terminal_resize changes, and indexing a cols-sized allocation with a
     * larger new cols overruns the buffer.  When a resize cannot keep them
     * equal (realloc failed) the history is simply not appended to. */
    int sb_cols;

    /* current attributes */
    uint32_t cur_fg, cur_bg;
    uint8_t  cur_flags;

    /* escape sequence parser */
    int  parse_state;
    char parse_buf[512];   /* 512 — Claude Code sends long OSC/SGR sequences */
    int  parse_len;

    int cursor_visible;
    int cursor_blink_count;

    /* xterm mouse-tracking state.  Set by parser when server emits
     *   ESC[?1000h / ?1002h / ?1003h    (any mouse tracking enabled)
     *   ESC[?1006h                       (SGR encoding, recommended by tmux)
     * and cleared by the corresponding `l` (low) variants.  When both
     * mouse_proto > 0 *and* mouse_sgr is set, the client should send
     *   ESC[<button;col;rowM
     * for every press (and "m" for release) instead of acting locally on
     * scrolls / clicks.  This is how PC terminals make tmux's mouse
     * support transparent. */
    int mouse_proto;   /* 0 = off; otherwise 1000/1002/1003 (last set wins) */
    int mouse_sgr;     /* ESC[?1006h: encode as SGR (\x1b[<...M) */

    /* Replies requested by the remote terminal application (for example
     * CSI 6n cursor-position reports used by fish's line editor).  The main
     * loop drains this buffer back into the SSH channel after parsing input. */
    char response_buf[TERM_RESPONSE_MAX];
    int  response_len;

    /* UTF-8 continuation stash: an SSH packet boundary can split a
     * multibyte codepoint, and terminal_write_bytes() carries the leading
     * 1-3 bytes over to the next read.  Per-instance on purpose — with
     * several shell channels multiplexed on one SSH session, a shared
     * buffer cross-posts CJK fragments between windows. */
    char utf8_frag[4];
    int  utf8_frag_len;
} terminal_t;

/* Convenience: true when the server has enabled any mouse tracking mode. */
static inline int terminal_mouse_enabled(const terminal_t *t) {
    return t && t->mouse_proto != 0;
}

terminal_t *terminal_init(int cols, int rows);
void        terminal_free(terminal_t *term);
void        terminal_write(terminal_t *term, const char *data);
void        terminal_write_n(terminal_t *term, const char *data, int len);

/* Raw link bytes → this terminal, reassembling any codepoint an SSH packet
 * boundary split in half.  This is what the SSH receive path calls; the
 * per-terminal stash makes it safe to interleave several shell channels.
 * `len` must not exceed TERM_FEED_MAX. */
#define TERM_FEED_MAX 2048
void        terminal_write_bytes(terminal_t *term, const char *data, int len);

void        terminal_reset(terminal_t *term);
void        terminal_resize(terminal_t *term, int cols, int rows);
void        terminal_scroll_view(terminal_t *term, int delta);
term_cell_t terminal_get_cell(terminal_t *term, int x, int y);

void         terminal_set_theme(terminal_t *term, term_theme_t theme);
term_theme_t terminal_get_theme(const terminal_t *term);
uint32_t     terminal_get_bg(const terminal_t *term);
uint32_t     terminal_get_fg(const terminal_t *term);
uint32_t     terminal_get_c2d_bg(const terminal_t *term);

/* Replace the THEME_CUSTOM slot.  theme_ini.c is its only caller, once at
 * startup — the terminal-side half of sdmc:/3ds/3dssh/theme.ini. */
void         terminal_theme_set_custom(const term_custom_pal_t *pal);

/* Copy and consume queued terminal-protocol replies. Returns bytes copied. */
int terminal_take_response(terminal_t *term, char *buf, int len);
