/*
 * citro2d-based terminal renderer.
 * Derived from skmtrd/3dssh (MIT License, Copyright (c) 2026 skmtrd).
 */

#include "renderer.h"
#include "font_atlas.h"
#include <stdlib.h>
#include <string.h>

/*
 * citro2d-based terminal renderer (forked from skmtrd/3dssh, with the touch
 * keyboard portions removed — those will live in M4's softkb.c).
 *
 * Two-pass terminal draw:
 *   pass 1  background rects per cell + cursor
 *   pass 2  glyph bitmaps blitted as 1-pixel-tall horizontal runs of
 *           C2D_DrawRectSolid; no texture atlas, just stamping pixels.
 *
 * Why pixel runs and not a texture: avoids the cost/complexity of uploading
 * texture atlases at startup; ARM11 GPU can handle thousands of solid rects
 * per frame easily and we're drawing ~1320 cells × ~10 lit pixels avg per
 * glyph at 60fps. Plenty of headroom.
 */

/* Raw passthrough — neovim sends exact 24-bit RGB via SGR-truecolor;
 * trust those colours and let the LCD render them as-is.  An earlier
 * "punch" gain was found to wash dark blues into greys and lift bright
 * blues toward white, defeating the user's colourscheme. */
static u32 rgba_to_c2d(uint32_t rgba) {
    return C2D_Color32((rgba >> 24) & 0xff,
                       (rgba >> 16) & 0xff,
                       (rgba >>  8) & 0xff,
                        rgba        & 0xff);
}

/* Walk a 1bpp layer (FA_CELL_H bytes) and paint contiguous lit runs as
 * solid 1×1-row rects in the given C2D-format colour.  Factored out so
 * draw_glyph and draw_glyph_scaled can share the inner loop and the AA
 * variant can call it three times with different alpha. */
static inline void draw_layer_runs(float fx, float fy, float z,
                                   const uint8_t *rows, u32 color) {
    for (int row = 0; row < FA_CELL_H; row++) {
        uint8_t bits = rows[row];
        if (!bits) continue;
        int col = 0;
        while (col < FA_CELL_W) {
            if (bits & (1 << (FA_CELL_W - 1 - col))) {
                int start = col;
                while (col < FA_CELL_W && (bits & (1 << (FA_CELL_W - 1 - col))))
                    col++;
                C2D_DrawRectSolid(fx + start, fy + row, z, col - start, 1, color);
            } else col++;
        }
    }
}

/* Three-pass anti-aliased glyph rendering.  Layer 0 is full-alpha
 * (matches the `color` argument); layer 1 is half alpha; layer 2 is
 * quarter alpha.  For pure bitmap fonts (Terminus, Zpix) only layer 0
 * has set bits, so this is identical-cost to the old 1bpp path.  For
 * TTF text fonts the edge pixels populate layers 1 and 2, smoothing
 * stroke edges via citro2d's natural alpha blending against whatever
 * background colour is below. */
static void draw_glyph(float fx, float fy, float z,
                       int glyph_idx, u32 color) {
    if (glyph_idx < 0 || glyph_idx >= FONT_NGLYPHS) return;
    const uint8_t *base = font_glyphs[glyph_idx];

    draw_layer_runs(fx, fy, z, base, color);

    /* Decompose the C2D colour back to channels so we can scale the
     * alpha for the half/quarter layers.  C2D_Color32 packs as
     * (a<<24)|(b<<16)|(g<<8)|r — the byte order is fixed by the macro
     * and matches the GPU's expected format. */
    u8 a = (color >> 24) & 0xff;
    if (a < 2) return;  /* fully transparent already; AA layers add nothing */
    u8 b = (color >> 16) & 0xff;
    u8 g = (color >>  8) & 0xff;
    u8 r =  color        & 0xff;

    u32 c_half    = C2D_Color32(r, g, b, a / 2);
    u32 c_quarter = C2D_Color32(r, g, b, a / 4);
    draw_layer_runs(fx, fy, z, base + FA_CELL_H,         c_half);
    draw_layer_runs(fx, fy, z, base + (FA_CELL_H * 2),   c_quarter);
}

