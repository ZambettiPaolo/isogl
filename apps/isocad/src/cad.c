#include "cad.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAD_CIRCLE_SEGMENTS 96

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static CadEntity *cad_push_entity(CadDocument *doc, CadEntityType type, int layer,
                                  isogl_line_type pattern, isogl_line_weight weight) {
    if (doc->count == doc->capacity) {
        size_t capacity = doc->capacity ? doc->capacity * 2 : 32;
        CadEntity *grown = realloc(doc->entities, capacity * sizeof(CadEntity));
        if (!grown) return NULL;
        doc->entities = grown;
        doc->capacity = capacity;
    }

    CadEntity *entity = &doc->entities[doc->count];
    memset(entity, 0, sizeof(*entity));
    entity->id = (uint32_t)doc->count + 1;
    entity->type = type;
    if (layer < 0) layer = 0;
    if (doc->layer_count > 0 && layer >= doc->layer_count) layer = doc->layer_count - 1;
    entity->layer = (uint8_t)layer;
    entity->pattern = pattern;
    entity->weight = weight;
    entity->visible = 1;
    doc->count++;
    return entity;
}

static isogl_vertex cad_vertex(const CadDocument *doc, const CadEntity *entity,
                               float x, float y, float distance) {
    isogl_vertex v;
    memset(&v, 0, sizeof(v));
    v.position[0] = x;
    v.position[1] = y;

    const CadLayer *layer = &doc->layers[entity->layer];
    memcpy(v.color, layer->color, sizeof(v.color));

    v.distanceFromStart = distance;
    v.pk_thickness_pattern_layer =
        isogl_pack((uint8_t)entity->weight, (uint8_t)entity->pattern, entity->layer);
    return v;
}

static int cad_upload_entity(CadDocument *doc, CadEntity *entity,
                             isogl_vertex *vertices, size_t vertex_count) {
    if (vertex_count == 0) return -1;

    isogl_segment_list list;
    if (isogl_segment_list_init(&list, vertex_count) != 0) return -1;

    for (size_t i = 0; i + 1 < vertex_count; i += 2) {
        if (isogl_segment_list_push_pair(&list, vertices[i], vertices[i + 1]) != 0) {
            isogl_segment_list_free(&list);
            return -1;
        }
    }

    GLuint id = isogl_vbo_array_add(&doc->parts, &list, ISOGL_VBO_SEGMENT);
    isogl_segment_list_free(&list);

    if (id == 0) return -1;
    entity->vbo_id = id;
    isogl_vbo_array_set_pick_id(&doc->parts, id, entity->id);
    return 0;
}

// ===== AREA SELECTION HELPERS =====

static int cad_point_in_rect(float x, float y,
                             float xmin, float ymin, float xmax, float ymax) {
    return x >= xmin && x <= xmax && y >= ymin && y <= ymax;
}

static float cad_min_edge_distance(float cx, float cy,
                                   float xmin, float ymin,
                                   float xmax, float ymax) {
    float dx = fminf(cx - xmin, xmax - cx);
    float dy = fminf(cy - ymin, ymax - cy);
    return fminf(dx, dy);
}

static float cad_rect_distance(float cx, float cy,
                               float xmin, float ymin,
                               float xmax, float ymax) {
    float dx = 0.0f;
    float dy = 0.0f;
    if (cx < xmin) dx = xmin - cx;
    else if (cx > xmax) dx = cx - xmax;
    if (cy < ymin) dy = ymin - cy;
    else if (cy > ymax) dy = cy - ymax;
    return sqrtf(dx * dx + dy * dy);
}

static float cad_max_corner_distance(float cx, float cy,
                                     float xmin, float ymin,
                                     float xmax, float ymax) {
    float d0 = hypotf(cx - xmin, cy - ymin);
    float d1 = hypotf(cx - xmax, cy - ymin);
    float d2 = hypotf(cx - xmin, cy - ymax);
    float d3 = hypotf(cx - xmax, cy - ymax);
    return fmaxf(fmaxf(d0, d1), fmaxf(d2, d3));
}

