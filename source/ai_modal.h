#ifndef AI_MODAL_H
#define AI_MODAL_H

#include "kb_theme.h"

/* Rounded-corner bottom-screen modal that displays the (question, answer)
 * returned by the M12 voice AI-ask path.  Sits over the soft keyboard,
 * 6 px gap from the screen edges, dims everything behind it with a 60 %
 * black overlay, and fades in over 0.5 s when ai_modal_open() is called.
 *
 * Inputs (q + a) are *copied*; the caller can free them afterwards.
 *
 * Interaction is handled in main.c:
 *   A      → caller calls ai_modal_close() + voice_ai_close_keep()
 *   B      → caller calls ai_modal_close() + voice_ai_close_clear()
 *   touch  → same as B
 *
 * The modal itself does not consume input — main.c routes those events.
 */

typedef struct ai_modal_t ai_modal_t;

ai_modal_t *ai_modal_init(void);
void        ai_modal_free(ai_modal_t *m);

/* Begin showing the modal (resets to start of fade-in animation).
 * Strings are copied internally up to a fixed cap; long answers are
 * truncated with a trailing "…". */
void ai_modal_open(ai_modal_t *m, const char *question, const char *answer);

/* Begin fade-out animation (~0.2 s).  Once it finishes, ai_modal_visible
 * will start returning 0 and the soft keyboard regains its surface.
 * Idempotent. */
void ai_modal_close(ai_modal_t *m);

/* Advance per-frame state (animation timers).  Call once per frame. */
void ai_modal_tick(ai_modal_t *m);

/* Render the modal — must be called inside C2D_SceneBegin(bottom_target),
 * AFTER the soft keyboard / clock / mascot have already drawn so this
 * lays over them.
 *
 * `pal` is the live keyboard palette (softkb_palette()); the modal is a
 * raised panel on the bottom screen, so it wears the same colours as the
 * keyboard behind it.  Pass NULL for THEME_DARK's palette — kb_pal_for()
 * clamps, so there is no way to render a black-on-black panel. */
void ai_modal_draw(ai_modal_t *m, const kb_pal_t *pal);

/* True iff the modal currently occupies the bottom screen (including
 * fade-in / fade-out animation frames).  main.c uses this for *layout*
 * precedence — it must stay 1 through the fade-out so nothing pops up
 * over a panel the user can still see. */
int  ai_modal_visible(const ai_modal_t *m);

/* True iff the modal should own the keypad / touch this frame: fade-in
 * and fully-open, but NOT the fade-out.
 *
 * The two are deliberately not the same question.  ai_modal_close() is
 * called the instant the user dismisses, which drops voice back to IDLE
 * and starts a ~0.2 s (12-frame) fade-out.  For those 12 frames the
 * panel is still painted but logically gone, so gating input on
 * ai_modal_visible() would:
 *   - swallow every keypress, including the soft keyboard, for 0.2 s
 *     after each dismissal, and
 *   - silently drop a second dismiss gesture (B / tap) arriving in that
 *     window, because voice_ai_close_clear() has already left
 *     VOICE_AI_SHOWING and returns early.
 * Fade-in *does* capture, because that is the modal arriving while the
 * user is still reading the answer it was opened for. */
int  ai_modal_captures_input(const ai_modal_t *m);

#endif /* AI_MODAL_H */
