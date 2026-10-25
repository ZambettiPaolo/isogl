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

// Rebuild the text GPU resources (call after adding/modifying text entities).
void cad_document_rebuild_text(CadDocument *doc);

#endif // CAD_H