static int cad_segment_intersects_rect(float ax, float ay, float bx, float by,
                                       float xmin, float ymin,
                                       float xmax, float ymax) {
    if (cad_point_in_rect(ax, ay, xmin, ymin, xmax, ymax) ||
        cad_point_in_rect(bx, by, xmin, ymin, xmax, ymax)) {
        return 1;
    }

    float dx = bx - ax;
    float dy = by - ay;
    float p[4] = {-dx, dx, -dy, dy};
    float q[4] = {ax - xmin, xmax - ax, ay - ymin, ymax - ay};
    float t0 = 0.0f;
    float t1 = 1.0f;

    for (int i = 0; i < 4; ++i) {
        if (fabsf(p[i]) < 1e-12f) {
            if (q[i] < 0.0f) return 0;
        } else {
            float r = q[i] / p[i];
            if (p[i] < 0.0f) {
                if (r > t1) return 0;
                if (r > t0) t0 = r;
            } else {
                if (r < t0) return 0;
                if (r < t1) t1 = r;
            }
        }
    }
    return t0 <= t1;
}

static void cad_obb_corners(float x, float y, float w, float h, float angle,
                            float corners[4][2]) {
    float c = cosf(angle);
    float s = sinf(angle);
    float lx[4] = {0.0f, w, w, 0.0f};
    float ly[4] = {0.0f, 0.0f, h, h};

    for (int i = 0; i < 4; ++i) {
        corners[i][0] = x + lx[i] * c - ly[i] * s;
        corners[i][1] = y + lx[i] * s + ly[i] * c;
    }
}

/* ---- Dimension geometry: parameters -> render and pick primitives ------- */

#define CAD_DIM_ARROW_MM 3.5
#define CAD_DIM_EXT_MM 2.0
#define CAD_DIM_TEXT_GAP_MM 1.0

typedef struct {
    double ax, ay;          /* dimension line A */
    double bx, by;          /* dimension line B */
    double e1x, e1y;        /* extension line 1 end (p1 -> beyond A) */
    double e2x, e2y;        /* extension line 2 end (p2 -> beyond B) */
    double arrow[4][4];     /* four arrow legs */
    int arrow_count;
    double text_x, text_y;  /* centred baseline origin */
    double text_angle;
    double text_width;      /* model units */
    double text_height;     /* model units */
    char text[128];
} CadDimensionGeometry;

static double cad_text_model_width(const CadDocument *doc, const char *text,
                                   double height_mm) {
    if (!doc || !doc->font || !text || height_mm <= 0.0) return 0.0;

    double font_height = (double)doc->font->height;
    double global_scale = doc->sheet_scale > 0.0
                        ? (double)doc->sheet_scale : 1.0;
    if (font_height <= 0.0) return 0.0;

    double advance = isogl_font_text_advance(doc->font, text);
    return advance * height_mm / (font_height * global_scale);
}

static void cad_dimension_text(const CadDimension *d, char *out,
                               size_t out_size) {
    double len = hypot(d->x1 - d->x0, d->y1 - d->y0);
    int precision = d->precision;
    if (precision < 0) precision = 0;
    if (precision > 6) precision = 6;
    snprintf(out, out_size, "%s%.*f%s%s", d->prefix, precision, len,
             d->unit, d->suffix);
}

