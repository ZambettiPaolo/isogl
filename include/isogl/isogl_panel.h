#ifndef ISOGL_PANEL_H
#define ISOGL_PANEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure screen-space panel layout.  No GL, no windowing toolkit: the caller
 * passes the viewport size in framebuffer pixels and gets the four strip
 * rectangles plus the content rectangle used by the CAD for its scene.
 *
 * Edges are viewport-local, top-left pixels.  A non-positive thickness
 * disables that edge.  The strips never overlap and the content rectangle is
 * never negative.
 */

typedef struct isogl_overlay isogl_overlay;

typedef struct {
    int x, y, width, height;
} isogl_panel_rect;

typedef struct {
    int top, left, right, bottom;  /* thickness, framebuffer px */
    int separator;                 /* separator thickness, <= 0 disables */
} isogl_panel_edges;

typedef struct {
    int width, height;             /* viewport size, framebuffer px */
    isogl_panel_edges edges;       /* clamped copy of the input */
    isogl_panel_rect top, left, right, bottom;
    isogl_panel_rect content;      /* CAD scene area */
} isogl_panel_layout;

typedef struct {
    float background[4];
    float edge[4];
} isogl_panel_style;

/* Compute the layout.  Returns 0, or -1 for out == NULL or a non-positive
 * viewport size. */
int isogl_panel_compute(isogl_panel_layout *out, int width, int height,
                        const isogl_panel_edges *edges);

/* Append the strip backgrounds and their inner separators to an overlay
 * group.  The group must have identity transform: coordinates are already
 * viewport-local.  Returns 0 on success, -1 on invalid arguments. */
int isogl_panel_emit(const isogl_panel_layout *layout,
                     const isogl_panel_style *style,
                     isogl_overlay *overlay,
                     uint32_t group);

#ifdef __cplusplus
}
#endif

#endif /* ISOGL_PANEL_H */
