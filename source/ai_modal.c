#include "ai_modal.h"
#include "kb_theme.h"
#include "renderer.h"
#include "text_fit.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ── Geometry (bottom screen 320×240) ───────────────────────────────
 *
 *   x ∈ [0, 320),  y ∈ [0, 240)
 *
 *   modal      = (6, 6, 308, 228)        — 6 px gap from each edge
 *   padding    = 10                       — inside the modal frame
 *   question   = at y=14, 12-px-tall font, single line, ellipsis on overflow
 *   separator  = horizontal hairline at y=29 (1 px)
 *   answer     = wrap-rendered from y=32 down to y=216 → 14 lines × 13 px
 *
 * z layers — citro2d/citro3d uses higher z = drawn on top.  softkb
 * draws keys around z=0.07-0.30 and renderer_draw_text_px puts label
 * glyphs at z=0.5; the modal layers must therefore sit ABOVE 0.5 so
 * the modal's own text can stack above its own background:
 *   0.55  outside-modal dimming overlay
 *   0.56  modal background
 *   0.57  modal border (subtle)
 *   0.60  separator line
 *   0.62  question row + answer text  (via renderer_draw_text_px_z)
 *
 * Every primitive here goes through the renderer_* wrappers — this TU
 * must not name a C2D_/C3D_ symbol.  Two reasons, same as project_ui.c:
 * renderer_draw_rect_px() already does the 0xRRGGBBAA → C2D_Color32
 * shuffle, so hand-rolling it per call site is four chances to get the
 * byte order wrong; and a TU with no C2D_ reference compiles against
 * tools/stub/citro2d.h, which is what makes the wrap/ellipsis/animation
 * logic host-testable at all.
 *
 * The answer area's line budget is geometric, not a constant: the walk
 * stops when the next row would cross ANSWER_BOTTOM.  There is no
 * "maximum lines" knob to keep in sync.
 */

#define MODAL_X       6
#define MODAL_Y       6
#define MODAL_W       308
#define MODAL_H       228
#define MODAL_PAD     10
#define INNER_X       (MODAL_X + MODAL_PAD)
#define INNER_W       (MODAL_W - 2 * MODAL_PAD)

#define Q_Y           (MODAL_Y + 8)              /* y=14 */
#define SEP_Y         (Q_Y + 14)                 /* y=28 */
#define ANSWER_Y      (SEP_Y + 4)                /* y=32 */
#define ANSWER_BOTTOM (MODAL_Y + MODAL_H - MODAL_PAD)    /* y=224 */
#define LINE_H        13

#define FADE_IN_FRAMES   30      /* 0.5 s @ 60 fps */
#define FADE_OUT_FRAMES  12      /* 0.2 s */

#define COL_OVERLAY_RGB   0x00000000u   /* black; alpha set per-frame */

/* ── Theming ──────────────────────────────────────────────────────────
 * The modal is a raised panel on the bottom screen, so it takes the live
 * keyboard palette like softkb.c and project_ui.c do.  Every role below
 * was picked against a measured WCAG ratio in all five themes; the floors
 * are asserted in tools/test_ai_modal.c, so swapping a role is a test
 * failure rather than a "looks a bit off" in one theme.
 *
 *   body      panel_bg                    one step above the keyboard
 *   hairline  key_top     (1.80-2.36 on panel_bg).  NOT key_border: that
 *                         equals panel_bg in DARK and MONO's is barely
 *                         above it, so the frame would vanish.  key_top is
 *                         the role the theme table guarantees is brighter
 *                         than the surface under it.
 *   answer    label       (10.6-14.5)
 *   question  label@0xB3  (6.0-7.6).  hint_fg is the palette's dim role but
 *                         is tuned against bottom_bg; on panel_bg it falls
 *                         to 2.92 in SOLAR.  Derived from the surface's own
 *                         ink instead — same rule project_ui.c uses.
 *   bullet    label@0x99  (4.8-5.9), a step below the question.
 *   header    batt_warn   (4.1-12.0).  In DARK this IS the 0xe0af68 the
 *                         modal shipped with, so the default look is
 *                         byte-identical to before theming.
 *   code      pinyin_fg   (4.1-12.0) — the palette's ink for content the
 *                         program is echoing back.  It collides with
 *                         batt_warn in MONO only, which that theme's own
 *                         comment blesses: six greys feed 23 roles and
 *                         "unrelated roles legitimately share a value".
 *
 * The scrim stays pure black at a per-frame alpha: dimming whatever is
 * behind is the same job in a light theme as in a dark one, and the
 * modal's own body colour is what carries the theme.
 */
