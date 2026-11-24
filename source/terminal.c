/*
 * Terminal emulation and ANSI/VT100 state machine.
 * Derived from skmtrd/3dssh (MIT License, Copyright (c) 2026 skmtrd).
 */

#include "terminal.h"
#include "font_atlas.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* THEME_CUSTOM is a runtime slot: theme_ini.c writes the keys its file
 * supplies on top of these DARK seeds, so a missing or unusable theme.ini
 * leaves index 4 rendering exactly like the default theme. */
static uint32_t theme_bg[THEME_COUNT] = {
    0x1a1b26ff,   /* THEME_DARK: Tokyo Night */
    0x0d1117ff,   /* THEME_MATRIX: Dark Slate/Black */
    0x000000ff,   /* THEME_MONO: Pure Black */
    0x002b36ff,   /* THEME_SOLAR: Solarized Dark */
    [THEME_CUSTOM] = 0x1a1b26ff,
};

static uint32_t theme_fg[THEME_COUNT] = {
    0xc0caf5ff,   /* THEME_DARK */
    0x38ef7dff,   /* THEME_MATRIX */
    0xe2e8f0ff,   /* THEME_MONO */
    0x839496ff,   /* THEME_SOLAR */
    [THEME_CUSTOM] = 0xc0caf5ff,
};

#define THEME_ANSI_DARK {                                                 \
    0x15161eff, 0xf7768eff, 0x9ece6aff, 0xe0af68ff,                       \
    0x7aa2f7ff, 0xbb9af7ff, 0x7dcfc0ff, 0xa9b1d6ff,                       \
    0x414868ff, 0xff7a93ff, 0xb9f27cff, 0xff9e64ff,                       \
    0x7da6ffff, 0xbb9af7ff, 0x0db9d7ff, 0xc0caf5ff,                       \
}

static uint32_t theme_palettes[THEME_COUNT][16] = {
    /* THEME_DARK (Tokyo Night) */
    THEME_ANSI_DARK,
    /* THEME_MATRIX (Matrix Phosphor Green) */
    {
        0x000000ff, 0x22c55eff, 0x4ade80ff, 0x86efacff,
        0x10b981ff, 0x059669ff, 0x34d399ff, 0xa7f3d0ff,
        0x1e293bff, 0x86efacff, 0xbbf7d0ff, 0xdcfce7ff,
        0x6ee7b7ff, 0xa7f3d0ff, 0x5eead4ff, 0xf0fdf4ff,
    },
    /* THEME_MONO (Monochrome) */
    {
        0x000000ff, 0x94a3b8ff, 0xcbd5e1ff, 0xe2e8f0ff,
        0x64748bff, 0x94a3b8ff, 0xcbd5e1ff, 0xf8fafcff,
        0x475569ff, 0x94a3b8ff, 0xcbd5e1ff, 0xe2e8f0ff,
        0x64748bff, 0x94a3b8ff, 0xcbd5e1ff, 0xffffffff,
    },
    /* THEME_SOLAR (Solarized Dark) */
    {
        0x073642ff, 0xdc322fff, 0x859900ff, 0xb58900ff,
        0x268bd2ff, 0xd33682ff, 0x2aa198ff, 0xeee8d5ff,
        0x002b36ff, 0xcb4b16ff, 0x586e75ff, 0x657b83ff,
        0x839496ff, 0x6c71c4ff, 0x93a1a1ff, 0xfdf6e3ff,
    },
    [THEME_CUSTOM] = THEME_ANSI_DARK,
};

#define DEFAULT_FG (theme_fg[0])
#define DEFAULT_BG (theme_bg[0])

#define STATE_NORMAL 0
#define STATE_ESC    1
#define STATE_CSI    2
#define STATE_OSC    3
#define STATE_SCS    4   /* waiting for charset designator after ESC ( ) * + */

static void cell_reset(terminal_t *t, term_cell_t *c) {
    c->codepoint = ' ';
    if (t && t->theme >= 0 && t->theme < THEME_COUNT) {
        c->fg = theme_fg[t->theme];
        c->bg = theme_bg[t->theme];
    } else {
        c->fg = DEFAULT_FG;
        c->bg = DEFAULT_BG;
    }
    /* A freshly reset cell is the prototype of "fg is the theme default"
     * — mark it so terminal_set_theme can migrate it across theme
     * switches. */
    c->flags = CELL_FLAG_FG_DEFAULT;
}

terminal_t *terminal_init(int cols, int rows) {
    terminal_t *t = calloc(1, sizeof(terminal_t));
    if (!t) return NULL;
    t->cols = cols; t->rows = rows;
    t->scroll_top = 0; t->scroll_bottom = rows - 1;
    t->cursor_visible = 1;
    t->cur_fg = theme_fg[0]; t->cur_bg = theme_bg[0];
    t->parse_state = STATE_NORMAL;
    t->cells = calloc(cols * rows, sizeof(term_cell_t));
    if (!t->cells) { free(t); return NULL; }
    t->scrollback = calloc(cols * TERM_SCROLLBACK, sizeof(term_cell_t));
    if (!t->scrollback) { free(t->cells); free(t); return NULL; }
    t->sb_cols = cols;
    for (int i = 0; i < cols * rows; i++) cell_reset(t, &t->cells[i]);
    return t;
}

void terminal_free(terminal_t *t) {
    if (!t) return;
    free(t->cells);
    free(t->alt_cells);
    free(t->scrollback);
    free(t);
}

