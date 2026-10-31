#ifndef CAD_H
#define CAD_H

#include <isogl/isogl.h>
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
    CAD_TEXT,
    CAD_DIMENSION
} CadEntityType;

/*
 * Aligned linear dimension.  p1 and p2 are the measured points; p3 is any
 * point on the dimension line, which is parallel to p1-p2 and therefore
 * defines both the offset distance and the side.  The value is never stored:
 * it is computed from |p1-p2| in document units.  Text height, arrow size and
 * extension overshoot are paper-space quantities and are converted with
 * doc->sheet_scale by the generator.
 *
 * pick_text includes the text OBB in the rectangle pick geometry.  The text
 * remains clickable through the GPU pick pass regardless of this flag.
 */
typedef struct {
    double x0, y0;          /* p1 */
    double x1, y1;          /* p2 */
    double px, py;          /* p3: point on the dimension line */
    double text_height_mm;  /* ISO 3098 height on paper */
    char prefix[16];
    char suffix[16];
    char unit[8];
    int precision;          /* decimals in the value */
    int pick_text;          /* 1 = text OBB participates in rectangle pick */
} CadDimension;

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
        CadDimension dimension;
    } geom;
} CadEntity;

typedef struct {
    CadLayer layers[CAD_MAX_LAYERS];
    int layer_count;
    CadEntity *entities;
    size_t count;
    size_t capacity;
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

/* Initialize a dimension with sensible ISO defaults (3.5 mm text, 1 decimal,
 * " mm" unit, text OBB included in rectangle selection). */
void cad_dimension_init(CadDimension *dim);
uint32_t cad_add_dimension(CadDocument *doc, int layer, const CadDimension *dim);

/*
 * Area selection in paper mm.  contained receives entities completely inside
 * the rectangle; hit receives entities that intersect it or are inside
 * (the crossing-selection set).  Entity visibility and layer visibility are
 * applied.  Text uses an approximate oriented bounding box.
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
