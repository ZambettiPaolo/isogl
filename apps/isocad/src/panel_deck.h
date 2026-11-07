#ifndef ISOCAD_PANEL_DECK_H
#define ISOCAD_PANEL_DECK_H

#include <isogl/isogl_panel.h>

/* Pure placement math for the isocad panel deck.  Pages are logical groups of
 * icons inside the existing top/left/right strips; the active page sits in its
 * dock cell, the other pages are parked one full viewport extent away. */

#define ISOCAD_DECK_PAGE_COUNT 4

typedef enum {
    ISOCAD_PANEL_TOP = 0,
    ISOCAD_PANEL_LEFT,
    ISOCAD_PANEL_RIGHT
} IsocadPanelSide;

typedef struct {
    int placed;       /* 0 if the strip is disabled or the cell overflows */
    float cx, cy;     /* cell centre, viewport-local top-left pixels */
    float cell;       /* cell side in pixels */
} IsocadDeckSlot;

/* Returns 0 on success, -1 on invalid arguments. */
int isocad_deck_slot(const isogl_panel_layout *layout,
                     IsocadPanelSide side, int slot,
                     IsocadDeckSlot *out);

/* Offset that parks a page outside the visible field.  The active page has a
 * zero offset.  A non-active page is at least one viewport extent away. */
void isocad_deck_park_offset(IsocadPanelSide side,
                             int page, int active_page,
                             const isogl_panel_layout *layout,
                             float *dx, float *dy);

#endif /* ISOCAD_PANEL_DECK_H */
