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

/* Arrow style at one end of a dimension line.
 *
 * OPEN and CLOSED place the head inside the measured span.  The *_OUTSIDE
 * variants keep the tip on the extension line but draw the legs outside the
 * span, so the head points inward; the dimension line is extended 5 mm beyond
 * that end to host the head.  DOT is a 1.5 mm outline circle. */
typedef enum {
    ISOGL_DIM_ARROW_NONE = 0,
    ISOGL_DIM_ARROW_OPEN = 1,           /* two legs, open V */
    ISOGL_DIM_ARROW_CLOSED = 2,         /* closed triangle outline */
    ISOGL_DIM_ARROW_TICK = 3,           /* 45-degree slash */
    ISOGL_DIM_ARROW_DOT = 4,            /* 1.5 mm outline dot */
    ISOGL_DIM_ARROW_OPEN_OUTSIDE = 5,   /* open head outside, inward */
    ISOGL_DIM_ARROW_CLOSED_OUTSIDE = 6  /* closed head outside, inward */
} IsoglDimensionArrow;

/*
 * Aligned dimension.  p1 and p2 are the measured points, p3 chooses the side
 * of the dimension line, and `distance` optionally overrides how far the
 * dimension line sits from the p1-p2 line (in model units; 0 = derive it from
 * p3).  The CAD fills every field on each call: there are no hidden defaults.
 *
 * The automatic value is formatted with `precision` decimals.  text_left and
 * text_right are free text placed before and after it; no unit of measure is
 * appended by the generator (a caller that wants " mm" puts it in
 * text_right).  Extension lines start 3 mm away from p1/p2; mark_points draws
 * a 0.5 mm reference segment at those points.
 */
typedef struct {
    double x0, y0;          /* p1: first measured point */
    double x1, y1;          /* p2: second measured point */
    double px, py;          /* p3: direction/side point */
    double distance;        /* dimension-line offset in model units;
                               0 = use the p3 distance.  The side is chosen
                               by p3. */
    double text_offset_mm;  /* signed text-centre offset from the dimension
                               line midpoint, in paper mm */
    double text_height_mm;  /* ISO 3098 height on paper */
    double sheet_scale;     /* paper/model scale; 0 or negative -> 1 */
    char text_left[32];     /* free text before the automatic value */
    char text_right[32];    /* free text after the automatic value */
    int precision;          /* decimals in the value */
    int pick_text;          /* 1 = text OBB participates in rectangle pick */
    IsoglDimensionArrow arrow_left;   /* arrow at p1 */
    IsoglDimensionArrow arrow_right;  /* arrow at p2 */
    int mark_points;        /* 1 = draw a 0.5 mm reference segment at p1/p2 */
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
