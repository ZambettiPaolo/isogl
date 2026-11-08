#ifndef CAD_H
#define CAD_H

#include <isogl/isogl.h>
#include <isogl/isogl_symbol.h>
#include <isogl/glad.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CAD_MAX_LAYERS 8

typedef struct {
    char name[32];
    float color[4];
    int visible;
    isogl_line_weight default_weight;
    isogl_line_type default_pattern;
} CadLayer;

typedef enum {
    CAD_LINE,
    CAD_CIRCLE,
    CAD_ARC,
    CAD_ELLIPSE,
    CAD_RECTANGLE,
    CAD_TEXT
} CadEntityType;

/*
 * Analytic model of a CAD entity.  The GPU VBO is a derived mirror
 * (vbo_id); selection and bounds use the parameters below, never the
 * tessellated segments.
 *
 * arc: centre, radius, start angle and sweep in radians.  A live arc always
 * has sweep in (0, 2*pi].
 * ellipse: centre, semi-axes rx/ry and rotation in radians.
 */
typedef struct {
    uint32_t id;
    CadEntityType type;
    uint8_t layer;
    isogl_line_type pattern;
    isogl_line_weight weight;
    int visible;
    GLuint vbo_id; // 0 for text entities
    union {
        struct { float x0, y0, x1, y1; } line;
        struct { float cx, cy, r; } circle;
        struct { float cx, cy, r, a0, sweep; } arc;
        struct { float cx, cy, rx, ry, rotation; } ellipse;
        struct { float x0, y0, x1, y1; } rect;
        struct { float x, y, height, angle; char text[128]; } label;
    } geom;
} CadEntity;

/*
 * CAD-side record for an isogl-owned symbol.  The registry keeps ownership of
 * the live geometry; the document keeps an opaque ID plus a deep copy of the
 * typed parameters and a pointer to the persistent C type descriptor.  The
 * parameter copy is what the property UI, save and undo operate on; it is not
 * a copy of the symbol's segments.
 */
typedef struct {
    isogl_symbol_id id;
    const isogl_symbol_type *type;  /* from params_get; type registry persists */
    isogl_symbol_params *params;    /* owned deep copy, applied sheet scale */
} CadSymbolRef;

typedef struct {
    CadLayer layers[CAD_MAX_LAYERS];
    int layer_count;
    CadEntity *entities;
    size_t count;
    size_t capacity;
    uint32_t next_id;              /* monotonic CAD entity ID */
    CadSymbolRef *symbols;         /* refs to isogl-owned symbols */
    size_t symbol_count;
    size_t symbol_capacity;
    isogl_vbo_array parts;
    isogl_string_list strings;
    isogl_font_data *font;
    float sheet_scale;
} CadDocument;

int cad_document_init(CadDocument *doc, isogl_font_data *font, float sheet_scale);
void cad_document_free(CadDocument *doc);

uint32_t cad_add_line(CadDocument *doc, int layer, isogl_line_type pattern,
                      isogl_line_weight weight, float x0, float y0,
                      float x1, float y1);
uint32_t cad_add_circle(CadDocument *doc, int layer, isogl_line_type pattern,
                        isogl_line_weight weight, float cx, float cy, float r);

/*
 * Add an arc/ellipse entity and upload its tessellated VBO.  The chord
 * tolerance is CAD_CHORD_TOLERANCE_MM / doc->sheet_scale, so it is a paper
 * quantity independent of zoom.  The model is analytic; the VBO is derived
 * and never mirrored on the CPU.
 *
 * Arc constructors:
 *   cad_add_arc_three_point: radius = |start - centre|; the end must lie on
 *     the same circle within CAD_ARC_RADIUS_ABS_TOL +
 *     CAD_ARC_RADIUS_REL_TOL * radius.  The sweep is the counter-clockwise
 *     delta (right-hand rule about +Z); a null or ambiguous sweep is rejected.
 *   cad_add_arc_center_angles: radius > 0; the sweep is the counter-clockwise
 *     delta from angle1 to angle2; a null/ambiguous sweep is rejected.
 *   cad_add_ellipse: p1-p2 is the major axis (rx half its length, centre its
 *     midpoint, rotation atan2(p2-p1)); ry is the perpendicular distance of
 *     p3 from the p1-p2 line.  Degenerate axes and non-finite input are
 *     rejected.
 *
 * All three return the new entity ID, or 0 on failure.  Failure leaves no
 * partially built entity in the document (the monotonic ID is consumed but
 * never reused).
 */
uint32_t cad_add_ellipse(CadDocument *doc, int layer, isogl_line_type pattern,
                         isogl_line_weight weight, float p1x, float p1y,
                         float p2x, float p2y, float p3x, float p3y);
uint32_t cad_add_arc_three_point(CadDocument *doc, int layer,
                                 isogl_line_type pattern,
                                 isogl_line_weight weight, float sx, float sy,
                                 float ex, float ey, float cx, float cy);
