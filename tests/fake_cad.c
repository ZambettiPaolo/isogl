#include "fake_cad.h"

#include <isogl/isogl_font.h>
#include <toml.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FAKE_CIRCLE_SEGMENTS 48

typedef struct {
    double ox, oy;
    double angle;
    double scale;
} FakeXform;

/* ---- Small helpers ------------------------------------------------------ */

static int fake_group_index(const FakeDrawingSpec *s, int id) {
    for (size_t i = 0; i < s->group_count; ++i) {
        if (s->groups[i].id == id) return (int)i;
    }
    return -1;
}

static int fake_group_xform_rec(const FakeDrawingSpec *s, int gid,
                                int depth, FakeXform *out) {
    if (!s || !out || depth > FAKE_MAX_GROUPS) return -1;

    int index = fake_group_index(s, gid);
    if (index < 0) return -1;
    const FakeGroup *g = &s->groups[index];
    double local_scale = g->scale > 0.0 ? g->scale : 1.0;

    if (g->parent < 0) {
        out->ox = g->x;
        out->oy = g->y;
        out->angle = g->angle_deg * M_PI / 180.0;
        out->scale = local_scale;
        return 0;
    }

    FakeXform parent;
    if (fake_group_xform_rec(s, g->parent, depth + 1, &parent) != 0) {
        return -1;
    }

    double cs = cos(parent.angle);
    double sn = sin(parent.angle);
    double lx = parent.scale * g->x;
    double ly = parent.scale * g->y;

    out->ox = parent.ox + lx * cs - ly * sn;
    out->oy = parent.oy + lx * sn + ly * cs;
    out->angle = parent.angle + g->angle_deg * M_PI / 180.0;
    out->scale = parent.scale * local_scale;
    return 0;
}

static int fake_group_xform(const FakeDrawingSpec *s, int gid, FakeXform *out) {
    return fake_group_xform_rec(s, gid, 0, out);
}

static void fake_xform_point(const FakeXform *xf, double x, double y,
                             double *ax, double *ay) {
    double c = cos(xf->angle);
    double s = sin(xf->angle);
    *ax = xf->ox + xf->scale * (x * c - y * s);
    *ay = xf->oy + xf->scale * (x * s + y * c);
}

static void fake_inverse_point(const FakeXform *xf, double ax, double ay,
                               double *x, double *y) {
    double dx = ax - xf->ox;
    double dy = ay - xf->oy;
    double c = cos(-xf->angle);
    double s = sin(-xf->angle);
    double rx = dx * c - dy * s;
    double ry = dx * s + dy * c;
    double scale = xf->scale != 0.0 ? xf->scale : 1.0;
    *x = rx / scale;
    *y = ry / scale;
}

static isogl_vertex fake_vertex(const FakePrimitive *p, double x, double y,
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

static int fake_push_pair(isogl_segment_list *list, const FakePrimitive *p,
                          double ax, double ay, double bx, double by,
                          float start_distance, float *next_distance) {
    float length = (float)hypot(bx - ax, by - ay);
    isogl_vertex a = fake_vertex(p, ax, ay, start_distance);
    isogl_vertex b = fake_vertex(p, bx, by, start_distance + length);
    *next_distance = start_distance + length;
    return isogl_segment_list_push_pair(list, a, b);
}

/* ---- TOML loader -------------------------------------------------------- */

static double fake_double(toml_table_t *t, const char *key, double def) {
    toml_datum_t d = toml_double_in(t, key);
    return d.ok ? d.u.d : def;
}

static int fake_int(toml_table_t *t, const char *key, int def) {
    toml_datum_t d = toml_int_in(t, key);
    return d.ok ? (int)d.u.i : def;
}

static void fake_string(toml_table_t *t, const char *key, char *out,
                        size_t size, const char *def) {
    toml_datum_t d = toml_string_in(t, key);
    if (d.ok) {
        snprintf(out, size, "%s", d.u.s);
        free(d.u.s);
    } else {
        snprintf(out, size, "%s", def);
    }
}

static void fake_color(toml_table_t *t, float color[4]) {
    color[0] = color[1] = color[2] = color[3] = 1.0f;
    toml_array_t *array = toml_array_in(t, "color");
    if (!array) return;
    int n = toml_array_nelem(array);
    for (int i = 0; i < 4 && i < n; ++i) {
        toml_datum_t d = toml_double_at(array, i);
        if (d.ok) color[i] = (float)d.u.d;
    }
}

static void fake_style(toml_table_t *t, uint8_t *pattern, uint8_t *weight,
                       uint8_t *layer, float color[4]) {
    int p = fake_int(t, "pattern", 0);
    int w = fake_int(t, "weight", 3);
    int l = fake_int(t, "layer", 0);
    if (p < 0 || p >= LINE_TYPE_COUNT) p = 0;
    if (w < 0 || w >= LINE_THICKNESS_COUNT) w = 3;
    if (l < 0 || l > 31) l = 0;
    *pattern = (uint8_t)p;
    *weight = (uint8_t)w;
    *layer = (uint8_t)l;
    fake_color(t, color);
}

void fake_drawing_spec_init(FakeDrawingSpec *spec) {
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));
    spec->sheet_scale = 1.0;
}

