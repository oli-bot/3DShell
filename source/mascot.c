#include "mascot.h"
#include "config.h"   /* for the assert below — config.c cannot include
                       * mascot.h, so the tie is made from this side */
#include <citro2d.h>
#include <stdlib.h>
#include <stdint.h>

/* mascot_mode is 0 = off and then one per species, so the top of the range
 * settings.ini validates against has to move with this list.  It did not:
 * the species list reached 6 while settings_fields[] still allowed 4, and
 * the two newest mascots were silently rewritten to the crab on save. */
_Static_assert(MASCOT_SPECIES_COUNT == DSSH_MASCOT_MODE_MAX,
               "MASCOT_SPECIES_COUNT changed: update DSSH_MASCOT_MODE_MAX in "
               "config.h, or the newest mascot will not survive a reboot");

/*
 * Multi-species Chibi Pet Mascot for 3DS bottom bar:
 *   - MASCOT_CRAB        🦀 螃蟹: original front-facing chibi crab from
 *                        Fishason/DSSH with banana-cat sway toddle
 *   - MASCOT_CRAB_XIETE  🦀 蟹特Crab: coral pink Anthropic crab with big
 *                        pincers, stalk eyes, and claw-tap typing
 *   - MASCOT_CHICK       🐔 KUN KUN 鸡你太美 — center-parted gray hair, yellow
 *                        face, black jacket w/ gray-white overalls straps,
 *                        red earring, basketball in the left hand (dribbles
 *                        while walking)
 *   - MASCOT_PHAGE       🔬 Blue T4 bacteriophage icon: outlined hexagonal
 *                        capsid, short tail & four bent tail-fiber legs
 *   - MASCOT_ALIEN       👽 Glowing green alien with antenna & cosmic eyes
 *   - MASCOT_NIULAI      🐂 牛来: 20×18 front-facing vivid-orange mascot
 *                        with dark-maroon stepped horns, white/black slit
 *                        eyes, and a pink snout/arms/feet (1:1 redraw of
 *                        the supplied pixel reference)
 *   - MASCOT_DOG         🐕 刀盾狗: 20×18 front-facing dog with a BIG round
 *                        head, tiny 6-px body, pointy ears, orange-yellow
 *                        fur, cream muzzle, small round shield on the left
 *                        paw and a short blade in the right.
 *
 * All sprites: 20 px wide; 12 px tall except the chick and 牛来
 * (both 18 px tall).
 *
 * States:
 *   WALK    horizontal traversal, playful movement & leg stepping.
 *   IDLE    stops in place with a gentle breathing bob.
 *   FLEE    fast panic scuttle away from touch.
 *   ALERT   red ✕ floats and sways above the head when connection stalls.
 *   LOOKUP  reconnecting: eyes look upward, cycling "..." loading dots.
 *   HAPPY   reconnect succeeded: joyful hop with celebration pose and green ✓.
 *   SAD     reconnect failed: shivering posture with red !.
 *   RECORD  voice recording: holds up gold mic, bobbing to speech beat.
 *   THINK   voice transcribing: thinking pose with drifting blue ?.
 *   TYPE    typing: rapid keyboard tapping animation.
 */

typedef enum {
    STATE_WALK,
    STATE_IDLE,
    STATE_FLEE,
    STATE_ALERT,
    STATE_LOOKUP,   /* reconnecting: look up + "..." dots */
    STATE_HAPPY,    /* reconnect succeeded: hop + green ✓ */
    STATE_SAD,      /* reconnect failed: shiver + red ! */
    STATE_RECORD,   /* voice recording: mic + bob to beat */
    STATE_THINK,    /* voice transcribing: thinking pose + "?" */
    STATE_TYPE,     /* typing: claw/paw/peck tap on keyboard */
    STATE_PLAY,     /* chick dribbling a basketball (KUN KUN) */
} mascot_state_t;

struct mascot_t {
    int              x_min, x_max, y_top;
    float            fx;
    int              facing;
    mascot_species_t species;
    mascot_state_t   state;
    mascot_state_t   prev_state;
    /* "ALERT is still the truth" — kept separately from prev_state because
     * main.c drives mascot_set_alert() off an *edge* (it only calls it when
     * the stall condition flips), so a temporary state that interrupted an
     * alert has to know to put it back on the way out.  prev_state cannot
     * carry this: it is one shared slot that any nested temporary state
     * overwrites. */
    int              alert_pending;
    int              state_frames;
    int              anim_frame;     /* walk cycle index (0..5) */
    int              anim_timer;
    int              bob_phase;
    int              alert_phase;
    int              dot_phase;      /* LOOKUP: cycles the "..." dots */
    int              hop_dy;         /* HAPPY: vertical hop offset */
    int              shiver_phase;   /* SAD: shiver tick */
    int              rec_bob;        /* RECORD: beat-bob offset */
    int              think_phase;    /* THINK: "?" sway */
    int              type_phase;     /* TYPE: tap cycle */
};

/* Sprite canvases.  Every species is 20 px wide today, but width goes
 * through species_width() rather than a bare CRAB_W so a future wider
 * pet only has to change OX_W (and its row data). */
#define CRAB_W   20
#define CRAB_H   12
#define CHICK_H  18
#define OX_W     20
#define OX_H     18
#define HIT_PAD  4

/* Forward decls: species_width() / species_height() are defined below the
 * sprite data but referenced from mascot_set_bounds()/mascot_draw(). */
static int species_width(mascot_species_t sp);
static int species_height(mascot_species_t sp);

/* Universal UI Colors */
#define COL_X      0xe54040ff
#define COL_CHECK  0x40b061ff   /* green */
#define COL_MIC    0xe0c06cff   /* warm gold */
#define COL_Q      0x6fa8dcff   /* soft blue */
#define COL_BANG   0xe54040ff   /* red (same hue as ✕) */
#define COL_EYE_W  0xffffffff
#define COL_EYE_B  0x181825ff

/* ── 1. ORIGINAL CRAB (🦀 螃蟹 - Fishason/DSSH) Frames ───────────────── */
static const char *const crab_orig_w0[12] = {
    "....................",
    ".......@@@@@@.......",
    ".....@@@@@@@@@@.....",
    "...@@@@@@@@@@@@@@...",
    "..@@@@##@@@@##@@@@..",
    "..@@@@##@@@@##@@@@..",
    ".@@@@@@@@@@@@@@@@@@.",
    ".@@@@@@@@@@@@@@@@@@.",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    "....FFF......FFF....",
};
static const char *const crab_orig_w1[12] = {
    "....................",
    "......@@@@@@........",
    "....@@@@@@@@@@......",
    "..@@@@@@@@@@@@@@....",
    ".@@@@##@@@@##@@@@...",
    ".@@@@##@@@@##@@@@...",
    ".@@@@@@@@@@@@@@@@@@.",
    ".@@@@@@@@@@@@@@@@@@.",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    "....FFF......FFF....",
};
static const char *const crab_orig_w2[12] = {
    ".....@@@@@@.........",
    "...@@@@@@@@@@.......",
    ".@@@@@@@@@@@@@@.....",
    ".@@@@##@@@@##@@@@...",
    ".@@@@##@@@@##@@@@...",
    "@@@@@@@@@@@@@@@@@@..",
    "@@@@@@@@@@@@@@@@@@..",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    ".............FFF....",
    "....FFF.............",
};
static const char *const crab_orig_w4[12] = {
    "....................",
    "........@@@@@@......",
    "......@@@@@@@@@@....",
    "....@@@@@@@@@@@@@@..",
    "...@@@@##@@@@##@@@@.",
    "...@@@@##@@@@##@@@@.",
    ".@@@@@@@@@@@@@@@@@@.",
    ".@@@@@@@@@@@@@@@@@@.",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    "....FFF......FFF....",
};
static const char *const crab_orig_w5[12] = {
    ".........@@@@@@.....",
    ".......@@@@@@@@@@...",
    ".....@@@@@@@@@@@@@@.",
    "...@@@@##@@@@##@@@@.",
    "...@@@@##@@@@##@@@@.",
    "..@@@@@@@@@@@@@@@@@@",
    "..@@@@@@@@@@@@@@@@@@",
    "..@@@@@@@@@@@@@@@@..",
    "...@@@@@@@@@@@@@@...",
    "....@@@@@@@@@@@@....",
    "....FFF.............",
    ".............FFF....",
};
static const char *const *const crab_orig_walk_frames[6] = {
    crab_orig_w0, crab_orig_w1, crab_orig_w2, crab_orig_w0, crab_orig_w4, crab_orig_w5,
};

