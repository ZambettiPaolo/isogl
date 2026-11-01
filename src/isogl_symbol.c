#include "isogl_symbol.h"
#include "isogl_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    ISOGL_SYMBOL_DIMENSION = 0,
    ISOGL_SYMBOL_CENTER_MARK = 1
} IsoglSymbolType;

typedef struct {
    int alive;
    int visible;
    int selected;
    IsoglSymbolType type;
    union {
        IsoglDimensionDesc dimension;
        IsoglCenterMarkDesc center_mark;
    } desc;
    GLuint vbo_id;
} IsoglSymbol;

static IsoglSymbol *symbols = NULL;
static size_t symbol_slots = 0;       /* highest assigned ID */
static size_t symbol_capacity = 0;
static uint32_t next_symbol_id = 1;
static isogl_vbo_array symbol_parts;
static isogl_string_list symbol_strings;
static isogl_font_data *symbol_font = NULL;
static int symbol_initialized = 0;

static IsoglSymbol *sym_get(isogl_symbol_id id) {
    if (id == 0 || (size_t)id > symbol_slots) return NULL;
    IsoglSymbol *s = &symbols[id - 1];
    return s->alive ? s : NULL;
}

static int sym_ensure_slot(uint32_t id) {
    if ((size_t)id <= symbol_slots) return 0;

    if ((size_t)id > symbol_capacity) {
        size_t capacity = symbol_capacity ? symbol_capacity : 16;
        while (capacity < (size_t)id) {
            if (capacity > SIZE_MAX / 2) return -1;
            capacity *= 2;
        }
        IsoglSymbol *grown = realloc(symbols, capacity * sizeof(*symbols));
        if (!grown) return -1;
        symbols = grown;
        symbol_capacity = capacity;
    }

    memset(symbols + symbol_slots, 0,
           ((size_t)id - symbol_slots) * sizeof(*symbols));
    symbol_slots = (size_t)id;
    return 0;
}

static IsoglSymbol *sym_new_record(isogl_symbol_id *out_id) {
    uint32_t id = next_symbol_id++;
    if (sym_ensure_slot(id) != 0) return NULL;

    IsoglSymbol *s = &symbols[id - 1];
    memset(s, 0, sizeof(*s));
    s->alive = 1;
    s->visible = 1;
    s->vbo_id = 0;
    *out_id = id;
    return s;
}

static double sym_text_width(void *user, const char *utf8, double height_mm) {
    const IsoglDimensionDesc *d = user;
    if (!symbol_font || !utf8 || height_mm <= 0.0) return 0.0;

    double font_height = (double)symbol_font->height;
    double scale = (d && d->sheet_scale > 0.0) ? d->sheet_scale : 1.0;
    if (font_height <= 0.0) return 0.0;

    return isogl_font_text_advance(symbol_font, utf8) * height_mm /
           (font_height * scale);
}

typedef struct {
    isogl_segment_list segments;
    int has_text;
    char text[128];
    double text_x, text_y, text_height_mm, text_angle;
    double sheet_scale;
} SymBuild;

static int sym_build_item(void *user, const IsoglSymItem *item) {
    SymBuild *b = user;
    if (!item) return -1;

    if (item->kind == ISOGL_SYM_SEGMENT) {
        isogl_vertex a;
        isogl_vertex c;
        memset(&a, 0, sizeof(a));
        a.position[0] = (float)item->x0;
        a.position[1] = (float)item->y0;
        memcpy(a.color, item->color, sizeof(a.color));
        a.distanceFromStart = 0.0f;
        a.pk_thickness_pattern_layer =
            isogl_pack(item->weight, item->pattern, item->layer);

        c = a;
        c.position[0] = (float)item->x1;
        c.position[1] = (float)item->y1;
        c.distanceFromStart =
            (float)hypot(item->x1 - item->x0, item->y1 - item->y0);

        return isogl_segment_list_push_pair(&b->segments, a, c);
    }

    if (item->kind == ISOGL_SYM_TEXT) {
        if (item->utf8) {
            snprintf(b->text, sizeof(b->text), "%s", item->utf8);
        }
        b->text_x = item->x;
        b->text_y = item->y;
        b->text_height_mm = item->height_mm;
        b->text_angle = item->angle;
        b->has_text = 1;
    }
    return 0;
}