void terminal_reset(terminal_t *t) {
    t->cur_x = 0; t->cur_y = 0;
    if (t->theme >= 0 && t->theme < THEME_COUNT) {
        t->cur_fg = theme_fg[t->theme];
        t->cur_bg = theme_bg[t->theme];
    } else {
        t->cur_fg = DEFAULT_FG; t->cur_bg = DEFAULT_BG;
    }
    t->cur_flags = 0;
    t->scroll_top = 0; t->scroll_bottom = t->rows - 1;
    t->cursor_visible = 1;
    t->response_len = 0;   /* drop queued query replies from the old screen */
    t->utf8_frag_len = 0;  /* and a half-received codepoint with it */
    for (int i = 0; i < t->cols * t->rows; i++) cell_reset(t, &t->cells[i]);
}

/* Resize the terminal widget.  Used by the START+L "dual-screen" toggle
 * which grows/shricks the PTY between 66×20 and 66×40.
 *
 * Semantics:
 *  - The top-left overlap of the live screen is preserved (so the top
 *    screen keeps showing exactly what was visible before the toggle;
 *    the remote repaints everything after SIGWINCH anyway).
 *  - The alternate screen (if one was ever opened) is resized the same
 *    way, keeping the two buffers interchangeable.
 *  - Scrollback rows are kept *only* when the column width is unchanged:
 *    it is a flat cols×TERM_SCROLLBACK ring, so a width change would
 *    misalign every stored line and it is dropped instead.
 *  - The scroll offset snaps back to live (a fresh insertion point). */
void terminal_resize(terminal_t *t, int cols, int rows) {
    if (!t || cols <= 0 || rows <= 0) return;
    if (cols == t->cols && rows == t->rows) return;

    int old_cols = t->cols, old_rows = t->rows;
    int keep_y = old_rows < rows ? old_rows : rows;
    int keep_x = old_cols < cols ? old_cols : cols;

    /* Main screen. */
    term_cell_t *ncells = calloc((size_t)cols * rows, sizeof(term_cell_t));
    if (!ncells) return;
    for (int i = 0; i < cols * rows; i++) cell_reset(t, &ncells[i]);
    for (int y = 0; y < keep_y; y++)
        for (int x = 0; x < keep_x; x++)
            ncells[y * cols + x] = t->cells[y * old_cols + x];
    free(t->cells);
    t->cells = ncells;

    /* Alternate screen, when it has been opened. */
    if (t->alt_cells) {
        term_cell_t *nalt = calloc((size_t)cols * rows, sizeof(term_cell_t));
        if (nalt) {
            for (int i = 0; i < cols * rows; i++) cell_reset(t, &nalt[i]);
            for (int y = 0; y < keep_y; y++)
                for (int x = 0; x < keep_x; x++)
                    nalt[y * cols + x] = t->alt_cells[y * old_cols + x];
            free(t->alt_cells);
            t->alt_cells = nalt;
        }
    }

    /* The ring is one flat cols×TERM_SCROLLBACK block, so its allocation
     * has to follow the width: resetting the counters alone left a buffer
     * sized for the old cols being indexed by the new one.  Contents are
     * still dropped, since old rows cannot be reflowed to a new width.
     * If the allocation fails, sb_cols keeps the old stride and the scroll
     * path stops storing history rather than writing past the end. */
    if (cols != old_cols) {
        term_cell_t *nsb = realloc(t->scrollback,
                                   (size_t)cols * TERM_SCROLLBACK *
                                   sizeof(term_cell_t));
        if (nsb) {
            t->scrollback = nsb;
            t->sb_cols    = cols;
        }
        t->sb_head = 0;
        t->sb_size = 0;
    }

    t->cols = cols;
    t->rows = rows;
    t->scroll_top = 0;
    t->scroll_bottom = rows - 1;
    t->sb_offset = 0;            /* snap view back to live */
    if (t->cur_x >= cols) t->cur_x = cols - 1;
    if (t->cur_y >= rows) t->cur_y = rows - 1;
    /* The save/restore-cursor pairs (ESC 7/8, CSI s/u, DEC 1048/1049) write
     * straight into cur_x/cur_y with no clamp of their own, so a position
     * saved in the tall dual-screen grid would be restored into the short one
     * and the next put_char would index rows past the end of the new
     * allocation.  START+L alone is enough to set that up, so re-clamp the
     * saved slot here rather than at each of the five restore sites. */
    if (t->saved_x < 0)     t->saved_x = 0;
    if (t->saved_y < 0)     t->saved_y = 0;
    if (t->saved_x >= cols) t->saved_x = cols - 1;
    if (t->saved_y >= rows) t->saved_y = rows - 1;
}

/* ── スクロール ── */
/* History can only be appended while the ring's row stride matches the
 * live width; after a resize whose realloc failed, lines are dropped
 * instead of being written past the end of the allocation. */
static int sb_storable(const terminal_t *t) {
    return t->scrollback && t->sb_cols == t->cols;
}

static void scroll_up_one(terminal_t *t) {
    int top = t->scroll_top, bot = t->scroll_bottom;
    if (top == 0 && sb_storable(t)) {
        int dst = t->sb_head * t->sb_cols;
        memcpy(&t->scrollback[dst], &t->cells[0], t->cols * sizeof(term_cell_t));
        t->sb_head = (t->sb_head + 1) % TERM_SCROLLBACK;
        if (t->sb_size < TERM_SCROLLBACK) t->sb_size++;
    }
    memmove(&t->cells[top * t->cols], &t->cells[(top+1) * t->cols],
            (bot - top) * t->cols * sizeof(term_cell_t));
    for (int x = 0; x < t->cols; x++) cell_reset(t, &t->cells[bot * t->cols + x]);
}

static void scroll_down_one(terminal_t *t) {
    int top = t->scroll_top, bot = t->scroll_bottom;
    memmove(&t->cells[(top+1) * t->cols], &t->cells[top * t->cols],
            (bot - top) * t->cols * sizeof(term_cell_t));
    for (int x = 0; x < t->cols; x++) cell_reset(t, &t->cells[top * t->cols + x]);
}