static int cad_dimension_geometry(const CadDocument *doc,
                                  const CadDimension *d,
                                  CadDimensionGeometry *g) {
    if (!doc || !d || !g) return -1;

    double dx = d->x1 - d->x0;
    double dy = d->y1 - d->y0;
    double len = hypot(dx, dy);
    if (len <= 1e-9) return -1;

    double ux = dx / len;
    double uy = dy / len;
    double nx = -uy;
    double ny = ux;

    /* Project p1 and p2 on the line through p3 parallel to p1-p2. */
    double t1 = (d->x0 - d->px) * ux + (d->y0 - d->py) * uy;
    double t2 = (d->x1 - d->px) * ux + (d->y1 - d->py) * uy;
    double ax = d->px + t1 * ux;
    double ay = d->py + t1 * uy;
    double bx = d->px + t2 * ux;
    double by = d->py + t2 * uy;

    /* p3 side decides the extension-line direction. */
    double side = (d->px - d->x0) * nx + (d->py - d->y0) * ny;
    double sign = side >= 0.0 ? 1.0 : -1.0;

    double scale = doc->sheet_scale > 0.0 ? (double)doc->sheet_scale : 1.0;
    double overshoot = CAD_DIM_EXT_MM / scale;
    double arrow = CAD_DIM_ARROW_MM / scale;
    double max_arrow = len * 0.45;
    if (arrow > max_arrow) arrow = max_arrow;
    double text_gap = CAD_DIM_TEXT_GAP_MM / scale;
    double text_height_mm = d->text_height_mm > 0.0 ? d->text_height_mm : 3.5;

    g->ax = ax;
    g->ay = ay;
    g->bx = bx;
    g->by = by;
    g->e1x = ax + sign * overshoot * nx;
    g->e1y = ay + sign * overshoot * ny;
    g->e2x = bx + sign * overshoot * nx;
    g->e2y = by + sign * overshoot * ny;

    double ca = cos(M_PI / 6.0);
    double sa = sin(M_PI / 6.0);
    g->arrow[0][0] = ax;
    g->arrow[0][1] = ay;
    g->arrow[0][2] = ax + arrow * (ca * ux + sa * nx);
    g->arrow[0][3] = ay + arrow * (ca * uy + sa * ny);
    g->arrow[1][0] = ax;
    g->arrow[1][1] = ay;
    g->arrow[1][2] = ax + arrow * (ca * ux - sa * nx);
    g->arrow[1][3] = ay + arrow * (ca * uy - sa * ny);
    g->arrow[2][0] = bx;
    g->arrow[2][1] = by;
    g->arrow[2][2] = bx - arrow * (ca * ux + sa * nx);
    g->arrow[2][3] = by - arrow * (ca * uy + sa * ny);
    g->arrow[3][0] = bx;
    g->arrow[3][1] = by;
    g->arrow[3][2] = bx - arrow * (ca * ux - sa * nx);
    g->arrow[3][3] = by - arrow * (ca * uy - sa * ny);
    g->arrow_count = 4;

    cad_dimension_text(d, g->text, sizeof(g->text));
    g->text_height = text_height_mm / scale;
    g->text_width = cad_text_model_width(doc, g->text, text_height_mm);

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
    g->text_x = mx - 0.5 * g->text_width * tx + text_gap * tnx;
    g->text_y = my - 0.5 * g->text_width * ty + text_gap * tny;
    return 0;
}

/* One pick segment: it must be contained for window, it may intersect for
 * crossing.  The caller starts `contained` at 1. */
static void cad_pick_segment(double ax, double ay, double bx, double by,
                             double xmin, double ymin, double xmax, double ymax,
                             int *contained, int *intersects) {
    if (*contained &&
        (!cad_point_in_rect((float)ax, (float)ay, (float)xmin, (float)ymin,
                            (float)xmax, (float)ymax) ||
         !cad_point_in_rect((float)bx, (float)by, (float)xmin, (float)ymin,
                            (float)xmax, (float)ymax))) {
        *contained = 0;
    }
    if (!*intersects &&
        cad_segment_intersects_rect((float)ax, (float)ay,
                                    (float)bx, (float)by,
                                    (float)xmin, (float)ymin,
                                    (float)xmax, (float)ymax)) {
        *intersects = 1;
    }
}

static int cad_point_in_obb(float px, float py, const float c[4][2]) {
    int positive = 0;
    int negative = 0;
    for (int i = 0; i < 4; ++i) {
        float ax = c[i][0];
        float ay = c[i][1];
        float bx = c[(i + 1) & 3][0];
        float by = c[(i + 1) & 3][1];
        float cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
        if (cross > 1e-5f) positive = 1;
        else if (cross < -1e-5f) negative = 1;
        if (positive && negative) return 0;
    }
    return 1;
}

static int cad_obb_intersects_rect(const float c[4][2],
                                   float xmin, float ymin,
                                   float xmax, float ymax) {
    for (int i = 0; i < 4; ++i) {
        int n = (i + 1) & 3;
        if (cad_segment_intersects_rect(c[i][0], c[i][1], c[n][0], c[n][1],
                                        xmin, ymin, xmax, ymax)) {
            return 1;
        }
    }

    /* The rectangle may be entirely inside the OBB. */
    float rx[4] = {xmin, xmax, xmax, xmin};
    float ry[4] = {ymin, ymin, ymax, ymax};
    for (int i = 0; i < 4; ++i) {
        if (cad_point_in_obb(rx[i], ry[i], c)) return 1;
    }
    return 0;
}

static void cad_add_id(uint32_t *ids, size_t max, size_t *count, uint32_t id) {
    if (*count < max) {
        ids[*count] = id;
        (*count)++;
    }
}

