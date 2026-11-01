#include "isogl_symbol_geometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SYM_DIM_ARROW_MM 3.5
#define SYM_DIM_EXT_MM 2.0
#define SYM_DIM_TEXT_GAP_MM 1.0
#define SYM_DIM_MAX_ARROW_SEGMENTS 6

typedef struct {
    double ax, ay;          /* dimension line A */
    double bx, by;          /* dimension line B */
    double e1x, e1y;        /* extension line 1 end */
    double e2x, e2y;        /* extension line 2 end */
    double arrow[SYM_DIM_MAX_ARROW_SEGMENTS][4];
    int arrow_count;
    double text_x, text_y;  /* centred baseline origin */
    double text_angle;
    double text_width;      /* model units */
    double text_height;     /* model units */
    double text_height_mm;  /* paper units */
    char text[160];
} SymDimLayout;

static int sym_finite(double v) {
    return isfinite(v) ? 1 : 0;
}

static double sym_scale(double s) {
    return (s > 0.0 && isfinite(s)) ? s : 1.0;
}

static void sym_obb_corners(double x, double y, double w, double h,
                            double angle, double corners[4][2]) {
    double c = cos(angle);
    double s = sin(angle);
    double lx[4] = {0.0, w, w, 0.0};
    double ly[4] = {0.0, 0.0, h, h};

    for (int i = 0; i < 4; ++i) {
        corners[i][0] = x + lx[i] * c - ly[i] * s;
        corners[i][1] = y + lx[i] * s + ly[i] * c;
    }
}

static int sym_arrow_segments(double tipx, double tipy,
                              double dirx, double diry,
                              double nx, double ny,
                              double length, IsoglDimensionArrow type,
                              double out[][4], int *count, int max) {
    if (!out || !count || max < 1) return -1;
    if (!(length > 0.0)) return -1;

    double ca = cos(M_PI / 6.0);
    double sa = sin(M_PI / 6.0);
    double seg[3][4];
    int n = 0;

    switch (type) {
    case ISOGL_DIM_ARROW_NONE:
        n = 0;
        break;

    case ISOGL_DIM_ARROW_OPEN: {
        seg[0][0] = tipx;
        seg[0][1] = tipy;
        seg[0][2] = tipx + length * (ca * dirx + sa * nx);
        seg[0][3] = tipy + length * (ca * diry + sa * ny);
        seg[1][0] = tipx;
        seg[1][1] = tipy;
        seg[1][2] = tipx + length * (ca * dirx - sa * nx);
        seg[1][3] = tipy + length * (ca * diry - sa * ny);
        n = 2;
        break;
    }

    case ISOGL_DIM_ARROW_CLOSED: {
        double x0 = tipx + length * (ca * dirx + sa * nx);
        double y0 = tipy + length * (ca * diry + sa * ny);
        double x1 = tipx + length * (ca * dirx - sa * nx);
        double y1 = tipy + length * (ca * diry - sa * ny);
        seg[0][0] = tipx;
        seg[0][1] = tipy;
        seg[0][2] = x0;
        seg[0][3] = y0;
        seg[1][0] = tipx;
        seg[1][1] = tipy;
        seg[1][2] = x1;
        seg[1][3] = y1;
        seg[2][0] = x0;
        seg[2][1] = y0;
        seg[2][2] = x1;
        seg[2][3] = y1;
        n = 3;
        break;
    }

    case ISOGL_DIM_ARROW_TICK: {
        double dx = dirx + nx;
        double dy = diry + ny;
        double d = hypot(dx, dy);
        if (!(d > 0.0)) return -1;
        dx /= d;
        dy /= d;
        double half = 0.5 * length;
        seg[0][0] = tipx - half * dx;
        seg[0][1] = tipy - half * dy;
        seg[0][2] = tipx + half * dx;
        seg[0][3] = tipy + half * dy;
        n = 1;
        break;
    }

    case ISOGL_DIM_ARROW_DOT: {
        double ax = dirx + nx;
        double ay = diry + ny;
        double bx = dirx - nx;
        double by = diry - ny;
        double da = hypot(ax, ay);
        double db = hypot(bx, by);
        if (!(da > 0.0) || !(db > 0.0)) return -1;
        ax /= da;
        ay /= da;
        bx /= db;
        by /= db;
        double half = 0.25 * length;
        seg[0][0] = tipx - half * ax;
        seg[0][1] = tipy - half * ay;
        seg[0][2] = tipx + half * ax;
        seg[0][3] = tipy + half * ay;
        seg[1][0] = tipx - half * bx;
        seg[1][1] = tipy - half * by;
        seg[1][2] = tipx + half * bx;
        seg[1][3] = tipy + half * by;
        n = 2;
        break;
    }

    default:
        return -1;
    }

    if (n > max) return -1;
    for (int i = 0; i < n; ++i) {
        memcpy(out[i], seg[i], sizeof(seg[i]));
    }
    *count = n;
    return 0;
}