/* ── 2. CRAB XIETE (🦀 蟹特Crab) Frames ────────────────────────────── */
static const char *const crab_w0[12] = {
    "CC.C..........C.CC..",
    "CCCCC..W#...#W.CCCCC",
    ".CCC...##...##..CCC.",
    "..CC....@@@@....CC..",
    "...C..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    "L..L.@@@@@@@@@@.L..L",
    ".L..L.@@@@@@@@.L..L.",
    "..L..L........L..L..",
    "....................",
};
static const char *const crab_w1[12] = {
    "C.CC..........C.CC..",
    "CCCCC..W#...#W.CCCCC",
    ".CCC...##...##..CCC.",
    "..CC....@@@@....CC..",
    "...C..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    "LL..@@@@@@@@@@@@.LL.",
    ".L.L.@@@@@@@@@@.L..L",
    "..L.L.@@@@@@@@.L..L.",
    ".....L........L..L..",
    "....................",
};
static const char *const crab_w2[12] = {
    ".CC...........C.CC..",
    "CC.C...W#...#W.CCCCC",
    "CCCCC..##...##..CCC.",
    ".CCC....@@@@....CC..",
    "..CC..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    "L..L.@@@@@@@@@@L..L.",
    ".L..L.@@@@@@@@..L..L",
    "..L..L............L.",
    "....................",
};
static const char *const crab_w4[12] = {
    "CC.C..........CC.C..",
    "CCCCC..W#...#W.CCCCC",
    ".CCC...##...##..CCC.",
    "..CC....@@@@....CC..",
    "...C..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@..LL",
    "L..L.@@@@@@@@@@.L.L.",
    ".L..L.@@@@@@@@.L.L..",
    "..L..L........L.....",
    "....................",
};
static const char *const crab_w5[12] = {
    "CC.C...........CC...",
    "CCCCC..W#...#W.C.CC.",
    ".CCC...##...##.CCCCC",
    "..CC....@@@@....CCC.",
    "...C..@@@@@@@@..CC..",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    ".L.L.@@@@@@@@@@.L..L",
    "L..L..@@@@@@@@.L..L.",
    ".L............L..L..",
    "....................",
};
static const char *const *const crab_walk_frames[6] = {
    crab_w0, crab_w1, crab_w2, crab_w0, crab_w4, crab_w5,
};
static const char *const crab_type_f0[12] = {
    "..............C.CC..",
    ".......W#...#W.CCCCC",
    "CC.C...##...##..CCC.",
    "CCCCC...@@@@....CC..",
    ".CCC..@@@@@@@@..C...",
    "..CC.@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    "L..L.@@@@@@@@@@.L..L",
    ".L..L.@@@@@@@@.L..L.",
    "..L..L........L..L..",
    "....................",
};
static const char *const crab_type_f1[12] = {
    "CC.C................",
    "CCCCC..W#...#W......",
    ".CCC...##...##.CC.C.",
    "..CC....@@@@...CCCCC",
    "...C..@@@@@@@@..CCC.",
    ".....@@K@@@@K@@..CC.",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    "L..L.@@@@@@@@@@.L..L",
    ".L..L.@@@@@@@@.L..L.",
    "..L..L........L..L..",
    "....................",
};
static const char *const crab_happy_f[12] = {
    ".CC...........CC....",
    "CC.C...W#...#W.C.CC.",
    "CCCCC..##...##.CCCCC",
    ".CCC....@@@@....CCC.",
    "..CC..@@@@@@@@..CC..",
    "...C.@@K@@@@K@@.C...",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    "L..L.@@@@@@@@@@.L..L",
    ".L..L.@@@@@@@@.L..L.",
    "..L..L........L..L..",
    "....................",
};
static const char *const crab_lookup_f[12] = {
    "CC.C...W#...#W.C.CC.",
    "CCCCC..##...##.CCCCC",
    ".CCC............CCC.",
    "..CC....@@@@....CC..",
    "...C..@@@@@@@@..C...",
    ".....@@K@@@@K@@.....",
    "....@@@@@@@@@@@@....",
    ".LL.@@@@@@@@@@@@.LL.",
    "L..L.@@@@@@@@@@.L..L",
    ".L..L.@@@@@@@@.L..L.",
    "..L..L........L..L..",
    "....................",
};

/* ── 2. CHICK (🐔 坤坤/KUN KUN) Frames ───────────────────────────────
 *
 * v5 redesign — mirrored reference pixel art (20×18 canvas):
 *   • center-parted gray hair framing a yellow face (brows, black eyes,
 *     orange beak, red earring on the right cheek)
 *   • black jacket with two gray-white overall straps running from the
 *     shoulders down to gray-white overalls pants; black feet
 *   • 9×9 basketball with black outline & seams in the LEFT hand —
 *     walk frames double as a dribble bounce (ball offset per step)
 *
 * Pixel legend (see CHICK palette in get_species_color):
 *   H = gray hair / brows      @ = yellow face skin / hand
 *   K = black jacket / feet    B = orange beak
 *   R = basketball orange      E = red earring
 *   S = gray-white overalls & straps
 *   # = pupils / ball outline & seams
 */
