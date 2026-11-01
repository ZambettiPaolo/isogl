#ifndef ISOGL_SYMBOL_H
#define ISOGL_SYMBOL_H

#include "isogl.h"
#include "isogl_symbol_geometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * isogl-owned symbols (dimensions, center marks, ...).  The CAD document keeps
 * only an opaque isogl_symbol_id; it never reads the symbol's VBO or internal
 * segments.  It can ask for bounds/hit/pick and, to take ownership of the
 * geometry, explode the symbol (which destroys it).
 *
 * The module owns its VBO/string arrays and requires a current GL context for
 * create/draw/pick/destroy.  isogl_symbol_cleanup() must run before
 * isogl_cleanup().
 */

typedef uint32_t isogl_symbol_id;

/* font is used for dimension text layout; it must stay alive until cleanup. */
int isogl_symbol_init(isogl_font_data *font);
void isogl_symbol_cleanup(void);

isogl_symbol_id isogl_symbol_create_dimension(const IsoglDimensionDesc *d);
isogl_symbol_id isogl_symbol_create_center_mark(const IsoglCenterMarkDesc *d);
int isogl_symbol_destroy(isogl_symbol_id id);

void isogl_symbol_draw_all(void);

/* Returns the distinct symbol IDs visible in the cursor box, centre-out.
 * px, py are viewport-local framebuffer pixels (origin top-left); extent_px
 * is the pick box side in framebuffer pixels. */
int isogl_symbol_pick_at(float px, float py, int extent_px,
                         uint32_t *ids, size_t max, size_t *count);

/*
 * Analytic rectangle predicate over the symbol's pick geometry.
 * crossing = 0: the symbol is entirely contained.
 * crossing != 0: the symbol intersects the query or is contained.
 * Returns 1 if selected, 0 if not, -1 on invalid arguments/dead ID.
 */
int isogl_symbol_hit(isogl_symbol_id id, const isogl_box *query, int crossing);

/* AABB of the symbol's pick geometry; 0 on success, -1 on error. */
int isogl_symbol_bounds(isogl_symbol_id id, isogl_box *out);

/* Set the persistent selection flag on the symbol's VBO and text string. */
int isogl_symbol_set_selected(isogl_symbol_id id, int selected);

/*
 * Emit the symbol's render geometry (segments and text runs) and, on success,
 * destroy the symbol.  On emitter failure the symbol is left alive.  The
 * emitted geometry is in model/document units; text height is paper mm.
 */
int isogl_symbol_explode(isogl_symbol_id id, IsoglSymEmitter *out);

#ifdef __cplusplus
}
#endif

#endif /* ISOGL_SYMBOL_H */