static int sym_build_geometry(IsoglSymbolType type, const void *desc,
                              SymBuild *build) {
    if (!desc || !build) return -1;

    memset(build, 0, sizeof(*build));
    if (isogl_segment_list_init(&build->segments, 16) != 0) return -1;

    build->sheet_scale = (type == ISOGL_SYMBOL_DIMENSION)
                       ? ((const IsoglDimensionDesc *)desc)->sheet_scale
                       : ((const IsoglCenterMarkDesc *)desc)->sheet_scale;
    if (!(build->sheet_scale > 0.0)) build->sheet_scale = 1.0;

    IsoglSymEmitter emitter = {build, sym_build_item};
    IsoglSymMetrics metrics = {(void *)desc, sym_text_width};
    int rc = (type == ISOGL_SYMBOL_DIMENSION)
           ? isogl_dimension_geometry(desc, &metrics, &emitter)
           : isogl_center_mark_geometry(desc, &emitter);

    if (rc != 0 || (build->segments.size == 0 && !build->has_text)) {
        isogl_segment_list_free(&build->segments);
        return -1;
    }
    return 0;
}

static isogl_symbol_id sym_create_common(const void *desc,
                                         IsoglSymbolType type) {
    if (!symbol_initialized || !symbol_font || !desc) return 0;

    SymBuild build;
    if (sym_build_geometry(type, desc, &build) != 0) return 0;

    isogl_symbol_id id = 0;
    IsoglSymbol *s = sym_new_record(&id);
    if (!s) {
        isogl_segment_list_free(&build.segments);
        return 0;
    }

    s->type = type;
    if (type == ISOGL_SYMBOL_DIMENSION) {
        s->desc.dimension = *(const IsoglDimensionDesc *)desc;
    } else {
        s->desc.center_mark = *(const IsoglCenterMarkDesc *)desc;
    }

    GLuint vbo = 0;
    if (build.segments.size > 0) {
        vbo = isogl_vbo_array_add(&symbol_parts, &build.segments,
                                  ISOGL_VBO_SEGMENT);
        if (vbo == 0) {
            isogl_segment_list_free(&build.segments);
            s->alive = 0;
            return 0;
        }
        isogl_vbo_array_set_pick_id(&symbol_parts, vbo, id);
    }
    isogl_segment_list_free(&build.segments);
    s->vbo_id = vbo;

    if (build.has_text) {
        isogl_glyph_string *string =
            isogl_create_glyph_string(build.text, symbol_font);
        if (!string) {
            if (vbo) isogl_vbo_array_remove(&symbol_parts, vbo);
            s->alive = 0;
            s->vbo_id = 0;
            return 0;
        }

        if (!isogl_string_list_append(&symbol_strings, string,
                                      (float)build.text_x,
                                      (float)build.text_y,
                                      (float)build.text_height_mm,
                                      (float)build.sheet_scale,
                                      (float)build.text_angle,
                                      symbol_font, id)) {
            isogl_free_glyph_string(string);
            if (vbo) isogl_vbo_array_remove(&symbol_parts, vbo);
            s->alive = 0;
            s->vbo_id = 0;
            return 0;
        }
    }

    s->vbo_id = vbo;
    s->alive = 1;
    return id;
}