typedef struct {
    uint32_t bg, hairline, question, answer, header, code, bullet;
} am_col_t;

static uint32_t with_alpha(uint32_t rgba, unsigned alpha) {
    return (rgba & 0xFFFFFF00u) | alpha;
}

static am_col_t am_cols(const kb_pal_t *p) {
    am_col_t c;
    c.bg       = p->panel_bg;
    c.hairline = p->key_top;
    c.question = with_alpha(p->label, 0xB3);
    c.answer   = p->label;
    c.header   = p->batt_warn;
    c.code     = p->pinyin_fg;
    c.bullet   = with_alpha(p->label, 0x99);
    return c;
}

/* Falls back to THEME_DARK's palette rather than zeroes if a caller has
 * no palette to hand — kb_pal_for() clamps, so this can never return NULL
 * and a modal can never come out black-on-black. */
static const kb_pal_t *pal_or_default(const kb_pal_t *pal) {
    return pal ? pal : kb_pal_for(THEME_DARK);
}


#define Q_BUF_CAP   384
#define A_BUF_CAP   4096
/* The transformed markdown can grow slightly: each "- " bullet expands
 * to "• " (3 bytes) but each `…`/`**`/etc. shrinks the source.  Allow
 * 1.5 × the raw answer cap as a safe upper bound. */
#define DOC_BUF_CAP (A_BUF_CAP * 3 / 2)

/* Per-byte style attribute looked up at render time to colour each
 * codepoint.  All bytes of a UTF-8 multibyte sequence share the same
 * style (we set them in lock-step inside the transform). */
typedef enum {
    STY_NORMAL = 0,
    STY_HEADER,
    STY_CODE,
    STY_BULLET,
} md_style_t;

typedef struct {
    char         text[DOC_BUF_CAP];   /* markdown-stripped UTF-8 */
    uint8_t      style[DOC_BUF_CAP];  /* parallel — one md_style_t per byte */
    int          len;
} md_doc_t;

typedef enum {
    PHASE_CLOSED = 0,
    PHASE_FADE_IN,
    PHASE_OPEN,
    PHASE_FADE_OUT,
} modal_phase_t;

struct ai_modal_t {
    modal_phase_t phase;
    int           frame;          /* in current phase */
    /* Alpha the modal was showing when close() was requested, 0..256.
     * FADE_OUT interpolates down from this rather than from 256, so
     * closing *during* the fade-in fades out from wherever the fade-in
     * had reached instead of snapping to fully opaque first. */
    int           out_from;
    char          q[Q_BUF_CAP];
    md_doc_t      doc;            /* answer post markdown transform */
};

/* ── tiny UTF-8 walk + width measurement ────────────────────────────
 *
 * These used to be a private decoder, a private copy of font_atlas.c's
 * wide-cell rule and a private ellipsis loop.  All three now come from
 * source/text_fit.h, which is built on the one decoder (utf8_next.c) and
 * the one wide rule (font_is_wide()) — the modal's copy was the second of
 * each, and the wide rule has to agree with font_atlas.c because that is
 * what renderer_draw_text_px() blits. */

/* ── alpha multiply helper ──────────────────────────────────────── */

/* Multiply the alpha channel of an RGBA uint32 by `mul01` ∈ [0..256].
 * 256 = unchanged; 0 = fully transparent. */
static uint32_t alpha_scale(uint32_t rgba, int mul01) {
    if (mul01 >= 256) return rgba;
    if (mul01 <= 0)   return rgba & 0xFFFFFF00u;
    uint32_t a = rgba & 0xFFu;
    a = (a * (uint32_t)mul01) >> 8;
    return (rgba & 0xFFFFFF00u) | (a & 0xFFu);
}

