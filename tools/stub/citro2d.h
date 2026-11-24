#ifndef CITRO2D_HOST_STUB_H
#define CITRO2D_HOST_STUB_H

/*
 * Host-build stand-in for <citro2d.h>.
 *
 * source/renderer.h includes <citro2d.h> for exactly one thing: the
 * C3D_RenderTarget pointers it keeps.  project_ui.c includes citro2d.h
 * transitively but calls no C2D_* / C3D_* symbol of its own — every draw
 * it does goes through the renderer_draw_* wrappers.  So a typedef plus
 * the key bits are enough to compile the whole UI state machine on the
 * host, which is where the interesting bugs live (which tab a card
 * opens on, whether switching tabs re-derives the "hand-written
 * command" flag, cursor rings, the card command composition).
 *
 * The KEY_* bits this header used to carry now live in tools/stub/3ds.h,
 * copied verbatim from $DEVKITPRO/libctru/include/3ds/services/hid.h —
 * one copy, so a test that presses KEY_DRIGHT presses the same bit the
 * device does.  Do not renumber them.
 *
 * This header exists ONLY for the host tests under tools/.  It is not on
 * the 3DS include path and must never be: on the device the real
 * <citro2d.h> is found first.
 */

#include "3ds.h"

typedef struct C3D_RenderTarget C3D_RenderTarget;

#endif /* CITRO2D_HOST_STUB_H */