static const char *const chick_w0[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    "..#####HHHH@@@@@@HHH",
    ".#RR#RR#.HHHH@HH@HHH",
    "#R#R#R#R#..##@##@E..",
    "#R#R#R#R#..@BBBB@E..",
    "#########..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    ".#RR#RR#..KKSKKKSKK.",
    "..#####...KKSKKKSKK@",
    "..........SSSSSSSSS@",
    "..........SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_w1[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    ".......HHHH@@@@@@HHH",
    ".........HHHH@HH@HHH",
    "..#####....##@##@E..",
    ".#RR#RR#...@BBBB@E..",
    "#R#R#R#R#..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#########.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK@",
    ".#RR#RR#..SSSSSSSSS@",
    "..#####...SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KK....KKK.",
};
static const char *const chick_w2[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    ".......HHHH@@@@@@HHH",
    ".........HHHH@HH@HHH",
    "...........##@##@E..",
    "...........@BBBB@E..",
    "..#####....@@BB@@...",
    ".#RR#RR#...@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK.",
    "#########.KKSKKKSKK@",
    "#R#R#R#R#.SSSSSSSSS@",
    "#R#R#R#R#.SSSSSSSSS.",
    ".#RR#RR#...SS....SS.",
    "..#####....KK....KK.",
    "..........KKK....KKK",
};
static const char *const chick_w3[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    ".......HHHH@@@@@@HHH",
    ".........HHHH@HH@HHH",
    "...........##@##@E..",
    "...........@BBBB@E..",
    "..#####....@@BB@@...",
    ".#RR#RR#...@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK.",
    "#########.KKSKKKSKK@",
    "#R#R#R#R#.SSSSSSSSS@",
    "#R#R#R#R#.SSSSSSSSS.",
    ".#RR#RR#...SS...SS..",
    "..#####....KK...KK..",
    "..........KKK...KK..",
};
static const char *const chick_w4[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    ".......HHHH@@@@@@HHH",
    ".........HHHH@HH@HHH",
    "..#####....##@##@E..",
    ".#RR#RR#...@BBBB@E..",
    "#R#R#R#R#..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#########.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK@",
    ".#RR#RR#..SSSSSSSSS@",
    "..#####...SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KK.....KK.",
};
static const char *const chick_w5[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    "..#####HHHH@@@@@@HHH",
    ".#RR#RR#.HHHH@HH@HHH",
    "#R#R#R#R#..##@##@E..",
    "#R#R#R#R#..@BBBB@E..",
    "#########..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    ".#RR#RR#..KKSKKKSKK.",
    "..#####...KKSKKKSKK@",
    "..........SSSSSSSSS@",
    "..........SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK....KK.",
};
static const char *const chick_type_f0[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    "..#####HHHH@@@@@@HHH",
    ".#RR#RR#.HHHH@HH@HHH",
    "#R#R#R#R#..##@##@E..",
    "#R#R#R#R#..@BBBB@E..",
    "#########..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    ".#RR#RR#..KKSKKKSKK@",
    "..#####...KKSKKKSKK@",
    "..........SSSSSSSSS.",
    "..........SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_type_f1[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    "..#####HHHH@@@@@@HHH",
    ".#RR#RR#.HHHH@HH@HHH",
    "#R#R#R#R#..##@##@E..",
    "#R#R#R#R#..@BBBB@E..",
    "#########..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    ".#RR#RR#..KKSKKKSKK.",
    "..#####...KKSKKKSKK.",
    "..........SSSSSSSSS@",
    "..........SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_happy_f[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    "..#####HHHH@@@@@@HHH",
    ".#RR#RR#.HHHH@HH@HHH",
    "#R#R#R#R#..##@##@E..",
    "#R#R#R#R#..@BBBB@E..",
    "#########..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK@",
    ".#RR#RR#..KKSKKKSKK@",
    "..#####...KKSKKKSKK.",
    "..........SSSSSSSSS.",
    "..........SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "...........KK...KK..",
};
static const char *const chick_lookup_f[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    "..#####HHHH@@@@@@HHH",
    ".#RR#RR#HHH##@##@HHH",
    "#R#R#R#R#..@@@@@@E..",
    "#R#R#R#R#..@BBBB@E..",
    "#########..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    ".#RR#RR#..KKSKKKSKK.",
    "..#####...KKSKKKSKK@",
    "..........SSSSSSSSS@",
    "..........SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_play_f0[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    "..#####HHHH@@@@@@HHH",
    ".#RR#RR#.HHHH@HH@HHH",
    "#R#R#R#R#..##@##@E..",
    "#R#R#R#R#..@BBBB@E..",
    "#########..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    ".#RR#RR#..KKSKKKSKK.",
    "..#####...KKSKKKSKK@",
    "..........SSSSSSSSS@",
    "..........SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_play_f1[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    ".......HHHH@@@@@@HHH",
    ".........HHHH@HH@HHH",
    "..#####....##@##@E..",
    ".#RR#RR#...@BBBB@E..",
    "#R#R#R#R#..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#########.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK@",
    ".#RR#RR#..SSSSSSSSS@",
    "..#####...SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_play_f2[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    ".......HHHH@@@@@@HHH",
    ".........HHHH@HH@HHH",
    "...........##@##@E..",
    "...........@BBBB@E..",
    "..#####....@@BB@@...",
    ".#RR#RR#...@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK.",
    "#########.KKSKKKSKK@",
    "#R#R#R#R#.SSSSSSSSS@",
    "#R#R#R#R#.SSSSSSSSS.",
    ".#RR#RR#...SS...SS..",
    "..#####....KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_play_f3[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    ".......HHHH@@@@@@HHH",
    ".........HHHH@HH@HHH",
    "...........##@##@E..",
    "...........@BBBB@E..",
    "..#####....@@BB@@...",
    ".#RR#RR#...@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK.",
    "#########.KKSKKKSKK@",
    "#R#R#R#R#.SSSSSSSSS@",
    "#R#R#R#R#.SSSSSSSSS.",
    ".#RR#RR#...SS...SS..",
    "..#####....KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_play_f4[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    ".......HHHH@@@@@@HHH",
    ".........HHHH@HH@HHH",
    "..#####....##@##@E..",
    ".#RR#RR#...@BBBB@E..",
    "#R#R#R#R#..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#########.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK.",
    "#R#R#R#R#.KKSKKKSKK@",
    ".#RR#RR#..SSSSSSSSS@",
    "..#####...SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK...KKK.",
};
static const char *const chick_play_f5[18] = {
    ".........HHHH.HHHH..",
    ".......HHHHHHHHHHHH.",
    ".......HHHHHHHHHHHHH",
    ".......HHHH@@@@@@HHH",
    "..#####HHHH@@@@@@HHH",
    ".#RR#RR#.HHHH@HH@HHH",
    "#R#R#R#R#..##@##@E..",
    "#R#R#R#R#..@BBBB@E..",
    "#########..@@BB@@...",
    "#R#R#R#R#..@@@@@@...",
    "#R#R#R#R#.KKSKKKSKK.",
    ".#RR#RR#..KKSKKKSKK.",
    "..#####...KKSKKKSKK@",
    "..........SSSSSSSSS@",
    "..........SSSSSSSSS.",
    "...........SS...SS..",
    "...........KK...KK..",
    "..........KKK...KKK.",
};

static const char *const *const chick_walk_frames[6] = {
    chick_w0, chick_w1, chick_w2, chick_w3, chick_w4, chick_w5,
};
static const char *const *const chick_play_frames[6] = {
    chick_play_f0, chick_play_f1, chick_play_f2,
    chick_play_f3, chick_play_f4, chick_play_f5,
};

