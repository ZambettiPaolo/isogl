#include "cad.h"
#include "cad_geometry.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CAD_TWO_PI (2.0 * M_PI)

/* Paper-space chord tolerance converted to model units.  Zoom never enters:
   the model is in paper millimetres, so tessellation is scale, not view,
   driven. */
static double cad_model_tolerance(const CadDocument *doc) {
    double scale = (doc && doc->sheet_scale > 0.0f)
                 ? (double)doc->sheet_scale : 1.0;
    return CAD_CHORD_TOLERANCE_MM / scale;
}

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
    if (doc->next_id == 0) doc->next_id = 1;
    entity->id = doc->next_id++;
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

/*
 * Transient staging for a curve tessellation: the pure generator emits
 * consecutive points, this sink turns them into GL_LINES vertex pairs with
 * the entity style.  Nothing survives the call: cad_finish_tessellation()
 * uploads and frees it.
 */
typedef struct {
    const CadDocument *doc;
    const CadEntity *entity;
    isogl_vertex *vertices;
    size_t count;
    size_t capacity;
    int have_previous;
    double previous_x;
    double previous_y;
    double previous_distance;
} CadTessellationSink;

static int cad_sink_reserve(CadTessellationSink *sink, size_t extra) {
    if (sink->count + extra <= sink->capacity) return 0;
    size_t capacity = sink->capacity ? sink->capacity : 64;
    while (capacity < sink->count + extra) capacity *= 2;
    isogl_vertex *grown =
        realloc(sink->vertices, capacity * sizeof(*grown));
    if (!grown) return -1;
    sink->vertices = grown;
    sink->capacity = capacity;
    return 0;
}

static int cad_tessellation_sink(void *user, double x, double y,
                                 double distance) {
    CadTessellationSink *sink = user;

    if (!sink->have_previous) {
        sink->have_previous = 1;
        sink->previous_x = x;
        sink->previous_y = y;
        sink->previous_distance = distance;
        return 0;
    }

    if (cad_sink_reserve(sink, 2) != 0) return -1;

    sink->vertices[sink->count++] = cad_vertex(
        sink->doc, sink->entity, (float)sink->previous_x,
        (float)sink->previous_y, (float)sink->previous_distance);
    sink->vertices[sink->count++] = cad_vertex(
        sink->doc, sink->entity, (float)x, (float)y, (float)distance);

    sink->previous_x = x;
    sink->previous_y = y;
    sink->previous_distance = distance;
    return 0;
}

/*
 * Upload the staged pairs (or discard them when the pure generator failed)
 * and free the staging buffer.  Returns 0 on success.
 */
static int cad_finish_tessellation(CadDocument *doc, CadEntity *entity,
                                   CadTessellationSink *sink,
                                   int tessellation_rc) {
    int rc = tessellation_rc;
    if (rc == 0) {
        rc = cad_upload_entity(doc, entity, sink->vertices, sink->count);
    }
    free(sink->vertices);
    sink->vertices = NULL;
    sink->count = 0;
    sink->capacity = 0;
    return rc;
}