static int sym_update_common(isogl_symbol_id id, IsoglSymbolType type,
                             const void *desc) {
    IsoglSymbol *s = sym_get(id);
    if (!s || s->type != type || !desc ||
        !symbol_initialized || !symbol_font) {
        return -1;
    }

    SymBuild build;
    if (sym_build_geometry(type, desc, &build) != 0) return -1;

    if (build.segments.size > 0) {
        if (s->vbo_id != 0) {
            if (isogl_vbo_array_update(&symbol_parts, s->vbo_id,
                                       &build.segments) != 0) {
                isogl_segment_list_free(&build.segments);
                return -1;
            }
        } else {
            GLuint vbo = isogl_vbo_array_add(&symbol_parts, &build.segments,
                                             ISOGL_VBO_SEGMENT);
            if (vbo == 0) {
                isogl_segment_list_free(&build.segments);
                return -1;
            }
            isogl_vbo_array_set_pick_id(&symbol_parts, vbo, id);
            s->vbo_id = vbo;
        }
    } else if (s->vbo_id != 0) {
        isogl_vbo_array_remove(&symbol_parts, s->vbo_id);
        s->vbo_id = 0;
    }
    isogl_segment_list_free(&build.segments);

    if (build.has_text) {
        int same_text = 0;
        for (uint32_t i = 0; i < symbol_strings.glyph_string_count; ++i) {
            isogl_glyph_string *old = &symbol_strings.glyph_strings[i];
            if (old->pick_id != id) continue;
            same_text = old->text && strcmp(old->text, build.text) == 0;
            break;
        }

        int rc;
        if (same_text) {
            rc = isogl_string_list_set_transform_by_pick_id(
                &symbol_strings, id, symbol_font,
                (float)build.text_x, (float)build.text_y,
                (float)build.text_height_mm, (float)build.sheet_scale,
                (float)build.text_angle);
        } else {
            rc = isogl_string_list_update_by_pick_id(
                &symbol_strings, id, build.text, symbol_font,
                (float)build.text_x, (float)build.text_y,
                (float)build.text_height_mm, (float)build.sheet_scale,
                (float)build.text_angle);
        }
        if (rc != 0) return -1;
    } else {
        isogl_string_list_remove_by_pick_id(&symbol_strings, id);
    }

    if (type == ISOGL_SYMBOL_DIMENSION) {
        s->desc.dimension = *(const IsoglDimensionDesc *)desc;
    } else {
        s->desc.center_mark = *(const IsoglCenterMarkDesc *)desc;
    }
    return 0;
}

int isogl_symbol_init(isogl_font_data *font) {
    if (symbol_initialized) {
        if (font) symbol_font = font;
        return 0;
    }
    if (!font) return -1;
    if (isogl_vbo_array_init(&symbol_parts) != 0) return -1;

    memset(&symbol_strings, 0, sizeof(symbol_strings));
    symbols = NULL;
    symbol_slots = 0;
    symbol_capacity = 0;
    next_symbol_id = 1;
    symbol_font = font;
    symbol_initialized = 1;
    return 0;
}

void isogl_symbol_cleanup(void) {
    if (!symbol_initialized) return;

    isogl_string_list_free(&symbol_strings);
    isogl_vbo_array_free(&symbol_parts);
    free(symbols);
    symbols = NULL;
    symbol_slots = 0;
    symbol_capacity = 0;
    next_symbol_id = 1;
    symbol_font = NULL;
    symbol_initialized = 0;
}

isogl_symbol_id isogl_symbol_create_dimension(const IsoglDimensionDesc *d) {
    return sym_create_common(d, ISOGL_SYMBOL_DIMENSION);
}

isogl_symbol_id isogl_symbol_create_center_mark(const IsoglCenterMarkDesc *d) {
    return sym_create_common(d, ISOGL_SYMBOL_CENTER_MARK);
}

int isogl_symbol_update_dimension(isogl_symbol_id id,
                                  const IsoglDimensionDesc *d) {
    return sym_update_common(id, ISOGL_SYMBOL_DIMENSION, d);
}

int isogl_symbol_update_center_mark(isogl_symbol_id id,
                                    const IsoglCenterMarkDesc *d) {
    return sym_update_common(id, ISOGL_SYMBOL_CENTER_MARK, d);
}

int isogl_symbol_destroy(isogl_symbol_id id) {
    IsoglSymbol *s = sym_get(id);
    if (!s) return -1;

    if (s->vbo_id != 0) {
        isogl_vbo_array_remove(&symbol_parts, s->vbo_id);
        s->vbo_id = 0;
    }
    isogl_string_list_remove_by_pick_id(&symbol_strings, id);

    s->alive = 0;
    s->visible = 0;
    s->selected = 0;
    return 0;
}