static int sym_dimension_layout(const IsoglDimensionDesc *d,
                                const IsoglSymMetrics *metrics,
                                SymDimLayout *g) {
    if (!d || !g) return -1;
    if (!sym_finite(d->x0) || !sym_finite(d->y0) ||
        !sym_finite(d->x1) || !sym_finite(d->y1) ||
        !sym_finite(d->px) || !sym_finite(d->py) ||
        !sym_finite(d->distance) || !sym_finite(d->text_offset_mm)) {
        return -1;
    }
    if (d->arrow_left < ISOGL_DIM_ARROW_NONE ||
        d->arrow_left > ISOGL_DIM_ARROW_DOT ||
        d->arrow_right < ISOGL_DIM_ARROW_NONE ||
        d->arrow_right > ISOGL_DIM_ARROW_DOT) {
        return -1;
    }

    double dx = d->x1 - d->x0;
    double dy = d->y1 - d->y0;
    double len = hypot(dx, dy);
    if (!(len > 0.0) || !sym_finite(len)) return -1;

    double ux = dx / len;
    double uy = dy / len;
    double nx = -uy;
    double ny = ux;

    /* p3 chooses the side; distance overrides the offset magnitude. */
    double side_proj = (d->px - d->x0) * nx + (d->py - d->y0) * ny;
    double sign = side_proj >= 0.0 ? 1.0 : -1.0;
    double lateral = d->distance != 0.0 ? fabs(d->distance)
                                         : fabs(side_proj);
    if (!sym_finite(lateral)) return -1;

    double ax = d->x0 + sign * lateral * nx;
    double ay = d->y0 + sign * lateral * ny;
    double bx = d->x1 + sign * lateral * nx;
    double by = d->y1 + sign * lateral * ny;

    double scale = sym_scale(d->sheet_scale);
    double overshoot = SYM_DIM_EXT_MM / scale;
    double arrow = SYM_DIM_ARROW_MM / scale;
    double max_arrow = len * 0.45;
    if (arrow > max_arrow) arrow = max_arrow;
    double text_gap = SYM_DIM_TEXT_GAP_MM / scale;
    double text_height_mm = d->text_height_mm > 0.0 ? d->text_height_mm : 3.5;
    if (!sym_finite(text_height_mm)) return -1;

    g->ax = ax;
    g->ay = ay;
    g->bx = bx;
    g->by = by;
    g->e1x = ax + sign * overshoot * nx;
    g->e1y = ay + sign * overshoot * ny;
    g->e2x = bx + sign * overshoot * nx;
    g->e2y = by + sign * overshoot * ny;

    int left_count = 0;
    int right_count = 0;
    if (sym_arrow_segments(ax, ay, ux, uy, nx, ny, arrow,
                           d->arrow_left, g->arrow, &left_count, 3) != 0) {
        return -1;
    }
    if (sym_arrow_segments(bx, by, -ux, -uy, nx, ny, arrow,
                           d->arrow_right, g->arrow + left_count,
                           &right_count, 3) != 0) {
        return -1;
    }
    g->arrow_count = left_count + right_count;

    int precision = d->precision;
    if (precision < 0) precision = 0;
    if (precision > 6) precision = 6;
    snprintf(g->text, sizeof(g->text), "%.31s%.*f%.31s",
             d->text_left, precision, len, d->text_right);

    g->text_height_mm = text_height_mm;
    g->text_height = text_height_mm / scale;
    double width = 0.0;
    if (metrics && metrics->text_width) {
        width = metrics->text_width(metrics->user, g->text, text_height_mm);
    }
    if (!sym_finite(width) || width < 0.0) width = 0.0;
    g->text_width = width;

    double mx = 0.5 * (ax + bx);
    double my = 0.5 * (ay + by);
    double angle = atan2(uy, ux);
    if (angle > M_PI / 2.0) angle -= M_PI;
    if (angle < -M_PI / 2.0) angle += M_PI;
    g->text_angle = angle;

    double tx = cos(angle);
    double ty = sin(angle);
    double tnx = -ty;
    double tny = tx;
    double text_offset = d->text_offset_mm / scale;
    g->text_x = mx + text_offset * tx - 0.5 * g->text_width * tx
                + text_gap * tnx;
    g->text_y = my + text_offset * ty - 0.5 * g->text_width * ty
                + text_gap * tny;
    return 0;
}