static void newline(terminal_t *t) {
    if (t->cur_y >= t->scroll_bottom) scroll_up_one(t);
    else t->cur_y++;
}

static void put_char(terminal_t *t, uint32_t cp) {
    int wide = font_is_wide(cp);
    /* 全角で残り1列しかない場合はスペースで埋めて折り返す */
    if (wide && t->cur_x >= t->cols - 1) { t->cur_x = 0; newline(t); }
    if (!wide && t->cur_x >= t->cols)    { t->cur_x = 0; newline(t); }

    uint32_t fg = (t->cur_flags & CELL_FLAG_REVERSE) ? t->cur_bg : t->cur_fg;
    uint32_t bg = (t->cur_flags & CELL_FLAG_REVERSE) ? t->cur_fg : t->cur_bg;

    term_cell_t *c = &t->cells[t->cur_y * t->cols + t->cur_x];
    c->codepoint = cp;
    c->fg = fg; c->bg = bg;
    /* Tag cells whose fg IS the theme default — they're safe to recolor
     * on a theme swap, while cells whose fg came from an explicit SGR
     * (ls --color, tput setaf, truecolor 38;2;R;G;B, etc.) keep their
     * color and stay untouched. */
    uint8_t base_flags = t->cur_flags | (wide ? CELL_FLAG_WIDE : 0);
    if (fg == theme_fg[t->theme])
        base_flags |= CELL_FLAG_FG_DEFAULT;
    else
        base_flags &= ~CELL_FLAG_FG_DEFAULT;
    c->flags = base_flags;
    t->cur_x++;

    if (wide) {
        /* 後続セルを WIDE_CONT でマーク — inherits the FG_DEFAULT tag
         * from the leading cell so theme migration stays consistent
         * across wide-glyph pairs. */
        term_cell_t *c2 = &t->cells[t->cur_y * t->cols + t->cur_x];
        c2->codepoint = 0;
        c2->fg = fg; c2->bg = bg;
        c2->flags = CELL_FLAG_WIDE_CONT | (base_flags & CELL_FLAG_FG_DEFAULT);
        t->cur_x++;
    }
}

static void queue_response(terminal_t *t, const char *s) {
    int n = (int)strlen(s);
    int room = TERM_RESPONSE_MAX - t->response_len;
    if (n > room) n = room;
    if (n > 0) {
        memcpy(t->response_buf + t->response_len, s, (size_t)n);
        t->response_len += n;
    }
}

/* ── 256色変換 ── */
static uint32_t ansi_256_to_rgba(terminal_t *t, int idx) {
    if (idx < 16) {
        int th = (t && t->theme >= 0 && t->theme < THEME_COUNT) ? t->theme : 0;
        return theme_palettes[th][idx];
    }
    if (idx >= 232) { uint8_t v = 8 + (idx-232)*10; return (v<<24)|(v<<16)|(v<<8)|0xff; }
    idx -= 16;
    uint8_t b=(idx%6)*51, g=((idx/6)%6)*51, r=(idx/36)*51;
    return (r<<24)|(g<<16)|(b<<8)|0xff;
}

/* ── SGR ── */
static void handle_sgr(terminal_t *t, int *params, int nparams) {
    uint32_t def_fg = terminal_get_fg(t);
    uint32_t def_bg = terminal_get_bg(t);
    for (int i = 0; i < nparams; ) {
        int p = params[i];
        switch (p) {
            case 0:  t->cur_fg=def_fg; t->cur_bg=def_bg; t->cur_flags=0; break;
            case 1:  t->cur_flags |=  CELL_FLAG_BOLD;      break;
            case 2:  break; // dim - ignore
            case 3:  break; // italic - ignore
            case 4:  t->cur_flags |=  CELL_FLAG_UNDERLINE; break;
            case 5:  t->cur_flags |=  CELL_FLAG_BLINK;     break;
            case 7:  t->cur_flags |=  CELL_FLAG_REVERSE;   break;
            case 21: case 22: t->cur_flags &= ~CELL_FLAG_BOLD;      break;
            case 24: t->cur_flags &= ~CELL_FLAG_UNDERLINE; break;
            case 25: t->cur_flags &= ~CELL_FLAG_BLINK;     break;
            case 27: t->cur_flags &= ~CELL_FLAG_REVERSE;   break;
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                t->cur_fg = ansi_256_to_rgba(t, p-30); break;
            case 39: t->cur_fg = def_fg; break;
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                t->cur_bg = ansi_256_to_rgba(t, p-40); break;
            case 49: t->cur_bg = def_bg; break;
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                t->cur_fg = ansi_256_to_rgba(t, p-90+8); break;
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                t->cur_bg = ansi_256_to_rgba(t, p-100+8); break;
            case 38:
                if (i+1 < nparams && params[i+1]==5 && i+2 < nparams) {
                    t->cur_fg = ansi_256_to_rgba(t, params[i+2]); i+=2;
                } else if (i+1 < nparams && params[i+1]==2 && i+4 < nparams) {
                    uint8_t r=params[i+2], g=params[i+3], b=params[i+4];
                    t->cur_fg = (r<<24)|(g<<16)|(b<<8)|0xff; i+=4;
                }
                break;
            case 48:
                if (i+1 < nparams && params[i+1]==5 && i+2 < nparams) {
                    t->cur_bg = ansi_256_to_rgba(t, params[i+2]); i+=2;
                } else if (i+1 < nparams && params[i+1]==2 && i+4 < nparams) {
                    uint8_t r=params[i+2], g=params[i+3], b=params[i+4];
                    t->cur_bg = (r<<24)|(g<<16)|(b<<8)|0xff; i+=4;
                }
                break;
            default: break;
        }
        i++;
    }
}