void cad_select_rect(const CadDocument *doc,
                     isogl_point p0, isogl_point p1,
                     uint32_t *contained, size_t max_contained,
                     size_t *n_contained,
                     uint32_t *hit, size_t max_hit, size_t *n_hit) {
    if (!doc || !n_contained || !n_hit) return;
    *n_contained = 0;
    *n_hit = 0;

    float xmin = fminf(p0.x, p1.x);
    float ymin = fminf(p0.y, p1.y);
    float xmax = fmaxf(p0.x, p1.x);
    float ymax = fmaxf(p0.y, p1.y);

    for (size_t i = 0; i < doc->count; ++i) {
        const CadEntity *e = &doc->entities[i];
        if (!e->visible) continue;
        if (e->layer < doc->layer_count &&
            !doc->layers[e->layer].visible) {
            continue;
        }

        int is_contained = 0;
        int intersects = 0;

        switch (e->type) {
            case CAD_LINE: {
                float ax = e->geom.line.x0;
                float ay = e->geom.line.y0;
                float bx = e->geom.line.x1;
                float by = e->geom.line.y1;
                is_contained =
                    cad_point_in_rect(ax, ay, xmin, ymin, xmax, ymax) &&
                    cad_point_in_rect(bx, by, xmin, ymin, xmax, ymax);
                intersects = cad_segment_intersects_rect(
                    ax, ay, bx, by, xmin, ymin, xmax, ymax);
                break;
            }
            case CAD_RECTANGLE: {
                float rx0 = fminf(e->geom.rect.x0, e->geom.rect.x1);
                float ry0 = fminf(e->geom.rect.y0, e->geom.rect.y1);
                float rx1 = fmaxf(e->geom.rect.x0, e->geom.rect.x1);
                float ry1 = fmaxf(e->geom.rect.y0, e->geom.rect.y1);
                float xs[4] = {rx0, rx1, rx1, rx0};
                float ys[4] = {ry0, ry0, ry1, ry1};
                is_contained = 1;
                for (int k = 0; k < 4; ++k) {
                    if (!cad_point_in_rect(xs[k], ys[k],
                                           xmin, ymin, xmax, ymax)) {
                        is_contained = 0;
                    }
                }
                for (int k = 0; k < 4; ++k) {
                    int n = (k + 1) & 3;
                    if (cad_segment_intersects_rect(xs[k], ys[k],
                                                    xs[n], ys[n],
                                                    xmin, ymin, xmax, ymax)) {
                        intersects = 1;
                    }
                }
                break;
            }
            case CAD_CIRCLE: {
                float cx = e->geom.circle.cx;
                float cy = e->geom.circle.cy;
                float r = e->geom.circle.r;
                int center_in =
                    cad_point_in_rect(cx, cy, xmin, ymin, xmax, ymax);
                float nearest = center_in
                    ? cad_min_edge_distance(cx, cy, xmin, ymin, xmax, ymax)
                    : cad_rect_distance(cx, cy, xmin, ymin, xmax, ymax);
                float farthest =
                    cad_max_corner_distance(cx, cy, xmin, ymin, xmax, ymax);

                is_contained = center_in && r <= nearest;
                intersects = (r >= nearest - 1e-6f &&
                              r <= farthest + 1e-6f);
                break;
            }
            case CAD_TEXT: {
                double scale = doc->sheet_scale > 0.0
                             ? (double)doc->sheet_scale : 1.0;
                float h = (float)((double)e->geom.label.height / scale);
                float w = (float)cad_text_model_width(
                    doc, e->geom.label.text, (double)e->geom.label.height);
                float corners[4][2];
                cad_obb_corners(e->geom.label.x, e->geom.label.y,
                                w, h, e->geom.label.angle, corners);
                is_contained = 1;
                for (int k = 0; k < 4; ++k) {
                    if (!cad_point_in_rect(corners[k][0], corners[k][1],
                                           xmin, ymin, xmax, ymax)) {
                        is_contained = 0;
                    }
                }
                if (cad_obb_intersects_rect(corners, xmin, ymin, xmax, ymax)) {
                    intersects = 1;
                }
                break;
            }
            case CAD_DIMENSION: {
                CadDimensionGeometry g;
                if (cad_dimension_geometry(doc, &e->geom.dimension, &g) != 0) {
                    break;
                }

                is_contained = 1;
                intersects = 0;

                cad_pick_segment(g.ax, g.ay, g.bx, g.by,
                                 xmin, ymin, xmax, ymax,
                                 &is_contained, &intersects);
                cad_pick_segment(e->geom.dimension.x0, e->geom.dimension.y0,
                                 g.e1x, g.e1y,
                                 xmin, ymin, xmax, ymax,
                                 &is_contained, &intersects);
                cad_pick_segment(e->geom.dimension.x1, e->geom.dimension.y1,
                                 g.e2x, g.e2y,
                                 xmin, ymin, xmax, ymax,
                                 &is_contained, &intersects);
                for (int k = 0; k < g.arrow_count; ++k) {
                    cad_pick_segment(g.arrow[k][0], g.arrow[k][1],
                                     g.arrow[k][2], g.arrow[k][3],
                                     xmin, ymin, xmax, ymax,
                                     &is_contained, &intersects);
                }

                if (e->geom.dimension.pick_text) {
                    float corners[4][2];
                    cad_obb_corners((float)g.text_x, (float)g.text_y,
                                    (float)g.text_width, (float)g.text_height,
                                    (float)g.text_angle, corners);
                    for (int k = 0; k < 4; ++k) {
                        if (!cad_point_in_rect(corners[k][0], corners[k][1],
                                               xmin, ymin, xmax, ymax)) {
                            is_contained = 0;
                        }
                    }
                    if (cad_obb_intersects_rect(corners, xmin, ymin,
                                                xmax, ymax)) {
                        intersects = 1;
                    }
                }
                break;
            }
            default:
                break;
        }

        if (is_contained) {
            cad_add_id(contained, max_contained, n_contained, e->id);
        }
        if (is_contained || intersects) {
            cad_add_id(hit, max_hit, n_hit, e->id);
        }
    }
}

