#pragma once

/*
 * Key auto-repeat ramp, shared by the two surfaces that repeat a held key:
 * keyboard.c (physical D-pad and B-as-backspace) and softkb.c (a held soft
 * key, and the settings page's D-pad navigation).
 *
 * Both surfaces used to carry their own byte-identical copy of these
 * constants — softkb.c duplicated them on purpose so the two modules stayed
 * decoupled.  That stopped being a trade-off the moment the cadence became a
 * user-facing setting: two tables would have meant the setting could only
 * ever be true for one of them.  A header of static inline functions gives
 * both surfaces the same ramp while still keeping them independent
 * translation units, and it is the only part of this behaviour a host test
 * can reach (both .c files pull in citro2d.h or libctru).
 *
 * 3DS runs at 60 fps, so 1 frame ≈ 16.7 ms.  KEYREPEAT_NORMAL is the value
 * every build before this setting shipped with, byte for byte: the default
 * must not change the feel of a machine that has never opened the page.
 */
typedef enum {
    KEYREPEAT_SLOW   = 0,
    KEYREPEAT_NORMAL = 1,
    KEYREPEAT_FAST   = 2
} keyrepeat_style_t;

#define KEYREPEAT_STYLE_COUNT 3

/* Frames a key must be held before the first repeated emission. */
static inline int keyrepeat_initial_delay(int style) {
    switch (style) {
    case KEYREPEAT_SLOW: return 25;   /* ~417 ms */
    case KEYREPEAT_FAST: return  8;   /* ~133 ms */
    default:             return 15;   /* ~250 ms, as shipped since M7 */
    }
}

/* Frames between emissions once repeating.  `phase` counts frames since the
 * first repeat, i.e. held_frames - keyrepeat_initial_delay(style), so it is
 * never negative for the callers here.  Accelerating in three steps; the
 * return value is always ≥ 1, so callers can use it as a divisor without
 * checking. */
static inline int keyrepeat_period(int style, int phase) {
    switch (style) {
    case KEYREPEAT_SLOW:
        if (phase < 30) return 9;     /* 6.7 fires/sec */
        if (phase < 90) return 5;     /*  12 fires/sec */
        return 3;                     /*  20 fires/sec */
    case KEYREPEAT_FAST:
        if (phase < 30) return 3;     /*  20 fires/sec */
        if (phase < 90) return 2;     /*  30 fires/sec */
        return 1;                     /*  60 fires/sec */
    default:
        if (phase < 30) return 5;     /*  12 fires/sec */
        if (phase < 90) return 2;     /*  30 fires/sec */
        return 1;                     /*  60 fires/sec */
    }
}

/* The ramp of `style` for a key held `held_frames` so far: 1 on the frame
 * that should emit a repeat.  Frames before the initial delay always yield
 * 0, so callers do not have to order the two tests themselves. */
static inline int keyrepeat_fires(int style, int held_frames) {
    int delay = keyrepeat_initial_delay(style);
    if (held_frames < delay) return 0;
    int phase = held_frames - delay;
    return (phase % keyrepeat_period(style, phase) == 0);
}