static void draw_wide_glyph(float fx, float fy, float z,
                            int glyph_idx, u32 color) {
    if (glyph_idx < 0 || glyph_idx >= FONT_WIDE_NGLYPHS) return;
    const uint16_t *rows = font_wide_glyphs[glyph_idx];
    int wide = FA_CELL_W * 2;
    for (int row = 0; row < FA_CELL_H; row++) {
        uint16_t bits = rows[row];
        if (!bits) continue;
        int col = 0;
        while (col < wide) {
            if (bits & (1 << (wide - 1 - col))) {
                int start = col;
                while (col < wide && (bits & (1 << (wide - 1 - col))))
                    col++;
                C2D_DrawRectSolid(fx + start, fy + row, z, col - start, 1, color);
            } else col++;
        }
    }
}

/* ── horizontally-scaled glyph paths (dual-screen bottom layer) ──
 *
 * The bottom screen is 320 px wide but the terminal grid is 66 cells
 * (396 px at 6 px/cell).  Instead of clipping the right ~13 columns we
 * render each row at x_scale = 320/396 ≈ 0.808, so all 66 columns fit.
 * The downscale is non-integer, so every lit run is drawn at
 * `run_len * x_scale` pixels; runs are clamped to ≥ 1 px so thin 1-bit
 * strokes never vanish into rounding.  The result is softer/mergier than
 * the crisp full-scale top screen — the accepted trade-off for zero
 * column loss on the narrower display.  Vertical resolution is untouched
 * (still 1 px per row bit × FA_CELL_H rows). */

static void draw_layer_runs_scaled_x(float fx, float fy, float z,
                                     const uint8_t *rows, u32 color,
                                     float x_scale) {
    for (int row = 0; row < FA_CELL_H; row++) {
        uint8_t bits = rows[row];
        if (!bits) continue;
        int col = 0;
        while (col < FA_CELL_W) {
            if (bits & (1 << (FA_CELL_W - 1 - col))) {
                int start = col;
                while (col < FA_CELL_W && (bits & (1 << (FA_CELL_W - 1 - col))))
                    col++;
                float w = (float)(col - start) * x_scale;
                if (w < 1.0f) w = 1.0f;   /* keep 1-bit strokes visible */
                C2D_DrawRectSolid(fx + start * x_scale, fy + row, z, w, 1, color);
            } else col++;
        }
    }
}

static void draw_glyph_scaled_x(float fx, float fy, float z,
                                int glyph_idx, u32 color, float x_scale) {
    if (glyph_idx < 0 || glyph_idx >= FONT_NGLYPHS) return;
    const uint8_t *base = font_glyphs[glyph_idx];

    /* Only the opaque core is drawn here — deliberately no half/quarter
     * AA layers.  At a ~4.85 px cell those soft-edge layers rasterize to
     * 1 px half-alpha blocks that melt adjacent strokes together, reading
     * as blur.  The hard 1 px core (same look the wide CJK glyphs already
     * get) keeps the squeezed text as legible as the size allows. */
    draw_layer_runs_scaled_x(fx, fy, z, base, color, x_scale);
}

static void draw_wide_glyph_scaled_x(float fx, float fy, float z,
                                     int glyph_idx, u32 color,
                                     float x_scale) {
    if (glyph_idx < 0 || glyph_idx >= FONT_WIDE_NGLYPHS) return;
    const uint16_t *rows = font_wide_glyphs[glyph_idx];
    int wide = FA_CELL_W * 2;
    for (int row = 0; row < FA_CELL_H; row++) {
        uint16_t bits = rows[row];
        if (!bits) continue;
        int col = 0;
        while (col < wide) {
            if (bits & (1 << (wide - 1 - col))) {
                int start = col;
                while (col < wide && (bits & (1 << (wide - 1 - col))))
                    col++;
                float w = (float)(col - start) * x_scale;
                if (w < 1.0f) w = 1.0f;
                C2D_DrawRectSolid(fx + start * x_scale, fy + row, z, w, 1, color);
            } else col++;
        }
    }
}