/* ── 3. PHAGE (🔬 噬菌体) Frames ──────────────────────────────────── */
static const char *const phage_w0[12] = {
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....L.L......L.L....",
    "....L.L......L.L....",
    "....................",
};
static const char *const phage_w1[12] = {
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    ".............L.L....",
    ".............L.L....",
    "....................",
};
static const char *const phage_w2[12] = {
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....L.L.............",
    "....L.L.............",
    "....................",
};
static const char *const phage_w4[12] = {
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    ".............L.L....",
    ".............L.L....",
    "....................",
};
static const char *const phage_w5[12] = {
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....L.L......L.L....",
    "....L.L......L.L....",
    "....................",
};
static const char *const *const phage_walk_frames[6] = {
    phage_w0, phage_w1, phage_w2, phage_w0, phage_w4, phage_w5,
};
static const char *const phage_type_f0[12] = {
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....L.L......L.L....",
    "....L.L......L.L....",
    "....................",
};
static const char *const phage_type_f1[12] = {
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....................",
    "....................",
    "....................",
};
static const char *const phage_happy_f[12] = {
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    ".....L.L....L.L.....",
    "....L.L......L.L....",
    "...L.L........L.L...",
    "...L.L........L.L...",
    "....................",
};
static const char *const phage_lookup_f[12] = {
    "....................",
    ".......@@@@@@.......",
    "......@@@@@@@@......",
    ".....@@@@DD@@@@.....",
    ".....@@@@DD@@@@.....",
    "......@@@@@@@@......",
    ".......@@@@@@.......",
    ".........@@.........",
    "......L.L..L.L......",
    ".....L.L....L.L.....",
    "....L.L......L.L....",
    "....L.L......L.L....",
};

/* ── 4. ALIEN (👽 绿外星人) Frames ────────────────────────────────── */
static const char *const alien_w0[12] = {
    ".........AA.........",
    ".........SS.........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    ".....GG@@@@@@GG.....",
    "......GG....GG......",
    ".......G....G.......",
};
static const char *const alien_w1[12] = {
    "........AA..........",
    "........SS..........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    "....GG.@@@@@@.GG....",
    ".....GG......GG.....",
    "....................",
};
static const char *const alien_w2[12] = {
    ".......AA...........",
    ".......SS...........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    ".....GG@@@@@@GG.....",
    "......GG....GG......",
    ".......G....G.......",
};
static const char *const alien_w4[12] = {
    "..........AA........",
    "..........SS........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    "....GG.@@@@@@.GG....",
    ".....GG......GG.....",
    "....................",
};
static const char *const alien_w5[12] = {
    "...........AA.......",
    "...........SS.......",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    ".....GG@@@@@@GG.....",
    "......GG....GG......",
    ".......G....G.......",
};
static const char *const *const alien_walk_frames[6] = {
    alien_w0, alien_w1, alien_w2, alien_w0, alien_w4, alien_w5,
};
static const char *const alien_type_f0[12] = {
    ".........AA.........",
    ".........SS.........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    ".....GG@@PP@@G......",
    "......G@@@@@@GG.....",
    "......GG....GG......",
    "....................",
};
static const char *const alien_type_f1[12] = {
    ".........AA.........",
    ".........SS.........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@GG.....",
    ".....GG@@@@@@G......",
    "......GG....GG......",
    "....................",
};
static const char *const alien_happy_f[12] = {
    "........AAAA........",
    ".........SS.........",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "....@@##@@@@##@@....",
    "....GG@@@@@@@@GG....",
    ".....G.@@PP@@.G.....",
    "......GG@@@@GG......",
    ".......G....G.......",
    "....................",
};
static const char *const alien_lookup_f[12] = {
    ".........AA.........",
    ".........SS.........",
    "...@@WW#@@@@#WW@@...",
    "...@@####@@####@@...",
    "......@@@@@@@@......",
    "....@@@@@@@@@@@@....",
    ".....@@@@@@@@@@.....",
    "......G@@PP@@G......",
    ".....GG@@@@@@GG.....",
    "......GG....GG......",
    ".......G....G.......",
    "....................",
};

/* ── 5. NIULAI (🐂 牛来) Frames ─────────────────────────────────────
 *
 * Strict 20×18 pixel sprite.
 *
 * Same logical sprite size as KUN KUN.
 *
 * Every character = exactly one pixel.
 *
 * Palette:
 *   Y = orange body
 *   H = dark maroon horn / eye / mouth
 *   W = white eye
 *   P = pink snout / arms / feet
 *   D = darker pink shading
 *   . = transparent
 *
 * Walk cycle (6 frames, every 4th anim step ≈ 100 ms at speed=6):
 *   0/3 plant: both feet aligned, body level
 *   1/4 step-up: feet wide stance, body lifted 1 px
 *   2/5 step-down: feet wide stance, body back to base
 * The vertical bob comes from mascot_draw (yi -= 1 on odd anim frames).
 */


/* ── Walk frame 0 ─────────────────────────────────────────────────── */

static const char *const niulai_w0[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYWHWYYYYWHWYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    ".....YYYY..YYYY.....",
    ".....PDP....PDP.....",
};


static const char *const niulai_w1[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYWHWYYYYWHWYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    "....PDP....YYYY.....",
    "...........PDP......",
};


static const char *const niulai_w2[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYWHWYYYYWHWYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    "....YYYY....YYYY....",
    "....PDP......PDP....",
};


static const char *const niulai_w3[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYWHWYYYYWHWYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    ".....YYYY..YYYY.....",
    ".....PDP....PDP.....",
};


static const char *const niulai_w4[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYWHWYYYYWHWYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    ".....YYYY....PDP....",
    "......PDP...........",
};


static const char *const niulai_w5[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYWHWYYYYWHWYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    "....YYYY....YYYY....",
    "....PDP......PDP....",
};






/* ── Typing frame 0 ───────────────────────────────────────────────── */

static const char *const niulai_type_f0[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYWHWYYYYWHWYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    ".....YYYY..YYYY.....",
    ".....PDP....PDP.....",
};


/* ── Typing frame 1 ───────────────────────────────────────────────── */

static const char *const niulai_type_f1[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYWHWYYYYWHWYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    "....YYYY....YYYY....",
    "....PDP......PDP....",
};


/* ── Happy ─────────────────────────────────────────────────────────── */

static const char *const niulai_happy_f[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYYYYYYYYYYYYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    ".....YYYY..YYYY.....",
    ".....PDP....PDP.....",
};


/* ── Lookup ────────────────────────────────────────────────────────── */

static const char *const niulai_lookup_f[18] = {
    "..HH............HH..",
    "..HHHH........HHHH..",
    "....HHYYYYYYYYHH....",
    "...HHYYYYYYYYYYHH...",
    "YYYYYYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYYYYYY",
    ".YYYYYHYYYYYYHYYYYY.",
    "..YYYYYYYYYYYYYYYY..",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHPPHPYYYY...",
    "...YYYYPPPPPPYYYY...",
    "...YYYYPHHHHPYYYY...",
    ".....YYPPPPPPYY.....",
    "...YYYYYYYYYYYYYY...",
    "...YYYYYYYYYYYYYY...",
    "...PPPYYYYYYYYPPP...",
    ".....YYYY..YYYY.....",
    ".....PDP....PDP.....",
};


/* ── Walk frame table ──────────────────────────────────────────────── */

static const char *const *const niulai_walk_frames[6] = {
    niulai_w0,
    niulai_w1,
    niulai_w2,
    niulai_w3,
    niulai_w4,
    niulai_w5,
};


