/*
 * Host-build stubs for the project-page test (tools/test_project_ui.c).
 *
 * project_ui.c touches the rest of DSSH through a deliberately narrow
 * surface — four renderer draw calls, two terminal colour getters and
 * the aux-channel API.  Standing those in lets the whole UI state
 * machine run on the host, which is where the bugs that matter live:
 * which tab a card opens on, whether switching tabs re-derives the
 * "run the hand-written command" flag, whether a delete needs its
 * second press, what the probe does with a failed answer.
 *
 * The renderer stubs do not draw anything; they append every string
 * they are handed to a capture log.  That is what lets a test assert on
 * what the screen would say ("this card's top screen shows the custom
 * command, not the three-part form") without a framebuffer.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "project_ui.h"
#include "renderer.h"
#include "terminal.h"
#include "ssh_client.h"

/* The log and framebuffer sizes live in the header, so the test and this
 * file cannot disagree about them. */
#include "pui_host_stubs.h"

/* ── Capture log ────────────────────────────────────────────────────── */

char hstub_log[HSTUB_LOG_MAX];
int  hstub_log_len   = 0;
int  hstub_text_calls = 0;
int  hstub_rect_calls = 0;

/* ── Framebuffer ──────────────────────────────────────────────────────
 *
 * The text log answers "what would the screen say"; this answers "what
 * colour would this pixel be".  It is what makes the tool marks testable:
 * the log cannot tell a mark drawn at the right offset from one drawn at
 * the wrong offset, and a mark that is one grid row too low overlaps the
 * description.  400×240 covers both panels, and a test only ever draws one
 * of them before reading, so the two never mix.
 *
 * ⚠️ This used to composite purely in CALL ORDER, on the assumption that
 * "the page never draws two opaque things over each other in the other
 * order".  That assumption was wrong and it hid a device-only defect for a
 * whole round: machine_ui.c drew a named row's two text lines at z 0.55 and
 * 0.50 and its row fill at 0.60, so citro2d (which DEPTH-TESTS) drew two
 * empty bars — while this stub, which threw z away, happily painted the
 * text and every host assertion passed.  The 3DS run is what caught it
 * (build/.verify/analyze_picker2.py found the text box still pure fill).
 *
 * So the framebuffer now carries a depth buffer and enforces the same rule
 * the GPU does: a write at a z LOWER than what is already there is dropped.
 * Equal z still wins (later wins at the same depth), which is what the
 * existing pages rely on when they draw two things at one level.
 *
 * The consequence is that "the text is above its surface" is now a host
 * assertion rather than something only a device run can see.  Keep it that
 * way: a page that wants to draw something behind a surface it has already
 * painted has to say so, and no page does. */
uint32_t hstub_fb[HSTUB_FB_W * HSTUB_FB_H];

/* z of the nearest write at each pixel.  citro2d is HIGHER-z-in-front, so
 * "nothing here yet" has to be a z so low that every real one beats it —
 * it is the far plane, not the near one.  Getting this backwards makes the
 * first write at every pixel lose to the sentinel and the whole screen
 * come back blank, which is exactly what the suite did the first time. */
#define HSTUB_Z_FAR (-1e9f)
static float hstub_depth[HSTUB_FB_W * HSTUB_FB_H];

void hstub_reset(void) {
    hstub_log[0]  = 0;
    hstub_log_len = 0;
    hstub_text_calls = 0;
    hstub_rect_calls = 0;
    memset(hstub_fb, 0, sizeof(hstub_fb));
    for (int i = 0; i < HSTUB_FB_W * HSTUB_FB_H; i++)
        hstub_depth[i] = HSTUB_Z_FAR;
}

/* Colour last painted at (x,y), or 0 where nothing was painted. */
uint32_t hstub_fb_at(int x, int y) {
    if (x < 0 || y < 0 || x >= HSTUB_FB_W || y >= HSTUB_FB_H) return 0;
    return hstub_fb[y * HSTUB_FB_W + x];
}