renderer_t *renderer_init(C3D_RenderTarget *top, C3D_RenderTarget *bot) {
    renderer_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->top = top;
    r->bot = bot;
    r->cell_w = FONT_CELL_W;
    r->cell_h = FONT_CELL_H;
    /* Derived from the same R_TOP_PX_W / R_PANEL_PX_H / R_BOT_PX_W the
     * R_*_COLS macros use, but off the live r->cell_* members, so a
     * scaled cell width lands here and nowhere else. */
    r->top_cols = R_TOP_PX_W / r->cell_w;
    r->top_rows = R_PANEL_PX_H / r->cell_h;
    r->bot_px   = R_BOT_PX_W;
    r->bot_cols = r->bot_px / r->cell_w;
    return r;
}

void renderer_free(renderer_t *r) { free(r); }

/* Cursor style: invert fg/bg of the cell at term->cur_x/cur_y.  This is
 * the xterm/iTerm/alacritty default — fully opaque, never leaves
 * a "ghost" trail when cursor moves (since the next frame rebuilds
 * the entire screen from cell state, the inversion only happens at
 * the new cursor position).  Far more reliable than alpha-blended
 * overlay blocks at 60 fps with double-buffering. */
static inline int cell_is_cursor(const terminal_t *t, int x, int y) {
    return t->cursor_visible && x == t->cur_x && y == t->cur_y;
}

/* Glyph blits for the two horizontal shapes: full cell width keeps the
 * anti-aliased edge layers, a squeeze uses the hard 1 px core only (see
 * draw_glyph_scaled_x for why soft edges melt together at ~4.85 px).
 * x_scale == 1.0f selects the crisp path, so the squeezed shapes only
 * diverge when they actually squeeze. */
static inline void blit_narrow(float fx, float fy, int gi, u32 color,
                               float x_scale) {
    if (x_scale == 1.0f) draw_glyph(fx, fy, 0.3f, gi, color);
    else draw_glyph_scaled_x(fx, fy, 0.3f, gi, color, x_scale);
}

static inline void blit_wide(float fx, float fy, int gi, u32 color,
                             float x_scale) {
    if (x_scale == 1.0f) draw_wide_glyph(fx, fy, 0.3f, gi, color);
    else draw_wide_glyph_scaled_x(fx, fy, 0.3f, gi, color, x_scale);
}

/* ── the one window painter ──
 *
 * Every public shape below differs only in *which* terminal columns it
 * shows and where the block starts on the panel; the two-pass
 * background/glyph loop, the inverted cursor cell and the "skip a default
 * background cell, the canvas clear already painted it" rule are the same
 * four times over.  One copy is also what lets a cell scale land once.
 *
 * x/y are absolute terminal cells; fx/fy are relative to the active render
 * target's own top-left, so the bottom screen drawing rows 20..39 at local
 * y 0..239 makes the two physical panels read as one continuous grid. */
static void draw_window(renderer_t *r, terminal_t *term,
                        int y0, int row_count,
                        int x_first, int x_count,
                        float x_scale, float px_off) {
    int y_end = y0 + row_count;
    if (y_end > term->rows) y_end = term->rows;
    if (x_first < 0) x_first = 0;
    if (x_first > term->cols) x_first = term->cols;
    if (x_count > term->cols - x_first) x_count = term->cols - x_first;

    float cw = (float)r->cell_w * x_scale;   /* cell width in px, squeezed */
    float ch = (float)r->cell_h;

    /* pass 1: backgrounds (the cursor cell painted with the fg color) */
    for (int y = y0; y < y_end; y++) {
        for (int x = x_first; x < x_first + x_count; x++) {
            term_cell_t c = terminal_get_cell(term, x, y);
            float fx = px_off + (x - x_first) * cw, fy = (y - y0) * ch;
            uint32_t bg_rgba;
            int draw_bg;
            if (cell_is_cursor(term, x, y)) {
                bg_rgba = c.fg;
                draw_bg = 1;
            } else {
                bg_rgba = c.bg;
                /* Skip a cell whose background is the default: the
                 * target was already cleared to it, so painting it again
                 * is a wasted rect per cell per frame.  This is only
                 * sound while the target really was cleared to
                 * terminal_get_bg(term) — main.c picks the bottom
                 * clear colour to match whichever layer ends up on that
                 * panel. */
                draw_bg = (c.bg != terminal_get_bg(term));
            }
            if (draw_bg)
                C2D_DrawRectSolid(fx, fy, 0.1f, cw, ch, rgba_to_c2d(bg_rgba));
        }
    }

    /* pass 2: glyphs (the cursor cell's glyph uses the cell's bg) */
    for (int y = y0; y < y_end; y++) {
        for (int x = x_first; x < x_first + x_count; x++) {
            term_cell_t c = terminal_get_cell(term, x, y);
            if (c.flags & CELL_FLAG_WIDE_CONT) continue;
            if (c.codepoint <= 0x20) continue;
            u32 fg = cell_is_cursor(term, x, y)
                   ? rgba_to_c2d(c.bg)   /* invert: glyph uses cell's bg */
                   : rgba_to_c2d(c.fg);
            float fx = px_off + (x - x_first) * cw, fy = (y - y0) * ch;
            if (c.flags & CELL_FLAG_WIDE) {
                int gi = font_wide_glyph_index(c.codepoint);
                if (gi >= 0)
                    blit_wide(fx, fy, gi, fg, x_scale);
                else
                    blit_narrow(fx, fy, font_glyph_index(c.codepoint),
                                fg, x_scale);
            } else {
                blit_narrow(fx, fy, font_glyph_index(c.codepoint),
                            fg, x_scale);
            }
        }
    }
}