static void sym_fill_style(IsoglSymItem *item, uint8_t pattern,
                           uint8_t weight, uint8_t layer,
                           const float color[4]) {
    item->pattern = pattern;
    item->weight = weight;
    item->layer = layer;
    if (color) {
        memcpy(item->color, color, sizeof(item->color));
    }
}

static int sym_emit_segment(IsoglSymEmitter *out,
                            uint8_t pattern, uint8_t weight, uint8_t layer,
                            const float color[4],
                            double x0, double y0, double x1, double y1) {
    IsoglSymItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_SEGMENT;
    item.x0 = x0;
    item.y0 = y0;
    item.x1 = x1;
    item.y1 = y1;
    sym_fill_style(&item, pattern, weight, layer, color);
    return out->emit(out->user, &item);
}

static int sym_emit_text(IsoglSymEmitter *out,
                         uint8_t pattern, uint8_t weight, uint8_t layer,
                         const float color[4], const SymDimLayout *g) {
    IsoglSymItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_TEXT;
    item.utf8 = g->text;
    item.x = g->text_x;
    item.y = g->text_y;
    item.height_mm = g->text_height_mm;
    item.angle = g->text_angle;
    item.align = 0;
    sym_fill_style(&item, pattern, weight, layer, color);
    return out->emit(out->user, &item);
}

static int sym_emit_pick_segment(IsoglSymPickEmitter *out,
                                 double x0, double y0,
                                 double x1, double y1) {
    IsoglSymPickItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_PICK_SEGMENT;
    item.x0 = x0;
    item.y0 = y0;
    item.x1 = x1;
    item.y1 = y1;
    return out->emit(out->user, &item);
}

static int sym_emit_pick_obb(IsoglSymPickEmitter *out,
                             double x, double y, double w, double h,
                             double angle) {
    IsoglSymPickItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_PICK_OBB;
    sym_obb_corners(x, y, w, h, angle, item.corners);
    return out->emit(out->user, &item);
}

