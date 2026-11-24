#pragma once
#include "mascot.h"

/*
 * Tiny audio subsystem: synthesized key sounds played through ndsp
 * channel 0 on demand.
 *
 * Three logical voices:
 *   CHAR  letters / digits / punctuation / space — the original 2.7 kHz
 *         "thock" (always the same, independent of mascot).
 *   FUNC  功能键  Enter / Backspace / IME toggle / page / tab
 *   MOD   修饰键  Shift (on-screen ⇧)
 *
 * When the mascot is enabled (mascot_mode > 0) the FUNC and MOD voices
 * are tinted by the active species — each mascot has its own timbre,
 * and within a species FUNC vs MOD are an octave apart so they stay
 * distinct even at speed.  When the mascot is off FUNC falls back to
 * the base click and MOD is silent (legacy behaviour).
 *
 * Initialization may fail silently (dsp firmware not present) — the
 * play helpers then become safe no-ops.
 */

void audio_init(void);
void audio_exit(void);

/* Key-sound categories for audio_play_key(). */
enum {
    AUDIO_KEY_CHAR = 0,  /* alphanumeric / punctuation / space */
    AUDIO_KEY_FUNC,      /* 功能键: Enter, Backspace, IME toggle, page, tab */
    AUDIO_KEY_MOD,       /* 修饰键: Shift */
};

/* Play the sound for a tapped soft key.
 *   category        AUDIO_KEY_* for the key that was fired.
 *   mascot_enabled  1 when the mascot is on (mascot_mode > 0).
 *   species         active species (only used when mascot_enabled). */
void audio_play_key(int category, int mascot_enabled, mascot_species_t species);

/* Legacy alias — plays the base character click.  Kept for API
 * compatibility; new code should use audio_play_key(). */
void audio_play_click(void);