void terminal_set_theme(terminal_t *t, term_theme_t theme) {
    if (!t || theme < 0 || theme >= THEME_COUNT) return;
    if (t->theme == theme) return;
    uint32_t old_bg = theme_bg[t->theme];
    uint32_t old_fg = theme_fg[t->theme];
    uint32_t new_bg = theme_bg[theme];
    uint32_t new_fg = theme_fg[theme];
    t->theme  = theme;
    t->cur_fg = new_fg;
    t->cur_bg = new_bg;
    if (t->cells) {
        for (int i = 0; i < t->cols * t->rows; i++) {
            if (t->cells[i].bg == old_bg || t->cells[i].bg == 0)
                t->cells[i].bg = new_bg;
            /* Only recolor cells that were rendered with the OLD theme's
             * default fg — the CELL_FLAG_FG_DEFAULT bit is set at write
             * time for exactly those cells (including non-space ones
             * like a shell prompt's "~ %").  Cells without the bit
             * either came from an explicit ANSI SGR (ls --color,
             * tput setaf, truecolor 38;2;…) or pre-date the flag
             * change — both must keep their color. */
            if ((t->cells[i].flags & CELL_FLAG_FG_DEFAULT) &&
                t->cells[i].fg == old_fg) {
                t->cells[i].fg = new_fg;
            }
        }
    }
    if (t->alt_cells) {
        for (int i = 0; i < t->cols * t->rows; i++) {
            if (t->alt_cells[i].bg == old_bg || t->alt_cells[i].bg == 0)
                t->alt_cells[i].bg = new_bg;
            if ((t->alt_cells[i].flags & CELL_FLAG_FG_DEFAULT) &&
                t->alt_cells[i].fg == old_fg) {
                t->alt_cells[i].fg = new_fg;
            }
        }
    }
    if (t->scrollback) {
        int total = t->sb_cols * TERM_SCROLLBACK;
        for (int i = 0; i < total; i++) {
            if (t->scrollback[i].bg == old_bg || t->scrollback[i].bg == 0)
                t->scrollback[i].bg = new_bg;
            if ((t->scrollback[i].flags & CELL_FLAG_FG_DEFAULT) &&
                t->scrollback[i].fg == old_fg) {
                t->scrollback[i].fg = new_fg;
            }
        }
    }
}

term_theme_t terminal_get_theme(const terminal_t *t) {
    return t ? t->theme : THEME_DARK;
}

uint32_t terminal_get_bg(const terminal_t *t) {
    return t ? theme_bg[t->theme] : theme_bg[0];
}

uint32_t terminal_get_fg(const terminal_t *t) {
    return t ? theme_fg[t->theme] : theme_fg[0];
}