int cad_selection_apply(CadDocument *doc, const uint32_t *ids, size_t count) {
    if (!doc) return -1;
    int parts_rc = isogl_vbo_array_apply_selection(&doc->parts, ids, count);
    int text_rc = isogl_string_list_apply_selection(&doc->strings, ids, count);
    return (parts_rc == 0 && text_rc == 0) ? 0 : -1;
}

void cad_selection_clear(CadDocument *doc) {
    if (!doc) return;
    isogl_vbo_array_clear_selection(&doc->parts);
    isogl_string_list_apply_selection(&doc->strings, NULL, 0);
}

int cad_document_init(CadDocument *doc, isogl_font_data *font, float sheet_scale) {
    if (!doc) return -1;
    memset(doc, 0, sizeof(*doc));
    doc->font = font;
    doc->sheet_scale = sheet_scale > 0.0f ? sheet_scale : 1.0f;

    if (isogl_vbo_array_init(&doc->parts) != 0) return -1;

    doc->layer_count = 4;
    snprintf(doc->layers[0].name, sizeof(doc->layers[0].name), "Contorni");
    doc->layers[0].color[0] = 1.0f;
    doc->layers[0].color[1] = 1.0f;
    doc->layers[0].color[2] = 1.0f;
    doc->layers[0].color[3] = 1.0f;
    doc->layers[0].default_weight = LINE_THICKNESS_0_5;
    doc->layers[0].default_pattern = LINE_TYPE_CONTINUOUS;

    snprintf(doc->layers[1].name, sizeof(doc->layers[1].name), "Assi");
    doc->layers[1].color[0] = 0.2f;
    doc->layers[1].color[1] = 0.9f;
    doc->layers[1].color[2] = 0.2f;
    doc->layers[1].color[3] = 1.0f;
    doc->layers[1].default_weight = LINE_THICKNESS_0_18;
    doc->layers[1].default_pattern = LINE_TYPE_LONG_DASH_DOT;

    snprintf(doc->layers[2].name, sizeof(doc->layers[2].name), "Quote");
    doc->layers[2].color[0] = 1.0f;
    doc->layers[2].color[1] = 0.85f;
    doc->layers[2].color[2] = 0.2f;
    doc->layers[2].color[3] = 1.0f;
    doc->layers[2].default_weight = LINE_THICKNESS_0_18;
    doc->layers[2].default_pattern = LINE_TYPE_CONTINUOUS;

    snprintf(doc->layers[3].name, sizeof(doc->layers[3].name), "Testo");
    doc->layers[3].color[0] = 1.0f;
    doc->layers[3].color[1] = 1.0f;
    doc->layers[3].color[2] = 1.0f;
    doc->layers[3].color[3] = 1.0f;
    doc->layers[3].default_weight = LINE_THICKNESS_0_25;
    doc->layers[3].default_pattern = LINE_TYPE_CONTINUOUS;

    for (int i = 0; i < doc->layer_count; ++i) doc->layers[i].visible = 1;
    return 0;
}

