#ifndef ISOGL_OVERLAY_H
#define ISOGL_OVERLAY_H

#include "isogl.h"
#include "isogl_font.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Screen-space overlay with retained, per-group batching.
 *
 * Coordinates are viewport-local framebuffer pixels with the origin at the
 * TOP-LEFT of the active viewport, like every other pixel input in isogl.
 * The host/CAD applies the device pixel ratio before calling the overlay.
 *
 * A group owns local quads, lines and text.  set_transform() places and scales
 * the group in the viewport; primitives stay in local coordinates and are
 * baked into two shared GPU buffers at commit time.  Every mutation marks only
 * that group dirty, so static frames never upload or rebuild anything.
 *
 * Draw order is painter-like by layer, then creation order:
 *   for each layer: quads, lines, text of the groups in that layer.
 * A tooltip is simply a group in a higher layer with a quad and an optional
 * text; there is no tooltip-specific API.
 *
 * Clipping is per group: quads and lines carry the clip rect per vertex and
 * text carries it per string, so clipping does not split batches or add draw
 * calls.
 *
 * The overlay does not own the font.  Destroy every overlay before calling
 * isogl_font_free_data() and isogl_cleanup().  It uses the active viewport and
 * the current GL context; it is not thread-safe.
 */

typedef struct isogl_overlay isogl_overlay;
typedef uint32_t isogl_overlay_group;

#define ISOGL_OVERLAY_GROUP_INVALID 0u

typedef struct {
    size_t groups;     /* live groups */
    size_t quads;      /* baked quads */
    size_t lines;      /* baked line segments */
    size_t texts;      /* baked strings */
    size_t draw_calls; /* issued by the last isogl_overlay_draw() */
} isogl_overlay_stats;

/* Allocate the overlay and its GL objects.  Requires the current GL context
 * and a font with a loaded glyph set.  Returns NULL on failure. */
isogl_overlay *isogl_overlay_create(isogl_font_data *font);
void isogl_overlay_destroy(isogl_overlay *overlay);

/* Non-owning font replacement; NULL disables text from that point on. */
void isogl_overlay_set_font(isogl_overlay *overlay, isogl_font_data *font);

/* Groups.  IDs are stable for the overlay lifetime and are not reused. */
isogl_overlay_group isogl_overlay_group_create(isogl_overlay *overlay,
                                               int layer);
void isogl_overlay_group_destroy(isogl_overlay *overlay,
                                 isogl_overlay_group group);
void isogl_overlay_group_clear(isogl_overlay *overlay,
                               isogl_overlay_group group);

/* Primitives are in group-local coordinates.  Text x/y is the baseline-left
 * point in local top-left pixels; angle is radians CCW on screen. */
void isogl_overlay_group_quad(isogl_overlay *overlay,
                              isogl_overlay_group group,
                              float x, float y, float w, float h,
                              const float rgba[4]);
void isogl_overlay_group_line(isogl_overlay *overlay,
                              isogl_overlay_group group,
                              float x0, float y0, float x1, float y1,
                              const float rgba[4]);
void isogl_overlay_group_text(isogl_overlay *overlay,
                              isogl_overlay_group group,
                              const char *utf8,
                              float x, float baseline_y,
                              float height_px, float angle,
                              const float rgba[4]);

void isogl_overlay_group_set_transform(isogl_overlay *overlay,
                                       isogl_overlay_group group,
                                       float tx, float ty, float scale);
void isogl_overlay_group_set_layer(isogl_overlay *overlay,
                                   isogl_overlay_group group, int layer);
/* Clip rect is viewport-local top-left pixels.  enabled == 0 removes it. */
void isogl_overlay_group_set_clip(isogl_overlay *overlay,
                                  isogl_overlay_group group,
                                  int enabled,
                                  float x, float y, float w, float h);
/* Override colour for every primitive of the group (hover).  NULL restores the
 * per-primitive colours. */
void isogl_overlay_group_set_color(isogl_overlay *overlay,
                                   isogl_overlay_group group,
                                   const float rgba[4]);
void isogl_overlay_group_set_visible(isogl_overlay *overlay,
                                     isogl_overlay_group group, int visible);

/* Bake dirty groups and upload changed buffers.  Safe to call every frame;
 * it is a no-op when nothing changed.  isogl_overlay_draw() calls it
 * implicitly.  Returns 0 on success, -1 on invalid state. */
int isogl_overlay_commit(isogl_overlay *overlay);

/* Draw the layers through a private pixel projection.  The global isogl
 * matrices are left untouched.  Needs an active viewport; the clip and NDC
 * values are computed at draw time, never cached. */
int isogl_overlay_draw(isogl_overlay *overlay);

isogl_overlay_stats isogl_overlay_get_stats(const isogl_overlay *overlay);

#ifdef __cplusplus
}
#endif

#endif /* ISOGL_OVERLAY_H */