void isogl_symbol_draw_all(void) {
    if (!symbol_initialized) return;

    isogl_draw_all_vbo_objects(&symbol_parts);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    isogl_draw_string_list(&symbol_strings, isogl_shader_program_font,
                           isogl_model_matrix, isogl_view_matrix, identity);
}

int isogl_symbol_pick_at(float px, float py, int extent_px,
                         uint32_t *ids, size_t max, size_t *count) {
    if (!symbol_initialized) {
        if (count) *count = 0;
        return -1;
    }
    return isogl_pick_at(&symbol_parts, &symbol_strings,
                         px, py, extent_px, ids, max, count);
}

int isogl_symbol_get_selected(isogl_symbol_id id) {
    IsoglSymbol *s = sym_get(id);
    return s ? s->selected : -1;
}

int isogl_symbol_set_selected(isogl_symbol_id id, int selected) {
    IsoglSymbol *s = sym_get(id);
    if (!s) return -1;

    s->selected = selected ? 1 : 0;

    if (s->vbo_id != 0) {
        isogl_vbo_object *object =
            isogl_vbo_array_get(&symbol_parts, s->vbo_id);
        if (object) object->selected = s->selected;
    }

    for (uint32_t i = 0; i < symbol_strings.glyph_string_count; ++i) {
        if (symbol_strings.glyph_strings[i].pick_id == id) {
            symbol_strings.glyph_strings[i].selected = s->selected;
        }
    }
    return 0;
}

/* ---- Pick/bounds predicates (double, model units) ---------------------- */

static int sym_box_point(const isogl_box *b, double x, double y) {
    return x >= b->xmin && x <= b->xmax && y >= b->ymin && y <= b->ymax;
}

static int sym_segment_intersects_box(double ax, double ay,
                                      double bx, double by,
                                      const isogl_box *b) {
    if (sym_box_point(b, ax, ay) || sym_box_point(b, bx, by)) return 1;

    double dx = bx - ax;
    double dy = by - ay;
    double p[4] = {-dx, dx, -dy, dy};
    double q[4] = {ax - b->xmin, b->xmax - ax,
                   ay - b->ymin, b->ymax - ay};
    double t0 = 0.0;
    double t1 = 1.0;

    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return 0;
        } else {
            double r = q[i] / p[i];
            if (p[i] < 0.0) {
                if (r > t0) t0 = r;
            } else {
                if (r < t1) t1 = r;
            }
            if (t0 > t1) return 0;
        }
    }
    return 1;
}

static int sym_point_in_obb(double px, double py, const double c[4][2]) {
    int positive = 0;
    int negative = 0;
    for (int i = 0; i < 4; ++i) {
        double ax = c[i][0];
        double ay = c[i][1];
        double bx = c[(i + 1) & 3][0];
        double by = c[(i + 1) & 3][1];
        double cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
        if (cross > 1e-9) positive = 1;
        else if (cross < -1e-9) negative = 1;
        if (positive && negative) return 0;
    }
    return 1;
}

static int sym_obb_contained(const double c[4][2], const isogl_box *b) {
    for (int i = 0; i < 4; ++i) {
        if (!sym_box_point(b, c[i][0], c[i][1])) return 0;
    }
    return 1;
}

static int sym_obb_intersects_box(const double c[4][2],
                                  const isogl_box *b) {
    for (int i = 0; i < 4; ++i) {
        int n = (i + 1) & 3;
        if (sym_segment_intersects_box(c[i][0], c[i][1],
                                       c[n][0], c[n][1], b)) {
            return 1;
        }
    }

    double rx[4] = {b->xmin, b->xmax, b->xmax, b->xmin};
    double ry[4] = {b->ymin, b->ymin, b->ymax, b->ymax};
    for (int i = 0; i < 4; ++i) {
        if (sym_point_in_obb(rx[i], ry[i], c)) return 1;
    }
    return 0;
}

typedef struct {
    isogl_box query;
    int crossing;
    int contained;
    int intersects;
    int has_bounds;
    double xmin, ymin, xmax, ymax;
} SymHitContext;

