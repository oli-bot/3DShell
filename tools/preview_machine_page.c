/* Renders the machine picker's screens to image files on the host.
 *
 * Why this exists: this page's two new rules are both things you have to
 * LOOK at to review — whether a row still reads with a name above an
 * address, and whether the top half and the bottom half still look like one
 * page once the top half started taking the keyboard palette instead of the
 * terminal's ink.  "It passed the contrast checks" is not the same claim.
 *
 * The point of the theme sweep is the second half of that: five palettes,
 * one page.  A role that is right in Tokyo Night and unreadable in Solarized
 * is invisible in a single render, which is exactly how the page family got
 * its accent-as-text and dimmed-address defects in the first place.
 *
 * It is the same host stub the tests use (tools/stub/pui_host_stubs.c), with
 * text painting switched on: rects come out of the capture framebuffer and
 * glyphs are blitted from the real atlas, so what lands in the image is what
 * the page would draw.  The only thing added here is flattening the
 * framebuffer's raw RGBA over each screen's clear colour, which is what the
 * GPU would do — and that clear colour is the palette's bottom_bg for BOTH
 * screens, because render_frame() clears both that way while a page owns
 * them.
 *
 * Writes uncompressed BMP — no image library, no build dependency; the shell
 * script converts to PNG with sips.
 *
 * Run via: ./tools/preview_machine_page.sh
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/machine_ui.h"
#include "stub/pui_host_stubs.h"

#define BOT_W 320
#define BOT_H 240
#define TOP_W 400
#define TOP_H 240

static machine_ui_t *ui;
static ssh_config_t  cfg;

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

/* Uncompressed 24-bit BMP, rows bottom-up.  `clear` is the colour the render
 * target would have been cleared to before the page drew. */
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

/* One machine.  `name` may be NULL or empty, which is the layout every
 * config.ini written before name_N existed produces. */
static void set_machine(int i, const char *name, const char *host,
                        const char *user, int port, const char *key) {
    ssh_machine_t *m = &cfg.machines[i];
    memset(m, 0, sizeof(*m));
    if (name) snprintf(m->name, sizeof(m->name), "%s", name);
    snprintf(m->host, sizeof(m->host), "%s", host);
    snprintf(m->user, sizeof(m->user), "%s", user);
    m->port = port;
    snprintf(m->key_path, sizeof(m->key_path), "%s", key);
}

/* The realistic mix: named machines beside ones that never got a name, and
 * one long enough to need the ellipsis.  A fixture where every row is the
 * same shape would hide the layout question this preview is for. */
static void fill_fixture(int n) {
    static const char *names[] = {"家里的群晖", "build-box", NULL, "笔记本",
                                  NULL, "media", "pi-4", NULL};
    static const char *hosts[] = {"nas.lan", "build.example.com",
                                  "jump.example.com", "macbook.local",
                                  "lab.example.com",
                                  "a-very-long-hostname-for-a-media-server."
                                  "example.com", "pi.lan", "old.example.com"};
    static const char *users[] = {"me", "ubuntu", "root", "me",
                                  "lab", "media", "pi", "root"};
    memset(&cfg, 0, sizeof(cfg));
    cfg.machine_count = n;
    for (int i = 0; i < n; i++) {
        char key[64];
        snprintf(key, sizeof(key), "sdmc:/3ds/3dssh/id_%d", i);
        set_machine(i, names[i], hosts[i], users[i],
                    i == 1 ? 2222 : 22, key);
    }
}

static const struct {
    term_theme_t theme;
    const char  *slug;
    const char  *title;
} THEMES[] = {
    { THEME_DARK,   "dark",   "Tokyo Night" },
    { THEME_MATRIX, "matrix", "Matrix"      },
    { THEME_MONO,   "mono",   "Mono"        },
    { THEME_SOLAR,  "solar",  "Solarized"   },
    { THEME_CUSTOM, "custom", "Custom"      },
};

