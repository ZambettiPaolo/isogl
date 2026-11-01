#ifndef CAD_H
#define CAD_H

#include <isogl/isogl.h>
#include <isogl/isogl_symbol.h>
#include <isogl/glad.h>
#include <stddef.h>
#include <stdint.h>

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
    CAD_RECTANGLE,
    CAD_TEXT
} CadEntityType;

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
        struct { float x0, y0, x1, y1; } rect;
        struct { float x, y, height, angle; char text[128]; } label;
    } geom;
} CadEntity;

typedef struct {
    CadLayer layers[CAD_MAX_LAYERS];
    int layer_count;
    CadEntity *entities;
    size_t count;
    size_t capacity;
    uint32_t next_id;              /* monotonic CAD entity ID */
    isogl_symbol_id *symbols;      /* refs to isogl-owned symbols */
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
uint32_t cad_add_rectangle(CadDocument *doc, int layer, isogl_line_type pattern,
                           isogl_line_weight weight, float x0, float y0,
                           float x1, float y1);
uint32_t cad_add_text(CadDocument *doc, int layer, float x, float y,
                      float height_mm, float angle, const char *text);

/*
 * Create an isogl-owned symbol and store only its opaque reference in the
 * document.  The layer supplies pattern, weight and colour; sheet_scale comes
 * from the document.  Return the symbol ID, or 0 on failure.
 */
isogl_symbol_id cad_add_dimension_symbol(CadDocument *doc, int layer,
                                         const IsoglDimensionDesc *desc);
isogl_symbol_id cad_add_center_mark_symbol(CadDocument *doc, int layer,
                                           const IsoglCenterMarkDesc *desc);

/* Append an existing symbol reference to the document (low-level helper). */
int cad_document_add_symbol(CadDocument *doc, isogl_symbol_id symbol);

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