/* ── 6. DOG (🐕 刀盾狗) Frames ────────────────────────────────────────
 * Front-facing Shiba-style blade & shield dog, redrawn 2026-09-14.
 * BIG round head (~half the sprite height) on a tiny 6-px body, 2-px
 * pointy ears, orange fur with a cream muzzle, a small round shield on
 * the left paw and a short blade in the right.  Five flat colours with a
 * 1-px dark outline so the silhouette survives at 20 px.
 * Generated by tools/bake_mascot_dog20.py from the 32×32 master in
 * tools/gen_mascot_dog32.py — edit those, not this block.  20×18.
 *
 * Legend: '.' transparent   o outline   f fur   w cream
 *         m steel-lit       d steel-shade
 */

static const char *const dog_w0[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_w1[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_w2[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_w3[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_w4[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_w5[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_type_f0[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_type_f1[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofwoffwofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_happy_f[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....offffffffo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const dog_lookup_f[18] = {
    "......oo....oo......",
    ".....offo..offo.....",
    ".....offfoofffo...o.",
    ".....offffffffo..oo.",
    ".....ofooffoofo.ommo",
    ".....ofooffoofo.ommo",
    ".....offffffffo.ommo",
    ".....offffffffo.ommo",
    ".....offwwwwffo.ommo",
    ".....ofwwoowwfo.ommo",
    ".ooo.ofwwwwwwfo.ommo",
    "ommmooowwwwwwoo.ommo",
    "omdmo..offffo...oooo",
    "ommmooooffffoooooddo",
    ".ooooffoffffoffwwddo",
    "....offoffffoffwwddo",
    ".....oooffffoooooddo",
    ".......oooooo....oo.",
};

static const char *const *const dog_walk_frames[6] = {
    dog_w0, dog_w1, dog_w2, dog_w3, dog_w4, dog_w5,
};

// VALIDATION OK -- all rows 20x18

/* ── Overhead Indicator Icons ──────────────────────────────────────── */
static const char *const alert_x[5] = {
    "@...@",
    ".@.@.",
    "..@..",
    ".@.@.",
    "@...@",
};

static const char *const happy_check[5] = {
    "....@",
    "...@.",
    "@.@..",
    ".@...",
    "@....",
};

static const char *const sad_bang[5] = {
    "@@.",
    "@@.",
    "@@.",
    "...",
    "@@.",
};

static const char *const mic_icon[6] = {
    ".@@@.",
    "@@@@@",
    "@@@@@",
    ".@@@.",
    "..@..",
    ".@@@.",
};

static const char *const think_q[5] = {
    ".@@@.",
    "@...@",
    "..@@.",
    "..@..",
    "..@..",
};

static u32 rgba_to_c2d(uint32_t rgba) {
    return C2D_Color32((rgba >> 24) & 0xff,
                       (rgba >> 16) & 0xff,
                       (rgba >>  8) & 0xff,
                        rgba        & 0xff);
}

static void enter_walk(mascot_t *m) {
    m->state = STATE_WALK;
    m->state_frames = 0;
}
static void enter_idle(mascot_t *m) {
    m->state = STATE_IDLE;
    m->state_frames = 60 + (rand() % 120);
}
static void enter_flee(mascot_t *m) {
    m->state = STATE_FLEE;
    m->state_frames = 50 + (rand() % 30);
}
static void enter_lookup(mascot_t *m) {
    m->prev_state  = m->state;
    m->state       = STATE_LOOKUP;
    m->dot_phase   = 0;
    m->state_frames = 0;
}
static void enter_happy(mascot_t *m) {
    m->prev_state   = m->state;
    m->state        = STATE_HAPPY;
    m->hop_dy       = 0;
    m->state_frames = 1;
}
static void enter_sad(mascot_t *m) {
    m->prev_state   = m->state;
    m->state        = STATE_SAD;
    m->shiver_phase = 0;
    m->state_frames = 1;
}
static void enter_record(mascot_t *m) {
    m->prev_state  = m->state;
    m->state       = STATE_RECORD;
    m->rec_bob     = 0;
    m->state_frames = 0;
}
static void enter_think(mascot_t *m) {
    m->prev_state   = m->state;
    m->state        = STATE_THINK;
    m->think_phase  = 0;
    m->state_frames = 0;
}

#define TYPE_TIMEOUT_FRAMES 18   /* ~300 ms at 60 fps */

static void enter_type(mascot_t *m) {
    if (m->state == STATE_ALERT || m->state == STATE_LOOKUP ||
        m->state == STATE_HAPPY || m->state == STATE_SAD) return;
    if (m->state != STATE_TYPE) {
        m->prev_state = m->state;
        m->type_phase = 0;
    }
    m->state        = STATE_TYPE;
    m->state_frames = TYPE_TIMEOUT_FRAMES;
}

#define PLAY_TIMEOUT_FRAMES 180  /* ~3 s at 60 fps */

static void enter_play(mascot_t *m) {
    if (m->state == STATE_ALERT || m->state == STATE_LOOKUP ||
        m->state == STATE_HAPPY || m->state == STATE_SAD  ||
        m->state == STATE_RECORD || m->state == STATE_THINK ||
        m->state == STATE_TYPE) return;
    if (m->species != MASCOT_CHICK) return;  /* only the chick drips a ball */
    if (m->state != STATE_PLAY) {
        m->prev_state = m->state;
    }
    m->state        = STATE_PLAY;
    m->state_frames = PLAY_TIMEOUT_FRAMES;
}

mascot_t *mascot_init(int x_min, int x_max, int y) {
    mascot_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->x_min   = x_min;
    m->x_max   = x_max;
    m->y_top   = y;
    m->fx      = (float)x_min + 4.0f;
    m->facing  = +1;
    m->species = MASCOT_CRAB;
    enter_walk(m);
    return m;
}

void mascot_free(mascot_t *m) { free(m); }

void mascot_set_species(mascot_t *m, mascot_species_t species) {
    if (!m) return;
    if (species >= 0 && species < MASCOT_SPECIES_COUNT) {
        m->species = species;
    }
}

/* Trigger the chick's "dribbling a basketball" pose.  No-op for other
 * species.  Idempotent — repeated calls while already playing just
 * refresh the auto-expire timer. */
void mascot_set_playing(mascot_t *m, int on) {
    if (!m) return;
    if (!on) return;        /* only ever kick, never force-stop */
    enter_play(m);
}

/* Resize the walkable region for the mascot.  Current x position is
 * clamped into the new range and facing is flipped if we just pushed
 * against a wall — call before any update after a layout change so the
 * next frame doesn't visibly teleport. */
void mascot_set_bounds(mascot_t *m, int x_min, int x_max) {
    if (!m || x_max <= x_min) return;
    m->x_min = x_min;
    m->x_max = x_max;
    if (m->fx < (float)x_min) {
        m->fx = (float)x_min;
        if (m->facing < 0) m->facing = +1;
    }
    int sw = species_width(m->species);
    if (m->fx > (float)(x_max - sw)) {
        m->fx = (float)(x_max - sw);
        if (m->facing > 0) m->facing = -1;
    }
}

/* Leaving a temporary state (RECORD / THINK / LOOKUP) — put ALERT back if
 * that is what the temporary state interrupted.
 *
 * Why this has to exist: main.c drives the alert off an *edge* —
 * `if (want_alert != stall_alert) mascot_set_alert(mc, stall_alert)` — so
 * the alert is asserted exactly once per change of the stall condition.
 * A state that interrupted it and then went straight to enter_walk() lost
 * it for good: the edge had already fired, so nothing ever re-asserted ALERT
 * and the crab stopped showing the red ✕ for the rest of the stall (and the
 * matching "clear" call found a non-ALERT state and did nothing).
 *
 * Reads alert_pending rather than prev_state: prev_state is a single slot
 * shared by every temporary state, so a nested one (enter_type() firing
 * during RECORD, for instance) overwrites it and would lose the alert again. */
static void leave_temp_state(mascot_t *m) {
    if (m->alert_pending) {
        m->state       = STATE_ALERT;
        m->alert_phase = 0;
    } else {
        enter_walk(m);
    }
}

void mascot_set_alert(mascot_t *m, int alert) {
    if (!m) return;
    m->alert_pending = alert ? 1 : 0;
    if (alert && m->state != STATE_ALERT) {
        m->prev_state   = m->state;
        m->state        = STATE_ALERT;
        m->alert_phase  = 0;
    } else if (!alert && m->state == STATE_ALERT) {
        enter_walk(m);
    }
}

void mascot_set_reconnecting(mascot_t *m, int on) {
    if (!m) return;
    if (on && m->state != STATE_LOOKUP) {
        enter_lookup(m);
    } else if (!on && m->state == STATE_LOOKUP) {
        leave_temp_state(m);
    }
}

void mascot_celebrate(mascot_t *m) {
    if (!m) return;
    enter_happy(m);
}

void mascot_sadden(mascot_t *m) {
    if (!m) return;
    enter_sad(m);
}

void mascot_set_recording(mascot_t *m, int on) {
    if (!m) return;
    if (on && m->state != STATE_RECORD) {
        enter_record(m);
    } else if (!on && m->state == STATE_RECORD) {
        leave_temp_state(m);
    }
}

void mascot_set_thinking(mascot_t *m, int on) {
    if (!m) return;
    if (on && m->state != STATE_THINK) {
        enter_think(m);
    } else if (!on && m->state == STATE_THINK) {
        leave_temp_state(m);
    }
}

void mascot_type_kick(mascot_t *m) {
    if (!m) return;
    enter_type(m);
}

static void clamp_and_bounce(mascot_t *m, int *hit_wall) {
    int sw = species_width(m->species);
    *hit_wall = 0;
    if (m->fx <= (float)m->x_min) {
        m->fx = (float)m->x_min;
        m->facing = +1;
        *hit_wall = 1;
    }
    if (m->fx >= (float)(m->x_max - sw)) {
        m->fx = (float)(m->x_max - sw);
        m->facing = -1;
        *hit_wall = 1;
    }
}

void mascot_update(mascot_t *m) {
    if (!m) return;

    if (m->state == STATE_WALK || m->state == STATE_FLEE) {
        int speed = (m->state == STATE_FLEE) ? 3 : 6;
        if (++m->anim_timer >= speed) {
            m->anim_timer = 0;
            m->anim_frame = (m->anim_frame + 1) % 6;
        }
    }
    m->bob_phase     = (m->bob_phase + 1) % 60;
    m->alert_phase   = (m->alert_phase + 1) % 24;
    m->dot_phase     = (m->dot_phase + 1) % 36;
    m->shiver_phase  = (m->shiver_phase + 1) % 8;
    m->rec_bob       = (m->rec_bob + 1) % 24;
    m->think_phase   = (m->think_phase + 1) % 30;
    m->type_phase    = (m->type_phase + 1) % 18;

    int hit_wall = 0;
    switch (m->state) {
        case STATE_WALK:
            m->fx += 0.5f * (float)m->facing;
            clamp_and_bounce(m, &hit_wall);
            if ((rand() % 600) == 0) enter_idle(m);
            break;

        case STATE_IDLE:
            if (m->state_frames > 0) m->state_frames--;
            if (m->state_frames == 0) enter_walk(m);
            break;

        case STATE_FLEE:
            m->fx += 2.0f * (float)m->facing;
            clamp_and_bounce(m, &hit_wall);
            if (m->state_frames > 0) m->state_frames--;
            if (m->state_frames == 0 || hit_wall) enter_walk(m);
            break;

        case STATE_ALERT:
        case STATE_LOOKUP:
            break;

        case STATE_HAPPY: {
            int t = m->state_frames;
            int h = (t < 20) ? t : (40 - t);
            m->hop_dy = -(h / 4);
            if (++m->state_frames >= 40) enter_walk(m);
            break;
        }

        case STATE_SAD:
            if (++m->state_frames >= 90) {
                m->state       = STATE_ALERT;
                m->alert_phase = 0;
            }
            break;

        case STATE_RECORD:
        case STATE_THINK:
            break;

        case STATE_TYPE:
            if (m->state_frames > 0) m->state_frames--;
            if (m->state_frames == 0) enter_walk(m);
            break;

        case STATE_PLAY: {
            /* Dribble loop: 6 frames at 10 ticks each ≈ 600 ms/loop,
             * close to a quick-and-casual ball-bounce rhythm. */
            if (++m->anim_timer >= 10) {
                m->anim_timer = 0;
                m->anim_frame = (m->anim_frame + 1) % 6;
            }
            if (m->state_frames > 0) m->state_frames--;
            if (m->state_frames == 0) enter_walk(m);
            break;
        }
    }
}

static u32 get_species_color(mascot_species_t sp, char ch) {
    switch (ch) {
        case 'W': return rgba_to_c2d(COL_EYE_W);
        case '#': return rgba_to_c2d(COL_EYE_B);
        default: break;
    }
    switch (sp) {
        case MASCOT_CRAB:
            switch (ch) {
                case '@': return rgba_to_c2d(0xe89b89ff); /* salmon pink body */
                case 'F': return rgba_to_c2d(0xd88271ff); /* foot */
                case '#': return rgba_to_c2d(0x000000ff); /* eye */
                default: return 0;
            }
        case MASCOT_CRAB_XIETE:
            switch (ch) {
                case '@': return rgba_to_c2d(0xe87a5cff); /* coral body */
                case 'C': return rgba_to_c2d(0xf48e72ff); /* claw */
                case 'L': return rgba_to_c2d(0xc65840ff); /* leg */
                case 'K': return rgba_to_c2d(0xffa898ff); /* blush */
                default: return 0;
            }
        case MASCOT_CHICK:
            switch (ch) {
                case '@': return rgba_to_c2d(0xfacc15ff); /* yellow face skin / hand */
                case 'H': return rgba_to_c2d(0x9ca3afff); /* gray hair & brows */
                case 'K': return rgba_to_c2d(0x0d0f18ff); /* black jacket / feet */
                case 'B': return rgba_to_c2d(0xf97316ff); /* orange beak */
                case 'R': return rgba_to_c2d(0xe2853aff); /* basketball orange */
                case 'E': return rgba_to_c2d(0xcd202fff); /* red earring */
                case 'S': return rgba_to_c2d(0xd6d9deff); /* gray-white overalls & straps */
                default: return 0;
            }
        case MASCOT_PHAGE:
            switch (ch) {
                case '@': return rgba_to_c2d(0xb8e2f8ff); /* light-blue outline */
                case 'D': return rgba_to_c2d(0x5f9fd6ff); /* inner capsid mark */
                case 'L': return rgba_to_c2d(0xb8e2f8ff); /* tail-fiber legs */
                default: return 0;
            }
        case MASCOT_ALIEN:
            switch (ch) {
                case '@': return rgba_to_c2d(0x4ade80ff); /* green skin */
                case 'G': return rgba_to_c2d(0x22c55eff); /* dark green arms */
                case 'A': return rgba_to_c2d(0xfacc15ff); /* glowing orb */
                case 'S': return rgba_to_c2d(0x38bdf8ff); /* antenna stem */
                case 'P': return rgba_to_c2d(0xc084fcff); /* purple belly */
                case 'K': return rgba_to_c2d(0x86efacff); /* cheek glow */
                default: return 0;
            }
        case MASCOT_NIULAI:
            switch (ch) {
                case 'Y': return rgba_to_c2d(0xf7931eff); /* vivid orange body */
                case 'P': return rgba_to_c2d(0xe3a49eff); /* pink snout / arms / feet */
                case 'D': return rgba_to_c2d(0xc68686ff); /* darker pink shading */
                case 'H': return rgba_to_c2d(0x5d3a3aff); /* dark maroon horn / eye / mouth */
                case 'W': return rgba_to_c2d(0xffffffff); /* white eye */
                case '@': return rgba_to_c2d(0x28141eff); /* near-black for TYPE bubble (so it shows on orange) */
                default: return 0;
            }
        case MASCOT_DOG:
            switch (ch) {
                case 'o': return rgba_to_c2d(0x2b1a10ff); /* dark outline / eyes / nose */
                case 'f': return rgba_to_c2d(0xf2a03cff); /* orange-yellow fur */
                case 'w': return rgba_to_c2d(0xfff4dfff); /* cream muzzle / paws / eye glint */
                case 'm': return rgba_to_c2d(0xc9d8e4ff); /* steel lit face (shield, blade) */
                case 'd': return rgba_to_c2d(0x7b8fa0ff); /* steel shade (shield boss, grip) */
                default: return 0;
            }
        default:
            return 0;
    }
}

static int species_height(mascot_species_t sp) {
    if (sp == MASCOT_CHICK)  return CHICK_H;
    if (sp == MASCOT_NIULAI) return OX_H;
    if (sp == MASCOT_DOG)    return OX_H;
    return CRAB_H;
}

/* Per-species width.  Currently OX_W == CRAB_W (all sprites are 20 px
 * wide), so this is a no-op today — it exists so a wider pet can be
 * added without re-plumbing every centering and bounds calculation. */
static int species_width(mascot_species_t sp) {
    if (sp == MASCOT_NIULAI) return OX_W;
    return CRAB_W;
}

static void draw_species_sprite(mascot_species_t sp, int x0, int y0,
                                const char *const *rows, int w, int h, float z) {
    for (int r = 0; r < h; r++) {
        const char *line = rows[r];
        for (int c = 0; c < w; c++) {
            char ch = line[c];
            if (ch == '.') continue;
            u32 color = get_species_color(sp, ch);
            if (color) {
                C2D_DrawRectSolid((float)(x0 + c), (float)(y0 + r), z, 1, 1, color);
            }
        }
    }
}

void mascot_draw(mascot_t *m) {
    if (!m) return;
    int xi = (int)m->fx;
    int yi = m->y_top;

    if (m->state == STATE_IDLE && ((m->bob_phase / 8) & 1)) yi -= 1;
    /* WALK/FLEE bob: lift the body 1 px on odd walk frames so the foot
     * phase looks like a stride (the foot pixel stays on the ground row
     * but the head/body pops up).  Only for the 20×18 species (KUN KUN,
     * 牛来) where every row counts. */
    if ((m->state == STATE_WALK || m->state == STATE_FLEE) &&
        (m->species == MASCOT_NIULAI || m->species == MASCOT_CHICK ||
         m->species == MASCOT_DOG) &&
        (m->anim_frame & 1)) yi -= 1;
    if (m->state == STATE_HAPPY) yi += m->hop_dy;
    if (m->state == STATE_SAD) xi += (m->shiver_phase < 4) ? 0 : -1;
    if (m->state == STATE_RECORD) yi -= (m->rec_bob < 12) ? 1 : 0;
    if (m->state == STATE_THINK) xi += (m->think_phase < 15) ? 0 : -1;

    /* Select appropriate species sprite frames */
    const char *const *const *walk_set =
        (m->species == MASCOT_CRAB)        ? crab_orig_walk_frames
      : (m->species == MASCOT_CRAB_XIETE)  ? crab_walk_frames
      : (m->species == MASCOT_CHICK)       ? chick_walk_frames
      : (m->species == MASCOT_PHAGE)       ? phage_walk_frames
      : (m->species == MASCOT_ALIEN)       ? alien_walk_frames
      : (m->species == MASCOT_NIULAI)      ? niulai_walk_frames
      :                                      dog_walk_frames;

    const char *const *current_sprite = walk_set[0];
    if (m->state == STATE_WALK || m->state == STATE_FLEE) {
        current_sprite = walk_set[m->anim_frame % 6];
    } else if (m->state == STATE_TYPE) {
        int tframe = (m->type_phase / 5) % 2;
        if (m->species == MASCOT_CRAB) {
            current_sprite = crab_orig_w0;
        } else if (m->species == MASCOT_CRAB_XIETE) {
            current_sprite = (tframe == 0) ? crab_type_f0 : crab_type_f1;
        } else if (m->species == MASCOT_CHICK) {
            current_sprite = (tframe == 0) ? chick_type_f0 : chick_type_f1;
        } else if (m->species == MASCOT_PHAGE) {
            current_sprite = (tframe == 0) ? phage_type_f0 : phage_type_f1;
        } else if (m->species == MASCOT_ALIEN) {
            current_sprite = (tframe == 0) ? alien_type_f0 : alien_type_f1;
        } else if (m->species == MASCOT_NIULAI) {
            current_sprite = (tframe == 0) ? niulai_type_f0 : niulai_type_f1;
        } else if (m->species == MASCOT_DOG) {
            current_sprite = (tframe == 0) ? dog_type_f0 : dog_type_f1;
        } else {
            current_sprite = (tframe == 0) ? crab_type_f0 : crab_type_f1;
        }
    } else if (m->state == STATE_PLAY && m->species == MASCOT_CHICK) {
        current_sprite = chick_play_frames[m->anim_frame % 6];
    } else if (m->state == STATE_HAPPY) {
        current_sprite = (m->species == MASCOT_CRAB)        ? crab_orig_w0
                       : (m->species == MASCOT_CRAB_XIETE)  ? crab_happy_f
                       : (m->species == MASCOT_CHICK)       ? chick_happy_f
                       : (m->species == MASCOT_PHAGE)       ? phage_happy_f
                       : (m->species == MASCOT_ALIEN)       ? alien_happy_f
                       : (m->species == MASCOT_NIULAI)      ? niulai_happy_f
                       : (m->species == MASCOT_DOG)         ? dog_happy_f
                       :                                      crab_orig_w0;
    } else if (m->state == STATE_LOOKUP) {
        current_sprite = (m->species == MASCOT_CRAB)        ? crab_orig_w0
                       : (m->species == MASCOT_CRAB_XIETE)  ? crab_lookup_f
                       : (m->species == MASCOT_CHICK)       ? chick_lookup_f
                       : (m->species == MASCOT_PHAGE)       ? phage_lookup_f
                       : (m->species == MASCOT_ALIEN)       ? alien_lookup_f
                       : (m->species == MASCOT_NIULAI)      ? niulai_lookup_f
                       : (m->species == MASCOT_DOG)         ? dog_lookup_f
                       :                                      crab_orig_w0;
    }

    draw_species_sprite(m->species, xi, yi, current_sprite,
                        species_width(m->species),
                        species_height(m->species), 0.6f);

    /* TYPE speech bubble */
    if (m->state == STATE_TYPE) {
        u32 c = get_species_color(m->species, '@');
        const int BW = 11, BH = 7;
        int sw = species_width(m->species);
        int bx = xi + (sw - BW) / 2;
        int by = yi - BH - 3;
        for (int y = 0; y < BH; y++) {
            for (int x = 0; x < BW; x++) {
                int edge = (y == 0 || y == BH - 1 || x == 0 || x == BW - 1);
                int corner = (x < 1 || x > BW - 2) && (y < 1 || y > BH - 2);
                if (edge && !corner) {
                    C2D_DrawRectSolid((float)(bx + x), (float)(by + y),
                                      0.7f, 1, 1, c);
                }
            }
        }
        int tx = bx + 3;
        C2D_DrawRectSolid((float)(tx), (float)(by + BH), 0.7f, 1, 1, c);
        C2D_DrawRectSolid((float)(tx), (float)(by + BH + 1), 0.7f, 1, 1, c);
        int ndots = (m->type_phase < 15) ? (m->type_phase / 5) + 1 : 0;
        if (ndots > 3) ndots = 3;
        for (int d = 0; d < ndots; d++) {
            int dx = bx + 3 + d * 2;
            int dy = by + 3;
            C2D_DrawRectSolid((float)(dx), (float)(dy), 0.75f, 1, 1, c);
        }
    }

    /* Red ✕ overlay for ALERT. */
    if (m->state == STATE_ALERT) {
        u32 x_c = rgba_to_c2d(COL_X);
        int sway = ((m->alert_phase / 12) & 1) ? 1 : -1;
        int sw = species_width(m->species);
        int x_x  = xi + (sw - 5) / 2 + sway;
        int x_y  = yi - 6;
        for (int row = 0; row < 5; row++) {
            const char *src = alert_x[row];
            for (int col = 0; col < 5; col++) {
                if (src[col] == '@') {
                    C2D_DrawRectSolid((float)(x_x + col),
                                      (float)(x_y + row),
                                      0.7f, 1, 1, x_c);
                }
            }
        }
    }

    /* "..." dots above the head for LOOKUP. */
    if (m->state == STATE_LOOKUP) {
        u32 dim = C2D_Color32(0xcc, 0xcc, 0xcc, 0x80);
        u32 lit = C2D_Color32(0xff, 0xff, 0xff, 0xff);
        int lit_count = (m->dot_phase / 6) % 4;
        int sw = species_width(m->species);
        int base_x = xi + (sw - 10) / 2;
        int base_y = yi - 5;
        for (int d = 0; d < 3; d++) {
            u32 c = (d < lit_count) ? lit : dim;
            int dx = base_x + d * 4;
            for (int row = 0; row < 2; row++) {
                for (int col = 0; col < 2; col++) {
                    C2D_DrawRectSolid((float)(dx + col),
                                      (float)(base_y + row),
                                      0.7f, 1, 1, c);
                }
            }
        }
    }

    /* Green ✓ overlay for HAPPY. */
    if (m->state == STATE_HAPPY) {
        u32 c = rgba_to_c2d(COL_CHECK);
        int sw = species_width(m->species);
        int cx = xi + (sw - 5) / 2;
        int cy = yi - 6;
        for (int row = 0; row < 5; row++) {
            const char *src = happy_check[row];
            for (int col = 0; col < 5; col++) {
                if (src[col] == '@') {
                    C2D_DrawRectSolid((float)(cx + col),
                                      (float)(cy + row),
                                      0.7f, 1, 1, c);
                }
            }
        }
    }

    /* Red ! overlay for SAD. */
    if (m->state == STATE_SAD) {
        u32 c = rgba_to_c2d(COL_BANG);
        int sway = ((m->shiver_phase < 4) ? 0 : -1);
        int sw = species_width(m->species);
        int bx = xi + (sw - 3) / 2 + sway;
        int by = yi - 7;
        for (int row = 0; row < 5; row++) {
            const char *src = sad_bang[row];
            for (int col = 0; col < 3; col++) {
                if (src[col] == '@') {
                    C2D_DrawRectSolid((float)(bx + col),
                                      (float)(by + row),
                                      0.7f, 1, 1, c);
                }
            }
        }
    }

    /* Gold mic overlay for RECORD. */
    if (m->state == STATE_RECORD) {
        u32 c = rgba_to_c2d(COL_MIC);
        int sw = species_width(m->species);
        int mx = xi + (sw - 5) / 2;
        int my = yi - 7;
        for (int row = 0; row < 6; row++) {
            const char *src = mic_icon[row];
            for (int col = 0; col < 5; col++) {
                if (src[col] == '@') {
                    C2D_DrawRectSolid((float)(mx + col),
                                      (float)(my + row),
                                      0.7f, 1, 1, c);
                }
            }
        }
    }

    /* Blue ? overlay for THINK. */
    if (m->state == STATE_THINK) {
        u32 c = rgba_to_c2d(COL_Q);
        int sway = ((m->think_phase < 15) ? 0 : -1);
        int sw = species_width(m->species);
        int qx = xi + (sw - 5) / 2 + sway;
        int qy = yi - 6;
        for (int row = 0; row < 5; row++) {
            const char *src = think_q[row];
            for (int col = 0; col < 5; col++) {
                if (src[col] == '@') {
                    C2D_DrawRectSolid((float)(qx + col),
                                      (float)(qy + row),
                                      0.7f, 1, 1, c);
                }
            }
        }
    }
}

int mascot_hit_test(const mascot_t *m, int tx, int ty) {
    if (!m) return 0;
    int xi = (int)m->fx;
    int yi = m->y_top;
    int sw = species_width(m->species);
    return tx >= xi - HIT_PAD && tx < xi + sw + HIT_PAD &&
           ty >= yi - HIT_PAD && ty < yi + species_height(m->species) + HIT_PAD;
}

void mascot_on_touched(mascot_t *m, int from_tx) {
    if (!m) return;
    if (m->state == STATE_ALERT || m->state == STATE_LOOKUP ||
        m->state == STATE_HAPPY || m->state == STATE_SAD  ||
        m->state == STATE_RECORD || m->state == STATE_THINK ||
        m->state == STATE_TYPE || m->state == STATE_PLAY) return;
    int center = (int)m->fx + species_width(m->species) / 2;
    m->facing = (from_tx < center) ? +1 : -1;
    /* KUN KUN (chick) sometimes shows off a ball instead of running. */
    if (m->species == MASCOT_CHICK && (rand() % 10) < 3) {
        enter_play(m);
    } else {
        enter_flee(m);
    }
}
