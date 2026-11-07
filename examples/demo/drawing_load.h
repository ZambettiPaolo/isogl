#ifndef ISOGL_DEMO_DRAWING_LOAD_H
#define ISOGL_DEMO_DRAWING_LOAD_H

#include "isogl.h"
#include "isogl_font.h"
#include "isogl_symbol_geometry.h"

/* Hierarchical demo drawing:
 *   [sheet]       format, orientation, scale_num/scale_den
 *   [[groups]]    id, parent, x, y, angle_deg, scale
 *   [[primitives]] group, type = line | rect | circle | arrow | text
 *   [[symbols]]   group, type = dimension | center_mark
 *
 * Groups form a tree (like data_structure: Drawing -> Group -> Primitive).
 * Primitives and symbol control points are in group-local coordinates and are
 * transformed by the group chain.  The loader emits only isogl segments and
 * strings and returns symbol descriptors: it never calls GL or the symbol
 * registry.  The caller initializes isogl_symbol and creates the symbols.
 *
 * If the drawing bounds do not fit the requested sheet (A3 default), the
 * loader promotes the sheet to A2.  The caller then calls
 * isogl_set_sheet() and isogl_zoom_all_sheet().
 */

#define DRAW_MAX_SYMBOLS 32

typedef struct {
    float xmin, ymin, xmax, ymax;
    int has_bounds;
} DrawingBounds;

typedef enum {
    DRAW_SYMBOL_DIMENSION = 0,
    DRAW_SYMBOL_CENTER_MARK = 1
} DrawSymbolType;

typedef struct {
    int group;
    DrawSymbolType type;
    IsoglDimensionDesc dimension;
    IsoglCenterMarkDesc center_mark;
} DrawingSymbolSpec;

typedef struct {
    DrawingSymbolSpec items[DRAW_MAX_SYMBOLS];
    size_t count;
} DrawingSymbols;

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
                 DrawingSheetInfo *sheet,
                 DrawingSymbols *symbols);

#endif /* ISOGL_DEMO_DRAWING_LOAD_H */
