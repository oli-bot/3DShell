#pragma once

/*
 * Anthropic-style salmon-pink crab mascot that scampers along the
 * bottom row of the soft keyboard.
 *
 * States (internal):
 *   WALK    slow horizontal traversal, bouncing off the configured x range.
 *   IDLE    stops and bobs in place.
 *   FLEE    triggered by a touch on the crab; runs fast away for ~1 sec
 *           then returns to WALK.
 *   ALERT   set externally by main.c when the SSH connection has stalled.
 *           Crab freezes and holds up a red ✕, waving it gently until
 *           the alert is cleared.
 *   LOOKUP  reconnect in progress: crab freezes, looks up, and three
 *           dots "..." cycle above its head.  Set externally while the
 *           blocking SSH handshake is in flight; cleared on success/fail.
 *   HAPPY   reconnect succeeded: a brief hop with a green ✓, then back
 *           to WALK.  One-shot, auto-expires.
 *   SAD     reconnect failed: crab tilts and shivers with a red !,
 *           then back to ALERT.  One-shot, auto-expires.
 *   RECORD  voice recording: crab holds up a microphone icon and bobs
 *           in time to the beat, like it's listening.  Set externally
 *           while the mic is capturing.
 *   THINK   voice transcribing: crab pauses in a "thinking" pose with a
 *           ? above its head while the server transcribes.  Set externally
 *           around the blocking ASR round-trip.
 *   TYPE    typing: crab taps its claws as if on a keyboard.  Kicked per
 *           character by mascot_type_kick() — re-kicks refresh the timer
 *           so a run of typing (soft-key press, handwriting, voice TYPING
 *           streaming) keeps the crab typing continuously; once input
 *           stops it auto-expires back to its prior state (~300 ms).
 *
 * All coordinates are bottom-screen pixel space (320×240, top-left origin).
 */

/* Mascot species:
 *   MASCOT_CRAB        - 🦀 螃蟹: original front-facing chibi crab from
 *                        Fishason/DSSH with banana-cat sway toddle
 *   MASCOT_CRAB_XIETE  - 🦀 蟹特Crab: coral pink Anthropic crab with big
 *                        pincers, stalk eyes, and claw-tap typing
 *   MASCOT_CHICK       - 🐔 KUN KUN 鸡你太美: center-parted gray hair, yellow
 *                        face, black jacket w/ gray-white overalls straps,
 *                        red earring, basketball in the left hand (18 px tall)
 *   MASCOT_PHAGE       - 🔬 Blue T4 bacteriophage icon: outlined hexagonal
 *                        capsid, short tail & four bent tail-fiber legs
 *   MASCOT_ALIEN       - 👽 Glowing green alien with antenna & cosmic eyes
 *   MASCOT_NIULAI      - 🐂 牛来: front-facing vivid-orange mascot with
 *                        dark-maroon stepped horns, dusty-pink snout/hooves,
 *                        and white/black half-lidded eyes (20×18 px)
 *   MASCOT_DOG         - 🐕 刀盾狗: orange-yellow Shiba with pointy ears and a
 *                        cream muzzle, holding a small round steel shield in
 *                        the left paw and a short blade in the right; big
 *                        head, tiny body, 5 flat colours (20×18 px)
 */
typedef enum {
    MASCOT_CRAB = 0,
    MASCOT_CRAB_XIETE,
    MASCOT_CHICK,
    MASCOT_PHAGE,
    MASCOT_ALIEN,
    MASCOT_NIULAI,
    MASCOT_DOG,
    MASCOT_SPECIES_COUNT,
} mascot_species_t;

typedef struct mascot_t mascot_t;

/* Allocate a mascot bouncing inside [x_min, x_max] at fixed y row. */
mascot_t *mascot_init(int x_min, int x_max, int y);
void      mascot_free(mascot_t *m);

/* Set/get active mascot species (switches appearance and animations live). */
void             mascot_set_species(mascot_t *m, mascot_species_t species);

/* Resize the mascot's walkable region.  Position is clamped and facing
 * flipped if it was outside the new bounds — call after the bottom row
 * layout changes (e.g. toggling the left-side info strip). */
void mascot_set_bounds(mascot_t *m, int x_min, int x_max);

/* Advance one frame. */
void mascot_update(mascot_t *m);

/* Draw at current position. Caller must already be inside C2D_SceneBegin(bot). */
void mascot_draw(mascot_t *m);

/* Touch helpers. */
int  mascot_hit_test(const mascot_t *m, int tx, int ty);
void mascot_on_touched(mascot_t *m, int from_tx);

/* Toggle the alert overlay (red ✕ above the body, waving).  Called by
 * main.c when SSH stall detection fires; idempotent — repeated calls
 * with the same value are no-ops. */
void mascot_set_alert(mascot_t *m, int alert);

/* Toggle the reconnecting state (crab looks up, "..." dots above).
 * Called by main.c around the blocking SSH handshake; idempotent. */
void mascot_set_reconnecting(mascot_t *m, int on);

/* One-shot reconnect-succeeded celebration (hop + green ✓).
 * Auto-expires back to WALK after a moment. */
void mascot_celebrate(mascot_t *m);

/* One-shot reconnect-failed reaction (tilt/shiver + red !).
 * Auto-expires back to ALERT after a moment. */
void mascot_sadden(mascot_t *m);

/* Toggle the voice-recording state (crab holds a mic, bobs to beat).
 * Called by main.c while the mic is capturing; idempotent. */
void mascot_set_recording(mascot_t *m, int on);

/* Toggle the thinking state (crab pauses, "?" above head) while the
 * server transcribes voice input; idempotent. */
void mascot_set_thinking(mascot_t *m, int on);

/* Kick one "typing" cycle (~300 ms): the crab taps its claws like it's
 * mashing a keyboard.  Call once per character sent to SSH — re-kicks
 * while active just refresh the timer so continuous typing stays in the
 * pose, and it auto-expires ~300 ms after the last character. */
void mascot_type_kick(mascot_t *m);

/* Trigger the chick's "dribbling a basketball" pose (~3 s, no-op on
 * other species).  Idempotent — re-kicks while already playing just
 * refresh the auto-expire timer. */
void mascot_set_playing(mascot_t *m, int on);
