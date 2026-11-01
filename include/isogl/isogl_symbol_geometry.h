#ifndef ISOGL_SYMBOL_GEOMETRY_H
#define ISOGL_SYMBOL_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure CPU geometry for isogl-owned symbols.
 *
 * This module has no GL/SDL dependency and no knowledge of the CAD document.
 * The same functions feed the renderer, the pick geometry and the explode
 * operation: dimension/center-mark parameters in, segments and text runs out.
 */

typedef enum {
    ISOGL_SYM_SEGMENT = 0,
    ISOGL_SYM_TEXT = 1
} IsoglSymItemKind;

typedef struct {
    IsoglSymItemKind kind;

    /* Segment: from (x0,y0) to (x1,y1). */
    double x0, y0, x1, y1;

    /* Text: UTF-8 string valid only during the emit callback; the origin is
     * the baseline-left point already centred by the generator. */
    const char *utf8;
    double x, y, height_mm, angle;
    int align; /* 0 = baseline-left; reserved */

    /* Style copied to the generated primitive/vertex. */
    uint8_t pattern, weight, layer;
    float color[4];
} IsoglSymItem;

typedef struct {
    void *user;
    int (*emit)(void *user, const IsoglSymItem *item);
} IsoglSymEmitter;

/* Text width in model units.  The caller supplies the real font metrics (or a
 * deterministic stub in headless tests). */
typedef struct {
    void *user;
    double (*text_width)(void *user, const char *utf8, double height_mm);
} IsoglSymMetrics;

typedef struct {
    double x0, y0;          /* p1: first measured point */
    double x1, y1;          /* p2: second measured point */
    double px, py;          /* p3: point on the dimension line */
    double text_height_mm;  /* ISO 3098 height on paper */
    double sheet_scale;     /* paper/model scale; 0 or negative -> 1 */
    char prefix[16];
    char suffix[16];
    char unit[8];
    int precision;          /* decimals in the value */
    int pick_text;          /* 1 = text OBB participates in rectangle pick */
    uint8_t pattern, weight, layer;
    float color[4];
} IsoglDimensionDesc;

typedef struct {
    double cx, cy;
    double size_mm;         /* arm length on paper */
    double sheet_scale;     /* paper/model scale; 0 or negative -> 1 */
    uint8_t pattern, weight, layer;
    float color[4];
} IsoglCenterMarkDesc;

typedef enum {
    ISOGL_SYM_PICK_SEGMENT = 0,
    ISOGL_SYM_PICK_OBB = 1
} IsoglSymPickKind;

typedef struct {
    IsoglSymPickKind kind;
    double x0, y0, x1, y1;  /* segment */
    double corners[4][2];   /* OBB, counter-clockwise */
} IsoglSymPickItem;

typedef struct {
    void *user;
    int (*emit)(void *user, const IsoglSymPickItem *item);
} IsoglSymPickEmitter;

int isogl_dimension_geometry(const IsoglDimensionDesc *d,
                             const IsoglSymMetrics *metrics,
                             IsoglSymEmitter *out);

int isogl_dimension_pick_geometry(const IsoglDimensionDesc *d,
                                  const IsoglSymMetrics *metrics,
                                  IsoglSymPickEmitter *out);

int isogl_center_mark_geometry(const IsoglCenterMarkDesc *d,
                               IsoglSymEmitter *out);

int isogl_center_mark_pick_geometry(const IsoglCenterMarkDesc *d,
                                    IsoglSymPickEmitter *out);

#ifdef __cplusplus
}
#endif

#endif /* ISOGL_SYMBOL_GEOMETRY_H */
