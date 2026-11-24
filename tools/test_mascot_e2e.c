/* End-to-end host verification for DSSH mascots.
 * Covers all 7 species, including restored original crab (MASCOT_CRAB)
 * and preserved 蟹特Crab (MASCOT_CRAB_XIETE), animation states,
 * and draw rasterisation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

typedef uint32_t u32;

static int g_rect_draw_count = 0;
static uint32_t g_last_drawn_color = 0;
static uint32_t g_color_histogram[256] = {0};

static inline u32 C2D_Color32(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | (u32)a;
}

static inline void C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 clr) {
    (void)x; (void)y; (void)z; (void)w; (void)h;
    g_rect_draw_count++;
    g_last_drawn_color = clr;
    uint8_t r = (clr >> 24) & 0xff;
    g_color_histogram[r]++;
}

#include "../source/mascot.h"
#include "../source/config.h"
#include "../source/mascot.c"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        failures++; \
    } else { \
        fprintf(stderr, "ok   %s\n", msg); \
    } \
} while (0)

int main(void) {
    printf("--- Running Mascot End-to-End Tests ---\n");

    /* 1. Verify species count and config bounds */
    CHECK(MASCOT_SPECIES_COUNT == 7, "MASCOT_SPECIES_COUNT is 7");
    CHECK(DSSH_MASCOT_MODE_MAX == 7, "DSSH_MASCOT_MODE_MAX is 7");
    CHECK(MASCOT_CRAB == 0, "MASCOT_CRAB is index 0");
    CHECK(MASCOT_CRAB_XIETE == 1, "MASCOT_CRAB_XIETE is index 1");

    /* 2. Init mascot */
    mascot_t *m = mascot_init(60, 300, 220);
    CHECK(m != NULL, "mascot_init succeeded");
    CHECK(m->species == MASCOT_CRAB, "default species is MASCOT_CRAB (restored original crab)");

    /* 3. Test Original Crab (MASCOT_CRAB) drawing & walk */
    mascot_set_species(m, MASCOT_CRAB);
    CHECK(m->species == MASCOT_CRAB, "active species set to MASCOT_CRAB");
    g_rect_draw_count = 0;
    mascot_draw(m);
    CHECK(g_rect_draw_count > 0, "MASCOT_CRAB draws pixels");
    /* Check that original salmon pink body color is drawn (r=0xe8, g=0x9b, b=0x89) */
    u32 orig_body = rgba_to_c2d(0xe89b89ff);
    u32 orig_foot = rgba_to_c2d(0xd88271ff);
    CHECK(get_species_color(MASCOT_CRAB, '@') == orig_body, "MASCOT_CRAB body color matches upstream salmon pink");
    CHECK(get_species_color(MASCOT_CRAB, 'F') == orig_foot, "MASCOT_CRAB foot color matches upstream");

    /* 4. Test 蟹特Crab (MASCOT_CRAB_XIETE) drawing & colors */
    mascot_set_species(m, MASCOT_CRAB_XIETE);
    CHECK(m->species == MASCOT_CRAB_XIETE, "active species set to MASCOT_CRAB_XIETE");
    g_rect_draw_count = 0;
    mascot_draw(m);
    CHECK(g_rect_draw_count > 0, "MASCOT_CRAB_XIETE draws pixels");
    u32 xiete_body = rgba_to_c2d(0xe87a5cff);
    u32 xiete_claw = rgba_to_c2d(0xf48e72ff);
    CHECK(get_species_color(MASCOT_CRAB_XIETE, '@') == xiete_body, "MASCOT_CRAB_XIETE body color is coral");
    CHECK(get_species_color(MASCOT_CRAB_XIETE, 'C') == xiete_claw, "MASCOT_CRAB_XIETE claw color is present");

    /* 5. Test all species rendering through multiple animation frames */
    for (int s = 0; s < MASCOT_SPECIES_COUNT; s++) {
        mascot_set_species(m, (mascot_species_t)s);
        CHECK(m->species == (mascot_species_t)s, "species set successfully");

        /* Step frames in WALK */
        for (int f = 0; f < 12; f++) {
            mascot_update(m);
            g_rect_draw_count = 0;
            mascot_draw(m);
            CHECK(g_rect_draw_count > 0, "walk frame rendered pixels");
        }

        /* Test TYPE state */
        mascot_type_kick(m);
        CHECK(m->state == STATE_TYPE, "mascot entered TYPE state");
        g_rect_draw_count = 0;
        mascot_draw(m);
        CHECK(g_rect_draw_count > 0, "type pose rendered pixels");

        /* Test ALERT state */
        mascot_set_alert(m, 1);
        CHECK(m->state == STATE_ALERT, "mascot entered ALERT state");
        g_rect_draw_count = 0;
        mascot_draw(m);
        CHECK(g_rect_draw_count > 0, "alert pose rendered pixels");
        mascot_set_alert(m, 0);
        CHECK(m->state == STATE_WALK, "mascot cleared ALERT state back to WALK");

        /* Test LOOKUP / RECONNECTING state */
        mascot_set_reconnecting(m, 1);
        CHECK(m->state == STATE_LOOKUP, "mascot entered LOOKUP state");
        g_rect_draw_count = 0;
        mascot_draw(m);
        CHECK(g_rect_draw_count > 0, "lookup pose rendered pixels");
        mascot_celebrate(m);
        CHECK(m->state == STATE_HAPPY, "mascot entered HAPPY state after celebrate");
        g_rect_draw_count = 0;
        mascot_draw(m);
        CHECK(g_rect_draw_count > 0, "happy pose rendered pixels");
        mascot_sadden(m);
        CHECK(m->state == STATE_SAD, "mascot entered SAD state after sadden");

        /* Test RECORD state */
        mascot_set_recording(m, 1);
        CHECK(m->state == STATE_RECORD, "mascot entered RECORD state");
        g_rect_draw_count = 0;
        mascot_draw(m);
        CHECK(g_rect_draw_count > 0, "recording pose rendered pixels");
        mascot_set_recording(m, 0);

        /* Test THINK state */
        mascot_set_thinking(m, 1);
        CHECK(m->state == STATE_THINK, "mascot entered THINK state");
        g_rect_draw_count = 0;
        mascot_draw(m);
        CHECK(g_rect_draw_count > 0, "thinking pose rendered pixels");
        mascot_set_thinking(m, 0);
    }

    /* 6. Hit testing & touch reaction */
    mascot_set_species(m, MASCOT_CRAB);
    int center_x = (int)m->fx + species_width(m->species) / 2;
    int center_y = m->y_top + species_height(m->species) / 2;
    CHECK(mascot_hit_test(m, center_x, center_y) == 1, "mascot_hit_test center hits");
    CHECK(mascot_hit_test(m, 0, 0) == 0, "mascot_hit_test miss (0,0)");
    mascot_on_touched(m, center_x - 5);
    CHECK(m->state == STATE_FLEE, "touch triggers FLEE state");

    /* 7. Bounds clamping */
    mascot_set_bounds(m, 100, 200);
    CHECK((int)m->fx >= 100 && (int)m->fx <= 200, "mascot clamped within bounds");

    mascot_free(m);
    CHECK(failures == 0, "All mascot tests passed without failure");

    if (failures == 0) {
        printf("All mascot E2E tests PASSED!\n");
        return 0;
    } else {
        fprintf(stderr, "%d mascot tests FAILED!\n", failures);
        return 1;
    }
}
