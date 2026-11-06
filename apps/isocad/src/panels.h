#ifndef ISOCAD_PANELS_H
#define ISOCAD_PANELS_H

#include <isogl/isogl.h>
#include <isogl/isogl_overlay.h>
#include <isogl/isogl_panel.h>
#include <stddef.h>

#define ISOCAD_PANEL_LOGICAL_THICKNESS 32.0f
#define ISOCAD_PANEL_MAX_ICONS         32

typedef enum {
    ISOCAD_PANEL_TOP = 0,
    ISOCAD_PANEL_LEFT,
    ISOCAD_PANEL_RIGHT
} IsocadPanelSide;

typedef struct {
    const char *name;
    IsocadPanelSide side;
    float cx, cy;                 /* cell centre, framebuffer px, top-left */
    float half;                   /* half cell side */
    isogl_overlay_group group;    /* icon primitives, local coords */
} IsocadPanelIcon;

typedef struct {
    isogl_overlay *overlay;       /* not owned */
    isogl_font_data *font;        /* not owned */

    IsocadPanelIcon icons[ISOCAD_PANEL_MAX_ICONS];
    int icon_count;
    int hover;                    /* icon index or -1 */

    isogl_overlay_group panel_group;
    isogl_overlay_group hover_group;
    isogl_overlay_group tooltip_group;

    int built;
    int last_width, last_height;
    int last_top, last_left, last_right, last_bottom;

    float tooltip_x, tooltip_y, tooltip_w, tooltip_h;
    int tooltip_shown;
} IsocadPanels;

void isocad_panels_init(IsocadPanels *p, isogl_overlay *overlay,
                        isogl_font_data *font);
void isocad_panels_free(IsocadPanels *p);

/* Idempotent.  Recomputes icon transforms and re-emits the panel strips only
 * when the layout changes. */
void isocad_panels_layout(IsocadPanels *p, const isogl_panel_layout *layout);

/* Per-frame: updates hover, the hover cell and the tooltip group; moves the
 * tooltip with the cursor.  cursor_x/y are framebuffer pixels, top-left. */
void isocad_panels_emit(IsocadPanels *p, const isogl_panel_layout *layout,
                        float cursor_x, float cursor_y);

int isocad_panels_hover(const IsocadPanels *p);

/* Re-emit every icon group from the registry, for a runtime icon change. */
void isocad_panels_rebuild_icons(IsocadPanels *p);

#endif /* ISOCAD_PANELS_H */
