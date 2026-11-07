#include "drawing_load.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toml.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DRAW_MAX_GROUPS     128
#define DRAW_MAX_PRIMITIVES 4096
#define DRAW_CIRCLE_STEPS   48
#define DRAW_SHEET_MARGIN   10.0f

typedef enum {
    DRAW_LINE = 0,
    DRAW_RECT,
    DRAW_CIRCLE,
    DRAW_ARROW,
    DRAW_TEXT
} DrawPrimType;

typedef struct {
    int id;
    int parent;
    double x, y;
    double angle_deg;
    double scale;
} DrawGroup;

typedef struct {
    double ox, oy, angle, scale;
} DrawXform;

typedef struct {
    int group;
    DrawPrimType type;
    double p[4];
    char text[256];
    double height_mm;
    double angle_deg;
    float head;
    uint8_t pattern, weight, layer;
    float color[4];
} DrawPrimitive;

static double draw_double(toml_table_t *t, const char *key, double def) {
    toml_datum_t d = toml_double_in(t, key);
    return d.ok ? d.u.d : def;
}

static int draw_int(toml_table_t *t, const char *key, int def) {
    toml_datum_t d = toml_int_in(t, key);
    return d.ok ? (int)d.u.i : def;
}

static void draw_string(toml_table_t *t, const char *key, char *out,
                        size_t size, const char *def) {
    toml_datum_t d = toml_string_in(t, key);
    if (d.ok) {
        snprintf(out, size, "%s", d.u.s);
        free(d.u.s);
    } else {
        snprintf(out, size, "%s", def);
    }
}

static void draw_color(toml_table_t *t, float color[4]) {
    color[0] = color[1] = color[2] = color[3] = 1.0f;
    toml_array_t *array = toml_array_in(t, "color");
    if (!array) return;
    int n = toml_array_nelem(array);
    for (int i = 0; i < 4 && i < n; ++i) {
        toml_datum_t d = toml_double_at(array, i);
        if (d.ok) color[i] = (float)d.u.d;
    }
}

static void draw_style_fields(toml_table_t *t, uint8_t *pattern,
                              uint8_t *weight, uint8_t *layer,
                              float color[4]) {
    int p = draw_int(t, "pattern", 0);
    int w = draw_int(t, "weight", 3);
    int l = draw_int(t, "layer", 0);
    if (p < 0 || p >= LINE_TYPE_COUNT) p = 0;
    if (w < 0 || w >= LINE_THICKNESS_COUNT) w = 3;
    if (l < 0 || l > 31) l = 0;
    *pattern = (uint8_t)p;
    *weight = (uint8_t)w;
    *layer = (uint8_t)l;
    draw_color(t, color);
}

static void draw_style(toml_table_t *t, DrawPrimitive *p) {
    draw_style_fields(t, &p->pattern, &p->weight, &p->layer, p->color);
}

static int draw_group_index(const DrawGroup *groups, int count, int id) {
    for (int i = 0; i < count; ++i) {
        if (groups[i].id == id) return i;
    }
    return -1;
}

static int draw_group_xform_rec(const DrawGroup *groups, int count, int gid,
                                int depth, DrawXform *out) {
    if (!out || depth > DRAW_MAX_GROUPS) return -1;

    int index = draw_group_index(groups, count, gid);
    if (index < 0) return -1;

    const DrawGroup *g = &groups[index];
    double local_scale = g->scale > 0.0 ? g->scale : 1.0;
    double angle = g->angle_deg * M_PI / 180.0;

    if (g->parent < 0) {
        out->ox = g->x;
        out->oy = g->y;
        out->angle = angle;
        out->scale = local_scale;
        return 0;
    }

    DrawXform parent;
    if (draw_group_xform_rec(groups, count, g->parent, depth + 1, &parent) != 0) {
        return -1;
    }

    double cs = cos(parent.angle);
    double sn = sin(parent.angle);
    double lx = parent.scale * g->x;
    double ly = parent.scale * g->y;

    out->ox = parent.ox + lx * cs - ly * sn;
    out->oy = parent.oy + lx * sn + ly * cs;
    out->angle = parent.angle + angle;
    out->scale = parent.scale * local_scale;
    return 0;
}