uint32_t terminal_get_c2d_bg(const terminal_t *t) {
    uint32_t rgba = terminal_get_bg(t);
    uint32_t r = (rgba >> 24) & 0xff;
    uint32_t g = (rgba >> 16) & 0xff;
    uint32_t b = (rgba >>  8) & 0xff;
    uint32_t a =  rgba        & 0xff;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

void terminal_theme_set_custom(const term_custom_pal_t *p) {
    if (!p) return;
    /* Re-seed first, then overlay.  Without this the slot would accumulate
     * across calls and a theme.ini that stopped mentioning a key would keep
     * the previous load's colour — the palette side (kb_theme_set_custom)
     * already describes all 23 roles, so this side has to as well. */
    theme_bg[THEME_CUSTOM] = theme_bg[THEME_DARK];
    theme_fg[THEME_CUSTOM] = theme_fg[THEME_DARK];
    {
        const uint32_t seed[16] = THEME_ANSI_DARK;
        memcpy(theme_palettes[THEME_CUSTOM], seed, sizeof(seed));
    }
    /* Opaque, always: these three tables feed cell backgrounds and the SGR
     * palette, and a translucent cell fill would show the clear colour
     * through the text — a difference no built-in theme has ever made. */
    if (p->have_bg)
        theme_bg[THEME_CUSTOM] = (p->bg & 0xffffff00u) | 0xffu;
    if (p->have_fg)
        theme_fg[THEME_CUSTOM] = (p->fg & 0xffffff00u) | 0xffu;
    for (int i = 0; i < 16; i++)
        if (p->have_ansi[i])
            theme_palettes[THEME_CUSTOM][i] =
                (p->ansi[i] & 0xffffff00u) | 0xffu;
}

/* ── CSI ── */
/* Upper bound applied to every parsed CSI parameter.  xterm clamps to 9999
 * and no sequence in the wild asks for more, while the bound is what keeps
 * the arithmetic below (`cur_x + n`, `(bot - top) * cols`, the repeat counts
 * of S/T/L/M) inside int and therefore away from a negative-length memmove. */
#define TERM_PARAM_MAX 9999

static void handle_csi(terminal_t *t, char final, const char *param_str) {
    int params[32] = {0};
    int nparams = 0;
    const char *p = param_str;
    /* CSI prefixes/intermediate bytes used by DEC/xterm/Kitty protocols. */
    char priv_prefix = (*p == '?' || *p == '>' || *p == '!' ||
                        *p == '=' || *p == '<') ? *p : 0;
    int priv = priv_prefix != 0;
    if (priv) p++;
    if (*p) {
        char tmp[256];
        int tmplen = 0;
        while (*p && tmplen < 254) tmp[tmplen++] = *p++;
        tmp[tmplen] = '\0';
        char *tok = tmp, *end;
        while (*tok && nparams < 32) {
            /* `-` (0x2d) is a legal *intermediate* byte — it sits inside the
             * 0x20-0x3f collection range above — but it is never a legal
             * *parameter*.  Unclamped, strtol hands back a negative number
             * and the consumers below index with it: `ESC[-5B` drove cur_y
             * to -5 and put_char then wrote cells[cur_y*cols+cur_x] well
             * outside the grid, and `ESC[38;5;-5m` read
             * theme_palettes[theme][-5].  Clamping once here fixes every
             * case at the source instead of re-auditing all twenty-odd. */
            long v = strtol(tok, &end, 10);
            if (v < 0) v = 0;
            if (v > TERM_PARAM_MAX) v = TERM_PARAM_MAX;
            params[nparams++] = (int)v;
            if (*end == ';') tok = end+1; else break;
        }
    }
    if (nparams == 0) nparams = 1;
    int p1 = params[0];

    switch (final) {
        /* ── カーソル移動 ── */
        case 'A': t->cur_y -= (p1?p1:1); if(t->cur_y<0) t->cur_y=0; break;
        case 'B': t->cur_y += (p1?p1:1); if(t->cur_y>=t->rows) t->cur_y=t->rows-1; break;
        case 'C': t->cur_x += (p1?p1:1); if(t->cur_x>=t->cols) t->cur_x=t->cols-1; break;
        case 'D': t->cur_x -= (p1?p1:1); if(t->cur_x<0) t->cur_x=0; break;
        case 'E': t->cur_y += (p1?p1:1); t->cur_x=0; if(t->cur_y>=t->rows) t->cur_y=t->rows-1; break;
        case 'F': t->cur_y -= (p1?p1:1); t->cur_x=0; if(t->cur_y<0) t->cur_y=0; break;
        case 'G': {
            int col = (p1?p1:1)-1;
            t->cur_x = (col<0)?0:(col>=t->cols?t->cols-1:col);
            break;
        }
        case 'H': case 'f': {
            int row = (params[0]?params[0]:1)-1;
            int col = (nparams>1 && params[1]?params[1]:1)-1;
            t->cur_y = (row<0)?0:(row>=t->rows?t->rows-1:row);
            t->cur_x = (col<0)?0:(col>=t->cols?t->cols-1:col);
            break;
        }
        case 'd': {
            int row = (p1?p1:1)-1;
            t->cur_y = (row<0)?0:(row>=t->rows?t->rows-1:row);
            break;
        }
        /* ── 消去 ── */
        case 'J':
            if (p1==0) {
                for (int i=t->cur_y*t->cols+t->cur_x; i<t->cols*t->rows; i++) cell_reset(t, &t->cells[i]);
            } else if (p1==1) {
                for (int i=0; i<=t->cur_y*t->cols+t->cur_x; i++) cell_reset(t, &t->cells[i]);
            } else if (p1==2 || p1==3) {
                for (int i=0; i<t->cols*t->rows; i++) cell_reset(t, &t->cells[i]);
                t->cur_x=0; t->cur_y=0;
            }
            break;
        case 'K':
            if (p1==0) { for(int x=t->cur_x;x<t->cols;x++) cell_reset(t, &t->cells[t->cur_y*t->cols+x]); }
            else if (p1==1) { for(int x=0;x<=t->cur_x;x++) cell_reset(t, &t->cells[t->cur_y*t->cols+x]); }
            else if (p1==2) { for(int x=0;x<t->cols;x++) cell_reset(t, &t->cells[t->cur_y*t->cols+x]); }
            break;
        case 'X': { /* erase chars */
            int n = p1?p1:1;
            for (int x=t->cur_x; x<t->cur_x+n && x<t->cols; x++) cell_reset(t, &t->cells[t->cur_y*t->cols+x]);
            break;
        }
        /* ── 行操作 ──
         * The cursor is clamped to the grid, never to the scroll region, so
         * cur_y can legitimately sit at or below scroll_bottom.  The length
         * (bot - cur_y) then goes negative and, read as a size_t, becomes
         * enormous — the memmove below would write straight through the heap
         * (and at cur_y == rows-1 the source pointer is already one row past
         * the end).  Handle the two boundary positions explicitly. */
        case 'L': { /* insert lines */
            int n = p1?p1:1;
            int bot = t->scroll_bottom;
            if (t->cur_y > bot) break;      /* outside the region: no effect */
            if (t->cur_y == bot) {          /* the line pushed past the bottom
                                             * edge leaves the region, so this
                                             * row ends up blank */
                for (int x=0;x<t->cols;x++) cell_reset(t, &t->cells[bot*t->cols+x]);
                break;
            }
            /* More lines than the region holds is the same as enough of
             * them; clamping also bounds the loop for a huge parameter. */
            if (n > bot-t->cur_y) n = bot-t->cur_y;
            for (int i=0;i<n;i++) {
                memmove(&t->cells[(t->cur_y+1)*t->cols], &t->cells[t->cur_y*t->cols],
                        (bot-t->cur_y)*t->cols*sizeof(term_cell_t));
                for(int x=0;x<t->cols;x++) cell_reset(t, &t->cells[t->cur_y*t->cols+x]);
            }
            break;
        }
        case 'M': { /* delete lines */
            int n = p1?p1:1;
            int bot = t->scroll_bottom;
            if (t->cur_y > bot) break;      /* outside the region: no effect */
            if (t->cur_y == bot) {          /* nothing below to pull up */
                for (int x=0;x<t->cols;x++) cell_reset(t, &t->cells[bot*t->cols+x]);
                break;
            }
            if (n > bot-t->cur_y) n = bot-t->cur_y;
            for (int i=0;i<n;i++) {
                memmove(&t->cells[t->cur_y*t->cols], &t->cells[(t->cur_y+1)*t->cols],
                        (bot-t->cur_y)*t->cols*sizeof(term_cell_t));
                for(int x=0;x<t->cols;x++) cell_reset(t, &t->cells[bot*t->cols+x]);
            }
            break;
        }
        /* ── 文字操作 ── */
        case 'P': { /* delete chars */
            int n = p1?p1:1;
            int end = t->cols-n;
            if (t->cur_x < end)
                memmove(&t->cells[t->cur_y*t->cols+t->cur_x],
                        &t->cells[t->cur_y*t->cols+t->cur_x+n],
                        (end-t->cur_x)*sizeof(term_cell_t));
            for(int x=(t->cur_x<end?end:t->cur_x);x<t->cols;x++) cell_reset(t, &t->cells[t->cur_y*t->cols+x]);
            break;
        }
        case '@': { /* insert chars */
            int n = p1?p1:1;
            int mv = t->cols-t->cur_x-n;
            if (mv > 0)
                memmove(&t->cells[t->cur_y*t->cols+t->cur_x+n],
                        &t->cells[t->cur_y*t->cols+t->cur_x],
                        mv*sizeof(term_cell_t));
            for(int x=t->cur_x;x<t->cur_x+n&&x<t->cols;x++) cell_reset(t, &t->cells[t->cur_y*t->cols+x]);
            break;
        }
        /* ── 属性 ── */
        case 'm':
            /* `CSI > 4 ; Ps m` is xterm modifyOtherKeys, not SGR. */
            if (!priv) handle_sgr(t, params, nparams);
            break;
        /* ── スクロール領域 ── */
        case 'r': {
            /* DECSTBM.  Both ends must be clamped *and* ordered: scroll_up_one
             * memmoves `(bot - top) * cols` cells, so an inverted region
             * (ESC[10;5r) or a top past the last row (ESC[9999;1r — the old
             * code clamped only at 0) made that length negative.  Read as a
             * size_t it is enormous, and the memmove writes straight through
             * the heap.  An inverted region is malformed; xterm ignores the
             * sequence and keeps the current margins, which is also the only
             * reply that cannot leave top > bottom behind. */
            int top = (params[0] ? params[0] : 1) - 1;
            int bot = (nparams > 1 && params[1] ? params[1] : t->rows) - 1;
            if (top < 0)          top = 0;
            if (bot < 0)          bot = 0;
            if (top >= t->rows)   top = t->rows - 1;
            if (bot >= t->rows)   bot = t->rows - 1;
            if (top <= bot) {
                t->scroll_top    = top;
                t->scroll_bottom = bot;
            }
            t->cur_x = 0; t->cur_y = 0;
            break;
        }
        /* SU / SD.  TERM_PARAM_MAX bounds the *value* of the parameter,
         * not the *work*: each iteration is a full-region memmove of
         * (bot-top) rows, so an unclamped count of 9999 on the 66x40
         * dual-screen grid is ~309 MB of memmove inside one frame —
         * tens of seconds of frozen UI on a 268 MHz ARM11.  `printf
         * '\e[9999S'` is enough to trigger it.
         *
         * Scrolling more lines than the region holds is identical to
         * scrolling exactly that many: everything has already moved
         * out.  So clamp the count to the region height (this is what
         * xterm does), which caps the work at one full region. */
        case 'S': {
            int n = p1?p1:1;
            int h = t->scroll_bottom - t->scroll_top + 1;
            if (h < 1) h = 1;
            if (n > h) n = h;
            for (int i=0;i<n;i++) scroll_up_one(t);
            break;
        }
        case 'T': {
            int n = p1?p1:1;
            int h = t->scroll_bottom - t->scroll_top + 1;
            if (h < 1) h = 1;
            if (n > h) n = h;
            for (int i=0;i<n;i++) scroll_down_one(t);
            break;
        }
        /* ── カーソル保存/復元 ── */
        case 's':
            if (!priv && !*param_str) {
                t->saved_x=t->cur_x; t->saved_y=t->cur_y;
                t->saved_fg=t->cur_fg; t->saved_bg=t->cur_bg; t->saved_flags=t->cur_flags;
            }
            break;
        case 'u':
            /* Fish enables Kitty's keyboard protocol with `CSI = 5 u`.
             * Only a parameterless CSI u is the ANSI/SCO restore-cursor
             * command. Treating Kitty's sequence as restore moved fish's
             * input line to the default saved position at row 1. */
            if (!priv && !*param_str) {
                t->cur_x=t->saved_x; t->cur_y=t->saved_y;
                t->cur_fg=t->saved_fg; t->cur_bg=t->saved_bg; t->cur_flags=t->saved_flags;
            }
            break;
        /* ── 终端查询回复 ──
         * fish's interactive line editor asks for the cursor position with
         * CSI 6n.  Ignoring it makes fish assume row 1 and repaint every input
         * line at the top of the screen. */
        case 'n':
            if (p1 == 5 && !priv) {
                queue_response(t, "\x1b[0n");
            } else if (p1 == 6) {
                char reply[32];
                snprintf(reply, sizeof(reply),
                         priv_prefix == '?' ? "\x1b[?%d;%dR" : "\x1b[%d;%dR",
                         t->cur_y + 1, t->cur_x + 1);
                queue_response(t, reply);
            }
            break;
        case 'c':
            if (priv_prefix == '>')
                queue_response(t, "\x1b[>0;0;0c"); /* secondary DA */
            else
                queue_response(t, "\x1b[?1;2c");  /* VT100 + advanced video */
            break;
        /* ── DEC private モード ── */
        case 'h': case 'l': {
            int on = (final=='h');
            if (priv) {
                switch (p1) {
                    case 25: t->cursor_visible = on; break;
                    case 1047:
                    case 1049:
                        if (on) { /* enter alt screen */
                            if (!t->alt_cells) {
                                t->alt_cells = calloc(t->cols*t->rows, sizeof(term_cell_t));
                                if (!t->alt_cells) break;
                                for(int i=0;i<t->cols*t->rows;i++) cell_reset(t, &t->alt_cells[i]);
                            }
                            if (p1==1049) { /* save cursor */
                                t->saved_x=t->cur_x; t->saved_y=t->cur_y;
                                t->saved_fg=t->cur_fg; t->saved_bg=t->cur_bg; t->saved_flags=t->cur_flags;
                            }
                            /* swap main ↔ alt */
                            term_cell_t *tmp=t->cells; t->cells=t->alt_cells; t->alt_cells=tmp;
                            t->use_alt=1;
                            /* clear alt + home */
                            for(int i=0;i<t->cols*t->rows;i++) cell_reset(t, &t->cells[i]);
                            t->cur_x=0; t->cur_y=0;
                            t->scroll_top=0; t->scroll_bottom=t->rows-1;
                        } else { /* leave alt screen */
                            if (t->alt_cells) {
                                term_cell_t *tmp=t->cells; t->cells=t->alt_cells; t->alt_cells=tmp;
                                t->use_alt=0;
                            }
                            if (p1==1049) { /* restore cursor */
                                t->cur_x=t->saved_x; t->cur_y=t->saved_y;
                                t->cur_fg=t->saved_fg; t->cur_bg=t->saved_bg; t->cur_flags=t->saved_flags;
                            }
                        }
                        break;
                    case 1048:
                        if (on) { t->saved_x=t->cur_x; t->saved_y=t->cur_y; }
                        else    { t->cur_x=t->saved_x; t->cur_y=t->saved_y; }
                        break;
                    /* xterm mouse-tracking modes.  When tmux has
                     * `set -g mouse on` it emits these on startup and
                     * the terminal is supposed to forward mouse events
                     * (especially scroll-wheel) back as escape sequences
                     * — that's how PC terminals' scroll wheel "just
                     * works" inside tmux. */
                    case 1000:        /* X11 mouse: press + release */
                    case 1002:        /* button-event tracking         */
                    case 1003:        /* any-event tracking             */
                        t->mouse_proto = on ? p1 : 0;
                        break;
                    case 1006:        /* SGR encoding (recommended)    */
                        t->mouse_sgr = on;
                        break;
                    default: break;
                }
            }
            break;
        }
        default: break;
    }
}

/* ── UTF-8 デコード ── */
static int utf8_decode(const char *s, int len, uint32_t *cp) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp=c; return 1; }
    if (c < 0xc0) { *cp='?'; return 1; }
    int n; uint32_t min;
    if      (c<0xe0) { n=2; min=0x80;    *cp=c&0x1f; }
    else if (c<0xf0) { n=3; min=0x800;   *cp=c&0x0f; }
    else             { n=4; min=0x10000; *cp=c&0x07; }
    if (len<n) { *cp='?'; return 1; }
    for (int i=1;i<n;i++) {
        unsigned char b=(unsigned char)s[i];
        if ((b&0xc0)!=0x80) { *cp='?'; return 1; }
        *cp=(*cp<<6)|(b&0x3f);
    }
    if (*cp<min) *cp='?';
    return n;
}