static void draw_bot_to(const char *path, const kb_pal_t *pal) {
    hstub_reset();
    machine_ui_draw_bot(ui, &cfg, NULL, pal);
    write_bmp(path, BOT_W, BOT_H, pal->bottom_bg);
}

static void draw_top_to(const char *path, const kb_pal_t *pal) {
    hstub_reset();
    machine_ui_draw_top(ui, &cfg, NULL, pal);
    /* Both screens clear to the palette's background while a page owns
     * them — the top screen stopped taking the terminal's ink, which is a
     * different table. */
    write_bmp(path, TOP_W, TOP_H, pal->bottom_bg);
}

static void key(uint32_t k) {
    int ignored = -1;
    machine_ui_tick(ui, &cfg, k, 0, NULL, -1, -1, 0, 0, &ignored);
}

static void type(const char *kt) {
    int ignored = -1;
    machine_ui_tick(ui, &cfg, 0, 0, kt, -1, -1, 0, 0, &ignored);
}

int main(void) {
    const char *dir = getenv("PREVIEW_DIR");
    char path[512];

    hstub_paint_text = 1;

    ui = machine_ui_create();
    if (!ui) return 1;

    /* ── 1. The list, on all five palettes ──────────────────────────
     *
     * Five machines, four names, one of them long — the mix the list is
     * actually used with.  The highlight sits on machine 2 so a selected
     * row and a resting row are both in frame: the two fills have
     * different inks, and that is the part a palette change can break. */
    for (unsigned t = 0; t < sizeof(THEMES) / sizeof(THEMES[0]); t++) {
        const kb_pal_t *pal = kb_pal_for(THEMES[t].theme);
        fill_fixture(5);
        machine_ui_open(ui, &cfg, 1, 0);

        snprintf(path, sizeof(path), "%s/list-%s-bot.bmp", dir,
                 THEMES[t].slug);
        draw_bot_to(path, pal);
        snprintf(path, sizeof(path), "%s/list-%s-top.bmp", dir,
                 THEMES[t].slug);
        draw_top_to(path, pal);
    }

    /* ── 2. Eight machines, scrolled to the bottom ───────────────────
     *
     * The window, not the palette: this is the frame that used to be blank
     * (the highlight was 64 px below the screen) and the one where a row
     * used to land on the footer hint line. */
    {
        const kb_pal_t *pal = kb_pal_for(THEME_DARK);
        fill_fixture(8);
        machine_ui_open(ui, &cfg, 0, 0);
        for (int i = 0; i < 7; i++) key(KEY_DOWN);
        snprintf(path, sizeof(path), "%s/scroll-eight-bot.bmp", dir);
        draw_bot_to(path, pal);
        snprintf(path, sizeof(path), "%s/scroll-eight-top.bmp", dir);
        draw_top_to(path, pal);
    }

    /* ── 3. The rename field, mid-word ───────────────────────────────
     *
     * Typed into rather than opened and left blank, so the caret, the echo
     * of the draft and the byte counter are all in frame. */
    {
        const kb_pal_t *pal = kb_pal_for(THEME_DARK);
        fill_fixture(5);
        machine_ui_open(ui, &cfg, 0, 0);
        key(KEY_X);
        type("\xe5\xae\xb6\xe9\x87\x8c");     /* 家里 */
        snprintf(path, sizeof(path), "%s/rename-dark-top.bmp", dir);
        draw_top_to(path, pal);
    }

    /* ── 4. English, to catch a footer that only fits in one language ──
     *
     * The hint line grew a "✎ rename" entry with this feature, and the
     * Chinese and English strings are different widths. */
    {
        const kb_pal_t *pal = kb_pal_for(THEME_DARK);
        fill_fixture(5);
        machine_ui_open(ui, &cfg, 0, 1);
        snprintf(path, sizeof(path), "%s/list-en-bot.bmp", dir);
        draw_bot_to(path, pal);
    }

    machine_ui_free(ui);
    return 0;
}