static void draw_xform_point(const DrawXform *xf, double x, double y,
                             double *ax, double *ay) {
    double c = cos(xf->angle);
    double s = sin(xf->angle);
    *ax = xf->ox + xf->scale * (x * c - y * s);
    *ay = xf->oy + xf->scale * (x * s + y * c);
}

static void draw_bounds_add(DrawingBounds *bounds, double x, double y) {
    if (!bounds->has_bounds) {
        bounds->xmin = bounds->xmax = (float)x;
        bounds->ymin = bounds->ymax = (float)y;
        bounds->has_bounds = 1;
        return;
    }
    if (x < bounds->xmin) bounds->xmin = (float)x;
    if (x > bounds->xmax) bounds->xmax = (float)x;
    if (y < bounds->ymin) bounds->ymin = (float)y;
    if (y > bounds->ymax) bounds->ymax = (float)y;
}

static isogl_vertex draw_vertex(const DrawPrimitive *p, double x, double y,
                                float distance) {
    isogl_vertex v;
    memset(&v, 0, sizeof(v));
    v.position[0] = (float)x;
    v.position[1] = (float)y;
    memcpy(v.color, p->color, sizeof(v.color));
    v.distanceFromStart = distance;
    v.pk_thickness_pattern_layer =
        isogl_pack(p->weight, p->pattern, p->layer);
    return v;
}

static int draw_push_segment(isogl_segment_list *segments,
                             const DrawPrimitive *p,
                             double x0, double y0, double x1, double y1) {
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float length = sqrtf(dx * dx + dy * dy);
    isogl_vertex a = draw_vertex(p, x0, y0, 0.0f);
    isogl_vertex b = draw_vertex(p, x1, y1, length);
    return isogl_segment_list_push_pair(segments, a, b);
}

static int draw_arrow_head(isogl_segment_list *segments,
                           const DrawPrimitive *p,
                           double tip_x, double tip_y,
                           double dir_x, double dir_y,
                           double head) {
    double norm = sqrt(dir_x * dir_x + dir_y * dir_y);
    if (norm < 1e-9 || head <= 0.0) return 0;
    dir_x /= norm;
    dir_y /= norm;

    const double angle = 0.42;
    double ca = cos(angle);
    double sa = sin(angle);
    double back_x = -dir_x;
    double back_y = -dir_y;

    double left_x = back_x * ca - back_y * sa;
    double left_y = back_x * sa + back_y * ca;
    double right_x = back_x * ca + back_y * sa;
    double right_y = -back_x * sa + back_y * ca;

    int ok = 1;
    if (draw_push_segment(segments, p, tip_x, tip_y,
                          tip_x + left_x * head,
                          tip_y + left_y * head) != 0) ok = 0;
    if (draw_push_segment(segments, p, tip_x, tip_y,
                          tip_x + right_x * head,
                          tip_y + right_y * head) != 0) ok = 0;
    return ok ? 0 : -1;
}

static void draw_text_bounds(DrawingBounds *bounds, const DrawPrimitive *p,
                             double x, double y, double angle, double scale,
                             const isogl_font_data *font) {
    double width = 0.0;
    if (font && font->height > 0.0 && p->text[0]) {
        width = isogl_font_text_advance(font, p->text) *
                p->height_mm / font->height;
    }
    double height = p->height_mm;
    double corners[4][2] = {
        {0.0, -height},
        {width, -height},
        {width, height * 0.25},
        {0.0, height * 0.25}
    };
    double c = cos(angle);
    double s = sin(angle);
    for (int i = 0; i < 4; ++i) {
        double lx = corners[i][0] * scale;
        double ly = corners[i][1] * scale;
        draw_bounds_add(bounds, x + lx * c - ly * s,
                        y + lx * s + ly * c);
    }
}

