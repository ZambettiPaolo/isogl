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

static char *draw_strdup(const char *text) {
    if (!text) text = "";
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

static int draw_param_index(const isogl_symbol_params *params,
                            const char *key) {
    if (!params || !key) return -1;
    for (size_t i = 0; i < params->param_count; ++i) {
        if (params->params[i].desc &&
            strcmp(params->params[i].desc->key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Setters by key.  A key the C schema does not declare is ignored: the schema
 * is authoritative and legacy fields must never invent parameters. */
static int draw_param_set_number(isogl_symbol_params *params, const char *key,
                                 double value) {
    int index = draw_param_index(params, key);
    if (index < 0) return 0;
    isogl_symbol_param *p = &params->params[index];
    if (!p->desc || (p->desc->kind != ISOGL_SYM_PARAM_NUMBER &&
                     p->desc->kind != ISOGL_SYM_PARAM_SCALE)) {
        return -1;
    }
    p->value.number = value;
    return 0;
}

static int draw_param_set_int(isogl_symbol_params *params, const char *key,
                              int value) {
    int index = draw_param_index(params, key);
    if (index < 0) return 0;
    isogl_symbol_param *p = &params->params[index];
    if (!p->desc || (p->desc->kind != ISOGL_SYM_PARAM_INTEGER &&
                     p->desc->kind != ISOGL_SYM_PARAM_ENUM &&
                     p->desc->kind != ISOGL_SYM_PARAM_BOOLEAN)) {
        return -1;
    }
    if (p->desc->kind == ISOGL_SYM_PARAM_BOOLEAN) {
        p->value.boolean = value ? 1 : 0;
    } else {
        p->value.integer = value;
    }
    return 0;
}

static int draw_param_set_color(isogl_symbol_params *params,
                                const float color[4]) {
    int index = draw_param_index(params, "color");
    if (index < 0) return 0;
    isogl_symbol_param *p = &params->params[index];
    if (!p->desc || p->desc->kind != ISOGL_SYM_PARAM_COLOR) return -1;
    memcpy(p->value.color, color, sizeof(p->value.color));
    return 0;
}

static int draw_array_number(const toml_array_t *array, int index,
                             double *out) {
    toml_datum_t d = toml_double_at(array, index);
    if (d.ok) {
        *out = d.u.d;
        return 0;
    }
    toml_datum_t i = toml_int_at(array, index);
    if (i.ok) {
        *out = (double)i.u.i;
        return 0;
    }
    return -1;
}

static int draw_table_number(toml_table_t *t, const char *key, double *out,
                             int *present) {
    *present = toml_key_exists(t, key) ? 1 : 0;
    if (!*present) return 0;

    toml_datum_t d = toml_double_in(t, key);
    if (d.ok) {
        *out = d.u.d;
        return 0;
    }
    d = toml_int_in(t, key);
    if (d.ok) {
        *out = (double)d.u.i;
        return 0;
    }
    return -1;
}

static int draw_table_int(toml_table_t *t, const char *key, int *out,
                          int *present) {
    *present = toml_key_exists(t, key) ? 1 : 0;
    if (!*present) return 0;

    toml_datum_t d = toml_int_in(t, key);
    if (!d.ok) return -1;
    *out = (int)d.u.i;
    return 0;
}

static int draw_table_bool(toml_table_t *t, const char *key, int *out,
                           int *present) {
    *present = toml_key_exists(t, key) ? 1 : 0;
    if (!*present) return 0;

    toml_datum_t d = toml_bool_in(t, key);
    if (d.ok) {
        *out = d.u.b ? 1 : 0;
        return 0;
    }
    d = toml_int_in(t, key);
    if (d.ok) {
        *out = d.u.i != 0;
        return 0;
    }
    return -1;
}

static int draw_check_count(const isogl_symbol_param_desc *desc, int count) {
    if (count < 0) return -1;
    if ((size_t)count < desc->min_count) return -1;
    if (desc->max_count != 0 && (size_t)count > desc->max_count) return -1;
    return 0;
}

/* Parse a [symbols.params] table into a params object created from the C type
 * schema.  Missing keys keep the create() defaults.  Points and segments are
 * group-local and are transformed here; paper sizes (mm) are not scaled.
 * Returns 0 on success, -1 on a malformed/mismatched value. */
static int draw_parse_param_table(toml_table_t *table,
                                  isogl_symbol_params *params,
                                  const DrawXform *xf) {
    if (!params) return -1;

    for (size_t i = 0; i < params->param_count; ++i) {
        isogl_symbol_param *p = &params->params[i];
        const isogl_symbol_param_desc *desc = p->desc;
        if (!desc) return -1;

        /* The sheet scale is global and read-only; never load it per symbol. */
        if (desc->kind == ISOGL_SYM_PARAM_SCALE) continue;
        if (!table || !toml_key_exists(table, desc->key)) continue;

        switch (desc->kind) {
            case ISOGL_SYM_PARAM_POINTS: {
                toml_array_t *array = toml_array_in(table, desc->key);
                if (!array) return -1;
                int count = toml_array_nelem(array);
                if (draw_check_count(desc, count) != 0) return -1;
                if (isogl_symbol_params_resize(params, i,
                                               (size_t)count) != 0) {
                    return -1;
                }
                for (int j = 0; j < count; ++j) {
                    toml_array_t *pair = toml_array_at(array, j);
                    if (!pair || toml_array_nelem(pair) < 2) return -1;
                    double x, y;
                    if (draw_array_number(pair, 0, &x) != 0 ||
                        draw_array_number(pair, 1, &y) != 0) {
                        return -1;
                    }
                    double tx, ty;
                    draw_xform_point(xf, x, y, &tx, &ty);
                    p->value.points[j].x = tx;
                    p->value.points[j].y = ty;
                }
                break;
            }

            case ISOGL_SYM_PARAM_TEXTS: {
                toml_array_t *array = toml_array_in(table, desc->key);
                if (!array) return -1;
                int count = toml_array_nelem(array);
                if (draw_check_count(desc, count) != 0) return -1;
                if (isogl_symbol_params_resize(params, i,
                                               (size_t)count) != 0) {
                    return -1;
                }
                for (int j = 0; j < count; ++j) {
                    toml_datum_t s = toml_string_at(array, j);
                    if (!s.ok) return -1;
                    int rc = isogl_symbol_params_set_text(
                        params, i, (size_t)j, s.u.s);
                    free(s.u.s);
                    if (rc != 0) return -1;
                }
                break;
            }

            case ISOGL_SYM_PARAM_SEGMENTS: {
                toml_array_t *array = toml_array_in(table, desc->key);
                if (!array) return -1;
                int count = toml_array_nelem(array);
                if (draw_check_count(desc, count) != 0) return -1;
                if (isogl_symbol_params_resize(params, i,
                                               (size_t)count) != 0) {
                    return -1;
                }
                for (int j = 0; j < count; ++j) {
                    toml_array_t *quad = toml_array_at(array, j);
                    if (!quad || toml_array_nelem(quad) < 4) return -1;
                    double v[4];
                    for (int k = 0; k < 4; ++k) {
                        if (draw_array_number(quad, k, &v[k]) != 0) return -1;
                    }
                    double x0, y0, x1, y1;
                    draw_xform_point(xf, v[0], v[1], &x0, &y0);
                    draw_xform_point(xf, v[2], v[3], &x1, &y1);
                    p->value.segments[j] =
                        (isogl_symbol_segment){x0, y0, x1, y1};
                }
                break;
            }

            case ISOGL_SYM_PARAM_NUMBER: {
                double value = 0.0;
                int present = 0;
                if (draw_table_number(table, desc->key, &value,
                                      &present) != 0) {
                    return -1;
                }
                if (present) p->value.number = value;
                break;
            }

            case ISOGL_SYM_PARAM_INTEGER:
            case ISOGL_SYM_PARAM_ENUM: {
                int value = 0;
                int present = 0;
                if (draw_table_int(table, desc->key, &value, &present) != 0) {
                    return -1;
                }
                if (present) p->value.integer = value;
                break;
            }

            case ISOGL_SYM_PARAM_BOOLEAN: {
                int value = 0;
                int present = 0;
                if (draw_table_bool(table, desc->key, &value, &present) != 0) {
                    return -1;
                }
                if (present) p->value.boolean = value;
                break;
            }

            case ISOGL_SYM_PARAM_COLOR: {
                toml_array_t *array = toml_array_in(table, desc->key);
                if (!array) return -1;
                int count = toml_array_nelem(array);
                if (count < 3 || count > 4) return -1;
                for (int j = 0; j < count; ++j) {
                    double value;
                    if (draw_array_number(array, j, &value) != 0) return -1;
                    p->value.color[j] = (float)value;
                }
                break;
            }

            default:
                return -1;
        }
    }

    /* Unknown keys are a warning, not fatal: the C schema stays authoritative
     * and a newer file must not break an older reader. */
    if (table) {
        for (int k = 0;; ++k) {
            const char *key = toml_key_in(table, k);
            if (!key) break;
            if (draw_param_index(params, key) < 0) {
                fprintf(stderr,
                        "drawing_load: unknown symbol parameter '%s' ignored\n",
                        key);
            }
        }
    }
    return 0;
}

/* ---- legacy flat symbol schema (translated to typed params) ------------- */

static int draw_parse_legacy_symbol(toml_table_t *t, const DrawXform *xf,
                                    const char *raw_type,
                                    DrawingSymbolEntry *out) {
    const char *type_name =
        strcmp(raw_type, "center") == 0 ? "center_mark" : raw_type;
    const isogl_symbol_type *type = isogl_symbol_find_type(type_name);

    out->type = draw_strdup(type_name);
    if (!out->type) return -1;

    if (!type) {
        fprintf(stderr,
                "drawing_load: unknown symbol type '%s'; entry skipped\n",
                raw_type);
        out->params = NULL;
        return 0;
    }

    if (strcmp(type_name, "dimension") != 0 &&
        strcmp(type_name, "center_mark") != 0) {
        fprintf(stderr,
                "drawing_load: legacy flat schema not supported for type '%s'; "
                "entry skipped\n", raw_type);
        out->params = NULL;
        return 0;
    }

    isogl_symbol_params *params = isogl_symbol_params_create(type_name);
    if (!params) return -1;

    int rc = 0;
    if (strcmp(type_name, "dimension") == 0) {
        int points = draw_param_index(params, "points");
        int texts = draw_param_index(params, "texts");
        if (points < 0 || params->params[points].count != 3 ||
            texts < 0 || params->params[texts].count != 2) {
            rc = -1;
        } else {
            const char *xkeys[3] = {"p1x", "p2x", "p3x"};
            const char *ykeys[3] = {"p1y", "p2y", "p3y"};
            for (int i = 0; i < 3; ++i) {
                double x = draw_double(t, xkeys[i], 0.0);
                double y = draw_double(t, ykeys[i], 0.0);
                double tx, ty;
                draw_xform_point(xf, x, y, &tx, &ty);
                params->params[points].value.points[i].x = tx;
                params->params[points].value.points[i].y = ty;
            }

            char left[32], right[32];
            draw_string(t, "text_left", left, sizeof(left), "");
            draw_string(t, "text_right", right, sizeof(right), "");
            if (isogl_symbol_params_set_text(params, (size_t)texts, 0,
                                             left) != 0 ||
                isogl_symbol_params_set_text(params, (size_t)texts, 1,
                                             right) != 0) {
                rc = -1;
            } else {
                int arrow_left =
                    draw_int(t, "arrow_left", ISOGL_DIM_ARROW_OPEN);
                int arrow_right =
                    draw_int(t, "arrow_right", ISOGL_DIM_ARROW_OPEN);
                if (arrow_left < 0 ||
                    arrow_left > ISOGL_DIM_ARROW_CLOSED_OUTSIDE) {
                    arrow_left = ISOGL_DIM_ARROW_OPEN;
                }
                if (arrow_right < 0 ||
                    arrow_right > ISOGL_DIM_ARROW_CLOSED_OUTSIDE) {
                    arrow_right = ISOGL_DIM_ARROW_OPEN;
                }

                rc |= draw_param_set_number(
                    params, "distance", draw_double(t, "distance", 0.0));
                rc |= draw_param_set_number(
                    params, "text_offset_mm",
                    draw_double(t, "text_offset_mm", 0.0));
                rc |= draw_param_set_number(
                    params, "text_height_mm",
                    draw_double(t, "text_height_mm", 3.5));
                rc |= draw_param_set_int(
                    params, "precision", draw_int(t, "precision", 1));
                rc |= draw_param_set_int(
                    params, "pick_text", draw_int(t, "pick_text", 1));
                rc |= draw_param_set_int(
                    params, "mark_points", draw_int(t, "mark_points", 0));
                rc |= draw_param_set_int(params, "arrow_left", arrow_left);
                rc |= draw_param_set_int(params, "arrow_right", arrow_right);
            }
        }
    } else {
        int points = draw_param_index(params, "points");
        if (points < 0 || params->params[points].count != 1) {
            rc = -1;
        } else {
            double x = draw_double(t, "cx", 0.0);
            double y = draw_double(t, "cy", 0.0);
            double tx, ty;
            draw_xform_point(xf, x, y, &tx, &ty);
            params->params[points].value.points[0].x = tx;
            params->params[points].value.points[0].y = ty;
            rc |= draw_param_set_number(
                params, "size_mm", draw_double(t, "size_mm", 8.0));
        }
    }

    uint8_t pattern = 0;
    uint8_t weight = 1;
    uint8_t layer = 0;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (rc == 0) {
        draw_style_fields(t, &pattern, &weight, &layer, color);
        rc |= draw_param_set_int(params, "pattern", pattern);
        rc |= draw_param_set_int(params, "weight", weight);
        rc |= draw_param_set_int(params, "layer", layer);
        rc |= draw_param_set_color(params, color);
    }

    if (rc != 0 || (type->validate && type->validate(params) != 0)) {
        isogl_symbol_params_free(params);
        free(out->type);
        out->type = NULL;
        return -1;
    }

    out->params = params;
    return 0;
}

/* ---- symbol entry parsing ---------------------------------------------- */

static void drawing_symbol_entry_free(DrawingSymbolEntry *entry) {
    if (!entry) return;
    free(entry->type);
    isogl_symbol_params_free(entry->params);
    memset(entry, 0, sizeof(*entry));
}

static int draw_parse_symbol_entry(toml_table_t *t, const DrawGroup *groups,
                                   int group_count,
                                   const DrawingSheetInfo *sheet,
                                   DrawingSymbolEntry *out) {
    if (!t || !out) return -1;

    memset(out, 0, sizeof(*out));
    out->group = draw_int(t, "group", 0);

    DrawXform xf;
    if (draw_group_xform_rec(groups, group_count, out->group, 0, &xf) != 0) {
        fprintf(stderr, "drawing_load: symbol uses invalid group %d\n",
                out->group);
        return -1;
    }

    char type[64];
    draw_string(t, "type", type, sizeof(type), "");
    if (type[0] == '\0') {
        fprintf(stderr, "drawing_load: symbol entry without a type\n");
        return -1;
    }

    toml_table_t *param_table = toml_table_in(t, "params");

    /* No params table and schema v1: the flat p1x/cx layout. */
    if (!param_table && sheet->schema_version < 2) {
        return draw_parse_legacy_symbol(t, &xf, type, out);
    }

    const isogl_symbol_type *type_desc = isogl_symbol_find_type(type);
    out->type = draw_strdup(type);
    if (!out->type) return -1;

    if (!type_desc) {
        fprintf(stderr,
                "drawing_load: unknown symbol type '%s'; entry skipped\n",
                type);
        out->params = NULL;
        return 0;
    }

    isogl_symbol_params *params = isogl_symbol_params_create(type);
    if (!params) {
        drawing_symbol_entry_free(out);
        return -1;
    }

    if (draw_parse_param_table(param_table, params, &xf) != 0) {
        fprintf(stderr,
                "drawing_load: malformed parameters for symbol type '%s'\n",
                type);
        isogl_symbol_params_free(params);
        drawing_symbol_entry_free(out);
        return -1;
    }

    if (type_desc->validate && type_desc->validate(params) != 0) {
        fprintf(stderr,
                "drawing_load: symbol type '%s' failed schema validation\n",
                type);
        isogl_symbol_params_free(params);
        drawing_symbol_entry_free(out);
        return -1;
    }

    out->params = params;
    return 0;
}

/* ---- symbol bounds ------------------------------------------------------ */

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

static int draw_symbol_noop_emit(void *user, const IsoglSymItem *item) {
    (void)user;
    (void)item;
    return 0;
}

static int draw_symbol_bounds(const DrawingSymbolEntry *entry,
                              const isogl_font_data *font,
                              double sheet_scale,
                              DrawingBounds *bounds) {
    /* Unknown types have no params and no geometry; they never contribute. */
    if (!entry || !entry->params) return 0;

    DrawSymbolBoundsCtx ctx = {bounds};
    IsoglSymPickEmitter pick = {&ctx, draw_symbol_pick_emit};
    IsoglSymEmitter render = {NULL, draw_symbol_noop_emit};
    isogl_symbol_gen_context gen = {font, sheet_scale};
    return entry->params->type->generate(entry->params, &gen, &render, &pick);
}

/* ---- writer ------------------------------------------------------------- */

static int draw_write_double(FILE *out, double value) {
    if (!isfinite(value)) return -1;
    char buffer[64];
    int n = snprintf(buffer, sizeof(buffer), "%.17g", value);
    if (n < 0 || (size_t)n >= sizeof(buffer)) return -1;
    /* TOML distinguishes integers from floats.  Force a decimal point so the
     * value reloads as a double even when %g drops the trailing ".0". */
    if (!strpbrk(buffer, ".eE")) {
        if ((size_t)n + 2 >= sizeof(buffer)) return -1;
        buffer[n++] = '.';
        buffer[n++] = '0';
        buffer[n] = '\0';
    }
    return fputs(buffer, out) < 0 ? -1 : 0;
}

static int draw_write_string(FILE *out, const char *text) {
    if (!text) text = "";
    if (fputc('"', out) == EOF) return -1;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        switch (*p) {
            case '"': if (fputs("\\\"", out) < 0) return -1; break;
            case '\\': if (fputs("\\\\", out) < 0) return -1; break;
            case '\n': if (fputs("\\n", out) < 0) return -1; break;
            case '\r': if (fputs("\\r", out) < 0) return -1; break;
            case '\t': if (fputs("\\t", out) < 0) return -1; break;
            default:
                if (*p < 0x20) {
                    if (fprintf(out, "\\u%04X", *p) < 0) return -1;
                } else if (fputc(*p, out) == EOF) {
                    return -1;
                }
                break;
        }
    }
    return fputc('"', out) == EOF ? -1 : 0;
}

static int draw_write_param(FILE *out, const isogl_symbol_param *p) {
    const isogl_symbol_param_desc *desc = p->desc;
    if (!desc) return -1;

    if (fprintf(out, "%s = ", desc->key) < 0) return -1;
    int rc = 0;

    switch (desc->kind) {
        case ISOGL_SYM_PARAM_POINTS:
            if (fputc('[', out) == EOF) return -1;
            for (size_t i = 0; i < p->count; ++i) {
                if (i > 0 && fputc(',', out) == EOF) return -1;
                if (fputs("[", out) < 0 ||
                    draw_write_double(out, p->value.points[i].x) != 0 ||
                    fputs(", ", out) < 0 ||
                    draw_write_double(out, p->value.points[i].y) != 0 ||
                    fputs("]", out) < 0) {
                    return -1;
                }
            }
            rc = fputc(']', out) == EOF ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_TEXTS:
            if (fputc('[', out) == EOF) return -1;
            for (size_t i = 0; i < p->count; ++i) {
                if (i > 0 && fputs(", ", out) < 0) return -1;
                if (draw_write_string(out,
                                      p->value.texts[i]) != 0) {
                    return -1;
                }
            }
            rc = fputc(']', out) == EOF ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_SEGMENTS:
            if (fputc('[', out) == EOF) return -1;
            for (size_t i = 0; i < p->count; ++i) {
                const isogl_symbol_segment *s = &p->value.segments[i];
                if (i > 0 && fputc(',', out) == EOF) return -1;
                if (fputs("[", out) < 0 ||
                    draw_write_double(out, s->x0) != 0 ||
                    fputs(", ", out) < 0 ||
                    draw_write_double(out, s->y0) != 0 ||
                    fputs(", ", out) < 0 ||
                    draw_write_double(out, s->x1) != 0 ||
                    fputs(", ", out) < 0 ||
                    draw_write_double(out, s->y1) != 0 ||
                    fputs("]", out) < 0) {
                    return -1;
                }
            }
            rc = fputc(']', out) == EOF ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_NUMBER:
            rc = draw_write_double(out, p->value.number);
            break;

        case ISOGL_SYM_PARAM_INTEGER:
        case ISOGL_SYM_PARAM_ENUM:
            rc = fprintf(out, "%d", p->value.integer) < 0 ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_BOOLEAN:
            rc = fputs(p->value.boolean ? "true" : "false", out) < 0 ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_COLOR:
            if (fputc('[', out) == EOF) return -1;
            for (int i = 0; i < 4; ++i) {
                if (i > 0 && fputs(", ", out) < 0) return -1;
                if (draw_write_double(out, p->value.color[i]) != 0) return -1;
            }
            rc = fputc(']', out) == EOF ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_SCALE:
            /* Global sheet scale; never saved per symbol. */
            return 0;

        default:
            return -1;
    }
    return rc;
}

int drawing_write_symbols(FILE *out, const DrawingSymbols *symbols) {
    if (!out || !symbols) return -1;

    for (size_t i = 0; i < symbols->count; ++i) {
        const DrawingSymbolEntry *entry = &symbols->items[i];
        /* Unknown types have no typed params.  There is no raw representation
         * yet, so they cannot be preserved by the writer. */
        if (!entry->params) continue;

        if (fputs("[[symbols]]\ntype = ", out) < 0 ||
            draw_write_string(out, entry->type) != 0 ||
            fprintf(out, "\ngroup = %d\n\n[symbols.params]\n",
                    entry->group) < 0) {
            return -1;
        }

        for (size_t j = 0; j < entry->params->param_count; ++j) {
            const isogl_symbol_param *p = &entry->params->params[j];
            if (!p->desc) return -1;
            if (p->desc->kind == ISOGL_SYM_PARAM_SCALE) continue;
            if (draw_write_param(out, p) != 0) return -1;
            if (fputc('\n', out) == EOF) return -1;
        }
        if (fputc('\n', out) == EOF) return -1;
    }
    return ferror(out) ? -1 : 0;
}

int drawing_save_symbols(const char *path, const DrawingSymbols *symbols) {
    if (!path || !symbols) return -1;
    FILE *out = fopen(path, "w");
    if (!out) return -1;
    int rc = drawing_write_symbols(out, symbols);
    if (fclose(out) != 0) rc = -1;
    return rc;
}

void drawing_symbols_free(DrawingSymbols *symbols) {
    if (!symbols) return;
    for (size_t i = 0; i < symbols->count; ++i) {
        drawing_symbol_entry_free(&symbols->items[i]);
    }
    memset(symbols, 0, sizeof(*symbols));
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
    if (symbols) memset(symbols, 0, sizeof(*symbols));

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
    sheet->schema_version = 1;

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
        sheet->schema_version = draw_int(sheet_table, "schema_version", 1);
        if (sheet->schema_version < 1) sheet->schema_version = 1;
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
                drawing_symbols_free(&parsed_symbols);
                toml_free(doc);
                fprintf(stderr, "drawing_load: too many symbols\n");
                return -1;
            }
            toml_table_t *t = toml_table_at(symbol_array, i);
            if (!t) continue;

            DrawingSymbolEntry *entry =
                &parsed_symbols.items[parsed_symbols.count];
            if (draw_parse_symbol_entry(t, groups, group_count, sheet,
                                        entry) != 0) {
                drawing_symbol_entry_free(entry);
                fprintf(stderr, "drawing_load: symbol %d skipped\n", i);
                continue;
            }
            parsed_symbols.count++;
        }
    }

    DrawingBounds bounds = {0, 0, 0, 0, 0};

    for (int i = 0; i < primitive_count; ++i) {
        DrawPrimitive *p = &primitives[i];
        DrawXform xf = {0.0, 0.0, 0.0, 1.0};
        if (draw_group_xform_rec(groups, group_count, p->group, 0, &xf) != 0) {
            drawing_symbols_free(&parsed_symbols);
            toml_free(doc);
            fprintf(stderr, "drawing_load: primitive %d has an invalid group\n",
                    i);
            return -1;
        }
        if (draw_emit_primitive(segments, strings, font, global_scale,
                                p, &xf, &bounds) != 0) {
            drawing_symbols_free(&parsed_symbols);
            toml_free(doc);
            fprintf(stderr, "drawing_load: failed to emit primitive %d\n", i);
            return -1;
        }
    }

    for (size_t i = 0; i < parsed_symbols.count; ++i) {
        if (draw_symbol_bounds(&parsed_symbols.items[i], font,
                               (double)global_scale, &bounds) != 0) {
            drawing_symbols_free(&parsed_symbols);
            toml_free(doc);
            fprintf(stderr, "drawing_load: failed to bound symbol %zu\n", i);
            return -1;
        }
    }

    if (symbols) {
        *symbols = parsed_symbols;
    } else {
        drawing_symbols_free(&parsed_symbols);
    }

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
