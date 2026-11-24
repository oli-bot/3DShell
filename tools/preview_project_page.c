/* Renders the project page's screens to image files on the host.
 *
 * Why this exists: the page's layout is the part of the feature that
 * cannot be reviewed by reading it — whether a 16 px mark crowds the
 * tool's name, whether the card list still reads with an icon in it, how
 * the top screen's title block sits next to a 2× mark.  All of that is
 * one `make preview-project-page` away from being looked at, instead of
 * a flash of the console.
 *
 * It is the same host stub the tests use (tools/stub/pui_host_stubs.c),
 * with text painting switched on: rects come out of the capture
 * framebuffer and glyphs are blitted from the real atlas, so what lands
 * in the image is what the page would draw.  The only thing added here
 * is flattening the framebuffer's raw RGBA over each screen's clear
 * colour, which is what the GPU would do.
 *
 * Writes uncompressed BMP — no image library, no build dependency; the
 * shell script converts to PNG with sips.
 *
 * Run via: ./tools/preview_project_page.sh
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/project_ui.h"
#include "../source/project_presets.h"
#include "stub/pui_host_stubs.h"

#define BOT_W 320
#define BOT_H 240
#define TOP_W 400
#define TOP_H 240

static project_ui_t   *ui;
static dssh_settings_t st;
static kb_pal_t        pal;

/* project_ui_probe_cwd() short-circuits on a NULL ssh.  The stub ignores
 * what it is handed. */
static char ssh_dummy[8];
#define SSH ((ssh_client_t *)ssh_dummy)

/* App colours are 0xRRGGBBAA.  Source-over, forced opaque. */
static uint32_t over(uint32_t src, uint32_t dst) {
    unsigned a = src & 0xffu;
    if (a == 0)   return dst | 0xffu;
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

static void put32(FILE *f, unsigned v) {
    putc(v & 0xff, f); putc((v >> 8) & 0xff, f);
    putc((v >> 16) & 0xff, f); putc((v >> 24) & 0xff, f);
}

static void put16(FILE *f, unsigned v) {
    putc(v & 0xff, f); putc((v >> 8) & 0xff, f);
}

/* Uncompressed 24-bit BMP, rows bottom-up.  `clear` is the colour the
 * render target would have been cleared to before the page drew. */
static void write_bmp(const char *path, int w, int h, uint32_t clear) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }

    int stride = (w * 3 + 3) & ~3;
    int size   = 54 + stride * h;

    putc('B', f); putc('M', f);
    put32(f, (unsigned)size);
    put16(f, 0); put16(f, 0);
    put32(f, 54);                  /* pixel data offset */
    put32(f, 40);                  /* DIB header size */
    put32(f, (unsigned)w);
    put32(f, (unsigned)h);         /* positive = bottom-up */
    put16(f, 1);                   /* planes */
    put16(f, 24);                  /* bits per pixel */
    put32(f, 0);                   /* BI_RGB, uncompressed */
    put32(f, 0);                   /* image size (may be 0 for BI_RGB) */
    put32(f, 2835); put32(f, 2835);
    put32(f, 0); put32(f, 0);

    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t p = over(hstub_fb_at(x, y), clear);
            putc((p >>  8) & 0xff, f);   /* B */
            putc((p >> 16) & 0xff, f);   /* G */
            putc((p >> 24) & 0xff, f);   /* R */
        }
        for (int k = w * 3; k < stride; k++) putc(0, f);
    }
    fclose(f);
    fprintf(stderr, "wrote %s (%dx%d)\n", path, w, h);
}

static void set_proj(int i, const char *name, const char *path,
                     const char *tool, const char *args,
                     const char *cmd, int raw, int uses) {
    dssh_project_t *p = &st.projects[i];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    snprintf(p->path, sizeof(p->path), "%s", path);
    snprintf(p->tool, sizeof(p->tool), "%s", tool);
    snprintf(p->args, sizeof(p->args), "%s", args);
    snprintf(p->cmd,  sizeof(p->cmd),  "%s", cmd);
    p->raw  = raw;
    p->uses = uses;
    p->last_used = 1757836800L - (long)i * 86400L;
}

static pui_act_t key(uint32_t k) {
    return project_ui_tick(ui, &st, SSH, k, 0, NULL, -1, -1, 0, 0, NULL);
}

static void draw_bot_to(const char *path) {
    hstub_reset();
    project_ui_draw_bot(ui, &st, NULL, &pal);
    write_bmp(path, BOT_W, BOT_H, pal.bottom_bg);
}

static void draw_top_to(const char *path) {
    hstub_reset();
    project_ui_draw_top(ui, &st, NULL, &pal);
    /* Both screens are cleared to the palette's background now — the top
     * screen stopped taking the terminal's ink, which is a different table. */
    write_bmp(path, TOP_W, TOP_H, pal.bottom_bg);
}

int main(void) {
    const char *dir = getenv("PREVIEW_DIR");
    char path[512];

    hstub_paint_text = 1;

    const kb_pal_t *p = kb_pal_for(THEME_DARK);
    if (p) pal = *p;

    memset(&st, 0, sizeof(st));
    st.ui_language     = 0;              /* 中文, like the shipped default */
    st.project_count   = 4;
    st.selected_project = 0;
    set_proj(0, "dssh", "/home/me/work/dssh", "claude", "--continue",
             "", 0, 42);
    set_proj(1, "web", "/srv/web", "opencode", "", "", 0, 7);
    set_proj(2, "notes", "/home/me/notes", "gemini", "--yolo", "", 0, 3);
    set_proj(3, "venv", "/srv/app", "aider", "--yes",
             "source .venv/bin/activate && aider", 1, 1);

    ui = project_ui_create();
    if (!ui) return 1;

    /* 1. The card list, as it comes up after a connect. */
    project_ui_open(ui, PUI_ENTRY_RUN, 0, "/home/me/work/dssh");
    snprintf(path, sizeof(path), "%s/card-list.bmp", dir);
    draw_bot_to(path);
    snprintf(path, sizeof(path), "%s/card-list-top.bmp", dir);
    draw_top_to(path);

    /* 2. The 工具 tab — the row the marks were drawn for. */
    key(KEY_X);                          /* edit the selected card */
    key(KEY_R);                          /* Dir → Tool */
    snprintf(path, sizeof(path), "%s/tool-tab.bmp", dir);
    draw_bot_to(path);
    snprintf(path, sizeof(path), "%s/tool-tab-top.bmp", dir);
    draw_top_to(path);

    /* 3. The 参数 tab, which is where the editor's corner mark earns its
     *    keep: it says whose flags are listed. */
    key(KEY_R);                          /* Tool → Args */
    snprintf(path, sizeof(path), "%s/args-tab.bmp", dir);
    draw_bot_to(path);
    snprintf(path, sizeof(path), "%s/args-tab-top.bmp", dir);
    draw_top_to(path);

    project_ui_free(ui);
    return 0;
}
