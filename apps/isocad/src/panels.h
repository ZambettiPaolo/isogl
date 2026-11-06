#ifndef ISOCAD_PANELS_H
#define ISOCAD_PANELS_H

#include <isogl/isogl.h>

/* Screen-space tool panels: a 32-logical-px strip on the top, left and right
 * edges of the window.  The bottom strip belongs to the console (100 px).
 * The panels are opaque overlays drawn with scissor+clear, and the icons are
 * GL_LINES drawn with a private pixel-orthographic program.  No isogl
 * viewport is installed or changed: the full-window viewport stays active,
 * like for the console. */

#define ISOCAD_PANEL_LOGICAL_THICKNESS 32.0f
#define ISOCAD_PANEL_MAX_ICONS         32

typedef struct {
    int top, left, right, bottom;   /* framebuffer pixels */
} IsocadUiInsets;

typedef struct {
    const char *name;   /* prototype label, not drawn */
    float cx, cy;       /* icon centre, framebuffer px, origin top-left */
    float half;         /* half side of the square hit cell */
    GLint first;        /* first vertex of this icon in the shared VBO */
    GLsizei count;      /* vertices of this icon */
} IsocadPanelIcon;

typedef struct {
    GLuint program;
    GLuint vao, vbo;
    GLint view_loc, color_loc;
    float view[16];

    int fb_w, fb_h;
    int thickness_px;
    int bottom_px;
    IsocadUiInsets insets;

    int icon_count;
    IsocadPanelIcon icons[ISOCAD_PANEL_MAX_ICONS];
    GLsizei vertex_count;
    int built;
    int hover;          /* hovered icon index, -1 when none */
} IsocadPanels;

/* Compile the icon program and allocate the VAO/VBO.  Returns 0 on success;
 * on failure the panels stay empty and draw() is a no-op. */
int isocad_panels_init(IsocadPanels *p);
void isocad_panels_free(IsocadPanels *p);

/* Idempotent.  thickness_px and bottom_px are framebuffer pixels.  Rebuilds
 * the icon geometry only when the size or thickness changes. */
void isocad_panels_layout(IsocadPanels *p, int fb_w, int fb_h,
                          int thickness_px, int bottom_px);

/* Draw the three strips and their icons; cursor_x/y are framebuffer pixels,
 * origin top-left, and select the hovered icon. */
void isocad_panels_draw(IsocadPanels *p, float cursor_x, float cursor_y);

#endif /* ISOCAD_PANELS_H */
