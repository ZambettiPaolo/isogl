#include "panel_deck.h"

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static int close_to(float a, float b) {
    return fabsf(a - b) <= 0.001f;
}

int main(void) {
    isogl_panel_layout layout;
    isogl_panel_edges edges = {
        .top = 32, .left = 32, .right = 32, .bottom = 100, .separator = 1
    };
    CHECK(isogl_panel_compute(&layout, 800, 600, &edges) == 0);
    CHECK(ISOCAD_DECK_PAGE_COUNT == 4);

    IsocadDeckSlot slot;

    /* TOP: cells start at the top-left, one strip-thickness wide. */
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_TOP, 0, &slot) == 0);
    CHECK(slot.placed == 1);
    CHECK(close_to(slot.cell, 32.0f));
    CHECK(close_to(slot.cx, 16.0f) && close_to(slot.cy, 16.0f));

    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_TOP, 24, &slot) == 0);
    CHECK(slot.placed == 1);   /* last cell that still fits in 800 px */
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_TOP, 25, &slot) == 0);
    CHECK(slot.placed == 0);

    /* LEFT: cells start below the top strip and stop above the console. */
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_LEFT, 0, &slot) == 0);
    CHECK(slot.placed == 1);
    CHECK(close_to(slot.cx, 16.0f) && close_to(slot.cy, 48.0f));
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_LEFT, 13, &slot) == 0);
    CHECK(slot.placed == 1);
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_LEFT, 14, &slot) == 0);
    CHECK(slot.placed == 0);

    /* RIGHT: mirrored horizontally. */
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_RIGHT, 0, &slot) == 0);
    CHECK(slot.placed == 1);
    CHECK(close_to(slot.cx, 784.0f) && close_to(slot.cy, 48.0f));

    /* Disabled strip: no place. */
    isogl_panel_edges no_top = {
        .top = 0, .left = 32, .right = 32, .bottom = 100, .separator = 1
    };
    CHECK(isogl_panel_compute(&layout, 800, 600, &no_top) == 0);
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_TOP, 0, &slot) == 0);
    CHECK(slot.placed == 0);

    /* Invalid arguments. */
    CHECK(isocad_deck_slot(NULL, ISOCAD_PANEL_TOP, 0, &slot) == -1);
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_TOP, 0, NULL) == -1);
    CHECK(isocad_deck_slot(&layout, ISOCAD_PANEL_TOP, -1, &slot) == -1);

    /* Parking: active page at zero; other pages at least one extent away. */
    CHECK(isogl_panel_compute(&layout, 800, 600, &edges) == 0);
    float dx = 0.0f, dy = 0.0f;

    isocad_deck_park_offset(ISOCAD_PANEL_TOP, 1, 1, &layout, &dx, &dy);
    CHECK(close_to(dx, 0.0f) && close_to(dy, 0.0f));

    isocad_deck_park_offset(ISOCAD_PANEL_TOP, 1, 0, &layout, &dx, &dy);
    CHECK(close_to(dx, 800.0f) && close_to(dy, 0.0f));

    isocad_deck_park_offset(ISOCAD_PANEL_TOP, 0, 1, &layout, &dx, &dy);
    CHECK(close_to(dx, -800.0f) && close_to(dy, 0.0f));

    isocad_deck_park_offset(ISOCAD_PANEL_LEFT, 3, 0, &layout, &dx, &dy);
    CHECK(close_to(dx, 0.0f) && close_to(dy, 1800.0f));

    isocad_deck_park_offset(ISOCAD_PANEL_RIGHT, 0, 3, &layout, &dx, &dy);
    CHECK(close_to(dx, 0.0f) && close_to(dy, -1800.0f));

    /* The page is not an input to the panel layout: content is unchanged. */
    CHECK(layout.content.x == 32 && layout.content.y == 32 &&
          layout.content.width == 736 && layout.content.height == 468);

    if (failures == 0) {
        printf("test-panel-deck: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test-panel-deck: %d failure(s)\n", failures);
    return 1;
}