/* ── current animation alpha ────────────────────────────────────── */

static int current_alpha_x256(const ai_modal_t *m) {
    switch (m->phase) {
        case PHASE_CLOSED:    return 0;
        case PHASE_FADE_IN: {
            /* Ease-out:  α = 1 - (1 - t)² */
            int f = m->frame;
            if (f >= FADE_IN_FRAMES) return 256;
            int t = (f * 256) / FADE_IN_FRAMES;
            int u = 256 - t;
            int eased = 256 - ((u * u) >> 8);
            return eased;
        }
        case PHASE_OPEN:      return 256;
        case PHASE_FADE_OUT: {
            int f = m->frame;
            if (f >= FADE_OUT_FRAMES) return 0;
            /* Fade from the alpha the modal was actually showing when
             * close() was called — not from 256.  During FADE_IN that is
             * a small number (frame 0 eases to 0), so starting at 256
             * would flash the panel to full opacity on the first
             * fade-out frame before it started to dim. */
            int from = m->out_from;
            if (from < 0)   from = 0;
            if (from > 256) from = 256;
            return from - ((from * f) / FADE_OUT_FRAMES);
        }
    }
    return 0;
}

/* ── Markdown → (text, style) transform ─────────────────────────────
 *
 * Walks the DeepSeek answer line by line; emits a flat UTF-8 byte
 * stream into doc.text plus a parallel style byte per output byte.
 * The renderer below picks colour by looking up doc.style[i] at byte
 * offset i.  Multi-byte UTF-8 codepoints get the same style on all
 * their bytes (we copy them as a unit).
 *
 * Supported markdown forms:
 *   #/##/### + space + content    → STY_HEADER for the whole line
 *   - / *  + space + content      → "• " prefix (STY_BULLET) + content
 *   ``` line                       → toggles STY_CODE for following lines
 *   `inline code`                  → strip backticks, STY_CODE for content
 *   **bold** / *italic*            → strip asterisks, STY_NORMAL content
 *   ~~strike~~                     → strip ~~, STY_NORMAL content
 *   [text](url)                    → emit text only, drop (url)
 * Anything else passes through verbatim.
 */

static void md_emit_byte(md_doc_t *d, char b, md_style_t s) {
    if (d->len < (int)sizeof(d->text) - 1) {
        d->text[d->len]  = b;
        d->style[d->len] = (uint8_t)s;
        d->len++;
    }
}

static void md_emit_run(md_doc_t *d, const char *p, int n, md_style_t s) {
    for (int i = 0; i < n; i++) md_emit_byte(d, p[i], s);
}

/* Bullet glyph U+2022 in UTF-8: 0xE2 0x80 0xA2.  Followed by a space.
 * Both styled STY_BULLET so the • appears in the dim accent colour
 * regardless of the bullet's content style. */
static void md_emit_bullet_prefix(md_doc_t *d) {
    md_emit_byte(d, (char)0xE2, STY_BULLET);
    md_emit_byte(d, (char)0x80, STY_BULLET);
    md_emit_byte(d, (char)0xA2, STY_BULLET);
    md_emit_byte(d, ' ',         STY_BULLET);
}

/* Walk a single line's content (already stripped of any block-level
 * prefix like #+ or -+) and emit it with inline markdown applied.
 * `base_style` is the line-level style (STY_NORMAL or STY_HEADER for
 * lines under a `#` heading; STY_CODE if we're inside a ``` block).  */