void renderer_draw_terminal_window(renderer_t *r, terminal_t *term,
                                   int y0, int row_count, int col_max) {
    if (!r || !term) return;
    if (col_max > term->cols) col_max = term->cols;
    draw_window(r, term, y0, row_count, 0, col_max, 1.0f, 0.0f);
}

void renderer_draw_terminal(renderer_t *r, terminal_t *term) {
    if (!r || !term) return;
    int rows = (term->rows < r->top_rows) ? term->rows : r->top_rows;
    int cols = (term->cols < r->top_cols) ? term->cols : r->top_cols;
    renderer_draw_terminal_window(r, term, 0, rows, cols);
}

/* Same window renderer, but every cell column is squeezed horizontally by
 * x_scale (a fractional cell maps to 6×x_scale px).  Used for the bottom
 * screen in dual-screen mode: the full 66-column grid is compressed into
 * the 320 px display so the right-edge columns are never clipped — at the
 * cost of slightly softer, narrower text on that screen only.  Row height
 * stays 12 px. */
void renderer_draw_terminal_window_scaledx(renderer_t *r, terminal_t *term,
                                           int y0, int row_count,
                                           int col_max, float x_scale) {
    if (!r || !term) return;
    if (x_scale <= 0.0f) {
        renderer_draw_terminal_window(r, term, y0, row_count, col_max);
        return;
    }
    if (col_max > term->cols) col_max = term->cols;
    draw_window(r, term, y0, row_count, 0, col_max, x_scale, 0.0f);
}

/* Crop-then-squeeze window — TRIM mode for the dual-screen bottom
 * screen: skip `x0` columns on the left and keep `col_max` columns, each
 * squeezed horizontally by `x_scale`, centered in the 320 px panel.  With
 * a small edge trim (e.g. 2 left + 3 right of 66) the remaining 61
 * columns only need ~0.87× scaling — noticeably crisper than the full
 * 0.808× squeeze — while still showing almost the whole grid width.
 * NOTE: an odd `x0` can orphan a wide glyph's right half at the crop seam;
 * pass 2 skips CELL_FLAG_WIDE_CONT cells so the worst case is a blank gap,
 * never garbage. */
void renderer_draw_terminal_window_crop_scaledx(renderer_t *r,
                                                terminal_t *term,
                                                int y0, int row_count,
                                                int x0, int col_max,
                                                float x_scale) {
    if (!r || !term) return;
    if (x0 < 0) x0 = 0;
    if (x0 > term->cols) x0 = term->cols;
    if (col_max > term->cols - x0) col_max = term->cols - x0;
    if (x_scale <= 0.0f || col_max <= 0) {
        renderer_draw_terminal_window(r, term, y0, row_count, col_max);
        return;
    }
    float cw = (float)r->cell_w * x_scale;
    float px_off = ((float)r->bot_px - (float)col_max * cw) / 2.0f;
    draw_window(r, term, y0, row_count, x0, col_max, x_scale, px_off);
}