/* ── Text painting (preview only) ─────────────────────────────────────
 *
 * Off by default.  The tests want the log and assert on the colours rects
 * were given, and a glyph landing on a rect would change what
 * hstub_fb_at() reports there — so the two modes are kept apart rather
 * than made to agree.
 *
 * tools/preview_project_page.c turns it on, which is what makes the
 * preview images show words: the glyphs are blitted from the real atlas
 * (source/font_data.c, linked for real) with the same three-layer
 * anti-aliasing renderer.c uses, composited here instead of by the GPU.
 */
int hstub_paint_text = 0;

/* App colours are 0xRRGGBBAA; source-over, keeping the destination's
 * alpha (everything in this framebuffer ends up opaque or clear). */
static uint32_t hstub_over(uint32_t src, uint32_t dst) {
    unsigned a = src & 0xffu;
    if (a == 0)   return dst;
    if (a == 255) return src;
    unsigned sr = (src >> 24) & 0xffu, sg = (src >> 16) & 0xffu,
             sb = (src >>  8) & 0xffu;
    unsigned dr = (dst >> 24) & 0xffu, dg = (dst >> 16) & 0xffu,
             db = (dst >>  8) & 0xffu;
    unsigned r = (sr * a + dr * (255u - a)) / 255u;
    unsigned g = (sg * a + dg * (255u - a)) / 255u;
    unsigned b = (sb * a + db * (255u - a)) / 255u;
    return (r << 24) | (g << 16) | (b << 8) | 0xffu;
}

static void hstub_put(int x, int y, float z, uint32_t rgba) {
    if (x < 0 || y < 0 || x >= HSTUB_FB_W || y >= HSTUB_FB_H) return;
    int o = y * HSTUB_FB_W + x;
    if (z < hstub_depth[o]) return;      /* behind what is already there */
    hstub_depth[o] = z;
    /* Test mode stores the colour as handed over; preview mode composites.
     * The two want different things and only one of them can be right.
     *
     * A test reads this framebuffer as a PALETTE PROBE: "the header rule is
     * the palette's label at 0x55" is a claim about which palette entry was
     * drawn, and the assertion has to be able to name it.  Compositing
     * would replace that with this file's byte-space approximation of the
     * GPU's blend — which is neither the palette constant the test wants
     * nor the value the device would produce, since the hardware blends in
     * float.  So the semi-transparent hairline would be asserted against a
     * fiction.  (It is the same reason the log exists at all: assert on
     * what was drawn, not on a re-derivation of it.)
     *
     * The preview wants the opposite — a picture — so it composites, which
     * is what makes the anti-aliased glyph passes in hstub_blit land
     * correctly over a background instead of showing up as three quarters-
     * transparent pixels.  That is the same split as hstub_paint_text
     * itself, so it costs nothing to keep them together. */
    hstub_fb[o] = hstub_paint_text ? hstub_over(rgba, hstub_fb[o]) : rgba;
}

/* One glyph: three passes, full / half / quarter alpha, exactly as
 * renderer.c's draw_glyph does it. */
static void hstub_blit(int px, int py, int scale, int wide,
                       int glyph_idx, uint32_t rgba, float z) {
    if (glyph_idx < 0) return;
    unsigned base_a = rgba & 0xffu;
    int w = wide ? FA_CELL_W * 2 : FA_CELL_W;

    for (int pass = 0; pass < 3; pass++) {
        unsigned a = base_a;
        if (pass == 1)      a /= 2;
        else if (pass == 2) a /= 4;
        if (a == 0) continue;
        uint32_t c = (rgba & 0xffffff00u) | a;

        for (int r = 0; r < FA_CELL_H; r++) {
            unsigned bits;
            if (wide) {
                if (glyph_idx >= FONT_WIDE_NGLYPHS) return;
                bits = font_wide_glyphs[glyph_idx][r];
            } else {
                if (glyph_idx >= FONT_NGLYPHS) return;
                bits = font_glyphs[glyph_idx][pass * FA_CELL_H + r];
            }
            if (!bits) continue;
            for (int col = 0; col < w; col++) {
                if (!(bits & (1u << (w - 1 - col)))) continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        hstub_put(px + col * scale + sx,
                                  py + r * scale + sy, z, c);
            }
        }
        if (wide) break;      /* wide glyphs have no AA layers */
    }
}