static void md_emit_inline(md_doc_t *d, const char *p, int len,
                           md_style_t base_style) {
    int in_code = 0;     /* toggled by ` */
    int i = 0;
    while (i < len) {
        char c = p[i];

        /* Inline code: ` toggle.  Don't mix with block-level STY_CODE
         * (we leave that to fenced blocks). */
        if (c == '`') {
            in_code = !in_code;
            i++;
            continue;
        }

        /* Bold / italic — just strip syntax, keep content with current
         * (in_code-aware) style. */
        if (c == '*') {
            if (i + 1 < len && p[i + 1] == '*') i += 2;  /* ** */
            else                                  i += 1;  /* *  */
            continue;
        }
        /* Strikethrough ~~ — strip without styling. */
        if (c == '~' && i + 1 < len && p[i + 1] == '~') {
            i += 2;
            continue;
        }
        /* Markdown link [text](url) — render text only, drop url. */
        if (c == '[') {
            int close_bracket = -1;
            for (int j = i + 1; j < len; j++) {
                if (p[j] == ']') { close_bracket = j; break; }
            }
            if (close_bracket > 0 && close_bracket + 1 < len &&
                p[close_bracket + 1] == '(') {
                int close_paren = -1;
                for (int j = close_bracket + 2; j < len; j++) {
                    if (p[j] == ')') { close_paren = j; break; }
                }
                if (close_paren > 0) {
                    md_style_t s = in_code ? STY_CODE : base_style;
                    md_emit_run(d, p + i + 1, close_bracket - i - 1, s);
                    i = close_paren + 1;
                    continue;
                }
            }
            /* Not a real link — pass `[` through. */
        }

        md_style_t s = in_code ? STY_CODE : base_style;
        md_emit_byte(d, c, s);
        i++;
    }
}

static void md_transform(const char *src, md_doc_t *out) {
    out->len = 0;
    if (!src) return;

    int in_code_block = 0;
    const char *p = src;

    while (*p) {
        const char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        int line_len = (int)(line_end - p);

        /* Fenced code: a line whose only non-space content is ``` toggles
         * the in_code_block flag and is itself NOT rendered. */
        int strip_pos = 0;
        while (strip_pos < line_len && (p[strip_pos] == ' ' || p[strip_pos] == '\t'))
            strip_pos++;
        if (line_len - strip_pos >= 3 &&
            p[strip_pos] == '`' && p[strip_pos + 1] == '`' && p[strip_pos + 2] == '`') {
            in_code_block = !in_code_block;
            /* Skip this line entirely. */
            p = (*line_end == '\n') ? line_end + 1 : line_end;
            continue;
        }

        if (in_code_block) {
            /* Whole line is code-styled, no inline markdown. */
            md_emit_run(out, p, line_len, STY_CODE);
        } else {
            /* Detect header: 1-3 leading # then space. */
            int hdr = 0;
            while (hdr < 3 && hdr < line_len && p[hdr] == '#') hdr++;
            if (hdr > 0 && hdr < line_len && p[hdr] == ' ') {
                md_emit_inline(out, p + hdr + 1, line_len - hdr - 1, STY_HEADER);
            } else {
                /* Detect bullet: leading "- " or "* " (after optional spaces). */
                int b = 0;
                while (b < line_len && (p[b] == ' ' || p[b] == '\t')) b++;
                if (b + 1 < line_len && (p[b] == '-' || p[b] == '*') && p[b + 1] == ' ') {
                    /* Preserve any leading indent as-is, then the • prefix. */
                    md_emit_run(out, p, b, STY_NORMAL);
                    md_emit_bullet_prefix(out);
                    md_emit_inline(out, p + b + 2, line_len - b - 2, STY_NORMAL);
                } else {
                    md_emit_inline(out, p, line_len, STY_NORMAL);
                }
            }
        }

        md_emit_byte(out, '\n', STY_NORMAL);
        p = (*line_end == '\n') ? line_end + 1 : line_end;
    }
}

static uint32_t color_for_style(md_style_t s, const am_col_t *c) {
    switch (s) {
        case STY_HEADER:  return c->header;
        case STY_CODE:    return c->code;
        case STY_BULLET:  return c->bullet;
        default:          return c->answer;
    }
}

/* ── public API ─────────────────────────────────────────────────── */

ai_modal_t *ai_modal_init(void) {
    ai_modal_t *m = (ai_modal_t *)calloc(1, sizeof(*m));
    return m;
}

void ai_modal_free(ai_modal_t *m) { free(m); }

