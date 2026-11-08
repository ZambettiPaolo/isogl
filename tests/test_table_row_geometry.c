/*
 * Headless tests for the pure "table_row" symbol geometry.
 *
 * No GL, no SDL and no isogl_symbol.c link: the test builds isogl_symbol_params
 * manually from the type descriptor and calls the pure generator with a
 * deterministic metric stub.
 */

#include "isogl_symbol_builtins.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEGMENTS 64
#define MAX_TEXTS 16
#define MAX_PICKS 64

static int failures;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int near(double a, double b) {
    return fabs(a - b) < 1e-9;
}

static int param_index(const isogl_symbol_params *params, const char *key) {
    if (!params || !key) return -1;
    for (size_t i = 0; i < params->param_count; ++i) {
        if (params->params[i].desc &&
            strcmp(params->params[i].desc->key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int type_param_index(const isogl_symbol_type *type, const char *key) {
    if (!type || !type->params || !key) return -1;
    for (size_t i = 0; i < type->param_count; ++i) {
        if (type->params[i].key && strcmp(type->params[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int kind_is_list(isogl_symbol_param_kind kind) {
    return kind == ISOGL_SYM_PARAM_POINTS ||
           kind == ISOGL_SYM_PARAM_TEXTS ||
           kind == ISOGL_SYM_PARAM_SEGMENTS ||
           kind == ISOGL_SYM_PARAM_NUMBERS ||
           kind == ISOGL_SYM_PARAM_ENUM_LIST;
}

static int prepare_params(isogl_symbol_params *params,
                          const isogl_symbol_type *type, double scale) {
    memset(params, 0, sizeof(*params));
    params->type = type;
    params->param_count = type->param_count;
    params->sheet_scale = scale > 0.0 ? scale : 1.0;
    params->params = calloc(type->param_count, sizeof(isogl_symbol_param));
    if (!params->params) return -1;

    for (size_t i = 0; i < type->param_count; ++i) {
        const isogl_symbol_param_desc *desc = &type->params[i];
        isogl_symbol_param *p = &params->params[i];
        p->desc = desc;

        if (kind_is_list(desc->kind)) {
            size_t count = desc->min_count;
            p->count = count;
            if (count == 0) continue;
            switch (desc->kind) {
                case ISOGL_SYM_PARAM_POINTS:
                    p->value.points = calloc(count, sizeof(isogl_symbol_point));
                    break;
                case ISOGL_SYM_PARAM_SEGMENTS:
                    p->value.segments =
                        calloc(count, sizeof(isogl_symbol_segment));
                    break;
                case ISOGL_SYM_PARAM_NUMBERS:
                    p->value.numbers = calloc(count, sizeof(double));
                    break;
                case ISOGL_SYM_PARAM_ENUM_LIST:
                    p->value.integers = calloc(count, sizeof(int));
                    break;
                case ISOGL_SYM_PARAM_TEXTS:
                    p->value.texts = calloc(count, sizeof(char *));
                    if (p->value.texts) {
                        for (size_t j = 0; j < count; ++j) {
                            p->value.texts[j] = calloc(1, 1);
                        }
                    }
                    break;
                default:
                    return -1;
            }
            if (!p->value.points) return -1;
        } else {
            p->count = 1;
            if (desc->kind == ISOGL_SYM_PARAM_COLOR) {
                for (int j = 0; j < 4; ++j) p->value.color[j] = 1.0f;
            } else if (desc->kind == ISOGL_SYM_PARAM_SCALE) {
                p->value.number = params->sheet_scale;
            }
        }
    }

    if (type->init_params && type->init_params(params) != 0) return -1;
    return 0;
}

static void free_params(isogl_symbol_params *params) {
    if (!params || !params->params) return;
    for (size_t i = 0; i < params->param_count; ++i) {
        isogl_symbol_param *p = &params->params[i];
        if (!p->desc) continue;
        switch (p->desc->kind) {
            case ISOGL_SYM_PARAM_POINTS:
                free(p->value.points);
                break;
            case ISOGL_SYM_PARAM_SEGMENTS:
                free(p->value.segments);
                break;
            case ISOGL_SYM_PARAM_NUMBERS:
                free(p->value.numbers);
                break;
            case ISOGL_SYM_PARAM_ENUM_LIST:
                free(p->value.integers);
                break;
            case ISOGL_SYM_PARAM_TEXTS:
                for (size_t j = 0; j < p->count; ++j) {
                    free(p->value.texts[j]);
                }
                free(p->value.texts);
                break;
            default:
                break;
        }
    }
    free(params->params);
    memset(params, 0, sizeof(*params));
}

static void rebuild_numbers(isogl_symbol_param *p, const double *values,
                            size_t count) {
    free(p->value.numbers);
    p->value.numbers = calloc(count, sizeof(double));
    p->count = count;
    for (size_t i = 0; i < count; ++i) p->value.numbers[i] = values[i];
}

static void rebuild_integers(isogl_symbol_param *p, const int *values,
                             size_t count) {
    free(p->value.integers);
    p->value.integers = calloc(count, sizeof(int));
    p->count = count;
    for (size_t i = 0; i < count; ++i) p->value.integers[i] = values[i];
}

static void rebuild_texts(isogl_symbol_param *p, const char *const *values,
                          size_t count) {
    for (size_t i = 0; i < p->count; ++i) free(p->value.texts[i]);
    free(p->value.texts);
    p->value.texts = calloc(count, sizeof(char *));
    p->count = count;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(values[i]) + 1;
        p->value.texts[i] = malloc(length);
        if (p->value.texts[i]) memcpy(p->value.texts[i], values[i], length);
    }
}

/* ---- deterministic metric ---------------------------------------------- */

typedef struct {
    double scale;
} StubMetric;

static double stub_text_width(void *user, const char *utf8, double height_mm) {
    const StubMetric *stub = user;
    if (!stub || !utf8) return 0.0;
    /* One model unit per character at 1 mm on paper. */
    return (double)strlen(utf8) * height_mm / stub->scale;
}

/* ---- collectors --------------------------------------------------------- */

typedef struct {
    IsoglSymItem segments[MAX_SEGMENTS];
    size_t segment_count;
    struct {
        char text[128];
        double x, y, height_mm, angle;
        int align;
        uint8_t pattern, weight, layer;
        float color[4];
    } texts[MAX_TEXTS];
    size_t text_count;
    IsoglSymPickItem picks[MAX_PICKS];
    size_t pick_count;
} Collector;

static int collect_item(void *user, const IsoglSymItem *item) {
    Collector *c = user;
    if (!c || !item) return -1;

    if (item->kind == ISOGL_SYM_SEGMENT) {
        if (c->segment_count >= MAX_SEGMENTS) return -1;
        c->segments[c->segment_count++] = *item;
        return 0;
    }
    if (item->kind == ISOGL_SYM_TEXT) {
        if (c->text_count >= MAX_TEXTS) return -1;
        size_t index = c->text_count++;
        snprintf(c->texts[index].text, sizeof(c->texts[index].text), "%s",
                 item->utf8 ? item->utf8 : "");
        c->texts[index].x = item->x;
        c->texts[index].y = item->y;
        c->texts[index].height_mm = item->height_mm;
        c->texts[index].angle = item->angle;
        c->texts[index].align = item->align;
        c->texts[index].pattern = item->pattern;
        c->texts[index].weight = item->weight;
        c->texts[index].layer = item->layer;
        memcpy(c->texts[index].color, item->color,
               sizeof(c->texts[index].color));
        return 0;
    }
    return 0;
}

static int collect_pick(void *user, const IsoglSymPickItem *item) {
    Collector *c = user;
    if (!c || !item) return -1;
    if (c->pick_count >= MAX_PICKS) return -1;
    c->picks[c->pick_count++] = *item;
    return 0;
}

/* ---- fixtures ----------------------------------------------------------- */

static void set_three_cells(isogl_symbol_params *params,
                            const unsigned char *alignments) {
    int wi = param_index(params, "widths");
    int ai = param_index(params, "alignments");
    int ti = param_index(params, "texts");
    static const double widths[3] = {20.0, 30.0, 15.0};
    static const char *const texts[3] = {"AB", "C", "DE"};
    int aligns[3] = {(int)alignments[0], (int)alignments[1],
                     (int)alignments[2]};

    rebuild_numbers(&params->params[wi], widths, 3);
    rebuild_integers(&params->params[ai], aligns, 3);
    rebuild_texts(&params->params[ti], texts, 3);

    params->params[param_index(params, "height_mm")].value.number = 8.0;
    params->params[param_index(params, "text_height_mm")].value.number = 2.5;
    params->params[param_index(params, "padding_mm")].value.number = 0.5;
    params->params[param_index(params, "pick_text")].value.boolean = 1;
    params->params[param_index(params, "pattern")].value.integer =
        LINE_TYPE_DASHED;
    params->params[param_index(params, "weight")].value.integer =
        LINE_THICKNESS_0_5;
    params->params[param_index(params, "layer")].value.integer = 5;
    float color[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    memcpy(params->params[param_index(params, "color")].value.color, color,
           sizeof(color));
    isogl_symbol_point *origin =
        params->params[param_index(params, "points")].value.points;
    origin[0].x = 10.0;
    origin[0].y = 20.0;
}

int main(void) {
    const isogl_symbol_type *type = isogl_symbol_table_row_type();
    check(type != NULL, "table_row type accessor returns a descriptor");
    if (!type) return 1;

    check(type->name && strcmp(type->name, "table_row") == 0,
          "type name is table_row");
    check(type->params && type->param_count > 0,
          "type exposes a parameter schema");
    check(type_param_index(type, "points") >= 0, "schema has points");
    check(type_param_index(type, "widths") >= 0 &&
          type_param_index(type, "alignments") >= 0 &&
          type_param_index(type, "texts") >= 0,
          "schema exposes the cell list parameters");

    isogl_symbol_params params;
    check(prepare_params(&params, type, 1.0) == 0,
          "parameters built from the descriptor");
    check(type->validate(&params) == 0, "default parameters validate");

    /* ---- defaults ------------------------------------------------------- */
    int height_i = param_index(&params, "height_mm");
    int text_height_i = param_index(&params, "text_height_mm");
    int padding_i = param_index(&params, "padding_mm");
    int widths_i = param_index(&params, "widths");
    int align_i = param_index(&params, "alignments");
    int pick_i = param_index(&params, "pick_text");
    check(params.params[height_i].value.number == 8.0,
          "default row height is 8 mm");
    check(params.params[text_height_i].value.number == 2.5,
          "default text height is 2.5 mm");
    check(params.params[padding_i].value.number == 0.5,
          "default padding is 0.5 mm");
    check(params.params[widths_i].count == 1 &&
          params.params[widths_i].value.numbers[0] == 20.0,
          "default cell width is 20 mm");
    check(params.params[align_i].count == 1 &&
          params.params[align_i].value.integers[0] == 0,
          "default alignment is left");
    check(params.params[pick_i].value.boolean == 1,
          "default pick_text is true");

    /* ---- scale 1 -------------------------------------------------------- */
    unsigned char aligns_left_center_right[3] = {0, 1, 2};
    set_three_cells(&params, aligns_left_center_right);
    check(type->validate(&params) == 0, "three-cell parameters validate");

    isogl_symbol_gen_context ctx = {NULL, 1.0};
    StubMetric stub = {1.0};
    IsoglSymMetrics metrics = {&stub, stub_text_width};

    Collector c;
    memset(&c, 0, sizeof(c));
    IsoglSymEmitter emitter = {&c, collect_item};
    int rc = isogl_table_row_geometry(&params, &ctx, &metrics, &emitter);
    check(rc == 0, "table row geometry generates at scale 1");
    check(c.segment_count == 6,
          "two borders plus N+1 = four vertical separators");
    check(c.text_count == 3, "one text run per cell");

    check(near(c.segments[0].x0, 10.0) && near(c.segments[0].y0, 20.0) &&
          near(c.segments[0].x1, 75.0) && near(c.segments[0].y1, 20.0),
          "bottom border spans the cumulative widths");
    check(near(c.segments[1].x0, 10.0) && near(c.segments[1].y0, 28.0) &&
          near(c.segments[1].x1, 75.0) && near(c.segments[1].y1, 28.0),
          "top border is one row height above the bottom");
    check(near(c.segments[2].x0, 10.0) && near(c.segments[2].x1, 10.0) &&
          near(c.segments[3].x0, 30.0) && near(c.segments[4].x0, 60.0) &&
          near(c.segments[5].x0, 75.0),
          "vertical separators sit at 10, 30, 60, 75");

    check(strcmp(c.texts[0].text, "AB") == 0 &&
          near(c.texts[0].x, 10.5),
          "left-aligned text starts one padding from the cell edge");
    check(strcmp(c.texts[1].text, "C") == 0 &&
          near(c.texts[1].x, 43.75),
          "centre-aligned text is centred in its cell");
    check(strcmp(c.texts[2].text, "DE") == 0 &&
          near(c.texts[2].x, 69.5),
          "right-aligned text ends one padding from the cell edge");
    check(near(c.texts[0].y, 22.75) && near(c.texts[1].y, 22.75) &&
          near(c.texts[2].y, 22.75),
          "text baseline is vertically centred in the row");
    check(near(c.texts[0].height_mm, 2.5),
          "text height stays a paper measurement");

    int style_ok = 1;
    for (size_t i = 0; i < c.segment_count; ++i) {
        if (c.segments[i].pattern != LINE_TYPE_DASHED ||
            c.segments[i].weight != LINE_THICKNESS_0_5 ||
            c.segments[i].layer != 5) {
            style_ok = 0;
        }
    }
    for (size_t i = 0; i < c.text_count; ++i) {
        if (c.texts[i].pattern != LINE_TYPE_DASHED ||
            c.texts[i].weight != LINE_THICKNESS_0_5 ||
            c.texts[i].layer != 5) {
            style_ok = 0;
        }
    }
    check(style_ok, "every emitted item carries the row style");

    /* ---- pick geometry -------------------------------------------------- */
    memset(&c, 0, sizeof(c));
    IsoglSymPickEmitter pick_emitter = {&c, collect_pick};
    rc = isogl_table_row_pick_geometry(&params, &ctx, &metrics, &pick_emitter);
    check(rc == 0, "pick geometry generates");
    check(c.pick_count == 9,
          "pick geometry has 6 segments and 3 text OBBs");
    const IsoglSymPickItem *obb = &c.picks[c.pick_count - 3];
    check(obb->kind == ISOGL_SYM_PICK_OBB &&
          near(obb->corners[0][0], 10.5) &&
          near(obb->corners[0][1], 22.75) &&
          near(obb->corners[1][0], 15.5) &&
          near(obb->corners[2][1], 25.25),
          "text OBB follows the measured text width and height");

    params.params[pick_i].value.boolean = 0;
    memset(&c, 0, sizeof(c));
    rc = isogl_table_row_pick_geometry(&params, &ctx, &metrics, &pick_emitter);
    check(rc == 0 && c.pick_count == 6,
          "pick_text = 0 excludes the text OBBs");
    params.params[pick_i].value.boolean = 1;

    /* ---- paper to model conversion with sheet_scale = 2 ----------------- */
    ctx.sheet_scale = 2.0;
    stub.scale = 2.0;
    memset(&c, 0, sizeof(c));
    rc = isogl_table_row_geometry(&params, &ctx, &metrics, &emitter);
    check(rc == 0, "table row geometry generates at sheet scale 2");
    check(near(c.segments[0].x1, 42.5) && near(c.segments[1].y0, 24.0),
          "row height and widths are divided by the sheet scale");
    check(near(c.texts[0].x, 10.25) &&
          near(c.texts[0].y, 21.375),
          "padding and vertical centring use model units");
    check(near(c.texts[1].x, 26.875) && near(c.texts[2].x, 39.75),
          "centre and right alignment use the scaled text width");

    /* ---- validation ----------------------------------------------------- */
    int texts_i = param_index(&params, "texts");
    size_t saved_count = params.params[texts_i].count;
    params.params[texts_i].count = saved_count - 1;
    check(type->validate(&params) != 0,
          "mismatched widths/alignments/texts counts are rejected");
    params.params[texts_i].count = saved_count;

    double saved_width = params.params[widths_i].value.numbers[0];
    params.params[widths_i].value.numbers[0] = 0.0;
    memset(&c, 0, sizeof(c));
    check(type->validate(&params) != 0 &&
          isogl_table_row_geometry(&params, &ctx, &metrics, &emitter) != 0,
          "a non-positive cell width is rejected");
    params.params[widths_i].value.numbers[0] = saved_width;

    free_params(&params);

    if (failures == 0) {
        printf("PASSED: table row geometry tests\n");
        return 0;
    }
    printf("FAILED: %d table row geometry check(s)\n", failures);
    return 1;
}
