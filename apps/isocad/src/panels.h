#ifndef ISOCAD_PANELS_H
#define ISOCAD_PANELS_H

#include <isogl/isogl.h>
#include <stddef.h>

/* Screen-space tool panels: a 32-logical-px strip on the top, left and right
 * edges of the window.  The bottom strip belongs to the console (100 px).
 *
 * Contrary to the first prototype, this version does not use scissor+clear or
 * a private shader: after the scene is drawn the whole overlay is rendered
 * with isogl itself.  The application switches isogl_model_matrix and
 * isogl_view_matrix to a pixel orthographic projection, fills the panels with
 * isogl_draw_filled_rect() and draws the icons from an isogl VBO of segments
 * with isogl_draw_vbo_object().  The matrices are restored afterwards. */

#define ISOCAD_PANEL_LOGICAL_THICKNESS 32.0f
#define ISOCAD_PANEL_MAX_ICONS         32

typedef struct {
    int top, left, right, bottom;   /* framebuffer pixels */
} IsocadUiInsets;

typedef struct {
    const char *name;   /* prototype label, not drawn */
    float cx, cy;       /* icon centre, framebuffer px, origin top-left */
    float half;         /* half side of the square hit cell */
    GLint first;        /* first vertex of this icon in the segment VBO */
    GLsizei count;      /* vertices of this icon */
} IsocadPanelIcon;

typedef struct {
    int fb_w, fb_h;
    int thickness_px;
    int bottom_px;
    IsocadUiInsets insets;

    int icon_count;
    IsocadPanelIcon icons[ISOCAD_PANEL_MAX_ICONS];

    /* CPU copy of the icon line pairs, GL pixel space, four floats per
     * segment.  Kept so the VBO can be recoloured when the hovered icon
     * changes without re-running the icon emitters. */
    float *art;
    size_t art_count;   /* floats */

    isogl_vbo_array vbos;
    GLuint vbo_id;
    int built;
    int hover;          /* hovered icon index, -1 when none */
    int hover_drawn;    /* hover state currently baked into the VBO */
} IsocadPanels;

int isocad_panels_init(IsocadPanels *p);
void isocad_panels_free(IsocadPanels *p);

/* Idempotent.  thickness_px and bottom_px are framebuffer pixels.  Rebuilds
 * the icon geometry only when the size or thickness changes. */
void isocad_panels_layout(IsocadPanels *p, int fb_w, int fb_h,
                          int thickness_px, int bottom_px);

/* Draw the three strips and their icons with the isogl pipelines; cursor_x/y
 * are framebuffer pixels, origin top-left, and select the hovered icon. */
void isocad_panels_draw(IsocadPanels *p, float cursor_x, float cursor_y);

#endif /* ISOCAD_PANELS_H */
