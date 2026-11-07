#ifndef ISOGL_DEMO_DRAWING_LOAD_H
#define ISOGL_DEMO_DRAWING_LOAD_H

#include <stdio.h>

#include "isogl.h"
#include "isogl_font.h"
#include "isogl_symbol.h"
#include "isogl_symbol_geometry.h"

/* Hierarchical demo drawing:
 *   [sheet]       format, orientation, scale_num/scale_den, schema_version
 *   [[groups]]    id, parent, x, y, angle_deg, scale
 *   [[primitives]] group, type = line | rect | circle | arrow | text
 *   [[symbols]]   group, type = <registered C symbol type>
 *   [symbols.params] typed parameter values, by schema key
 *
 * Groups form a tree (like data_structure: Drawing -> Group -> Primitive).
 * Primitives and symbol control points are in group-local coordinates and are
 * transformed by the group chain.  The loader emits only isogl segments and
 * strings and returns generic symbol descriptors: it never calls GL and never
 * creates a symbol.  The caller initializes isogl_symbol first (so the C type
 * registry is available) and then creates each symbol with
 * isogl_symbol_create(type, params, scale).
 *
 * Symbol file schema v2 stores the type name and typed parameter values only;
 * generation stays in C.  Parameter keys and kinds come from the registered
 * type schema (isogl_symbol_find_type / isogl_symbol_param_desc).  The global
 * sheet scale is [sheet] scale_num/scale_den and is never stored per symbol.
 *
 * Backward compatibility: the old flat p1x/p2x/p3x (dimension) and cx/cy
 * (center_mark) schema is translated into the same typed parameters.
 *
 * Unknown type: the entry is kept (type owned, params == NULL) and the symbol
 * is skipped with a warning; the loader never invents fallback geometry.  The
 * writer cannot preserve an unknown parameter block, so it omits such entries.
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

typedef struct {
    int group;
    char *type;                     /* owned; never NULL for a parsed entry */
    isogl_symbol_params *params;    /* owned; NULL when the type is unknown */
} DrawingSymbolEntry;

typedef struct {
    DrawingSymbolEntry items[DRAW_MAX_SYMBOLS];
    size_t count;
} DrawingSymbols;

typedef struct {
    int format;          /* isogl_sheet_format */
    int orientation;     /* isogl_sheet_orientation */
    int promoted;        /* 1 if A2 replaced the requested A3 */
    int schema_version;  /* 1 = legacy flat symbols; 2 = typed params */
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

/* Free the owned type strings and parameter objects inside `symbols`. */
void drawing_symbols_free(DrawingSymbols *symbols);

/* Write a schema-v2 [[symbols]] section (type, group and [symbols.params]) to
 * an open stream.  SCALE parameters are omitted: the sheet scale is global.
 * Entries with params == NULL (unknown types) are omitted; there is no raw
 * representation of their parameter block yet.  Returns 0/-1. */
int drawing_write_symbols(FILE *out, const DrawingSymbols *symbols);

/* Same, opening/closing `path` as a text file.  Returns 0/-1. */
int drawing_save_symbols(const char *path, const DrawingSymbols *symbols);

#endif /* ISOGL_DEMO_DRAWING_LOAD_H */