static void cad_discard_last_entity(CadDocument *doc) {
    if (doc && doc->count > 0) doc->count--;
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

/* ---- CAD dimension adapter around the shared symbol geometry ----------- */

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

/*
 * Rectangle predicate for arcs and ellipses: the pure generator is replayed
 * on demand at the print chord tolerance and each sampled segment is tested.
 * No VBO and no persistent segment copy is read.
 */
typedef struct {
    float xmin;
    float ymin;
    float xmax;
    float ymax;
    int contained;
    int intersects;
    int have_previous;
    double previous_x;
    double previous_y;
} CadCurveRectTest;

static int cad_curve_rect_sink(void *user, double x, double y,
                               double distance) {
    (void)distance;
    CadCurveRectTest *test = user;

    if (!cad_point_in_rect((float)x, (float)y, test->xmin, test->ymin,
                           test->xmax, test->ymax)) {
        test->contained = 0;
    }
    if (test->have_previous &&
        cad_segment_intersects_rect((float)test->previous_x,
                                    (float)test->previous_y,
                                    (float)x, (float)y,
                                    test->xmin, test->ymin,
                                    test->xmax, test->ymax)) {
        test->intersects = 1;
    }
    test->have_previous = 1;
    test->previous_x = x;
    test->previous_y = y;
    return 0;
}

static void cad_curve_select_rect(const CadDocument *doc, const CadEntity *e,
                                  float xmin, float ymin, float xmax,
                                  float ymax, int *contained, int *intersects) {
    CadCurveRectTest test;
    memset(&test, 0, sizeof(test));
    test.xmin = xmin;
    test.ymin = ymin;
    test.xmax = xmax;
    test.ymax = ymax;
    test.contained = 1;

    double tolerance = cad_model_tolerance(doc);
    int rc;
    if (e->type == CAD_ARC) {
        rc = cad_tessellate_arc(e->geom.arc.cx, e->geom.arc.cy,
                                e->geom.arc.r, e->geom.arc.a0,
                                e->geom.arc.sweep, tolerance,
                                cad_curve_rect_sink, &test);
    } else {
        rc = cad_tessellate_ellipse(e->geom.ellipse.cx, e->geom.ellipse.cy,
                                    e->geom.ellipse.rx, e->geom.ellipse.ry,
                                    e->geom.ellipse.rotation, tolerance,
                                    cad_curve_rect_sink, &test);
    }

    if (rc != 0) {
        /* Invalid stored parameters (e.g. non-finite): report nothing rather
           than a partial result.  The sink itself never allocates. */
        *contained = 0;
        *intersects = 0;
        return;
    }
    *contained = test.contained;
    *intersects = test.intersects;
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
            case CAD_ARC:
            case CAD_ELLIPSE:
                cad_curve_select_rect(doc, e, xmin, ymin, xmax, ymax,
                                      &is_contained, &intersects);
                break;
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
    doc->next_id = 1;
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

    /* Symbols are isogl-owned; the document keeps the ID, type pointer and an
       owned parameter copy. */
    for (size_t i = 0; i < doc->symbol_count; ++i) {
        isogl_symbol_destroy(doc->symbols[i].id);
        isogl_symbol_params_free(doc->symbols[i].params);
    }
    free(doc->symbols);
    doc->symbols = NULL;
    doc->symbol_count = 0;
    doc->symbol_capacity = 0;

    isogl_string_list_free(&doc->strings);
    isogl_vbo_array_free(&doc->parts);
    free(doc->entities);
    doc->entities = NULL;
    doc->count = 0;
    doc->capacity = 0;
}

int cad_document_add_symbol(CadDocument *doc, isogl_symbol_id symbol) {
    if (!doc || symbol == 0) return -1;

    /* The parameter copy is the document's view of the symbol.  If it cannot
       be obtained the symbol has no CAD-side record, so destroy it instead of
       leaving an untracked live object. */
    isogl_symbol_params *params = NULL;
    if (isogl_symbol_params_get(symbol, &params) != 0 || !params) {
        isogl_symbol_destroy(symbol);
        return -1;
    }

    if (doc->symbol_count == doc->symbol_capacity) {
        size_t capacity = doc->symbol_capacity ? doc->symbol_capacity * 2 : 8;
        CadSymbolRef *grown =
            realloc(doc->symbols, capacity * sizeof(*grown));
        if (!grown) {
            isogl_symbol_params_free(params);
            isogl_symbol_destroy(symbol);
            return -1;
        }
        doc->symbols = grown;
        doc->symbol_capacity = capacity;
    }

    CadSymbolRef *ref = &doc->symbols[doc->symbol_count++];
    ref->id = symbol;
    ref->type = params->type;
    ref->params = params;
    return 0;
}

CadSymbolRef *cad_symbol_find(CadDocument *doc, isogl_symbol_id symbol) {
    if (!doc || symbol == 0) return NULL;
    for (size_t i = 0; i < doc->symbol_count; ++i) {
        if (doc->symbols[i].id == symbol) return &doc->symbols[i];
    }
    return NULL;
}

const CadSymbolRef *cad_symbol_find_const(const CadDocument *doc,
                                          isogl_symbol_id symbol) {
    if (!doc || symbol == 0) return NULL;
    for (size_t i = 0; i < doc->symbol_count; ++i) {
        if (doc->symbols[i].id == symbol) return &doc->symbols[i];
    }
    return NULL;
}

isogl_symbol_params *cad_symbol_params(const CadSymbolRef *ref) {
    return ref ? ref->params : NULL;
}

int cad_symbol_update_params(CadDocument *doc, isogl_symbol_id symbol,
                             const isogl_symbol_params *params) {
    if (!doc || !params) return -1;

    CadSymbolRef *ref = cad_symbol_find(doc, symbol);
    if (!ref) return -1;

    if (isogl_symbol_params_update(symbol, params) != 0) return -1;

    /* Refresh the document copy from the registry so it carries the applied
       global scale.  The update already committed; if the readback fails the
       old copy is stale but the geometry and ID remain valid. */
    isogl_symbol_params *fresh = NULL;
    if (isogl_symbol_params_get(symbol, &fresh) != 0 || !fresh) return -1;

    isogl_symbol_params_free(ref->params);
    ref->params = fresh;
    ref->type = fresh->type;
    return 0;
}

int cad_remove_symbol(CadDocument *doc, isogl_symbol_id symbol) {
    if (!doc || symbol == 0) return -1;

    size_t index = doc->symbol_count;
    for (size_t i = 0; i < doc->symbol_count; ++i) {
        if (doc->symbols[i].id == symbol) {
            index = i;
            break;
        }
    }
    if (index == doc->symbol_count) return -1;

    /* Destroy first: once the registry entry is gone the record cannot be
       used, so the CAD copy is removed unconditionally afterwards. */
    int rc = isogl_symbol_destroy(symbol);
    isogl_symbol_params_free(doc->symbols[index].params);

    /* Preserve document order; the list is small and stable ordering keeps
       `sym list` deterministic. */
    if (index + 1 < doc->symbol_count) {
        memmove(&doc->symbols[index], &doc->symbols[index + 1],
                (doc->symbol_count - index - 1) * sizeof(doc->symbols[0]));
    }
    doc->symbol_count--;
    return rc;
}

static int cad_symbol_style(CadDocument *doc, int layer,
                            uint8_t *pattern, uint8_t *weight,
                            uint8_t *layer_out, float color[4]) {
    if (!doc || doc->layer_count <= 0) return -1;

    int index = layer;
    if (index < 0) index = 0;
    if (index >= doc->layer_count) index = doc->layer_count - 1;

    const CadLayer *l = &doc->layers[index];
    *pattern = (uint8_t)l->default_pattern;
    *weight = (uint8_t)l->default_weight;
    *layer_out = (uint8_t)index;
    memcpy(color, l->color, sizeof(float) * 4);
    return index;
}

static int cad_symbol_param_index(const isogl_symbol_params *params,
                                  const char *key) {
    if (!params || !params->params || !key) return -1;
    for (size_t i = 0; i < params->param_count; ++i) {
        const isogl_symbol_param *p = &params->params[i];
        if (p->desc && p->desc->key && strcmp(p->desc->key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int cad_symbol_set_number(isogl_symbol_params *params,
                                 const char *key, double value) {
    int index = cad_symbol_param_index(params, key);
    if (index < 0) return -1;
    params->params[index].value.number = value;
    return 0;
}

static int cad_symbol_set_integer(isogl_symbol_params *params,
                                  const char *key, int value) {
    int index = cad_symbol_param_index(params, key);
    if (index < 0) return -1;
    params->params[index].value.integer = value;
    return 0;
}

static int cad_symbol_set_boolean(isogl_symbol_params *params,
                                  const char *key, int value) {
    int index = cad_symbol_param_index(params, key);
    if (index < 0) return -1;
    params->params[index].value.boolean = value ? 1 : 0;
    return 0;
}

static int cad_symbol_set_point(isogl_symbol_params *params,
                                const char *key, size_t element,
                                double x, double y) {
    int index = cad_symbol_param_index(params, key);
    if (index < 0) return -1;
    isogl_symbol_param *p = &params->params[index];
    if (element >= p->count || !p->value.points) return -1;
    p->value.points[element].x = x;
    p->value.points[element].y = y;
    return 0;
}

static int cad_symbol_set_text(isogl_symbol_params *params,
                               const char *key, size_t element,
                               const char *text) {
    int index = cad_symbol_param_index(params, key);
    if (index < 0) return -1;
    return isogl_symbol_params_set_text(params, (size_t)index, element, text);
}

static int cad_symbol_set_color(isogl_symbol_params *params,
                                const char *key, const float color[4]) {
    int index = cad_symbol_param_index(params, key);
    if (index < 0) return -1;
    memcpy(params->params[index].value.color, color, sizeof(float) * 4);
    return 0;
}

isogl_symbol_id cad_add_dimension_symbol(CadDocument *doc, int layer,
                                         const IsoglDimensionDesc *desc) {
    if (!doc || !desc) return 0;

    IsoglDimensionDesc d = *desc;
    if (cad_symbol_style(doc, layer, &d.pattern, &d.weight, &d.layer,
                         d.color) < 0) {
        return 0;
    }
    d.sheet_scale = doc->sheet_scale;

    isogl_symbol_id id = isogl_symbol_create_dimension(&d);
    if (id == 0) return 0;
    /* On failure cad_document_add_symbol() already destroyed the symbol. */
    if (cad_document_add_symbol(doc, id) != 0) return 0;
    return id;
}

isogl_symbol_id cad_add_center_mark_symbol(CadDocument *doc, int layer,
                                           const IsoglCenterMarkDesc *desc) {
    if (!doc || !desc) return 0;

    IsoglCenterMarkDesc d = *desc;
    if (cad_symbol_style(doc, layer, &d.pattern, &d.weight, &d.layer,
                         d.color) < 0) {
        return 0;
    }
    d.sheet_scale = doc->sheet_scale;

    isogl_symbol_id id = isogl_symbol_create_center_mark(&d);
    if (id == 0) return 0;
    /* On failure cad_document_add_symbol() already destroyed the symbol. */
    if (cad_document_add_symbol(doc, id) != 0) return 0;
    return id;
}

static isogl_symbol_id cad_add_circular_symbol(CadDocument *doc, int layer,
                                               const char *type_name,
                                               const double points[3][2],
                                               const char *text_left,
                                               const char *text_right,
                                               double text_height_mm,
                                               int precision, int pick_text,
                                               const int *arrows,
                                               int arrow_count,
                                               uint8_t pattern,
                                               uint8_t weight,
                                               uint8_t layer_value,
                                               const float color[4]) {
    if (!doc || !type_name || !points) return 0;
    /* The style (including the layer tag) is already carried by the desc
       fields; the caller's layer argument is redundant. */
    (void)layer;

    isogl_symbol_params *params = isogl_symbol_params_create(type_name);
    if (!params) return 0;

    int ok = 1;
    for (size_t i = 0; i < 3; ++i) {
        if (cad_symbol_set_point(params, "points", i,
                                 points[i][0], points[i][1]) != 0) {
            ok = 0;
        }
    }
    if (cad_symbol_set_text(params, "texts", 0, text_left) != 0 ||
        cad_symbol_set_text(params, "texts", 1, text_right) != 0 ||
        cad_symbol_set_number(params, "text_height_mm",
                              text_height_mm) != 0 ||
        cad_symbol_set_integer(params, "precision", precision) != 0 ||
        cad_symbol_set_boolean(params, "pick_text", pick_text) != 0 ||
        cad_symbol_set_integer(params, "pattern", pattern) != 0 ||
        cad_symbol_set_integer(params, "weight", weight) != 0 ||
        cad_symbol_set_integer(params, "layer", layer_value) != 0 ||
        cad_symbol_set_color(params, "color", color) != 0) {
        ok = 0;
    }
    if (arrow_count == 2) {
        if (cad_symbol_set_integer(params, "arrow_left", arrows[0]) != 0 ||
            cad_symbol_set_integer(params, "arrow_right", arrows[1]) != 0) {
            ok = 0;
        }
    } else if (arrow_count == 1) {
        if (cad_symbol_set_integer(params, "arrow", arrows[0]) != 0) {
            ok = 0;
        }
    }

    isogl_symbol_id id = 0;
    if (ok) {
        id = isogl_symbol_create(type_name, params, doc->sheet_scale);
    }
    isogl_symbol_params_free(params);
    if (id == 0) return 0;
    /* On failure cad_document_add_symbol() already destroyed the symbol. */
    if (cad_document_add_symbol(doc, id) != 0) return 0;
    return id;
}

isogl_symbol_id cad_add_diameter_symbol(CadDocument *doc, int layer,
                                        const IsoglDiameterDesc *desc) {
    if (!doc || !desc) return 0;

    IsoglDiameterDesc d = *desc;
    if (cad_symbol_style(doc, layer, &d.pattern, &d.weight, &d.layer,
                         d.color) < 0) {
        return 0;
    }
    d.sheet_scale = doc->sheet_scale;

    const double points[3][2] = {
        {d.cx, d.cy}, {d.px, d.py}, {d.tx, d.ty}
    };
    const int arrows[2] = {(int)d.arrow_left, (int)d.arrow_right};
    return cad_add_circular_symbol(doc, layer, "dimension_diameter", points,
                                   d.text_left, d.text_right,
                                   d.text_height_mm, d.precision, d.pick_text,
                                   arrows, 2, d.pattern, d.weight, d.layer,
                                   d.color);
}

isogl_symbol_id cad_add_radius_symbol(CadDocument *doc, int layer,
                                      const IsoglRadiusDesc *desc) {
    if (!doc || !desc) return 0;

    IsoglRadiusDesc d = *desc;
    if (cad_symbol_style(doc, layer, &d.pattern, &d.weight, &d.layer,
                         d.color) < 0) {
        return 0;
    }
    d.sheet_scale = doc->sheet_scale;

    const double points[3][2] = {
        {d.cx, d.cy}, {d.px, d.py}, {d.tx, d.ty}
    };
    const int arrows[1] = {(int)d.arrow};
    return cad_add_circular_symbol(doc, layer, "dimension_radius", points,
                                   d.text_left, d.text_right,
                                   d.text_height_mm, d.precision, d.pick_text,
                                   arrows, 1, d.pattern, d.weight, d.layer,
                                   d.color);
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
    if (!doc || !isfinite(cx) || !isfinite(cy) || !isfinite(r) || r <= 0.0f) {
        return 0;
    }

    CadEntity *entity = cad_push_entity(doc, CAD_CIRCLE, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.circle.cx = cx;
    entity->geom.circle.cy = cy;
    entity->geom.circle.r = r;

    CadTessellationSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.doc = doc;
    sink.entity = entity;

    int rc = cad_tessellate_arc((double)cx, (double)cy, (double)r, 0.0,
                                CAD_TWO_PI, cad_model_tolerance(doc),
                                cad_tessellation_sink, &sink);
    uint32_t id = entity->id;
    if (cad_finish_tessellation(doc, entity, &sink, rc) != 0) {
        cad_discard_last_entity(doc);
        return 0;
    }
    return id;
}

uint32_t cad_add_ellipse(CadDocument *doc, int layer, isogl_line_type pattern,
                         isogl_line_weight weight, float p1x, float p1y,
                         float p2x, float p2y, float p3x, float p3y) {
    if (!doc) return 0;

    double cx, cy, rx, ry, rotation;
    if (cad_ellipse_from_axis((double)p1x, (double)p1y, (double)p2x,
                              (double)p2y, (double)p3x, (double)p3y,
                              &cx, &cy, &rx, &ry, &rotation) != 0) {
        return 0;
    }

    CadEntity *entity =
        cad_push_entity(doc, CAD_ELLIPSE, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.ellipse.cx = (float)cx;
    entity->geom.ellipse.cy = (float)cy;
    entity->geom.ellipse.rx = (float)rx;
    entity->geom.ellipse.ry = (float)ry;
    entity->geom.ellipse.rotation = (float)rotation;

    CadTessellationSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.doc = doc;
    sink.entity = entity;

    int rc = cad_tessellate_ellipse(cx, cy, rx, ry, rotation,
                                    cad_model_tolerance(doc),
                                    cad_tessellation_sink, &sink);
    uint32_t id = entity->id;
    if (cad_finish_tessellation(doc, entity, &sink, rc) != 0) {
        cad_discard_last_entity(doc);
        return 0;
    }
    return id;
}

uint32_t cad_add_arc_three_point(CadDocument *doc, int layer,
                                 isogl_line_type pattern,
                                 isogl_line_weight weight, float sx, float sy,
                                 float ex, float ey, float cx, float cy) {
    if (!doc) return 0;

    double radius, a0, sweep;
    if (cad_arc_three_point((double)sx, (double)sy, (double)ex, (double)ey,
                            (double)cx, (double)cy,
                            &radius, &a0, &sweep) != 0) {
        return 0;
    }

    CadEntity *entity =
        cad_push_entity(doc, CAD_ARC, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.arc.cx = cx;
    entity->geom.arc.cy = cy;
    entity->geom.arc.r = (float)radius;
    entity->geom.arc.a0 = (float)a0;
    entity->geom.arc.sweep = (float)sweep;

    CadTessellationSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.doc = doc;
    sink.entity = entity;

    int rc = cad_tessellate_arc((double)cx, (double)cy, radius, a0, sweep,
                                cad_model_tolerance(doc),
                                cad_tessellation_sink, &sink);
    uint32_t id = entity->id;
    if (cad_finish_tessellation(doc, entity, &sink, rc) != 0) {
        cad_discard_last_entity(doc);
        return 0;
    }
    return id;
}

uint32_t cad_add_arc_center_angles(CadDocument *doc, int layer,
                                   isogl_line_type pattern,
                                   isogl_line_weight weight, float cx,
                                   float cy, float radius, float angle1,
                                   float angle2) {
    if (!doc) return 0;

    double a0, sweep;
    if (cad_arc_center_angles((double)cx, (double)cy, (double)radius,
                              (double)angle1, (double)angle2,
                              &a0, &sweep) != 0) {
        return 0;
    }

    CadEntity *entity =
        cad_push_entity(doc, CAD_ARC, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.arc.cx = cx;
    entity->geom.arc.cy = cy;
    entity->geom.arc.r = radius;
    entity->geom.arc.a0 = (float)a0;
    entity->geom.arc.sweep = (float)sweep;

    CadTessellationSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.doc = doc;
    sink.entity = entity;

    int rc = cad_tessellate_arc((double)cx, (double)cy, (double)radius, a0,
                                sweep, cad_model_tolerance(doc),
                                cad_tessellation_sink, &sink);
    uint32_t id = entity->id;
    if (cad_finish_tessellation(doc, entity, &sink, rc) != 0) {
        cad_discard_last_entity(doc);
        return 0;
    }
    return id;
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

static CadEntity *cad_find_entity(CadDocument *doc, uint32_t id) {
    if (!doc || id == 0) return NULL;
    for (size_t i = 0; i < doc->count; ++i) {
        if (doc->entities[i].id == id) return &doc->entities[i];
    }
    return NULL;
}

static int cad_text_append(CadDocument *doc, const CadEntity *entity) {
    if (!doc || !entity || !doc->font) return -1;

    isogl_glyph_string *string =
        isogl_create_glyph_string(entity->geom.label.text, doc->font);
    if (!string) return -1;

    if (!isogl_string_list_append(&doc->strings, string,
                                  entity->geom.label.x,
                                  entity->geom.label.y,
                                  entity->geom.label.height,
                                  doc->sheet_scale,
                                  entity->geom.label.angle,
                                  doc->font, entity->id)) {
        isogl_free_glyph_string(string);
        return -1;
    }
    return 0;
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

    /* Incremental: only this entity's string is created. */
    cad_text_append(doc, entity);
    return entity->id;
}

int cad_update_text(CadDocument *doc, uint32_t entity_id, const char *text) {
    if (!doc || !text) return -1;

    CadEntity *entity = cad_find_entity(doc, entity_id);
    if (!entity || entity->type != CAD_TEXT) return -1;

    char old[sizeof(entity->geom.label.text)];
    memcpy(old, entity->geom.label.text, sizeof(old));
    snprintf(entity->geom.label.text, sizeof(entity->geom.label.text), "%s", text);

    int rc = isogl_string_list_update_by_pick_id(
        &doc->strings, entity_id, entity->geom.label.text, doc->font,
        entity->geom.label.x, entity->geom.label.y,
        entity->geom.label.height, doc->sheet_scale,
        entity->geom.label.angle);

    if (rc != 0) {
        memcpy(entity->geom.label.text, old, sizeof(old));
        return -1;
    }
    return 0;
}

int cad_set_text_transform(CadDocument *doc, uint32_t entity_id,
                           float x, float y, float height, float angle) {
    if (!doc) return -1;

    CadEntity *entity = cad_find_entity(doc, entity_id);
    if (!entity || entity->type != CAD_TEXT) return -1;

    float old_x = entity->geom.label.x;
    float old_y = entity->geom.label.y;
    float old_height = entity->geom.label.height;
    float old_angle = entity->geom.label.angle;

    entity->geom.label.x = x;
    entity->geom.label.y = y;
    entity->geom.label.height = height;
    entity->geom.label.angle = angle;

    int rc = isogl_string_list_set_transform_by_pick_id(
        &doc->strings, entity_id, doc->font, x, y, height,
        doc->sheet_scale, angle);

    if (rc != 0) {
        entity->geom.label.x = old_x;
        entity->geom.label.y = old_y;
        entity->geom.label.height = old_height;
        entity->geom.label.angle = old_angle;
        return -1;
    }
    return 0;
}

void cad_document_rebuild_text(CadDocument *doc) {
    if (!doc || !doc->font) return;

    /* Explicit global rebuild (font reload / load).  This frees the string
       list, so per-string selection flags are lost; callers that keep a
       selection must reapply it by entity ID.  Normal add/update paths do not
       call this function. */
    isogl_string_list_free(&doc->strings);

    for (size_t i = 0; i < doc->count; ++i) {
        CadEntity *entity = &doc->entities[i];
        if (entity->type != CAD_TEXT) continue;
        cad_text_append(doc, entity);
    }
}

/* ---- schema-v2 symbol section writer (save only) ----------------------- */

static int cad_write_double(FILE *out, double value) {
    if (!isfinite(value)) return -1;
    char buffer[64];
    int n = snprintf(buffer, sizeof(buffer), "%.17g", value);
    if (n < 0 || (size_t)n >= sizeof(buffer)) return -1;
    /* TOML distinguishes integers from floats: force a decimal point. */
    if (!strpbrk(buffer, ".eE")) {
        if ((size_t)n + 2 >= sizeof(buffer)) return -1;
        buffer[n++] = '.';
        buffer[n++] = '0';
        buffer[n] = '\0';
    }
    return fputs(buffer, out) < 0 ? -1 : 0;
}

static int cad_write_string(FILE *out, const char *text) {
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

static int cad_write_param(FILE *out, const isogl_symbol_param *p) {
    const isogl_symbol_param_desc *desc = p ? p->desc : NULL;
    if (!desc) return -1;

    if (fprintf(out, "%s = ", desc->key) < 0) return -1;
    int rc = 0;

    switch (desc->kind) {
        case ISOGL_SYM_PARAM_POINTS:
            if (fputc('[', out) == EOF) return -1;
            for (size_t i = 0; i < p->count; ++i) {
                if (i > 0 && fputc(',', out) == EOF) return -1;
                if (fputs("[", out) < 0 ||
                    cad_write_double(out, p->value.points[i].x) != 0 ||
                    fputs(", ", out) < 0 ||
                    cad_write_double(out, p->value.points[i].y) != 0 ||
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
                if (cad_write_string(out, p->value.texts[i]) != 0) return -1;
            }
            rc = fputc(']', out) == EOF ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_SEGMENTS:
            if (fputc('[', out) == EOF) return -1;
            for (size_t i = 0; i < p->count; ++i) {
                const isogl_symbol_segment *s = &p->value.segments[i];
                if (i > 0 && fputc(',', out) == EOF) return -1;
                if (fputs("[", out) < 0 ||
                    cad_write_double(out, s->x0) != 0 ||
                    fputs(", ", out) < 0 ||
                    cad_write_double(out, s->y0) != 0 ||
                    fputs(", ", out) < 0 ||
                    cad_write_double(out, s->x1) != 0 ||
                    fputs(", ", out) < 0 ||
                    cad_write_double(out, s->y1) != 0 ||
                    fputs("]", out) < 0) {
                    return -1;
                }
            }
            rc = fputc(']', out) == EOF ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_NUMBERS:
            if (fputc('[', out) == EOF) return -1;
            for (size_t i = 0; i < p->count; ++i) {
                if (i > 0 && fputs(", ", out) < 0) return -1;
                if (cad_write_double(out, p->value.numbers[i]) != 0) {
                    return -1;
                }
            }
            rc = fputc(']', out) == EOF ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_ENUM_LIST:
            if (fputc('[', out) == EOF) return -1;
            for (size_t i = 0; i < p->count; ++i) {
                if (i > 0 && fputs(", ", out) < 0) return -1;
                if (fprintf(out, "%d", p->value.integers[i]) < 0) {
                    return -1;
                }
            }
            rc = fputc(']', out) == EOF ? -1 : 0;
            break;

        case ISOGL_SYM_PARAM_NUMBER:
            rc = cad_write_double(out, p->value.number);
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
                if (cad_write_double(out, p->value.color[i]) != 0) return -1;
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

int cad_symbols_write_v2(FILE *out, const CadDocument *doc) {
    if (!out || !doc) return -1;

    for (size_t i = 0; i < doc->symbol_count; ++i) {
        const CadSymbolRef *ref = &doc->symbols[i];
        if (!ref->params || !ref->type || !ref->type->name) continue;

        if (fputs("[[symbols]]\ntype = ", out) < 0 ||
            cad_write_string(out, ref->type->name) != 0 ||
            fputs("\ngroup = 0\n\n[symbols.params]\n", out) < 0) {
            return -1;
        }

        for (size_t j = 0; j < ref->params->param_count; ++j) {
            const isogl_symbol_param *p = &ref->params->params[j];
            if (!p->desc) return -1;
            if (p->desc->kind == ISOGL_SYM_PARAM_SCALE) continue;
            if (cad_write_param(out, p) != 0) return -1;
            if (fputc('\n', out) == EOF) return -1;
        }
        if (fputc('\n', out) == EOF) return -1;
    }
    return ferror(out) ? -1 : 0;
}

int cad_save_symbols_v2(const char *path, const CadDocument *doc) {
    if (!path || !doc) return -1;
    FILE *out = fopen(path, "w");
    if (!out) return -1;
    int rc = cad_symbols_write_v2(out, doc);
    if (fclose(out) != 0) rc = -1;
    return rc;
}
