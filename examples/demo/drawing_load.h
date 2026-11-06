#ifndef ISOGL_DEMO_DRAWING_LOAD_H
#define ISOGL_DEMO_DRAWING_LOAD_H

#include "isogl.h"
#include "isogl_font.h"

/* Hierarchical demo drawing:
 *   [sheet]       format, orientation, scale_num/scale_den
 *   [[groups]]    id, parent, x, y, angle_deg, scale
 *   [[primitives]] group, type = line | rect | circle | arrow | text
 *
 * Groups form a tree (like data_structure: Drawing -> Group -> Primitive).
 * Primitives are in group-local coordinates and are transformed by the group
 * chain.  The loader emits only isogl segments and strings: no GL calls.
 *
 * If the drawing bounds do not fit the requested sheet (A3 default), the
 * loader promotes the sheet to A2.  The caller then calls
 * isogl_set_sheet() and isogl_zoom_all_sheet().
 */

typedef struct {
    float xmin, ymin, xmax, ymax;
    int has_bounds;
} DrawingBounds;

typedef struct {
    int format;          /* isogl_sheet_format */
    int orientation;     /* isogl_sheet_orientation */
    int promoted;        /* 1 if A2 replaced the requested A3 */
    float width_mm;
    float height_mm;
    int scale_num;
    int scale_den;
    DrawingBounds bounds;
} DrawingSheetInfo;

int drawing_load(const char *path, isogl_font_data *font,
                 isogl_segment_list *segments,
                 isogl_string_list *strings,
                 DrawingSheetInfo *sheet);

#endif /* ISOGL_DEMO_DRAWING_LOAD_H */