void ai_modal_open(ai_modal_t *m, const char *question, const char *answer) {
    if (!m) return;
    snprintf(m->q, sizeof(m->q), "%s", question ? question : "");
    md_transform(answer, &m->doc);
    m->phase = PHASE_FADE_IN;
    m->frame = 0;
    m->out_from = 256;
}

void ai_modal_close(ai_modal_t *m) {
    if (!m) return;
    if (m->phase == PHASE_CLOSED || m->phase == PHASE_FADE_OUT) return;
    /* Capture the on-screen alpha *before* the phase change — this is
     * what FADE_OUT interpolates down from.  Reached mid-FADE_IN the
     * value is small, so the panel dims out from where it had got to;
     * reached from PHASE_OPEN it is 256 and the fade is the original
     * full-to-zero one. */
    m->out_from = current_alpha_x256(m);
    m->phase = PHASE_FADE_OUT;
    m->frame = 0;
}

void ai_modal_tick(ai_modal_t *m) {
    if (!m) return;
    switch (m->phase) {
        case PHASE_CLOSED:
            break;
        case PHASE_FADE_IN:
            m->frame++;
            if (m->frame >= FADE_IN_FRAMES) {
                m->phase = PHASE_OPEN;
                m->frame = 0;
            }
            break;
        case PHASE_OPEN:
            m->frame++;
            break;
        case PHASE_FADE_OUT:
            m->frame++;
            if (m->frame >= FADE_OUT_FRAMES) {
                m->phase = PHASE_CLOSED;
                m->frame = 0;
            }
            break;
    }
}

int ai_modal_visible(const ai_modal_t *m) {
    return m && m->phase != PHASE_CLOSED;
}

int ai_modal_captures_input(const ai_modal_t *m) {
    if (!m) return 0;
    /* FADE_OUT is excluded on purpose — see the header.  The close has
     * already run by then, so the keypad belongs to the shell again. */
    return m->phase == PHASE_FADE_IN || m->phase == PHASE_OPEN;
}

/* Modal text z — must be HIGHER than any modal-bg layer (0.56) so the
 * glyphs render on top of the modal frame instead of getting depth-
 * test-occluded by it. */
#define MODAL_TEXT_Z 0.62f

/* Render the markdown-transformed doc inside the answer area, applying
 * per-byte style colours.  Walks codepoint-by-codepoint, hard-wrapping
 * at INNER_W and yielding one rendered codepoint at a time so we can
 * mix colours within a single visual row (e.g. "press . to toggle"
 * where `.` is highlighted as inline code).
 *
 * Returns 1 if the doc was truncated (didn't fit in the answer area)
 * so the caller can append a "..." indicator. */
static int draw_md_doc(const md_doc_t *doc, int alpha_x256,
                       const am_col_t *c) {
    int x = INNER_X;
    int y = ANSWER_Y;

    int i = 0;
    while (i < doc->len) {
        char ch = doc->text[i];

        /* Hard line break — newline in the source. */
        if (ch == '\n') {
            x = INNER_X;
            y += LINE_H;
            if (y + 12 > ANSWER_BOTTOM) return 1;   /* truncated */
            i++;
            continue;
        }

        /* Decode one UTF-8 codepoint to know its visual width. */
        uint32_t cp;
        int adv = tf_u8_next(doc->text + i, &cp);
        int cw  = tf_cp_w(cp);

        /* Soft wrap — codepoint won't fit on the current visual row. */
        if (x + cw > INNER_X + INNER_W) {
            x = INNER_X;
            y += LINE_H;
            if (y + 12 > ANSWER_BOTTOM) return 1;
        }

        /* Render the single codepoint with the colour mapped from its
         * (per-byte) style attribute, alpha-modulated by the modal's
         * current animation phase.  We have to do this per codepoint
         * because adjacent bytes can have different styles (e.g. the
         * boundary of inline code).  Performance-wise, on a typical
         * answer ~600 codepoints × one draw call each is well within
         * citro2d's per-frame budget. */
        md_style_t s = (md_style_t)doc->style[i];
        uint32_t rgba = alpha_scale(color_for_style(s, c), alpha_x256);

        /* Build a tiny NUL-terminated string for renderer_draw_text_px_z.
         * adv is at most 4 bytes for a UTF-8 codepoint, so 8 is plenty. */
        char one[8];
        int n = adv;
        if (n > 4) n = 4;
        memcpy(one, doc->text + i, (size_t)n);
        one[n] = 0;
        renderer_draw_text_px_z(x, y, MODAL_TEXT_Z, one, rgba);

        x += cw;
        i += adv;
    }
    return 0;
}