/* ── メインパーサー ── */
void terminal_write_n(terminal_t *t, const char *data, int len) {
    t->sb_offset = 0;  /* 新データ受信時はスクロールをリセット */
    int i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)data[i];

        if (t->parse_state == STATE_ESC) {
            switch (c) {
                case '[': t->parse_state=STATE_CSI; t->parse_len=0; t->parse_buf[0]='\0'; break;
                case ']': t->parse_state=STATE_OSC; t->parse_len=0; break;
                /* SCS - Select Character Set: ESC ( <C>, ESC ) <C>,
                 * ESC * <C>, ESC + <C> designate G0/G1/G2/G3.  We don't
                 * actually track multiple charsets (UTF-8 / ASCII only),
                 * so we just consume the next byte (the charset
                 * designator like 'B' for ASCII or '0' for DEC graphics)
                 * to keep it from being interpreted as a literal char.
                 *
                 * This was the cause of the "B trail" in Claude Code:
                 * tmux/Ink redraws repeatedly emit `ESC ( B`; if we
                 * didn't recognize this as a 2-byte sequence the 'B'
                 * landed on the screen as a literal character. */
                case '(': case ')': case '*': case '+':
                    t->parse_state = STATE_SCS;
                    break;
                case 'c': terminal_reset(t); t->parse_state=STATE_NORMAL; break;
                case '7': /* save cursor */
                    t->saved_x=t->cur_x; t->saved_y=t->cur_y;
                    t->saved_fg=t->cur_fg; t->saved_bg=t->cur_bg; t->saved_flags=t->cur_flags;
                    t->parse_state=STATE_NORMAL; break;
                case '8': /* restore cursor */
                    t->cur_x=t->saved_x; t->cur_y=t->saved_y;
                    t->cur_fg=t->saved_fg; t->cur_bg=t->saved_bg; t->cur_flags=t->saved_flags;
                    t->parse_state=STATE_NORMAL; break;
                case 'M': /* reverse index */
                    if (t->cur_y <= t->scroll_top) scroll_down_one(t);
                    else t->cur_y--;
                    t->parse_state=STATE_NORMAL; break;
                case 'D': /* index (like LF) */
                    newline(t); t->parse_state=STATE_NORMAL; break;
                case 'E': /* next line */
                    t->cur_x=0; newline(t); t->parse_state=STATE_NORMAL; break;
                default:  t->parse_state=STATE_NORMAL; break;
            }
            i++; continue;
        }

        if (t->parse_state == STATE_SCS) {
            /* Swallow the charset designator byte (B, 0, A, K, ...) and
             * return to normal mode.  We don't switch fonts — UTF-8 only. */
            t->parse_state = STATE_NORMAL;
            i++; continue;
        }

        if (t->parse_state == STATE_CSI) {
            if ((c>=0x20&&c<=0x3f)) {
                if (t->parse_len<(int)sizeof(t->parse_buf)-1)
                    t->parse_buf[t->parse_len++]=c;
            } else if (c>=0x40&&c<=0x7e) {
                t->parse_buf[t->parse_len]='\0';
                handle_csi(t, (char)c, t->parse_buf);
                t->parse_state=STATE_NORMAL;
            }
            i++; continue;
        }

        if (t->parse_state == STATE_OSC) {
            /* OSC: terminated by BEL (0x07) or ESC \ (ST) */
            if (c==0x07) {
                t->parse_state=STATE_NORMAL;
            } else if (c==0x1b) {
                /* next char should be '\' — just swallow both */
                t->parse_state=STATE_NORMAL;
                if (i+1<len && (unsigned char)data[i+1]=='\\') i++;
            } else {
                if (t->parse_len<(int)sizeof(t->parse_buf)-1)
                    t->parse_buf[t->parse_len++]=c;
            }
            i++; continue;
        }

        /* STATE_NORMAL */
        switch (c) {
            case 0x1b: t->parse_state=STATE_ESC; i++; break;
            case '\r':  t->cur_x=0; i++; break;
            case '\n':  newline(t); i++; break;
            case '\t': {
                int next_tab=((t->cur_x/8)+1)*8;
                while(t->cur_x<next_tab&&t->cur_x<t->cols) put_char(t,' ');
                i++; break;
            }
            case '\b': if(t->cur_x>0) t->cur_x--; i++; break;
            case 0x07: i++; break; /* BEL */
            case 0x0e: case 0x0f: i++; break; /* SO/SI charset switch - ignore */
            default:
                if (c < 0x20) { i++; break; }
                {
                    uint32_t cp;
                    int consumed=utf8_decode(data+i, len-i, &cp);
                    put_char(t, cp);
                    i+=consumed;
                }
                break;
        }
    }
}