uint32_t cad_add_arc_center_angles(CadDocument *doc, int layer,
                                   isogl_line_type pattern,
                                   isogl_line_weight weight, float cx,
                                   float cy, float radius, float angle1,
                                   float angle2);

uint32_t cad_add_rectangle(CadDocument *doc, int layer, isogl_line_type pattern,
                           isogl_line_weight weight, float x0, float y0,
                           float x1, float y1);
uint32_t cad_add_text(CadDocument *doc, int layer, float x, float y,
                      float height_mm, float angle, const char *text);

/* Incremental text updates: only the entity's glyph string is rebuilt. */
int cad_update_text(CadDocument *doc, uint32_t entity_id, const char *text);
int cad_set_text_transform(CadDocument *doc, uint32_t entity_id,
                           float x, float y, float height, float angle);

/*
 * Create an isogl-owned symbol and store only its opaque reference in the
 * document.  The layer supplies pattern, weight and colour; sheet_scale comes
 * from the document.  Return the symbol ID, or 0 on failure.
 */
isogl_symbol_id cad_add_dimension_symbol(CadDocument *doc, int layer,
                                         const IsoglDimensionDesc *desc);
isogl_symbol_id cad_add_center_mark_symbol(CadDocument *doc, int layer,
                                           const IsoglCenterMarkDesc *desc);
isogl_symbol_id cad_add_diameter_symbol(CadDocument *doc, int layer,
                                        const IsoglDiameterDesc *desc);
isogl_symbol_id cad_add_radius_symbol(CadDocument *doc, int layer,
                                      const IsoglRadiusDesc *desc);

/*
 * Append an existing symbol reference to the document (low-level helper).
 * On entry the symbol is isogl-owned; on success the document stores its ID,
 * type and a deep copy of its current parameters.  On failure the symbol is
 * destroyed and no reference is appended.
 */
int cad_document_add_symbol(CadDocument *doc, isogl_symbol_id symbol);

/* Look up a symbol reference by ID.  The returned pointer is invalidated by
 * append/remove; the ID itself stays stable. */
CadSymbolRef *cad_symbol_find(CadDocument *doc, isogl_symbol_id symbol);
const CadSymbolRef *cad_symbol_find_const(const CadDocument *doc,
                                          isogl_symbol_id symbol);

/* Parameter copy owned by the reference; never NULL for a live reference. */
isogl_symbol_params *cad_symbol_params(const CadSymbolRef *ref);

/*
 * Replace a symbol's typed parameters.  Calls isogl_symbol_params_update()
 * (ID, pick ID and selection are preserved) and then refreshes the document's
 * parameter copy with a fresh params_get() so it reflects the applied global
 * sheet scale.  On failure the reference keeps its previous copy.
 */
int cad_symbol_update_params(CadDocument *doc, isogl_symbol_id symbol,
                             const isogl_symbol_params *params);

/*
 * Destroy the isogl-owned symbol, free the document's parameter copy and
 * remove the reference.  The symbol ID is not reused.  Callers that keep a
 * persistent selection must drop the matching CAD_SEL_SYMBOL entries.
 */
int cad_remove_symbol(CadDocument *doc, isogl_symbol_id symbol);

/*
 * Write a schema-v2 symbol section ([[symbols]] with type, group = 0 and
 * [symbols.params]) using only stdio and the public parameter metadata.  The
 * SCALE parameter is omitted because the sheet scale is global.  This is a
 * save-only writer: isocad has no TOML parser and cannot load a full drawing.
 * Return 0 on success, -1 on I/O or metadata failure.
 */
int cad_symbols_write_v2(FILE *out, const CadDocument *doc);
int cad_save_symbols_v2(const char *path, const CadDocument *doc);

/*
 * Area selection for CAD entities in paper mm.  contained receives entities
 * completely inside the rectangle; hit receives entities that intersect it or
 * are inside (the crossing-selection set).  Entity visibility and layer
 * visibility are applied.  Text uses an oriented bounding box.
 * isogl-owned symbols are selected separately with isogl_symbol_hit().
 */
void cad_select_rect(const CadDocument *doc,
                     isogl_point p0, isogl_point p1,
                     uint32_t *contained, size_t max_contained,
                     size_t *n_contained,
                     uint32_t *hit, size_t max_hit, size_t *n_hit);

/*
 * Apply/clear the persistent selection.  ids are stable entity IDs; isogl
 * stores one flag per VBO object and per text string.  The set itself is UI
 * state owned by the caller, not document state, and is not saved.
 */
int cad_selection_apply(CadDocument *doc, const uint32_t *ids, size_t count);
void cad_selection_clear(CadDocument *doc);

// Rebuild the text GPU resources (call after adding/modifying text entities).
void cad_document_rebuild_text(CadDocument *doc);

#endif // CAD_H
