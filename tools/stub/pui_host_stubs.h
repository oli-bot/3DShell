#ifndef PUI_HOST_STUBS_H
#define PUI_HOST_STUBS_H

/*
 * Test-facing side of tools/stub/pui_host_stubs.c — see that file for
 * what is stubbed and why.  Only tools/test_project_ui.c includes this.
 */

#include <stdint.h>

/* ── Draw capture ───────────────────────────────────────────────────── */

#define HSTUB_LOG_MAX 16384

extern char hstub_log[HSTUB_LOG_MAX];
extern int  hstub_log_len;
extern int  hstub_text_calls;
extern int  hstub_rect_calls;

/* Every renderer_draw_rect_px call is stamped into this in call order,
 * so a test can assert where something was painted and in what colour —
 * which is the only way to check the tool marks.  Text is not painted
 * here (the glyph atlas is not modelled); use hstub_saw() for that. */
#define HSTUB_FB_W 400
#define HSTUB_FB_H 240
extern uint32_t hstub_fb[HSTUB_FB_W * HSTUB_FB_H];

void     hstub_reset(void);
int      hstub_saw(const char *needle);
uint32_t hstub_fb_at(int x, int y);

/* When set, text is also blitted into the framebuffer from the real glyph
 * atlas.  Off by default — the tests assert on rect colours, and a glyph
 * landing on a rect would change what hstub_fb_at() reports there.  The
 * preview tool turns it on. */
extern int hstub_paint_text;

/* ── Aux-channel scripting ──────────────────────────────────────────── */

extern int         hstub_connected;    /* 0 models "not logged in yet" */
extern const char *hstub_pwd;          /* what the `pwd` probe answers */
extern int         hstub_exec_fail;    /* force ssh_aux_exec → NULL */
extern int         hstub_probe_count;  /* probes started so far */
extern char        hstub_last_cmd[64]; /* command the last probe ran */
extern int         hstub_read_chunk;   /* max bytes per ssh_aux_read */

#endif /* PUI_HOST_STUBS_H */