static int draw_emit_primitive(isogl_segment_list *segments,
                               isogl_string_list *strings,
                               isogl_font_data *font, float global_scale,
                               const DrawPrimitive *p,
                               const DrawXform *xf,
                               DrawingBounds *bounds) {
    double x0, y0, x1, y1;

    switch (p->type) {
        case DRAW_LINE:
            draw_xform_point(xf, p->p[0], p->p[1], &x0, &y0);
            draw_xform_point(xf, p->p[2], p->p[3], &x1, &y1);
            draw_bounds_add(bounds, x0, y0);
            draw_bounds_add(bounds, x1, y1);
            return draw_push_segment(segments, p, x0, y0, x1, y1);

        case DRAW_RECT: {
            double xs[4], ys[4];
            draw_xform_point(xf, p->p[0], p->p[1], &xs[0], &ys[0]);
            draw_xform_point(xf, p->p[2], p->p[1], &xs[1], &ys[1]);
            draw_xform_point(xf, p->p[2], p->p[3], &xs[2], &ys[2]);
            draw_xform_point(xf, p->p[0], p->p[3], &xs[3], &ys[3]);
            int ok = 1;
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                draw_bounds_add(bounds, xs[i], ys[i]);
                if (draw_push_segment(segments, p, xs[i], ys[i],
                                      xs[j], ys[j]) != 0) ok = 0;
            }
            return ok ? 0 : -1;
        }

        case DRAW_CIRCLE: {
            double cx, cy;
            draw_xform_point(xf, p->p[0], p->p[1], &cx, &cy);
            double r = p->p[2] * fabs(xf->scale);
            draw_bounds_add(bounds, cx - r, cy - r);
            draw_bounds_add(bounds, cx + r, cy + r);
            double px = cx + r;
            double py = cy;
            int ok = 1;
            for (int i = 1; i <= DRAW_CIRCLE_STEPS; ++i) {
                double a = 2.0 * M_PI * i / DRAW_CIRCLE_STEPS;
                double nx = cx + r * cos(a);
                double ny = cy + r * sin(a);
                if (draw_push_segment(segments, p, px, py, nx, ny) != 0) {
                    ok = 0;
                }
                px = nx;
                py = ny;
            }
            return ok ? 0 : -1;
        }

        case DRAW_ARROW: {
            draw_xform_point(xf, p->p[0], p->p[1], &x0, &y0);
            draw_xform_point(xf, p->p[2], p->p[3], &x1, &y1);
            draw_bounds_add(bounds, x0, y0);
            draw_bounds_add(bounds, x1, y1);
            int ok = 1;
            if (draw_push_segment(segments, p, x0, y0, x1, y1) != 0) ok = 0;
            double head = p->head > 0.0 ? p->head : 3.5;
            if (draw_arrow_head(segments, p, x1, y1, x1 - x0, y1 - y0,
                                head) != 0) ok = 0;
            return ok ? 0 : -1;
        }

        case DRAW_TEXT: {
            double x, y;
            draw_xform_point(xf, p->p[0], p->p[1], &x, &y);
            double angle = xf->angle + p->angle_deg * M_PI / 180.0;
            draw_text_bounds(bounds, p, x, y, angle, xf->scale, font);

            if (!font || p->text[0] == '\0') return 0;

            isogl_glyph_string *string =
                isogl_create_glyph_string(p->text, font);
            if (!string) return -1;

            isogl_glyph_string_set_color(string, p->color);
            if (!isogl_string_list_append(strings, string, (float)x, (float)y,
                                          (float)p->height_mm, global_scale,
                                          (float)angle, font, 0)) {
                isogl_free_glyph_string(string);
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

/* ---- symbols ----------------------------------------------------------- */

typedef struct {
    const isogl_font_data *font;
    double sheet_scale;
} DrawTextMetrics;

static double draw_symbol_text_width(void *user, const char *utf8,
                                     double height_mm) {
    const DrawTextMetrics *metrics = user;
    if (!metrics || !metrics->font || !utf8 || height_mm <= 0.0) return 0.0;

    double font_height = (double)metrics->font->height;
    if (font_height <= 0.0) return 0.0;

    double scale = metrics->sheet_scale > 0.0 ? metrics->sheet_scale : 1.0;
    return isogl_font_text_advance(metrics->font, utf8) * height_mm /
           (font_height * scale);
}

typedef struct {
    DrawingBounds *bounds;
} DrawSymbolBoundsCtx;

static int draw_symbol_pick_emit(void *user, const IsoglSymPickItem *item) {
    DrawSymbolBoundsCtx *ctx = user;
    if (!ctx || !ctx->bounds || !item) return -1;

    if (item->kind == ISOGL_SYM_PICK_SEGMENT) {
        draw_bounds_add(ctx->bounds, item->x0, item->y0);
        draw_bounds_add(ctx->bounds, item->x1, item->y1);
    } else {
        for (int i = 0; i < 4; ++i) {
            draw_bounds_add(ctx->bounds, item->corners[i][0],
                            item->corners[i][1]);
        }
    }
    return 0;
}

static int draw_symbol_bounds(const DrawingSymbolSpec *spec,
                              const isogl_font_data *font,
                              DrawingBounds *bounds) {
    DrawSymbolBoundsCtx ctx = {bounds};
    IsoglSymPickEmitter emitter = {&ctx, draw_symbol_pick_emit};

    if (spec->type == DRAW_SYMBOL_DIMENSION) {
        IsoglDimensionDesc d = spec->dimension;
        d.pick_text = 1;   /* include the text OBB in the drawing bounds */
        DrawTextMetrics metrics = {font, d.sheet_scale};
        IsoglSymMetrics sym_metrics = {&metrics, draw_symbol_text_width};
        return isogl_dimension_pick_geometry(&d, &sym_metrics, &emitter);
    }

    return isogl_center_mark_pick_geometry(&spec->center_mark, &emitter);
}

static int draw_parse_symbol(toml_table_t *t, const DrawGroup *groups,
                             int group_count, double default_sheet_scale,
                             DrawingSymbolSpec *out) {
    if (!t || !out) return -1;

    memset(out, 0, sizeof(*out));
    out->group = draw_int(t, "group", 0);

    DrawXform xf;
    if (draw_group_xform_rec(groups, group_count, out->group, 0, &xf) != 0) {
        return -1;
    }

    char type[32];
    draw_string(t, "type", type, sizeof(type), "dimension");

    uint8_t pattern = 0;
    uint8_t weight = 1;
    uint8_t layer = 0;
    float color[4] = {1, 1, 1, 1};
    draw_style_fields(t, &pattern, &weight, &layer, color);

    if (strcmp(type, "dimension") == 0) {
        out->type = DRAW_SYMBOL_DIMENSION;
        IsoglDimensionDesc *d = &out->dimension;

        double x0 = draw_double(t, "p1x", 0.0);
        double y0 = draw_double(t, "p1y", 0.0);
        double x1 = draw_double(t, "p2x", 0.0);
        double y1 = draw_double(t, "p2y", 0.0);
        double px = draw_double(t, "p3x", 0.0);
        double py = draw_double(t, "p3y", 0.0);
        draw_xform_point(&xf, x0, y0, &d->x0, &d->y0);
        draw_xform_point(&xf, x1, y1, &d->x1, &d->y1);
        draw_xform_point(&xf, px, py, &d->px, &d->py);

        d->distance = draw_double(t, "distance", 0.0);
        d->text_offset_mm = draw_double(t, "text_offset_mm", 0.0);
        d->text_height_mm = draw_double(t, "text_height_mm", 3.5);
        d->sheet_scale = draw_double(t, "sheet_scale", default_sheet_scale);
        if (d->sheet_scale <= 0.0) d->sheet_scale = 1.0;
        draw_string(t, "text_left", d->text_left, sizeof(d->text_left), "");
        draw_string(t, "text_right", d->text_right, sizeof(d->text_right), "");
        d->precision = draw_int(t, "precision", 1);
        d->pick_text = draw_int(t, "pick_text", 1);
        int arrow_left = draw_int(t, "arrow_left", ISOGL_DIM_ARROW_OPEN);
        int arrow_right = draw_int(t, "arrow_right", ISOGL_DIM_ARROW_OPEN);
        if (arrow_left < 0 || arrow_left > ISOGL_DIM_ARROW_CLOSED_OUTSIDE) {
            arrow_left = ISOGL_DIM_ARROW_OPEN;
        }
        if (arrow_right < 0 || arrow_right > ISOGL_DIM_ARROW_CLOSED_OUTSIDE) {
            arrow_right = ISOGL_DIM_ARROW_OPEN;
        }
        d->arrow_left = (IsoglDimensionArrow)arrow_left;
        d->arrow_right = (IsoglDimensionArrow)arrow_right;
        d->mark_points = draw_int(t, "mark_points", 0);
        d->pattern = pattern;
        d->weight = weight;
        d->layer = layer;
        memcpy(d->color, color, sizeof(d->color));
        return 0;
    }

    if (strcmp(type, "center_mark") == 0 || strcmp(type, "center") == 0) {
        out->type = DRAW_SYMBOL_CENTER_MARK;
        IsoglCenterMarkDesc *d = &out->center_mark;

        double cx = draw_double(t, "cx", 0.0);
        double cy = draw_double(t, "cy", 0.0);
        draw_xform_point(&xf, cx, cy, &d->cx, &d->cy);

        d->size_mm = draw_double(t, "size_mm", 8.0);
        d->sheet_scale = draw_double(t, "sheet_scale", default_sheet_scale);
        if (d->sheet_scale <= 0.0) d->sheet_scale = 1.0;
        d->pattern = pattern;
        d->weight = weight;
        d->layer = layer;
        memcpy(d->color, color, sizeof(d->color));
        return 0;
    }

    return -1;
}

static int draw_sheet_format(const char *name) {
    if (strcmp(name, "A4") == 0) return ISOGL_SHEET_A4;
    if (strcmp(name, "A3") == 0) return ISOGL_SHEET_A3;
    if (strcmp(name, "A2") == 0) return ISOGL_SHEET_A2;
    if (strcmp(name, "A1") == 0) return ISOGL_SHEET_A1;
    if (strcmp(name, "A0") == 0) return ISOGL_SHEET_A0;
    return ISOGL_SHEET_A3;
}

static int draw_sheet_orientation(const char *name) {
    if (strcmp(name, "portrait") == 0) return ISOGL_ORIENTATION_PORTRAIT;
    return ISOGL_ORIENTATION_LANDSCAPE;
}

static int draw_next_sheet(int format) {
    switch (format) {
        case ISOGL_SHEET_A4: return ISOGL_SHEET_A3;
        case ISOGL_SHEET_A3: return ISOGL_SHEET_A2;
        case ISOGL_SHEET_A2: return ISOGL_SHEET_A1;
        case ISOGL_SHEET_A1: return ISOGL_SHEET_A0;
        default: return ISOGL_SHEET_A0;
    }
}

static int draw_sheet_fits(int format, int orientation,
                           const DrawingBounds *bounds) {
    if (!bounds || !bounds->has_bounds) return 1;

    float width = 0.0f, height = 0.0f;
    isogl_sheet_size((isogl_sheet_format)format,
                     (isogl_sheet_orientation)orientation,
                     &width, &height);
    float bw = bounds->xmax - bounds->xmin;
    float bh = bounds->ymax - bounds->ymin;
    return bw <= width - 2.0f * DRAW_SHEET_MARGIN &&
           bh <= height - 2.0f * DRAW_SHEET_MARGIN;
}

int drawing_load(const char *path, isogl_font_data *font,
                 isogl_segment_list *segments,
                 isogl_string_list *strings,
                 DrawingSheetInfo *sheet,
                 DrawingSymbols *symbols) {
    if (!path || !segments || !strings || !sheet) return -1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "drawing_load: cannot open %s\n", path);
        return -1;
    }

    char errbuf[256];
    toml_table_t *doc = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!doc) {
        fprintf(stderr, "drawing_load: TOML parse error: %s\n", errbuf);
        return -1;
    }

    memset(sheet, 0, sizeof(*sheet));
    sheet->format = ISOGL_SHEET_A3;
    sheet->orientation = ISOGL_ORIENTATION_LANDSCAPE;
    sheet->scale_num = 1;
    sheet->scale_den = 1;

    toml_table_t *sheet_table = toml_table_in(doc, "sheet");
    if (sheet_table) {
        char value[32];
        draw_string(sheet_table, "format", value, sizeof(value), "A3");
        sheet->format = draw_sheet_format(value);
        draw_string(sheet_table, "orientation", value, sizeof(value),
                    "landscape");
        sheet->orientation = draw_sheet_orientation(value);
        int num = draw_int(sheet_table, "scale_num", 1);
        int den = draw_int(sheet_table, "scale_den", 1);
        if (num > 0 && den > 0) {
            sheet->scale_num = num;
            sheet->scale_den = den;
        }
    }

    DrawGroup groups[DRAW_MAX_GROUPS];
    int group_count = 0;
    toml_array_t *group_array = toml_array_in(doc, "groups");    if (!group_array) {
        toml_free(doc);
        fprintf(stderr, "drawing_load: missing [[groups]]\n");
        return -1;
    }
    int n = toml_array_nelem(group_array);
    for (int i = 0; i < n; ++i) {
        if (group_count >= DRAW_MAX_GROUPS) {
            toml_free(doc);
            fprintf(stderr, "drawing_load: too many groups\n");
            return -1;
        }
        toml_table_t *t = toml_table_at(group_array, i);
        if (!t) continue;

        DrawGroup *g = &groups[group_count++];
        memset(g, 0, sizeof(*g));
        g->id = draw_int(t, "id", i);
        g->parent = draw_int(t, "parent", -1);
        g->x = draw_double(t, "x", 0.0);
        g->y = draw_double(t, "y", 0.0);
        g->angle_deg = draw_double(t, "angle_deg", 0.0);
        g->scale = draw_double(t, "scale", 1.0);
    }

    if (group_count == 0) {
        toml_free(doc);
        fprintf(stderr, "drawing_load: no groups\n");
        return -1;
    }

    DrawPrimitive primitives[DRAW_MAX_PRIMITIVES];
    int primitive_count = 0;
    toml_array_t *prim_array = toml_array_in(doc, "primitives");
    if (prim_array) {
        int count = toml_array_nelem(prim_array);
        for (int i = 0; i < count; ++i) {
            if (primitive_count >= DRAW_MAX_PRIMITIVES) {
                toml_free(doc);
                fprintf(stderr, "drawing_load: too many primitives\n");
                return -1;
            }
            toml_table_t *t = toml_table_at(prim_array, i);
            if (!t) continue;

            DrawPrimitive *p = &primitives[primitive_count++];
            memset(p, 0, sizeof(*p));
            p->group = draw_int(t, "group", 0);
            p->head = (float)draw_double(t, "head", 3.5);
            draw_style(t, p);

            char type[32];
            draw_string(t, "type", type, sizeof(type), "line");
            if (strcmp(type, "line") == 0) {
                p->type = DRAW_LINE;
                p->p[0] = draw_double(t, "x0", 0.0);
                p->p[1] = draw_double(t, "y0", 0.0);
                p->p[2] = draw_double(t, "x1", 0.0);
                p->p[3] = draw_double(t, "y1", 0.0);
            } else if (strcmp(type, "rect") == 0 ||
                       strcmp(type, "rectangle") == 0) {
                p->type = DRAW_RECT;
                p->p[0] = draw_double(t, "x0", 0.0);
                p->p[1] = draw_double(t, "y0", 0.0);
                p->p[2] = draw_double(t, "x1", 0.0);
                p->p[3] = draw_double(t, "y1", 0.0);
            } else if (strcmp(type, "circle") == 0) {
                p->type = DRAW_CIRCLE;
                p->p[0] = draw_double(t, "cx", 0.0);
                p->p[1] = draw_double(t, "cy", 0.0);
                p->p[2] = draw_double(t, "r", 1.0);
            } else if (strcmp(type, "arrow") == 0) {
                p->type = DRAW_ARROW;
                p->p[0] = draw_double(t, "x0", 0.0);
                p->p[1] = draw_double(t, "y0", 0.0);
                p->p[2] = draw_double(t, "x1", 0.0);
                p->p[3] = draw_double(t, "y1", 0.0);
            } else if (strcmp(type, "text") == 0) {
                p->type = DRAW_TEXT;
                p->p[0] = draw_double(t, "x", 0.0);
                p->p[1] = draw_double(t, "y", 0.0);
                p->height_mm = draw_double(t, "height_mm", 5.0);
                p->angle_deg = draw_double(t, "angle_deg", 0.0);
                draw_string(t, "text", p->text, sizeof(p->text), "");
            } else {
                primitive_count--;
            }
        }
    }

    float global_scale = (float)sheet->scale_num / (float)sheet->scale_den;

    DrawingSymbols parsed_symbols;
    memset(&parsed_symbols, 0, sizeof(parsed_symbols));

    toml_array_t *symbol_array = toml_array_in(doc, "symbols");
    if (symbol_array) {
        int count = toml_array_nelem(symbol_array);
        for (int i = 0; i < count; ++i) {
            if (parsed_symbols.count >= DRAW_MAX_SYMBOLS) {
                toml_free(doc);
                fprintf(stderr, "drawing_load: too many symbols\n");
                return -1;
            }
            toml_table_t *t = toml_table_at(symbol_array, i);
            if (!t) continue;

            DrawingSymbolSpec *spec =
                &parsed_symbols.items[parsed_symbols.count];
            if (draw_parse_symbol(t, groups, group_count, global_scale,
                                  spec) != 0) {
                toml_free(doc);
                fprintf(stderr, "drawing_load: invalid symbol %d\n", i);
                return -1;
            }
            parsed_symbols.count++;
        }
    }

    DrawingBounds bounds = {0, 0, 0, 0, 0};

    for (int i = 0; i < primitive_count; ++i) {
        DrawPrimitive *p = &primitives[i];
        DrawXform xf = {0.0, 0.0, 0.0, 1.0};
        if (draw_group_xform_rec(groups, group_count, p->group, 0, &xf) != 0) {
            toml_free(doc);
            fprintf(stderr, "drawing_load: primitive %d has an invalid group\n",
                    i);
            return -1;
        }
        if (draw_emit_primitive(segments, strings, font, global_scale,
                                p, &xf, &bounds) != 0) {
            toml_free(doc);
            fprintf(stderr, "drawing_load: failed to emit primitive %d\n", i);
            return -1;
        }
    }

    for (size_t i = 0; i < parsed_symbols.count; ++i) {
        if (draw_symbol_bounds(&parsed_symbols.items[i], font, &bounds) != 0) {
            toml_free(doc);
            fprintf(stderr, "drawing_load: failed to bound symbol %zu\n", i);
            return -1;
        }
    }

    if (symbols) *symbols = parsed_symbols;

    toml_free(doc);

    int chosen = sheet->format;
    int promoted = 0;
    while (!draw_sheet_fits(chosen, sheet->orientation, &bounds) &&
           chosen != ISOGL_SHEET_A0) {
        chosen = draw_next_sheet(chosen);
        promoted = 1;
    }

    sheet->format = chosen;
    sheet->promoted = promoted;
    sheet->bounds = bounds;
    isogl_sheet_size((isogl_sheet_format)sheet->format,
                     (isogl_sheet_orientation)sheet->orientation,
                     &sheet->width_mm, &sheet->height_mm);
    return 0;
}