int fake_drawing_load_toml(FakeDrawingSpec *spec, const char *path) {
    if (!spec || !path) return -1;

    fake_drawing_spec_init(spec);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char errbuf[256];
    toml_table_t *doc = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!doc) {
        fprintf(stderr, "fake_cad: TOML parse error: %s\n", errbuf);
        return -1;
    }

    toml_table_t *sheet = toml_table_in(doc, "sheet");
    if (sheet) {
        int num = fake_int(sheet, "scale_num", 1);
        int den = fake_int(sheet, "scale_den", 1);
        if (den != 0 && num > 0) {
            double scale = (double)num / (double)den;
            spec->sheet_scale = scale > 0.0 ? scale : 1.0;
        }
    }

    toml_array_t *groups = toml_array_in(doc, "groups");
    if (groups) {
        int n = toml_array_nelem(groups);
        for (int i = 0; i < n && spec->group_count < FAKE_MAX_GROUPS; ++i) {
            toml_table_t *t = toml_table_at(groups, i);
            if (!t) continue;
            FakeGroup *g = &spec->groups[spec->group_count++];
            memset(g, 0, sizeof(*g));
            g->id = fake_int(t, "id", i);
            g->parent = fake_int(t, "parent", -1);
            fake_string(t, "name", g->name, sizeof(g->name), "group");
            g->x = fake_double(t, "x", 0.0);
            g->y = fake_double(t, "y", 0.0);
            g->angle_deg = fake_double(t, "angle_deg", 0.0);
            g->scale = fake_double(t, "scale", 1.0);
        }
    }
    if (spec->group_count == 0) {
        toml_free(doc);
        return -1;
    }

    toml_array_t *prims = toml_array_in(doc, "primitives");
    if (prims) {
        int n = toml_array_nelem(prims);
        for (int i = 0; i < n && spec->primitive_count < FAKE_MAX_PRIMITIVES; ++i) {
            toml_table_t *t = toml_table_at(prims, i);
            if (!t) continue;

            FakePrimitive *p = &spec->primitives[spec->primitive_count++];
            memset(p, 0, sizeof(*p));
            p->group = fake_int(t, "group", 0);
            fake_style(t, &p->pattern, &p->weight, &p->layer, p->color);

            char type[32];
            fake_string(t, "type", type, sizeof(type), "line");
            if (strcmp(type, "line") == 0) {
                p->type = FAKE_PRIM_LINE;
                p->p[0] = fake_double(t, "x0", 0.0);
                p->p[1] = fake_double(t, "y0", 0.0);
                p->p[2] = fake_double(t, "x1", 0.0);
                p->p[3] = fake_double(t, "y1", 0.0);
            } else if (strcmp(type, "rect") == 0 ||
                       strcmp(type, "rectangle") == 0) {
                p->type = FAKE_PRIM_RECT;
                p->p[0] = fake_double(t, "x0", 0.0);
                p->p[1] = fake_double(t, "y0", 0.0);
                p->p[2] = fake_double(t, "x1", 0.0);
                p->p[3] = fake_double(t, "y1", 0.0);
            } else if (strcmp(type, "circle") == 0) {
                p->type = FAKE_PRIM_CIRCLE;
                p->p[0] = fake_double(t, "cx", 0.0);
                p->p[1] = fake_double(t, "cy", 0.0);
                p->p[2] = fake_double(t, "r", 1.0);
            } else if (strcmp(type, "text") == 0) {
                p->type = FAKE_PRIM_TEXT;
                p->p[0] = fake_double(t, "x", 0.0);
                p->p[1] = fake_double(t, "y", 0.0);
                p->p[2] = fake_double(t, "height_mm", 5.0);
                p->p[3] = fake_double(t, "angle_deg", 0.0) * M_PI / 180.0;
                fake_string(t, "text", p->text, sizeof(p->text), "");
            } else {
                spec->primitive_count--;
            }
        }
    }

    toml_array_t *symbols = toml_array_in(doc, "symbols");
    if (symbols) {
        int n = toml_array_nelem(symbols);
        for (int i = 0; i < n && spec->symbol_count < FAKE_MAX_SYMBOLS; ++i) {
            toml_table_t *t = toml_table_at(symbols, i);
            if (!t) continue;

            FakeSymbolSpec *s = &spec->symbols[spec->symbol_count++];
            memset(s, 0, sizeof(*s));
            s->group = fake_int(t, "group", 0);

            char type[32];
            fake_string(t, "type", type, sizeof(type), "dimension");
            if (strcmp(type, "dimension") == 0) {
                s->type = FAKE_SYMBOL_DIMENSION;
                IsoglDimensionDesc *d = &s->dimension;
                d->x0 = fake_double(t, "p1x", 0.0);
                d->y0 = fake_double(t, "p1y", 0.0);
                d->x1 = fake_double(t, "p2x", 0.0);
                d->y1 = fake_double(t, "p2y", 0.0);
                d->px = fake_double(t, "p3x", 0.0);
                d->py = fake_double(t, "p3y", 0.0);
                d->text_height_mm = fake_double(t, "text_height_mm", 3.5);
                d->sheet_scale = fake_double(t, "sheet_scale",
                                             spec->sheet_scale);
                d->distance = fake_double(t, "distance", 0.0);
                d->text_offset_mm = fake_double(t, "text_offset_mm", 0.0);
                fake_string(t, "text_left", d->text_left,
                            sizeof(d->text_left), "");
                fake_string(t, "text_right", d->text_right,
                            sizeof(d->text_right), "");
                d->arrow_left = (IsoglDimensionArrow)fake_int(
                    t, "arrow_left", ISOGL_DIM_ARROW_OPEN);
                d->arrow_right = (IsoglDimensionArrow)fake_int(
                    t, "arrow_right", ISOGL_DIM_ARROW_OPEN);
                d->precision = fake_int(t, "precision", 1);
                d->pick_text = fake_int(t, "pick_text", 1);
                d->mark_points = fake_int(t, "mark_points", 0);
                fake_style(t, &d->pattern, &d->weight, &d->layer, d->color);
            } else if (strcmp(type, "center_mark") == 0 ||
                       strcmp(type, "center") == 0) {
                s->type = FAKE_SYMBOL_CENTER_MARK;
                IsoglCenterMarkDesc *d = &s->center_mark;
                d->cx = fake_double(t, "cx", 0.0);
                d->cy = fake_double(t, "cy", 0.0);
                d->size_mm = fake_double(t, "size_mm", 8.0);
                d->sheet_scale = fake_double(t, "sheet_scale",
                                             spec->sheet_scale);
                fake_style(t, &d->pattern, &d->weight, &d->layer, d->color);
            } else {
                spec->symbol_count--;
            }
        }
    }

    toml_free(doc);
    return 0;
}