/* Draw the question row, ellipsised if the rendered width would overflow.
 *
 * tf_text_clip() (source/text_fit.h) is the same rule the project page's
 * card sub-lines use, and it is the one place the "does the ellipsis still
 * fit" arithmetic lives — this function used to carry a second copy that
 * charged a CJK glyph's first byte (6 px) instead of its real 12 px. */
static void draw_question_with_ellipsis(int x, int y, const char *q,
                                        int max_w, uint32_t rgba) {
    tf_text_clip(x, y, MODAL_TEXT_Z, q, rgba, max_w);
}

void ai_modal_draw(ai_modal_t *m, const kb_pal_t *pal) {
    if (!m || m->phase == PHASE_CLOSED) return;
    int alpha_x256 = current_alpha_x256(m);
    if (alpha_x256 <= 0) return;
    am_col_t c = am_cols(pal_or_default(pal));

    /* 1. Outside-modal dimming overlay covering the full bottom screen. */
    {
        /* Peak overlay = 60 % black; scaled by current animation alpha. */
        uint32_t base = COL_OVERLAY_RGB | 0x99u;       /* 0x99 ≈ 60 % */
        renderer_draw_rect_px(0, 0, 320, 240, 0.55f,
                              alpha_scale(base, alpha_x256));
    }

    /* 2. Modal body. */
    renderer_draw_rect_px(MODAL_X, MODAL_Y, MODAL_W, MODAL_H, 0.56f,
                          alpha_scale(c.bg, alpha_x256));

    /* 3. Subtle 1 px border on all four edges (corners stay clipped
     * because we don't have rounded corners in v1 — pretend they're
     * there via the gap from the screen edges). */
    {
        uint32_t col = alpha_scale(c.hairline, alpha_x256);
        renderer_draw_rect_px(MODAL_X, MODAL_Y, MODAL_W, 1, 0.57f, col);
        renderer_draw_rect_px(MODAL_X, MODAL_Y + MODAL_H - 1,
                              MODAL_W, 1, 0.57f, col);
        renderer_draw_rect_px(MODAL_X, MODAL_Y, 1, MODAL_H, 0.57f, col);
        renderer_draw_rect_px(MODAL_X + MODAL_W - 1, MODAL_Y,
                              1, MODAL_H, 0.57f, col);
    }

    /* 4. Question row (single line + ellipsis).  Drawn at z=0.62 so it
     * sits ABOVE the modal bg (0.56) — using the default z=0.5 in
     * renderer_draw_text_px would put it BELOW the bg and disappear. */
    {
        uint32_t col = alpha_scale(c.question, alpha_x256);
        draw_question_with_ellipsis(INNER_X, Q_Y, m->q, INNER_W, col);
    }

    /* 5. Separator line. */
    renderer_draw_rect_px(INNER_X, SEP_Y, INNER_W, 1, 0.60f,
                          alpha_scale(c.hairline, alpha_x256));

    /* 6. Answer — markdown-styled walk of m->doc.  draw_md_doc handles
     * its own wrapping + per-codepoint colour from the style table. */
    {
        int truncated = draw_md_doc(&m->doc, alpha_x256, &c);
        if (truncated) {
            uint32_t col = alpha_scale(c.answer, alpha_x256);
            renderer_draw_text_px_z(INNER_X + INNER_W - 18,
                                    ANSWER_BOTTOM - LINE_H,
                                    MODAL_TEXT_Z, "...", col);
        }
    }
}
