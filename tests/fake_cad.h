#ifndef FAKE_CAD_H
#define FAKE_CAD_H

#include <stddef.h>
#include <stdint.h>

#include <isogl/isogl_symbol.h>

/*
 * Test-only, minimal hierarchical CAD.  It emulates the future CAD model
 * (Drawing -> Group -> Primitive + symbol references) without the production
 * machinery.  It is not part of libisogl or isocad.
 */

#define FAKE_MAX_GROUPS 16
#define FAKE_MAX_PRIMITIVES 64
#define FAKE_MAX_SYMBOLS 16
#define FAKE_CAD_PICK_BASE 0x10000u

typedef struct {
    int id;
    int parent;              /* parent group id, -1 for root */
    char name[32];
    double x, y;             /* origin relative to parent */
    double angle_deg;        /* relative rotation */
    double scale;
} FakeGroup;

typedef enum {
    FAKE_PRIM_LINE = 0,
    FAKE_PRIM_RECT = 1,
    FAKE_PRIM_CIRCLE = 2,
    FAKE_PRIM_TEXT = 3
} FakePrimitiveType;

typedef struct {
    int group;               /* group id */
    FakePrimitiveType type;
    /*
     * line:   x0,y0,x1,y1
     * rect:   x0,y0,x1,y1
     * circle: cx,cy,r
     * text:   x,y,height_mm,angle_rad
     */
    double p[6];
    char text[64];
    uint8_t pattern, weight, layer;
    float color[4];
} FakePrimitive;

typedef enum {
    FAKE_SYMBOL_DIMENSION = 0,
    FAKE_SYMBOL_CENTER_MARK = 1
} FakeSymbolType;

typedef struct {
    int group;
    FakeSymbolType type;
    IsoglDimensionDesc dimension;
    IsoglCenterMarkDesc center_mark;
} FakeSymbolSpec;

typedef struct {
    double sheet_scale;
    FakeGroup groups[FAKE_MAX_GROUPS];
    size_t group_count;
    FakePrimitive primitives[FAKE_MAX_PRIMITIVES];
    size_t primitive_count;
    FakeSymbolSpec symbols[FAKE_MAX_SYMBOLS];
    size_t symbol_count;
} FakeDrawingSpec;

typedef struct {
    int group;
    isogl_symbol_id symbol;
} FakeSymbolRef;

typedef struct {
    FakeDrawingSpec spec;
    FakeSymbolRef refs[FAKE_MAX_SYMBOLS];
    size_t ref_count;
    isogl_vbo_array parts;
    isogl_string_list strings;
    isogl_font_data *font;
    int graphics_valid;
} FakeDrawing;

void fake_drawing_spec_init(FakeDrawingSpec *spec);

int fake_drawing_load_toml(FakeDrawingSpec *spec, const char *path);

int fake_drawing_build(FakeDrawing *drawing, const FakeDrawingSpec *spec,
                       isogl_font_data *font);
int fake_drawing_create_symbols(FakeDrawing *drawing);
void fake_drawing_draw(FakeDrawing *drawing);
void fake_drawing_rebuild_graphics(FakeDrawing *drawing);
void fake_drawing_free(FakeDrawing *drawing);

int fake_drawing_pick_symbols(FakeDrawing *drawing, SDL_Window *win,
                              float px, float py,
                              uint32_t *ids, size_t max, size_t *count);

int fake_drawing_select_symbols(FakeDrawing *drawing, const isogl_box *query,
                                int crossing,
                                uint32_t *ids, size_t max, size_t *count);

/*
 * Explode the symbol reference at ref_index through isogl.  The emitted
 * segments/texts become FakePrimitive records in the same group; on success
 * the reference is removed and the symbol is destroyed by isogl.  On emitter
 * failure the spec is rolled back and the symbol stays alive.
 */
int fake_drawing_explode_symbol(FakeDrawing *drawing, size_t ref_index,
                                size_t *segments_out, size_t *texts_out);

#endif /* FAKE_CAD_H */