/* ---- Graphics: hierarchy -> absolute VBO/text --------------------------- */

static int fake_build_group(FakeDrawing *drawing, const FakeGroup *g,
                            const FakeXform *xf,
                            isogl_segment_list *list) {
    for (size_t i = 0; i < drawing->spec.primitive_count; ++i) {
        const FakePrimitive *p = &drawing->spec.primitives[i];
        if (p->group != g->id) continue;

        switch (p->type) {
            case FAKE_PRIM_LINE: {
                double ax, ay, bx, by;
                fake_xform_point(xf, p->p[0], p->p[1], &ax, &ay);
                fake_xform_point(xf, p->p[2], p->p[3], &bx, &by);
                float distance = 0.0f;
                if (fake_push_pair(list, p, ax, ay, bx, by,
                                   distance, &distance) != 0) {
                    return -1;
                }
                break;
            }
            case FAKE_PRIM_RECT: {
                double x0 = p->p[0], y0 = p->p[1];
                double x1 = p->p[2], y1 = p->p[3];
                double lx[4] = {x0, x1, x1, x0};
                double ly[4] = {y0, y0, y1, y1};
                double ax[4], ay[4];
                for (int k = 0; k < 4; ++k) {
                    fake_xform_point(xf, lx[k], ly[k], &ax[k], &ay[k]);
                }
                float distance = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    int n = (k + 1) & 3;
                    if (fake_push_pair(list, p, ax[k], ay[k], ax[n], ay[n],
                                       distance, &distance) != 0) {
                        return -1;
                    }
                }
                break;
            }
            case FAKE_PRIM_CIRCLE: {
                double cx, cy;
                fake_xform_point(xf, p->p[0], p->p[1], &cx, &cy);
                double r = p->p[2] * fabs(xf->scale);
                float distance = 0.0f;
                double px = cx + r, py = cy;
                for (int k = 0; k < FAKE_CIRCLE_SEGMENTS; ++k) {
                    double a = (double)(k + 1) * 2.0 * M_PI /
                               (double)FAKE_CIRCLE_SEGMENTS;
                    double nx = cx + r * cos(a);
                    double ny = cy + r * sin(a);
                    if (fake_push_pair(list, p, px, py, nx, ny,
                                       distance, &distance) != 0) {
                        return -1;
                    }
                    px = nx;
                    py = ny;
                }
                break;
            }
            case FAKE_PRIM_TEXT: {
                if (!drawing->font) break;
                double ax, ay;
                fake_xform_point(xf, p->p[0], p->p[1], &ax, &ay);
                isogl_glyph_string *string =
                    isogl_create_glyph_string(p->text, drawing->font);
                if (!string) return -1;
                if (!isogl_string_list_append(&drawing->strings, string,
                                              (float)ax, (float)ay,
                                              (float)p->p[2],
                                              (float)drawing->spec.sheet_scale,
                                              (float)(xf->angle + p->p[3]),
                                              drawing->font,
                                              FAKE_CAD_PICK_BASE +
                                              (uint32_t)g->id)) {
                    isogl_free_glyph_string(string);
                    return -1;
                }
                break;
            }
        }
    }
    return 0;
}

