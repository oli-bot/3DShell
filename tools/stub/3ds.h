#ifndef THREEDS_HOST_STUB_H
#define THREEDS_HOST_STUB_H

/*
 * Host-build stand-in for libctru's <3ds.h>.
 *
 * source/keyboard.h is the only translation unit in source/ that pulls in
 * <3ds.h>, and it does so for exactly two things: the `u32` typedef used
 * by the keys_down / keys_held bitmasks, and the KEY_* button bits.  Those
 * two things are pure data — keyboard.c calls no libctru drawing symbol
 * at all (its C2D_ and C3D_ reference count is zero) — so supplying them
 * here is
 * enough to compile and run the whole physical-key state machine natively,
 * which is where the binding table's behaviour actually lives.
 *
 * The KEY_* values below are copied verbatim from
 *   $DEVKITPRO/libctru/include/3ds/services/hid.h
 * (libctru declares them as an `enum` of BIT(n); they are mirrored here as
 * macros so the existing `#if`-free but macro-tolerant consumers keep
 * compiling unchanged).  They are the ABI the 3DS build compiles against,
 * so a test that presses KEY_DRIGHT here presses the same bit the device
 * does.  Do not renumber them.
 *
 * This header exists ONLY for the host tests under tools/.  It is not on
 * the 3DS include path and must never be: on the device the real <3ds.h>
 * is found first.  tools/stub/citro2d.h includes it rather than keeping a
 * second copy of the bits.
 */

typedef unsigned int u32;

/* libctru <3ds/services/hid.h>, verbatim (BIT(n) == (1u << n)). */
#define KEY_A       (1u << 0)
#define KEY_B       (1u << 1)
#define KEY_SELECT  (1u << 2)
#define KEY_START   (1u << 3)
#define KEY_DRIGHT  (1u << 4)
#define KEY_DLEFT   (1u << 5)
#define KEY_DUP     (1u << 6)
#define KEY_DDOWN   (1u << 7)
#define KEY_R       (1u << 8)
#define KEY_L       (1u << 9)
#define KEY_X       (1u << 10)
#define KEY_Y       (1u << 11)
#define KEY_ZL      (1u << 14)
#define KEY_ZR      (1u << 15)
#define KEY_TOUCH   (1u << 20)
#define KEY_CSTICK_RIGHT (1u << 24)
#define KEY_CSTICK_LEFT  (1u << 25)
#define KEY_CSTICK_UP    (1u << 26)
#define KEY_CSTICK_DOWN  (1u << 27)
#define KEY_CPAD_RIGHT   (1u << 28)
#define KEY_CPAD_LEFT    (1u << 29)
#define KEY_CPAD_UP      (1u << 30)
#define KEY_CPAD_DOWN    (1u << 31)

/* Generic catch-all directions. */
#define KEY_UP      (KEY_DUP    | KEY_CPAD_UP)
#define KEY_DOWN    (KEY_DDOWN  | KEY_CPAD_DOWN)
#define KEY_LEFT    (KEY_DLEFT  | KEY_CPAD_LEFT)
#define KEY_RIGHT   (KEY_DRIGHT | KEY_CPAD_RIGHT)

#endif /* THREEDS_HOST_STUB_H */