static void hstub_paint_string(int px, int py, int scale, const char *text,
                               uint32_t rgba, float z) {
    const unsigned char *s = (const unsigned char *)text;
    int fx = px;
    while (*s) {
        uint32_t cp;
        int len;
        if (s[0] < 0x80)                 { cp = s[0];            len = 1; }
        else if ((s[0] & 0xE0) == 0xC0)  { cp = s[0] & 0x1F;     len = 2; }
        else if ((s[0] & 0xF0) == 0xE0)  { cp = s[0] & 0x0F;     len = 3; }
        else if ((s[0] & 0xF8) == 0xF0)  { cp = s[0] & 0x07;     len = 4; }
        else                             { cp = '?';             len = 1; }
        for (int i = 1; i < len && s[i]; i++) cp = (cp << 6) | (s[i] & 0x3F);

        if (font_is_wide(cp)) {
            hstub_blit(fx, py, scale, 1, font_wide_glyph_index(cp), rgba, z);
            fx += FA_CELL_W * 2 * scale;
        } else {
            hstub_blit(fx, py, scale, 0, font_glyph_index(cp), rgba, z);
            fx += FA_CELL_W * scale;
        }
        s += len;
    }
}

static void hstub_append(const char *text) {
    if (!text) return;
    size_t n = strlen(text);
    if ((size_t)hstub_log_len + n + 2 >= (size_t)HSTUB_LOG_MAX) return;
    memcpy(hstub_log + hstub_log_len, text, n);
    hstub_log_len += (int)n;
    hstub_log[hstub_log_len++] = '\n';
    hstub_log[hstub_log_len]   = 0;
}

/* Returns non-zero if `needle` appears anywhere in what was drawn since
 * the last hstub_reset(). */
int hstub_saw(const char *needle) {
    return needle && *needle && strstr(hstub_log, needle) != NULL;
}

/* ── renderer_t ─────────────────────────────────────────────────────── */

void renderer_draw_text_px_z(int px, int py, float z,
                             const char *text, uint32_t rgba) {
    hstub_text_calls++;
    hstub_append(text);
    if (hstub_paint_text) hstub_paint_string(px, py, 1, text, rgba, z);
}

/* 0.85f is not a choice made here — it is the depth renderer.c hard-codes
 * for this entry point (renderer.c:517/519/523).  Mirroring it matters:
 * a page that draws scaled text on a surface and relies on it landing in
 * front has to be told the truth about what the device will do, and this
 * stub is the only place that can say it without a 3DS. */
void renderer_draw_text_px_scaled(int px, int py, const char *text,
                                  uint32_t rgba, int scale) {
    hstub_text_calls++;
    hstub_append(text);
    if (hstub_paint_text) hstub_paint_string(px, py, scale, text, rgba, 0.85f);
}

/* renderer_utf8_text_width_px() is NOT stubbed any more.  It used to be a
 * local re-implementation here, because the tests did not link
 * source/utf8_next.c — but source/text_fit.h made the pages' clipping
 * depend on utf8_next() as well, so the real translation unit comes along
 * now and brings the real width function with it.  That is strictly
 * better: the stub's version had its own lax decoder (it accepted overlong
 * forms and truncated sequences), while utf8_next() rejects exactly what
 * RFC 3629 excludes.  Both agree on well-formed text, which is all the
 * fixtures use, and the real one is what the device runs. */

void renderer_draw_rect_cells(renderer_t *r, int x_cells, int y_cells,
                              int w_cells, int h_cells, uint32_t rgba) {
    (void)r; (void)x_cells; (void)y_cells; (void)w_cells; (void)h_cells;
    (void)rgba;
    hstub_rect_calls++;
}

