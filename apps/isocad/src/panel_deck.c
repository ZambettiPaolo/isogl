#include "panel_deck.h"

#include <stddef.h>

int isocad_deck_slot(const isogl_panel_layout *layout,
                     IsocadPanelSide side, int slot,
                     IsocadDeckSlot *out) {
    if (!layout || !out || slot < 0) return -1;

    out->placed = 0;
    out->cx = 0.0f;
    out->cy = 0.0f;
    out->cell = 0.0f;

    if (side == ISOCAD_PANEL_TOP) {
        float cell = (float)layout->top.height;
        out->cell = cell;
        if (cell <= 0.0f) return 0;
        out->cx = cell * ((float)slot + 0.5f);
        out->cy = cell * 0.5f;
        out->placed = (out->cx + cell * 0.5f <= (float)layout->width);
        return 0;
    }

    float top = (float)layout->top.height;
    float bottom = (float)layout->bottom.height;
    float limit = (float)layout->height - bottom;

    if (side == ISOCAD_PANEL_LEFT) {
        float cell = (float)layout->left.width;
        out->cell = cell;
        if (cell <= 0.0f) return 0;
        out->cx = cell * 0.5f;
        out->cy = top + cell * ((float)slot + 0.5f);
        out->placed = (out->cy + cell * 0.5f <= limit);
        return 0;
    }

    if (side == ISOCAD_PANEL_RIGHT) {
        float cell = (float)layout->right.width;
        out->cell = cell;
        if (cell <= 0.0f) return 0;
        out->cx = (float)layout->width - cell * 0.5f;
        out->cy = top + cell * ((float)slot + 0.5f);
        out->placed = (out->cy + cell * 0.5f <= limit);
        return 0;
    }

    return -1;
}

void isocad_deck_park_offset(IsocadPanelSide side,
                             int page, int active_page,
                             const isogl_panel_layout *layout,
                             float *dx, float *dy) {
    if (!dx || !dy) return;
    *dx = 0.0f;
    *dy = 0.0f;
    if (!layout || page == active_page) return;

    float pages = (float)(page - active_page);
    if (side == ISOCAD_PANEL_TOP) {
        *dx = pages * (float)layout->width;
    } else if (side == ISOCAD_PANEL_LEFT || side == ISOCAD_PANEL_RIGHT) {
        *dy = pages * (float)layout->height;
    }
}