void terminal_write(terminal_t *t, const char *data) {
    terminal_write_n(t, data, (int)strlen(data));
}

/* Total length of the UTF-8 sequence started by lead byte `b`, or 0 when `b`
 * is not a lead byte (continuation, ASCII handled by the caller). */
static int utf8_seq_len(unsigned char b) {
    if (b < 0x80) return 1;
    if (b < 0xc0) return 0;          /* continuation — cannot start one */
    if (b < 0xe0) return 2;
    if (b < 0xf0) return 3;
    if (b < 0xf8) return 4;
    return 0;
}

/* How many trailing bytes of `data` belong to a codepoint that started but
 * did not finish inside this buffer (0 = everything here is complete). */
static int utf8_incomplete_tail(const char *data, int len) {
    int lead = len;
    while (lead > 0 && ((unsigned char)data[lead - 1] & 0xc0) == 0x80) lead--;
    if (lead == 0) return 0;         /* orphan continuations — pass through */
    int seq  = utf8_seq_len((unsigned char)data[lead - 1]);
    int have = len - (lead - 1);
    return (seq >= 2 && have < seq) ? have : 0;
}

void terminal_write_bytes(terminal_t *t, const char *data, int len) {
    if (!t || len < 0) return;

    /* Complete the codepoint a previous read left half-done. */
    while (len > 0 && t->utf8_frag_len > 0) {
        int seq = utf8_seq_len((unsigned char)t->utf8_frag[0]);
        if (seq <= t->utf8_frag_len) {         /* unsalvageable — drop it */
            t->utf8_frag_len = 0;
            break;
        }
        int need = seq - t->utf8_frag_len;
        if (len < need) {
            memcpy(t->utf8_frag + t->utf8_frag_len, data, (size_t)len);
            t->utf8_frag_len += len;
            return;
        }
        memcpy(t->utf8_frag + t->utf8_frag_len, data, (size_t)need);
        int done = t->utf8_frag_len + need;
        t->utf8_frag_len = 0;
        terminal_write_n(t, t->utf8_frag, done);
        data += need;
        len  -= need;
    }

    if (len <= 0) return;
    int tail = utf8_incomplete_tail(data, len);
    if (tail > 0) {
        memcpy(t->utf8_frag, data + len - tail, (size_t)tail);
        t->utf8_frag_len = tail;
        len -= tail;
    }
    if (len > 0) terminal_write_n(t, data, len);
}