void renderer_draw_rect_px(int px, int py, int w, int h, float z,
                           uint32_t rgba) {
    if (w <= 0 || h <= 0) return;
    hstub_rect_calls++;
    for (int y = py; y < py + h; y++) {
        if (y < 0 || y >= HSTUB_FB_H) continue;
        for (int x = px; x < px + w; x++) {
            if (x < 0 || x >= HSTUB_FB_W) continue;
            /* Through hstub_put, not straight into the framebuffer: a fill
             * that ignores z would let a surface painted at a near depth
             * still be covered by text painted at a far one, which is
             * exactly the defect this depth buffer exists to catch.  An
             * opaque fill still lands byte-identical (hstub_over returns
             * src unchanged at a==255), so the colour assertions the tests
             * already make are unaffected. */
            hstub_put(x, y, z, rgba);
        }
    }
}

/* ── terminal_t ─────────────────────────────────────────────────────── */

uint32_t terminal_get_bg(const terminal_t *term) {
    (void)term;
    return 0x181825FFu;                 /* THEME_DARK bottom_bg */
}

uint32_t terminal_get_fg(const terminal_t *term) {
    (void)term;
    return 0xCDD6F4FFu;
}

/* ── aux channel (the `pwd` probe) ──────────────────────────────────── */

struct ssh_aux_channel_t {
    const char *payload;
    int         pos;
    int         closed;
};

/* The test sets these.  `hstub_connected = 0` models "not logged in
 * yet", which is what a card editor opened from the connect-time picker
 * would see if the session dropped. */
int         hstub_connected = 1;
const char *hstub_pwd       = "/home/me/work/dssh\n";
int         hstub_exec_fail = 0;      /* force ssh_aux_exec → NULL */
int         hstub_probe_count = 0;    /* how many probes were started */
char        hstub_last_cmd[64] = {0};

/* Read at most this many bytes per ssh_aux_read call, so the probe's
 * "dribble across frames" path is exercised rather than short-circuited. */
int hstub_read_chunk = 4096;

int ssh_is_connected(ssh_client_t *ssh) {
    (void)ssh;
    return hstub_connected;
}

ssh_aux_channel_t *ssh_aux_exec(ssh_client_t *ssh, const char *cmd,
                                char *err_buf, int err_sz) {
    (void)ssh;
    hstub_probe_count++;
    snprintf(hstub_last_cmd, sizeof(hstub_last_cmd), "%s", cmd ? cmd : "");
    if (err_buf && err_sz > 0) err_buf[0] = 0;
    if (hstub_exec_fail) return NULL;
    ssh_aux_channel_t *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->payload = hstub_pwd ? hstub_pwd : "";
    return a;
}

int ssh_aux_read(ssh_aux_channel_t *aux, char *buf, int len) {
    if (!aux || aux->closed || len <= 0) return -1;
    int left = (int)strlen(aux->payload) - aux->pos;
    if (left <= 0) return 0;
    int n = left;
    if (n > len) n = len;
    if (n > hstub_read_chunk) n = hstub_read_chunk;
    memcpy(buf, aux->payload + aux->pos, (size_t)n);
    aux->pos += n;
    return n;
}

int ssh_aux_eof(const ssh_aux_channel_t *aux) {
    if (!aux) return 1;
    return aux->pos >= (int)strlen(aux->payload);
}

void ssh_aux_close(ssh_aux_channel_t *aux) {
    if (!aux) return;
    aux->closed = 1;
    free(aux);
}

/* project_ui.c calls this; the host build has no remote. */
int ssh_aux_write(ssh_aux_channel_t *aux, const char *buf, int len) {
    (void)aux; (void)buf; (void)len;
    return -1;
}

int ssh_aux_send_eof(ssh_aux_channel_t *aux) {
    (void)aux;
    return 0;
}