void cad_document_free(CadDocument *doc) {
    if (!doc) return;
    isogl_string_list_free(&doc->strings);
    isogl_vbo_array_free(&doc->parts);
    free(doc->entities);
    doc->entities = NULL;
    doc->count = 0;
    doc->capacity = 0;
}

uint32_t cad_add_line(CadDocument *doc, int layer, isogl_line_type pattern,
                      isogl_line_weight weight, float x0, float y0,
                      float x1, float y1) {
    CadEntity *entity = cad_push_entity(doc, CAD_LINE, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.line.x0 = x0;
    entity->geom.line.y0 = y0;
    entity->geom.line.x1 = x1;
    entity->geom.line.y1 = y1;

    float dx = x1 - x0;
    float dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);

    isogl_vertex pair[2] = {
        cad_vertex(doc, entity, x0, y0, 0.0f),
        cad_vertex(doc, entity, x1, y1, length)
    };

    cad_upload_entity(doc, entity, pair, 2);
    return entity->id;
}

uint32_t cad_add_circle(CadDocument *doc, int layer, isogl_line_type pattern,
                        isogl_line_weight weight, float cx, float cy, float r) {
    CadEntity *entity = cad_push_entity(doc, CAD_CIRCLE, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.circle.cx = cx;
    entity->geom.circle.cy = cy;
    entity->geom.circle.r = r;

    isogl_vertex vertices[CAD_CIRCLE_SEGMENTS * 2];
    float px = cx + r;
    float py = cy;
    float distance = 0.0f;

    for (int i = 0; i < CAD_CIRCLE_SEGMENTS; ++i) {
        float angle = (float)(i + 1) * (2.0f * (float)M_PI / CAD_CIRCLE_SEGMENTS);
        float nx = cx + r * cosf(angle);
        float ny = cy + r * sinf(angle);
        float dx = nx - px;
        float dy = ny - py;

        vertices[i * 2 + 0] = cad_vertex(doc, entity, px, py, distance);
        distance += sqrtf(dx * dx + dy * dy);
        vertices[i * 2 + 1] = cad_vertex(doc, entity, nx, ny, distance);

        px = nx;
        py = ny;
    }

    cad_upload_entity(doc, entity, vertices, CAD_CIRCLE_SEGMENTS * 2);
    return entity->id;
}

uint32_t cad_add_rectangle(CadDocument *doc, int layer, isogl_line_type pattern,
                           isogl_line_weight weight, float x0, float y0,
                           float x1, float y1) {
    CadEntity *entity = cad_push_entity(doc, CAD_RECTANGLE, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.rect.x0 = x0;
    entity->geom.rect.y0 = y0;
    entity->geom.rect.x1 = x1;
    entity->geom.rect.y1 = y1;

    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};

    isogl_vertex vertices[8];
    float distance = 0.0f;

    for (int i = 0; i < 4; ++i) {
        int next = (i + 1) & 3;
        float dx = xs[next] - xs[i];
        float dy = ys[next] - ys[i];

        vertices[i * 2 + 0] = cad_vertex(doc, entity, xs[i], ys[i], distance);
        distance += sqrtf(dx * dx + dy * dy);
        vertices[i * 2 + 1] = cad_vertex(doc, entity, xs[next], ys[next], distance);
    }

    cad_upload_entity(doc, entity, vertices, 8);
    return entity->id;
}

uint32_t cad_add_text(CadDocument *doc, int layer, float x, float y,
                      float height_mm, float angle, const char *text) {
    if (!text) return 0;

    CadEntity *entity = cad_push_entity(doc, CAD_TEXT, layer,
                                        LINE_TYPE_CONTINUOUS, LINE_THICKNESS_0_25);
    if (!entity) return 0;

    entity->geom.label.x = x;
    entity->geom.label.y = y;
    entity->geom.label.height = height_mm;
    entity->geom.label.angle = angle;
    snprintf(entity->geom.label.text, sizeof(entity->geom.label.text), "%s", text);

    cad_document_rebuild_text(doc);
    return entity->id;
}

void cad_document_rebuild_text(CadDocument *doc) {
    if (!doc || !doc->font) return;

    /* Rebuilding frees the string list, so per-string selection flags are
       lost.  Callers that keep a selection must reapply it by entity ID with
       cad_selection_apply() after this function returns. */
    isogl_string_list_free(&doc->strings);

    for (size_t i = 0; i < doc->count; ++i) {
        CadEntity *entity = &doc->entities[i];

        if (entity->type == CAD_TEXT) {
            isogl_glyph_string *string =
                isogl_create_glyph_string(entity->geom.label.text, doc->font);
            if (!string) continue;

            isogl_string_list_append(&doc->strings, string,
                                     entity->geom.label.x, entity->geom.label.y,
                                     entity->geom.label.height, doc->sheet_scale,
                                     entity->geom.label.angle, doc->font,
                                     entity->id);
        } else if (entity->type == CAD_DIMENSION) {
            CadDimensionGeometry g;
            if (cad_dimension_geometry(doc, &entity->geom.dimension, &g) != 0) {
                continue;
            }

            isogl_glyph_string *string =
                isogl_create_glyph_string(g.text, doc->font);
            if (!string) continue;

            double height_mm = entity->geom.dimension.text_height_mm > 0.0
                             ? entity->geom.dimension.text_height_mm : 3.5;
            isogl_string_list_append(&doc->strings, string,
                                     (float)g.text_x, (float)g.text_y,
                                     (float)height_mm, doc->sheet_scale,
                                     (float)g.text_angle, doc->font,
                                     entity->id);
        }
    }
}

void cad_dimension_init(CadDimension *dim) {
    if (!dim) return;
    memset(dim, 0, sizeof(*dim));
    dim->text_height_mm = 3.5;
    dim->precision = 1;
    snprintf(dim->unit, sizeof(dim->unit), " mm");
    dim->pick_text = 1;
}

static void cad_push_dimension_segment(isogl_vertex *vertices, size_t *count,
                                       const CadDocument *doc,
                                       const CadEntity *entity,
                                       double x0, double y0,
                                       double x1, double y1) {
    if (*count + 2 > 14) return;

    double len = hypot(x1 - x0, y1 - y0);
    vertices[*count] = cad_vertex(doc, entity, (float)x0, (float)y0, 0.0f);
    vertices[*count + 1] = cad_vertex(doc, entity, (float)x1, (float)y1,
                                      (float)len);
    *count += 2;
}

uint32_t cad_add_dimension(CadDocument *doc, int layer,
                           const CadDimension *dim) {
    if (!doc || !dim) return 0;

    CadDimensionGeometry g;
    if (cad_dimension_geometry(doc, dim, &g) != 0) return 0;

    int layer_index = layer;
    if (layer_index < 0) layer_index = 0;
    if (doc->layer_count > 0 && layer_index >= doc->layer_count) {
        layer_index = doc->layer_count - 1;
    }

    /* Dimension and extension lines follow the layer. */
    isogl_line_type pattern = doc->layers[layer_index].default_pattern;
    isogl_line_weight weight = doc->layers[layer_index].default_weight;

    CadEntity *entity = cad_push_entity(doc, CAD_DIMENSION, layer_index,
                                        pattern, weight);
    if (!entity) return 0;

    entity->geom.dimension = *dim;

    /* 7 segments: dimension line, 2 extension lines, 4 arrow legs. */
    isogl_vertex vertices[14];
    size_t count = 0;

    cad_push_dimension_segment(vertices, &count, doc, entity,
                               g.ax, g.ay, g.bx, g.by);
    cad_push_dimension_segment(vertices, &count, doc, entity,
                               dim->x0, dim->y0, g.e1x, g.e1y);
    cad_push_dimension_segment(vertices, &count, doc, entity,
                               dim->x1, dim->y1, g.e2x, g.e2y);
    for (int i = 0; i < g.arrow_count; ++i) {
        cad_push_dimension_segment(vertices, &count, doc, entity,
                                   g.arrow[i][0], g.arrow[i][1],
                                   g.arrow[i][2], g.arrow[i][3]);
    }

    cad_upload_entity(doc, entity, vertices, count);
    cad_document_rebuild_text(doc);
    return entity->id;
}
