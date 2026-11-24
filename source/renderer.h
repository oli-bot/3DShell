/*
 * citro2d terminal rendering.
 * Derived from skmtrd/3dssh (MIT License, Copyright (c) 2026 skmtrd).
 */

#pragma once
#include <citro2d.h>
#include "terminal.h"
#include "font_atlas.h"
/* The UTF-8 decoder and the text measurement built on it live in their
 * own citro2d-free translation unit so they can be tested on the host
 * (tools/test_utf8_next.sh).  Included here rather than re-declared so
 * renderer_utf8_text_width_px has exactly one declaration. */
#include "utf8_next.h"

/* Cell size lives in font_atlas.h (FA_CELL_W / FA_CELL_H) since it's a
 * compile-time invariant tied to the bitmap atlas.  Re-export the names
 * the existing renderer code uses. */
#define FONT_CELL_W FA_CELL_W
#define FONT_CELL_H FA_CELL_H

/* Physical panel sizes — the one place the 400×240 / 320×240 numbers are
 * written.  Both the grid macros below and renderer_init derive from
 * them, so a new panel size is one edit instead of however many places
 * happened to spell the number out. */
#define R_TOP_PX_W    400
#define R_BOT_PX_W    320
#define R_PANEL_PX_H  240   /* both panels are 240 px tall */

#define R_TOP_COLS  (R_TOP_PX_W / FONT_CELL_W)   /* 66 at 6×12 */
#define R_TOP_ROWS  (R_PANEL_PX_H / FONT_CELL_H) /* 20 at 6×12 */
#define R_BOT_COLS  (R_BOT_PX_W / FONT_CELL_W)   /* 53 at 6×12 — bottom is 320 px */
#define R_BOT_ROWS  (R_PANEL_PX_H / FONT_CELL_H)

/* On-screen cell size, in pixels, for a terminal grid that exactly fills
 * the 400 px top panel.  This is the *display* geometry; the glyph atlas
 * has its own fixed geometry (FA_CELL_W / FA_CELL_H in font_atlas.h) and
 * every bitmap walk keeps using that.  The two are equal today because
 * nothing scales the bitmaps yet — renderer_t below carries the live
 * values, so a scale becomes one assignment at startup rather than a
 * macro re-derived at each place that needs it. */
typedef struct renderer_t {
    C3D_RenderTarget *top;
    C3D_RenderTarget *bot;
    int cell_w, cell_h;      /* one terminal cell, on screen */
    int top_cols, top_rows;  /* grid that fills the 400×240 top panel */
    int bot_px;              /* bottom panel width — squeeze/center math */
    int bot_cols;            /* full-width cells that fit the bottom panel */
} renderer_t;

renderer_t *renderer_init(C3D_RenderTarget *top, C3D_RenderTarget *bot);
void        renderer_free(renderer_t *r);
void        renderer_draw_terminal(renderer_t *r, terminal_t *term);

/* Draw a sub-window of the terminal grid: rows [y0, y0+row_count) and the
 * leftmost `col_max` columns, starting in the top-left corner of the active
 * render target.  Both bounds are absolute terminal coordinates.
 *
 * This is how the dual-screen mode renders one terminal split across
 * both physical screens: the top target draws rows 0..R_TOP_ROWS at full
 * width, the bottom target draws rows R_TOP_ROWS..2*R_TOP_ROWS (via
 * renderer_draw_terminal_window_scaledx so the 66-col grid fits the 320 px
 * bottom display).  The cursor renders only when it falls inside the
 * drawn window. */
void renderer_draw_terminal_window(renderer_t *r, terminal_t *term,
                                   int y0, int row_count, int col_max);

/* Same window, but each column is squeezed horizontally to `x_scale` of its
 * 6 px width.  This is how the dual-screen mode fits the full 66-column
 * grid onto the 320 px bottom screen without clipping: rows R_TOP_ROWS..
 * 2*R_TOP_ROWS are compressed into the display (x_scale ≈ 320/396), so no
 * columns are lost — the bottom half just renders ~20% narrower and softer
 * than the crisp full-scale top half.  Row height is unchanged. */
void renderer_draw_terminal_window_scaledx(renderer_t *r, terminal_t *term,
                                           int y0, int row_count,
                                           int col_max, float x_scale);

/* Crop-then-squeeze window — TRIM mode: skip `x0` left columns, keep
 * `col_max` columns squeezed by `x_scale`, centered in the 320 px panel.
 * A light edge trim lets the rest render at a milder scale (~0.90 vs
 * 0.808 full squeeze) so the bottom glyphs stay closer to full-width
 * crispness while losing only the far-edge columns. */
void renderer_draw_terminal_window_crop_scaledx(renderer_t *r,
                                                terminal_t *term,
                                                int y0, int row_count,
                                                int x0, int col_max,
                                                float x_scale);

/* Center-crop window — used by the dual-screen bottom screen when the
 * remote is in a full-screen TUI (term->use_alt): instead of squeezing the
 * whole grid, the left/right edge columns are cropped and the middle
 * `col_max` columns render at full 6 px cell width, centered on the panel.
 * The PTY keeps its real size — the remote app never knows — and for
 * full-screen TUIs the middle of the screen is where the content is, so
 * nothing important is lost and the glyphs stay as crisp as the top half. */
void renderer_draw_terminal_window_centered(renderer_t *r, terminal_t *term,
                                            int y0, int row_count,
                                            int col_max);

/* Bottom-screen status panel (M3 helper). Draws a single line of text at
 * (x_cells, y_cells) on the bottom render target. Must be called inside
 * C2D_SceneBegin(bot). Color is RGBA 0xRRGGBBAA. */
void renderer_draw_text(renderer_t *r, int x_cells, int y_cells,
                        const char *text, uint32_t rgba);

/* Pixel-precise version — draws each glyph at exact pixel coords.  Use
 * this when cell-grid rounding would mis-center labels (e.g. a 32 px
 * key cannot be exactly centered on a 6 px cell grid). */
void renderer_draw_text_px(int px, int py, const char *text, uint32_t rgba);

/* Same as renderer_draw_text_px but with an explicit z-depth.  Used by
 * the AI-ask modal which needs its text drawn ABOVE its own background
 * (the default 0.5 z would put text below the modal bg). */
void renderer_draw_text_px_z(int px, int py, float z,
                             const char *text, uint32_t rgba);

/* Same as renderer_draw_text_px but each glyph pixel is rendered as a
 * scale×scale block.  Used for the iOS-style key-press popup bubble
 * which shows the tapped character at 2× normal size. */
void renderer_draw_text_px_scaled(int px, int py, const char *text,
                                  uint32_t rgba, int scale);

/* Filled rect on bottom screen, cell-aligned. */
void renderer_draw_rect_cells(renderer_t *r, int x_cells, int y_cells,
                              int w_cells, int h_cells, uint32_t rgba);

/* Filled rect at exact pixel coords with an explicit z-depth, for overlays
 * that aren't cell-shaped (a 3 px bar) or must float above the default text
 * layer.  Color is RGBA 0xRRGGBBAA.  No-op when w/h <= 0. */
void renderer_draw_rect_px(int px, int py, int w, int h, float z,
                           uint32_t rgba);