/* Center-crop window — used by the dual-screen bottom screen when the
 * remote is in a full-screen TUI (term->use_alt): instead of squeezing the
 * whole grid into the 320 px display (~0.808×, soft glyphs), the left/right
 * edge columns are cropped and the middle `col_max` columns render at full
 * 6 px cell width, centered on the panel.  The PTY keeps its real size —
 * the remote app never knows — and for full-screen TUIs (opencode, htop,
 * vim) the middle of the screen is where the content is, so nothing
 * important is lost and the glyphs stay as crisp as the top half. */
void renderer_draw_terminal_window_centered(renderer_t *r, terminal_t *term,
                                            int y0, int row_count,
                                            int col_max) {
    if (!r || !term) return;
    if (col_max > term->cols) col_max = term->cols;
    int x_first = (term->cols - col_max) / 2;
    x_first &= ~1;   /* even → never split a wide glyph's left half */
    /* No right-edge clamp needed: the line above already has
     * col_max <= cols, and x_first = (cols - col_max)/2 (only ever
     * rounded down by & ~1) gives x_first + col_max <= (cols +
     * col_max)/2 <= cols — for a negative col_max too, since the
     * clamp above leaves it unmodified and the sum only shrinks. */
    float px_off = ((float)r->bot_px
                    - (float)col_max * (float)r->cell_w) / 2.0f;
    draw_window(r, term, y0, row_count, x_first, col_max, 1.0f, px_off);
}

/* utf8_next() and renderer_utf8_text_width_px() now live in
 * source/utf8_next.c — see renderer.h.  They are the one part of this
 * file a host can compile and test, so they are no longer static here. */

/* Dispatches one codepoint to either the narrow or wide blit path
 * and returns the pixel advance.  Used by every label helper so CJK
 * candidates (M7's IME bar) render with the right cell width. */
static int draw_codepoint_px(float fx, float fy, float z,
                             uint32_t cp, u32 color) {
    if (font_is_wide(cp)) {
        int gi = font_wide_glyph_index(cp);
        if (gi >= 0) {
            draw_wide_glyph(fx, fy, z, gi, color);
        } else {
            /* Wide font lookup miss — fall back to the narrow notdef
             * so the slot isn't visually empty. */
            draw_glyph(fx, fy, z, font_glyph_index(cp), color);
        }
        return FONT_CELL_W * 2;
    }
    draw_glyph(fx, fy, z, font_glyph_index(cp), color);
    return FONT_CELL_W;
}

void renderer_draw_text(renderer_t *r, int x_cells, int y_cells,
                        const char *text, uint32_t rgba) {
    if (!text) return;
    u32 color = rgba_to_c2d(rgba);
    float fx = (float)(x_cells * r->cell_w);
    float fy = (float)(y_cells * r->cell_h);
    const char *s = text;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        fx += (float)draw_codepoint_px(fx, fy, 0.5f, cp, color);
    }
}

void renderer_draw_text_px(int px, int py, const char *text, uint32_t rgba) {
    renderer_draw_text_px_z(px, py, 0.5f, text, rgba);
}

void renderer_draw_text_px_z(int px, int py, float z,
                             const char *text, uint32_t rgba) {
    if (!text) return;
    u32 color = rgba_to_c2d(rgba);
    float fx = (float)px;
    float fy = (float)py;
    const char *s = text;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        fx += (float)draw_codepoint_px(fx, fy, z, cp, color);
    }
}

static inline void draw_layer_runs_scaled(float fx, float fy, float z,
                                          const uint8_t *rows, u32 color,
                                          int scale) {
    for (int row = 0; row < FA_CELL_H; row++) {
        uint8_t bits = rows[row];
        if (!bits) continue;
        int col = 0;
        while (col < FA_CELL_W) {
            if (bits & (1 << (FA_CELL_W - 1 - col))) {
                int start = col;
                while (col < FA_CELL_W && (bits & (1 << (FA_CELL_W - 1 - col))))
                    col++;
                C2D_DrawRectSolid(fx + start * scale, fy + row * scale, z,
                                  (col - start) * scale, scale, color);
            } else col++;
        }
    }
}

/* Scaled glyph blit: same 3-layer AA as draw_glyph but each lit run
 * becomes a (run × scale) × scale rect.  Drawn at z=0.85 so the popup
 * bubble's label sits above the key but below any later overlay. */