static void sym_bounds_point(SymHitContext *c, double x, double y) {
    if (!c->has_bounds) {
        c->xmin = c->xmax = x;
        c->ymin = c->ymax = y;
        c->has_bounds = 1;
        return;
    }
    if (x < c->xmin) c->xmin = x;
    if (x > c->xmax) c->xmax = x;
    if (y < c->ymin) c->ymin = y;
    if (y > c->ymax) c->ymax = y;
}

static int sym_collect_pick(void *user, const IsoglSymPickItem *item) {
    SymHitContext *c = user;
    if (!item) return -1;

    if (item->kind == ISOGL_SYM_PICK_SEGMENT) {
        int contained = sym_box_point(&c->query, item->x0, item->y0) &&
                        sym_box_point(&c->query, item->x1, item->y1);
        int intersects = sym_segment_intersects_box(item->x0, item->y0,
                                                    item->x1, item->y1,
                                                    &c->query);
        if (c->crossing) {
            if (contained || intersects) c->intersects = 1;
        } else if (!contained) {
            c->contained = 0;
        }
        sym_bounds_point(c, item->x0, item->y0);
        sym_bounds_point(c, item->x1, item->y1);
        return 0;
    }

    if (item->kind == ISOGL_SYM_PICK_OBB) {
        int contained = sym_obb_contained(item->corners, &c->query);
        int intersects = sym_obb_intersects_box(item->corners, &c->query);
        if (c->crossing) {
            if (contained || intersects) c->intersects = 1;
        } else if (!contained) {
            c->contained = 0;
        }
        for (int i = 0; i < 4; ++i) {
            sym_bounds_point(c, item->corners[i][0], item->corners[i][1]);
        }
        return 0;
    }
    return 0;
}

static void sym_normalize_box(isogl_box *b) {
    if (b->xmin > b->xmax) {
        double t = b->xmin; b->xmin = b->xmax; b->xmax = t;
    }
    if (b->ymin > b->ymax) {
        double t = b->ymin; b->ymin = b->ymax; b->ymax = t;
    }
}

static int sym_collect_pick_geometry(IsoglSymbol *s, SymHitContext *ctx) {
    IsoglSymPickEmitter emitter = {ctx, sym_collect_pick};
    IsoglSymMetrics metrics = {&s->desc.dimension, sym_text_width};

    if (s->type == ISOGL_SYMBOL_DIMENSION) {
        return isogl_dimension_pick_geometry(&s->desc.dimension,
                                             &metrics, &emitter);
    }
    return isogl_center_mark_pick_geometry(&s->desc.center_mark, &emitter);
}

int isogl_symbol_hit(isogl_symbol_id id, const isogl_box *query, int crossing) {
    IsoglSymbol *s = sym_get(id);
    if (!s || !query) return -1;

    SymHitContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.query = *query;
    sym_normalize_box(&ctx.query);
    ctx.crossing = crossing ? 1 : 0;
    ctx.contained = 1;

    if (sym_collect_pick_geometry(s, &ctx) != 0 || !ctx.has_bounds) {
        return -1;
    }
    return ctx.crossing ? ctx.intersects : ctx.contained;
}

int isogl_symbol_bounds(isogl_symbol_id id, isogl_box *out) {
    IsoglSymbol *s = sym_get(id);
    if (!s || !out) return -1;

    SymHitContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.contained = 1;

    if (sym_collect_pick_geometry(s, &ctx) != 0 || !ctx.has_bounds) {
        return -1;
    }
    out->xmin = ctx.xmin;
    out->ymin = ctx.ymin;
    out->xmax = ctx.xmax;
    out->ymax = ctx.ymax;
    return 0;
}

int isogl_symbol_explode(isogl_symbol_id id, IsoglSymEmitter *out) {
    IsoglSymbol *s = sym_get(id);
    if (!s || !out || !out->emit) return -1;

    IsoglSymMetrics metrics = {&s->desc.dimension, sym_text_width};
    int rc = (s->type == ISOGL_SYMBOL_DIMENSION)
           ? isogl_dimension_geometry(&s->desc.dimension, &metrics, out)
           : isogl_center_mark_geometry(&s->desc.center_mark, out);
    if (rc != 0) return -1;

    return isogl_symbol_destroy(id);
}