void fake_drawing_rebuild_graphics(FakeDrawing *drawing) {
    if (!drawing) return;

    if (drawing->graphics_valid) {
        isogl_string_list_free(&drawing->strings);
        isogl_vbo_array_free(&drawing->parts);
        drawing->graphics_valid = 0;
    }

    if (isogl_vbo_array_init(&drawing->parts) != 0) return;
    memset(&drawing->strings, 0, sizeof(drawing->strings));
    drawing->graphics_valid = 1;

    for (size_t gi = 0; gi < drawing->spec.group_count; ++gi) {
        const FakeGroup *g = &drawing->spec.groups[gi];
        FakeXform xf;
        if (fake_group_xform(&drawing->spec, g->id, &xf) != 0) continue;

        isogl_segment_list list;
        if (isogl_segment_list_init(&list, 32) != 0) return;

        if (fake_build_group(drawing, g, &xf, &list) != 0) {
            isogl_segment_list_free(&list);
            continue;
        }

        if (list.size > 0) {
            GLuint vbo = isogl_vbo_array_add(&drawing->parts, &list,
                                             ISOGL_VBO_SEGMENT);
            if (vbo != 0) {
                isogl_vbo_array_set_pick_id(&drawing->parts, vbo,
                                            FAKE_CAD_PICK_BASE +
                                            (uint32_t)g->id);
            }
        }
        isogl_segment_list_free(&list);
    }
}

int fake_drawing_build(FakeDrawing *drawing, const FakeDrawingSpec *spec,
                       isogl_font_data *font) {
    if (!drawing || !spec || !font) return -1;

    memset(drawing, 0, sizeof(*drawing));
    drawing->spec = *spec;
    drawing->font = font;
    fake_drawing_rebuild_graphics(drawing);
    return drawing->graphics_valid ? 0 : -1;
}

/* ---- Symbols ------------------------------------------------------------ */

int fake_drawing_create_symbols(FakeDrawing *drawing) {
    if (!drawing) return -1;

    for (size_t i = 0; i < drawing->spec.symbol_count; ++i) {
        const FakeSymbolSpec *spec = &drawing->spec.symbols[i];
        FakeXform xf;
        if (fake_group_xform(&drawing->spec, spec->group, &xf) != 0) {
            return -1;
        }

        isogl_symbol_id id = 0;
        if (spec->type == FAKE_SYMBOL_DIMENSION) {
            IsoglDimensionDesc d = spec->dimension;
            fake_xform_point(&xf, spec->dimension.x0, spec->dimension.y0,
                             &d.x0, &d.y0);
            fake_xform_point(&xf, spec->dimension.x1, spec->dimension.y1,
                             &d.x1, &d.y1);
            fake_xform_point(&xf, spec->dimension.px, spec->dimension.py,
                             &d.px, &d.py);
            id = isogl_symbol_create_dimension(&d);
        } else {
            IsoglCenterMarkDesc d = spec->center_mark;
            fake_xform_point(&xf, spec->center_mark.cx, spec->center_mark.cy,
                             &d.cx, &d.cy);
            id = isogl_symbol_create_center_mark(&d);
        }

        if (id == 0) return -1;
        drawing->refs[drawing->ref_count].group = spec->group;
        drawing->refs[drawing->ref_count].symbol = id;
        drawing->ref_count++;
    }
    return 0;
}