void terminal_scroll_view(terminal_t *t, int delta) {
    t->sb_offset += delta;
    if (t->sb_offset < 0) t->sb_offset = 0;
    if (t->sb_offset > t->sb_size) t->sb_offset = t->sb_size;
}

term_cell_t terminal_get_cell(terminal_t *t, int x, int y) {
    if (x<0||x>=t->cols||y<0||y>=t->rows) {
        term_cell_t e={0,DEFAULT_FG,DEFAULT_BG,0}; return e;
    }
    if (t->sb_offset > 0) {
        /* スクロールバック表示: combined = scrollback上の絶対行 */
        int combined = t->sb_size - t->sb_offset + y;
        if (combined >= 0 && combined < t->sb_size) {
            int ring = (t->sb_head - t->sb_size + combined + TERM_SCROLLBACK) % TERM_SCROLLBACK;
            return t->scrollback[ring * t->sb_cols + x];
        }
        int cell_y = combined - t->sb_size;
        if (cell_y >= 0 && cell_y < t->rows)
            return t->cells[cell_y * t->cols + x];
        term_cell_t e={0,DEFAULT_FG,DEFAULT_BG,0}; return e;
    }
    return t->cells[y*t->cols+x];
}

int terminal_take_response(terminal_t *t, char *buf, int len) {
    if (!t || !buf || len <= 0 || t->response_len <= 0) return 0;
    int n = t->response_len < len ? t->response_len : len;
    memcpy(buf, t->response_buf, (size_t)n);
    t->response_len -= n;
    if (t->response_len > 0)
        memmove(t->response_buf, t->response_buf + n,
                (size_t)t->response_len);
    return n;
}