int isogl_dimension_geometry(const IsoglDimensionDesc *d,
                             const IsoglSymMetrics *metrics,
                             IsoglSymEmitter *out) {
    if (!d || !out || !out->emit) return -1;

    SymDimLayout g;
    if (sym_dimension_layout(d, metrics, &g) != 0) return -1;

    if (sym_emit_segment(out, d->pattern, d->weight, d->layer, d->color,
                         g.ax, g.ay, g.bx, g.by) != 0) return -1;
    if (sym_emit_segment(out, d->pattern, d->weight, d->layer, d->color,
                         d->x0, d->y0, g.e1x, g.e1y) != 0) return -1;
    if (sym_emit_segment(out, d->pattern, d->weight, d->layer, d->color,
                         d->x1, d->y1, g.e2x, g.e2y) != 0) return -1;
    for (int i = 0; i < g.arrow_count; ++i) {
        if (sym_emit_segment(out, d->pattern, d->weight, d->layer, d->color,
                             g.arrow[i][0], g.arrow[i][1],
                             g.arrow[i][2], g.arrow[i][3]) != 0) {
            return -1;
        }
    }
    if (sym_emit_text(out, d->pattern, d->weight, d->layer, d->color, &g) != 0) {
        return -1;
    }
    return 0;
}

int isogl_dimension_pick_geometry(const IsoglDimensionDesc *d,
                                  const IsoglSymMetrics *metrics,
                                  IsoglSymPickEmitter *out) {
    if (!d || !out || !out->emit) return -1;

    SymDimLayout g;
    if (sym_dimension_layout(d, metrics, &g) != 0) return -1;

    if (sym_emit_pick_segment(out, g.ax, g.ay, g.bx, g.by) != 0) return -1;
    if (sym_emit_pick_segment(out, d->x0, d->y0, g.e1x, g.e1y) != 0) return -1;
    if (sym_emit_pick_segment(out, d->x1, d->y1, g.e2x, g.e2y) != 0) return -1;
    for (int i = 0; i < g.arrow_count; ++i) {
        if (sym_emit_pick_segment(out, g.arrow[i][0], g.arrow[i][1],
                                  g.arrow[i][2], g.arrow[i][3]) != 0) {
            return -1;
        }
    }

    if (d->pick_text) {
        if (sym_emit_pick_obb(out, g.text_x, g.text_y,
                              g.text_width, g.text_height,
                              g.text_angle) != 0) {
            return -1;
        }
    }
    return 0;
}

static int sym_center_mark_layout(const IsoglCenterMarkDesc *d,
                                  double *cx, double *cy, double *size) {
    if (!d || !cx || !cy || !size) return -1;
    if (!sym_finite(d->cx) || !sym_finite(d->cy) ||
        !sym_finite(d->size_mm)) {
        return -1;
    }

    double scale = sym_scale(d->sheet_scale);
    double model_size = d->size_mm / scale;
    if (!(model_size > 0.0) || !sym_finite(model_size)) return -1;

    *cx = d->cx;
    *cy = d->cy;
    *size = model_size;
    return 0;
}

int isogl_center_mark_geometry(const IsoglCenterMarkDesc *d,
                               IsoglSymEmitter *out) {
    if (!d || !out || !out->emit) return -1;

    double cx = 0.0, cy = 0.0, size = 0.0;
    if (sym_center_mark_layout(d, &cx, &cy, &size) != 0) return -1;

    if (sym_emit_segment(out, d->pattern, d->weight, d->layer, d->color,
                         cx - size, cy, cx + size, cy) != 0) return -1;
    if (sym_emit_segment(out, d->pattern, d->weight, d->layer, d->color,
                         cx, cy - size, cx, cy + size) != 0) return -1;
    return 0;
}

int isogl_center_mark_pick_geometry(const IsoglCenterMarkDesc *d,
                                    IsoglSymPickEmitter *out) {
    if (!d || !out || !out->emit) return -1;

    double cx = 0.0, cy = 0.0, size = 0.0;
    if (sym_center_mark_layout(d, &cx, &cy, &size) != 0) return -1;

    if (sym_emit_pick_segment(out, cx - size, cy, cx + size, cy) != 0) return -1;
    if (sym_emit_pick_segment(out, cx, cy - size, cx, cy + size) != 0) return -1;
    return 0;
}