void fake_drawing_draw(FakeDrawing *drawing) {
    if (!drawing || !drawing->graphics_valid) return;

    isogl_draw_all_vbo_objects(&drawing->parts);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    isogl_draw_string_list(&drawing->strings, isogl_shader_program_font,
                           isogl_model_matrix, isogl_view_matrix, identity);
    isogl_symbol_draw_all();
}

int fake_drawing_pick_symbols(FakeDrawing *drawing,
                              float px, float py, int extent_px,
                              uint32_t *ids, size_t max, size_t *count) {
    (void)drawing;
    return isogl_symbol_pick_at(px, py, extent_px, ids, max, count);
}

int fake_drawing_select_symbols(FakeDrawing *drawing, const isogl_box *query,
                                int crossing,
                                uint32_t *ids, size_t max, size_t *count) {
    if (!drawing || !query || !count) return -1;
    size_t written = 0;

    for (size_t i = 0; i < drawing->ref_count; ++i) {
        int hit = isogl_symbol_hit(drawing->refs[i].symbol, query, crossing);
        if (hit == 1 && written < max) {
            ids[written++] = drawing->refs[i].symbol;
        }
    }
    *count = written;
    return 0;
}

typedef struct {
    FakeDrawing *drawing;
    const FakeXform *xf;
    int group;
    size_t start;
    size_t segments;
    size_t texts;
    int failed;
} FakeExplodeCtx;

static int fake_explode_emit(void *user, const IsoglSymItem *item) {
    FakeExplodeCtx *ctx = user;
    if (ctx->drawing->spec.primitive_count >= FAKE_MAX_PRIMITIVES) {
        ctx->failed = 1;
        return -1;
    }

    FakePrimitive *p =
        &ctx->drawing->spec.primitives[ctx->drawing->spec.primitive_count++];
    memset(p, 0, sizeof(*p));
    p->group = ctx->group;
    p->pattern = item->pattern;
    p->weight = item->weight;
    p->layer = item->layer;
    memcpy(p->color, item->color, sizeof(p->color));

    if (item->kind == ISOGL_SYM_SEGMENT) {
        fake_inverse_point(ctx->xf, item->x0, item->y0, &p->p[0], &p->p[1]);
        fake_inverse_point(ctx->xf, item->x1, item->y1, &p->p[2], &p->p[3]);
        p->type = FAKE_PRIM_LINE;
        ctx->segments++;
    } else if (item->kind == ISOGL_SYM_TEXT) {
        double x, y;
        fake_inverse_point(ctx->xf, item->x, item->y, &x, &y);
        p->type = FAKE_PRIM_TEXT;
        p->p[0] = x;
        p->p[1] = y;
        p->p[2] = item->height_mm;
        p->p[3] = item->angle - ctx->xf->angle;
        if (item->utf8) snprintf(p->text, sizeof(p->text), "%s", item->utf8);
        ctx->texts++;
    }
    return 0;
}

int fake_drawing_explode_symbol(FakeDrawing *drawing, size_t ref_index,
                                size_t *segments_out, size_t *texts_out) {
    if (!drawing || ref_index >= drawing->ref_count) return -1;

    FakeSymbolRef ref = drawing->refs[ref_index];
    FakeXform xf;
    if (fake_group_xform(&drawing->spec, ref.group, &xf) != 0) return -1;

    FakeExplodeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.drawing = drawing;
    ctx.xf = &xf;
    ctx.group = ref.group;
    ctx.start = drawing->spec.primitive_count;

    IsoglSymEmitter emitter = {&ctx, fake_explode_emit};
    if (isogl_symbol_explode(ref.symbol, &emitter) != 0 || ctx.failed) {
        drawing->spec.primitive_count = ctx.start;
        return -1;
    }

    for (size_t i = ref_index; i + 1 < drawing->ref_count; ++i) {
        drawing->refs[i] = drawing->refs[i + 1];
    }
    drawing->ref_count--;

    fake_drawing_rebuild_graphics(drawing);
    if (segments_out) *segments_out = ctx.segments;
    if (texts_out) *texts_out = ctx.texts;
    return 0;
}

void fake_drawing_free(FakeDrawing *drawing) {
    if (!drawing) return;

    for (size_t i = 0; i < drawing->ref_count; ++i) {
        isogl_symbol_destroy(drawing->refs[i].symbol);
    }
    drawing->ref_count = 0;

    if (drawing->graphics_valid) {
        isogl_string_list_free(&drawing->strings);
        isogl_vbo_array_free(&drawing->parts);
        drawing->graphics_valid = 0;
    }
}