static void draw_glyph_scaled(float fx, float fy, float z,
                              int glyph_idx, u32 color, int scale) {
    if (glyph_idx < 0 || glyph_idx >= FONT_NGLYPHS) return;
    const uint8_t *base = font_glyphs[glyph_idx];
    draw_layer_runs_scaled(fx, fy, z, base, color, scale);

    u8 a = (color >> 24) & 0xff;
    if (a < 2) return;
    u8 b = (color >> 16) & 0xff;
    u8 g = (color >>  8) & 0xff;
    u8 r =  color        & 0xff;
    u32 c_half    = C2D_Color32(r, g, b, a / 2);
    u32 c_quarter = C2D_Color32(r, g, b, a / 4);
    draw_layer_runs_scaled(fx, fy, z, base + FA_CELL_H,       c_half,    scale);
    draw_layer_runs_scaled(fx, fy, z, base + FA_CELL_H * 2,   c_quarter, scale);
}

/* Wide (CJK) glyph at integer block scale — the 2× popup counterpart of
 * draw_glyph_scaled().  Rows are 16-bit because a wide glyph spans two
 * cells.  Wide glyph data carries no AA layers (see blit_wide), so this
 * is a single pass, matching draw_wide_glyph. */
static void draw_wide_glyph_scaled(float fx, float fy, float z,
                                   int glyph_idx, u32 color, int scale) {
    if (glyph_idx < 0 || glyph_idx >= FONT_WIDE_NGLYPHS) return;
    const uint16_t *rows = font_wide_glyphs[glyph_idx];
    int wide = FA_CELL_W * 2;
    for (int row = 0; row < FA_CELL_H; row++) {
        uint16_t bits = rows[row];
        if (!bits) continue;
        int col = 0;
        while (col < wide) {
            if (bits & (uint16_t)(1u << (wide - 1 - col))) {
                int start = col;
                while (col < wide &&
                       (bits & (uint16_t)(1u << (wide - 1 - col))))
                    col++;
                C2D_DrawRectSolid(fx + (float)(start * scale),
                                  fy + (float)(row * scale), z,
                                  (float)((col - start) * scale),
                                  (float)scale, color);
            } else col++;
        }
    }
}

void renderer_draw_text_px_scaled(int px, int py, const char *text,
                                  uint32_t rgba, int scale) {
    if (!text || scale <= 0) return;
    u32 color = rgba_to_c2d(rgba);
    float fx = (float)px;
    float fy = (float)py;
    int step = FONT_CELL_W * scale;
    const char *s = text;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        /* CJK must go through the wide path on both counts — the blit and
         * the advance.  Using the narrow blit for a wide codepoint draws
         * a notdef at half the pitch, and renderer_utf8_text_width_px()
         * (which counts wide as 2 cells) then disagrees with what was
         * actually painted, so anything centred or right-aligned off it
         * lands in the wrong place.  The default UI language is Chinese
         * and the project-page titles it draws here are all CJK. */
        if (font_is_wide(cp)) {
            int gi = font_wide_glyph_index(cp);
            if (gi >= 0)
                draw_wide_glyph_scaled(fx, fy, 0.85f, gi, color, scale);
            else
                draw_glyph_scaled(fx, fy, 0.85f, font_glyph_index(cp),
                                  color, scale);
            fx += step * 2;
        } else {
            draw_glyph_scaled(fx, fy, 0.85f, font_glyph_index(cp),
                              color, scale);
            fx += step;
        }
    }
}

void renderer_draw_rect_cells(renderer_t *r, int x_cells, int y_cells,
                              int w_cells, int h_cells, uint32_t rgba) {
    C2D_DrawRectSolid((float)(x_cells * r->cell_w),
                      (float)(y_cells * r->cell_h), 0.4f,
                      (float)(w_cells * r->cell_w),
                      (float)(h_cells * r->cell_h),
                      rgba_to_c2d(rgba));
}

/* Pixel-precise filled rect with an explicit z-depth — for overlays that
 * are not cell-shaped (a 3 px countdown bar) or that must float above the
 * default 0.5 text layer. */
void renderer_draw_rect_px(int px, int py, int w, int h, float z,
                           uint32_t rgba) {
    if (w <= 0 || h <= 0) return;
    C2D_DrawRectSolid(px, py, z, w, h, rgba_to_c2d(rgba));
}
