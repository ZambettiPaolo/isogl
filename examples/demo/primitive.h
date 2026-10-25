#ifndef PRIMITIVE_H
#define PRIMITIVE_H

#include "isogl_style.h"

/*
 * Demo-level geometry model.
 *
 * ISO line types, weights and text heights live in isogl_style.h: this header
 * only defines the drawing primitives used by the example application.
 */

typedef enum {
    LEFT_ALIGNEMENT,
    RIGTH_ALIGNEMENT,
    CENTERD
} TextAlignement;

typedef enum {
    PRIM_LINE,
    PRIM_BOX,
    PRIM_CIRCLE,
    PRIM_ARC,
    PRIM_RECTANGLE,
    PRIM_text
} PrimitiveType;

typedef struct {
    double x;
    double y;
} Point;

typedef struct {
    Point ori;
    double angle;
    Point ori_abs;
    double angle_abs;
    double cos_angle_abs;
    double sin_angle_abs;
} Origin;

typedef struct {
    Point left_bottom; // absolute
    Point right_top;   // absolute
} Box;

typedef struct {
    Point center;
    Point center_abs;
    double radius;
} Circle;

typedef struct {
    Point center;
    Point center_abs;
    Point p1;
    Point p1_abs;
    double angle;
} Arc;

typedef struct {
    Point start;
    Point end;
    Point start_abs;
    Point end_abs;
} Line;

typedef struct {
    Point start;
    Point end;
    double angle;
    Point start_abs;
    Point end_abs;
    double angle_abs;
} Rectangle;

typedef struct {
    Point start;
    double angle;
    Point start_abs;
    double angle_abs;
    char* s;
    isogl_text_height t_height;
    TextAlignement t_alignement;
} Text;

typedef struct Primitive Primitive;
struct Primitive {
    Primitive *next;
    PrimitiveType type;
    isogl_line_type pattern;
    isogl_line_weight thickness;
    int visible;
    union {
        Box box;
        Line line;
        Circle circle;
        Arc arc;
        Rectangle rectangle;
        Text text;
    };
};

#endif // PRIMITIVE_H
