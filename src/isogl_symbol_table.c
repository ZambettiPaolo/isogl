/*
 * Table-row symbol type ("table_row").
 *
 * Interpretation of the parameters (agreed with the CAD model):
 *
 *   - points[0] is the bottom-left origin of the row, in model units;
 *   - height_mm is the row height on paper, converted to model units with
 *     model = paper / sheet_scale;
 *   - the row has N cells.  Cell i has widths[i] on paper, an alignment
 *     (left / centre / right) and a UTF-8 text;
 *   - text_height_mm is the text height on paper, baseline vertically
 *     centred: y = bottom + (height - text_height) / 2, all in model units;
 *   - padding_mm is the horizontal text inset from the cell edge on paper;
 *   - widths.count == alignments.count == texts.count and every width is
 *     greater than zero;
 *   - pattern / weight / layer / color style every emitted primitive;
 *   - scale is the read-only global sheet scale.
 *
 * Geometry: the top and bottom borders plus N+1 vertical separators at the
 * cumulative widths.  Each cell emits one text run at the baseline-left point
 * implied by its alignment; the width comes from metrics->text_width so the
 * same callback drives render and pick.
 *
 * This file is pure C: no GL, no SDL, no CAD document access.
 */

#include "isogl_symbol_builtins.h"
#include "isogl_font_data.h"
#include "isogl_symbol_enums.h"

#include <math.h>
#include <string.h>

#define TABLE_ALIGN_LEFT   0
#define TABLE_ALIGN_CENTER 1
#define TABLE_ALIGN_RIGHT  2
#define TABLE_ALIGN_COUNT  3

static const isogl_symbol_enum_value TABLE_ALIGN_VALUES[TABLE_ALIGN_COUNT] = {
    {TABLE_ALIGN_LEFT, "left", "Sinistra"},
    {TABLE_ALIGN_CENTER, "center", "Centro"},
    {TABLE_ALIGN_RIGHT, "right", "Destra"}
};

enum {
    TBL_POINTS = 0,
    TBL_HEIGHT_MM,
    TBL_TEXT_HEIGHT_MM,
    TBL_PADDING_MM,
    TBL_WIDTHS,
    TBL_ALIGNMENTS,
    TBL_TEXTS,
    TBL_PICK_TEXT,
    TBL_PATTERN,
    TBL_WEIGHT,
    TBL_LAYER,
    TBL_COLOR,
    TBL_SCALE,
    TBL_PARAM_COUNT
};

static const isogl_symbol_param_desc TABLE_PARAMS[TBL_PARAM_COUNT] = {
    [TBL_POINTS] = {
        "points", "Origine", "Angolo in basso a sinistra, unità modello",
        "mm", ISOGL_SYM_PARAM_POINTS, 1, 1, 1
    },
    [TBL_HEIGHT_MM] = {
        "height_mm", "Altezza riga", "Altezza della riga su carta", "mm",
        ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [TBL_TEXT_HEIGHT_MM] = {
        "text_height_mm", "Altezza testo", "Altezza ISO del testo su carta",
        "mm", ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [TBL_PADDING_MM] = {
        "padding_mm", "Padding", "Distanza orizzontale del testo dal bordo",
        "mm", ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [TBL_WIDTHS] = {
        "widths", "Larghezze celle", "Una larghezza su carta per cella", "mm",
        ISOGL_SYM_PARAM_NUMBERS, 1, 0, 1
    },
    [TBL_ALIGNMENTS] = {
        "alignments", "Allineamenti", "Uno per cella: left/center/right", "",
        ISOGL_SYM_PARAM_ENUM_LIST, 1, 0, 1,
        TABLE_ALIGN_VALUES, TABLE_ALIGN_COUNT
    },
    [TBL_TEXTS] = {
        "texts", "Testi delle celle", "Un testo UTF-8 per cella", "",
        ISOGL_SYM_PARAM_TEXTS, 1, 0, 1
    },
    [TBL_PICK_TEXT] = {
        "pick_text", "Testo selezionabile", "Include l'OBB dei testi", "",
        ISOGL_SYM_PARAM_BOOLEAN, 1, 1, 1
    },
    [TBL_PATTERN] = {
        "pattern", "Tipo linea ISO", "Pattern di bordi e separatori", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_type_values, ISOGL_SYM_LINE_TYPE_VALUE_COUNT
    },
    [TBL_WEIGHT] = {
        "weight", "Peso ISO", "Spessore di bordi e separatori", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_weight_values, ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT
    },
    [TBL_LAYER] = {
        "layer", "Layer", "Layer di stile", "",
        ISOGL_SYM_PARAM_INTEGER, 1, 1, 1
    },
    [TBL_COLOR] = {
        "color", "Colore", "Colore di stampa", "",
        ISOGL_SYM_PARAM_COLOR, 1, 1, 1
    },
    [TBL_SCALE] = {
        "scale", "Scala foglio", "Sola lettura; rigenera il simbolo", "1:n",
        ISOGL_SYM_PARAM_SCALE, 1, 1, 0
    }
};

static const isogl_symbol_param *table_param(const isogl_symbol_params *params,
                                             const char *key) {
    if (!params || !params->params || !key) return NULL;
    for (size_t i = 0; i < params->param_count; ++i) {
        const isogl_symbol_param *p = &params->params[i];
        if (p->desc && p->desc->key && strcmp(p->desc->key, key) == 0) {
            return p;
        }
    }
    return NULL;
}

static int table_has_kind(const isogl_symbol_param *p,
                          isogl_symbol_param_kind kind) {
    return p && p->desc && p->desc->kind == kind;
}

static int table_enum_member(const isogl_symbol_param_desc *desc, int value) {
    if (!desc || !desc->enum_values) return 0;
    for (size_t i = 0; i < desc->enum_count; ++i) {
        if (desc->enum_values[i].value == value) return 1;
    }
    return 0;
}

static int table_row_validate(const isogl_symbol_params *params) {
    if (!params || !params->params) return -1;

    const isogl_symbol_param *points = table_param(params, "points");
    const isogl_symbol_param *height = table_param(params, "height_mm");
    const isogl_symbol_param *text_height =
        table_param(params, "text_height_mm");
    const isogl_symbol_param *padding = table_param(params, "padding_mm");
    const isogl_symbol_param *widths = table_param(params, "widths");
    const isogl_symbol_param *alignments = table_param(params, "alignments");
    const isogl_symbol_param *texts = table_param(params, "texts");
    const isogl_symbol_param *pick_text = table_param(params, "pick_text");
    const isogl_symbol_param *pattern = table_param(params, "pattern");
    const isogl_symbol_param *weight = table_param(params, "weight");
    const isogl_symbol_param *layer = table_param(params, "layer");
    const isogl_symbol_param *color = table_param(params, "color");
    const isogl_symbol_param *scale = table_param(params, "scale");

    if (!table_has_kind(points, ISOGL_SYM_PARAM_POINTS) ||
        points->count != 1 || !points->value.points) {
        return -1;
    }
    if (!table_has_kind(height, ISOGL_SYM_PARAM_NUMBER) ||
        !table_has_kind(text_height, ISOGL_SYM_PARAM_NUMBER) ||
        !table_has_kind(padding, ISOGL_SYM_PARAM_NUMBER) ||
        !table_has_kind(pick_text, ISOGL_SYM_PARAM_BOOLEAN) ||
        !table_has_kind(layer, ISOGL_SYM_PARAM_INTEGER) ||
        !table_has_kind(color, ISOGL_SYM_PARAM_COLOR) ||
        !table_has_kind(scale, ISOGL_SYM_PARAM_SCALE)) {
        return -1;
    }
    if (!table_has_kind(pattern, ISOGL_SYM_PARAM_ENUM) ||
        !table_has_kind(weight, ISOGL_SYM_PARAM_ENUM)) {
        return -1;
    }

    double row_height = height->value.number;
    double txt_height = text_height->value.number;
    double txt_padding = padding->value.number;
    if (!isfinite(row_height) || !(row_height > 0.0)) return -1;
    if (!isfinite(txt_height) || !(txt_height > 0.0)) return -1;
    if (!isfinite(txt_padding) || !(txt_padding >= 0.0)) return -1;
    if (!isfinite(points->value.points[0].x) ||
        !isfinite(points->value.points[0].y)) {
        return -1;
    }

    if (!table_has_kind(widths, ISOGL_SYM_PARAM_NUMBERS) ||
        widths->count < 1 || !widths->value.numbers) {
        return -1;
    }
    if (!table_has_kind(alignments, ISOGL_SYM_PARAM_ENUM_LIST) ||
        !alignments->value.integers) {
        return -1;
    }
    if (!table_has_kind(texts, ISOGL_SYM_PARAM_TEXTS) ||
        !texts->value.texts) {
        return -1;
    }
    if (alignments->count != widths->count ||
        texts->count != widths->count) {
        return -1;
    }
    for (size_t i = 0; i < widths->count; ++i) {
        double w = widths->value.numbers[i];
        if (!isfinite(w) || !(w > 0.0)) return -1;
        if (!table_enum_member(alignments->desc,
                               alignments->value.integers[i])) {
            return -1;
        }
    }

    int pattern_value = pattern->value.integer;
    int weight_value = weight->value.integer;
    int layer_value = layer->value.integer;
    if (pattern_value < 0 || pattern_value >= LINE_TYPE_COUNT) return -1;
    if (weight_value < 0 || weight_value >= LINE_THICKNESS_COUNT) return -1;
    if (layer_value < 0 || layer_value > 31) return -1;
    if (!table_enum_member(pattern->desc, pattern_value)) return -1;
    if (!table_enum_member(weight->desc, weight_value)) return -1;
    return 0;
}

/* Text width in model units, mirroring the dimension symbol: advance at 1 mm
 * times the requested paper height divided by the global sheet scale. */
static double table_text_width(void *user, const char *utf8,
                               double height_mm) {
    const isogl_symbol_gen_context *ctx = user;
    if (!ctx || !ctx->font || !utf8 || !(height_mm > 0.0)) return 0.0;

    double font_height = (double)ctx->font->height;
    if (!(font_height > 0.0)) return 0.0;

    double scale = (ctx->sheet_scale > 0.0 && isfinite(ctx->sheet_scale))
                       ? ctx->sheet_scale
                       : 1.0;
    return isogl_font_text_advance(ctx->font, utf8) * height_mm /
           (font_height * scale);
}

typedef struct {
    const isogl_symbol_param *points;
    const isogl_symbol_param *widths;
    const isogl_symbol_param *alignments;
    const isogl_symbol_param *texts;
    double scale;
    double ox, oy;
    double height_model;
    double text_height_mm;
    double text_height_model;
    double padding_model;
    double total_width;
    uint8_t pattern, weight, layer;
    float color[4];
} TableLayout;

static double table_sheet_scale(const isogl_symbol_gen_context *ctx) {
    if (!ctx || !isfinite(ctx->sheet_scale) || !(ctx->sheet_scale > 0.0)) {
        return 1.0;
    }
    return ctx->sheet_scale;
}

static int table_layout_build(const isogl_symbol_params *params,
                              const isogl_symbol_gen_context *ctx,
                              TableLayout *layout) {
    if (!layout || table_row_validate(params) != 0) return -1;

    memset(layout, 0, sizeof(*layout));
    layout->points = table_param(params, "points");
    layout->widths = table_param(params, "widths");
    layout->alignments = table_param(params, "alignments");
    layout->texts = table_param(params, "texts");
    layout->scale = table_sheet_scale(ctx);

    layout->ox = layout->points->value.points[0].x;
    layout->oy = layout->points->value.points[0].y;

    double height_mm = table_param(params, "height_mm")->value.number;
    layout->height_model = height_mm / layout->scale;
    layout->text_height_mm =
        table_param(params, "text_height_mm")->value.number;
    layout->text_height_model = layout->text_height_mm / layout->scale;
    layout->padding_model =
        table_param(params, "padding_mm")->value.number / layout->scale;

    if (!isfinite(layout->height_model) ||
        !(layout->height_model > 0.0) ||
        !isfinite(layout->text_height_model) ||
        !(layout->text_height_model > 0.0) ||
        !isfinite(layout->padding_model) ||
        !(layout->padding_model >= 0.0)) {
        return -1;
    }

    double total = 0.0;
    for (size_t i = 0; i < layout->widths->count; ++i) {
        total += layout->widths->value.numbers[i] / layout->scale;
    }
    if (!isfinite(total) || !(total > 0.0)) return -1;
    layout->total_width = total;

    layout->pattern =
        (uint8_t)table_param(params, "pattern")->value.integer;
    layout->weight = (uint8_t)table_param(params, "weight")->value.integer;
    layout->layer = (uint8_t)table_param(params, "layer")->value.integer;
    memcpy(layout->color, table_param(params, "color")->value.color,
           sizeof(layout->color));
    return 0;
}

static void table_fill_style(IsoglSymItem *item, const TableLayout *layout) {
    item->pattern = layout->pattern;
    item->weight = layout->weight;
    item->layer = layout->layer;
    memcpy(item->color, layout->color, sizeof(item->color));
}

static int table_emit_segment(IsoglSymEmitter *out, const TableLayout *layout,
                              double x0, double y0, double x1, double y1) {
    IsoglSymItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_SEGMENT;
    item.x0 = x0;
    item.y0 = y0;
    item.x1 = x1;
    item.y1 = y1;
    table_fill_style(&item, layout);
    return out->emit(out->user, &item);
}

static int table_emit_text(IsoglSymEmitter *out, const TableLayout *layout,
                           const char *utf8, double x, double y) {
    IsoglSymItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_TEXT;
    item.utf8 = utf8 ? utf8 : "";
    item.x = x;
    item.y = y;
    item.height_mm = layout->text_height_mm;
    item.angle = 0.0;
    item.align = 0;
    table_fill_style(&item, layout);
    return out->emit(out->user, &item);
}

static int table_emit_pick_segment(IsoglSymPickEmitter *out,
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

static int table_emit_pick_obb(IsoglSymPickEmitter *out,
                               double x, double y,
                               double width, double height) {
    IsoglSymPickItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_PICK_OBB;
    item.corners[0][0] = x;
    item.corners[0][1] = y;
    item.corners[1][0] = x + width;
    item.corners[1][1] = y;
    item.corners[2][0] = x + width;
    item.corners[2][1] = y + height;
    item.corners[3][0] = x;
    item.corners[3][1] = y + height;
    return out->emit(out->user, &item);
}

static double table_measure(const IsoglSymMetrics *metrics, const char *utf8,
                            double text_height_mm) {
    if (!metrics || !metrics->text_width) return 0.0;
    double width = metrics->text_width(metrics->user, utf8 ? utf8 : "",
                                       text_height_mm);
    if (!isfinite(width) || width < 0.0) return 0.0;
    return width;
}

static double table_cell_w(const TableLayout *layout, size_t cell) {
    return layout->widths->value.numbers[cell] / layout->scale;
}

static double table_text_x(const TableLayout *layout, size_t cell,
                           double cell_x, double cell_w, double text_w) {
    int alignment = layout->alignments->value.integers[cell];
    switch (alignment) {
        case TABLE_ALIGN_CENTER:
            return cell_x + 0.5 * (cell_w - text_w);
        case TABLE_ALIGN_RIGHT:
            return cell_x + cell_w - text_w - layout->padding_model;
        case TABLE_ALIGN_LEFT:
        default:
            return cell_x + layout->padding_model;
    }
}

int isogl_table_row_geometry(const isogl_symbol_params *params,
                             const isogl_symbol_gen_context *ctx,
                             const IsoglSymMetrics *metrics,
                             IsoglSymEmitter *out) {
    if (!out || !out->emit) return -1;

    TableLayout layout;
    if (table_layout_build(params, ctx, &layout) != 0) return -1;

    double left = layout.ox;
    double right = layout.ox + layout.total_width;
    double bottom = layout.oy;
    double top = layout.oy + layout.height_model;

    /* Bottom and top borders. */
    if (table_emit_segment(out, &layout, left, bottom, right, bottom) != 0) {
        return -1;
    }
    if (table_emit_segment(out, &layout, left, top, right, top) != 0) {
        return -1;
    }

    /* N+1 vertical separators at the cumulative widths (including both
       borders). */
    double x = left;
    if (table_emit_segment(out, &layout, x, bottom, x, top) != 0) return -1;
    for (size_t i = 0; i < layout.widths->count; ++i) {
        x += table_cell_w(&layout, i);
        if (table_emit_segment(out, &layout, x, bottom, x, top) != 0) {
            return -1;
        }
    }

    /* One text run per cell, baseline-left and vertically centred. */
    x = left;
    double text_y = bottom +
                    0.5 * (layout.height_model - layout.text_height_model);
    for (size_t i = 0; i < layout.widths->count; ++i) {
        double cell_w = table_cell_w(&layout, i);
        const char *text = layout.texts->value.texts[i]
                               ? layout.texts->value.texts[i]
                               : "";
        double text_w = table_measure(metrics, text, layout.text_height_mm);
        double text_x = table_text_x(&layout, i, x, cell_w, text_w);
        if (table_emit_text(out, &layout, text, text_x, text_y) != 0) {
            return -1;
        }
        x += cell_w;
    }
    return 0;
}

int isogl_table_row_pick_geometry(const isogl_symbol_params *params,
                                  const isogl_symbol_gen_context *ctx,
                                  const IsoglSymMetrics *metrics,
                                  IsoglSymPickEmitter *out) {
    if (!out || !out->emit) return -1;

    TableLayout layout;
    if (table_layout_build(params, ctx, &layout) != 0) return -1;

    double left = layout.ox;
    double right = layout.ox + layout.total_width;
    double bottom = layout.oy;
    double top = layout.oy + layout.height_model;

    if (table_emit_pick_segment(out, left, bottom, right, bottom) != 0) {
        return -1;
    }
    if (table_emit_pick_segment(out, left, top, right, top) != 0) {
        return -1;
    }
    double x = left;
    if (table_emit_pick_segment(out, x, bottom, x, top) != 0) return -1;
    for (size_t i = 0; i < layout.widths->count; ++i) {
        x += table_cell_w(&layout, i);
        if (table_emit_pick_segment(out, x, bottom, x, top) != 0) return -1;
    }

    int pick_text = table_param(params, "pick_text")->value.boolean;
    if (pick_text) {
        x = left;
        double text_y = bottom +
                        0.5 * (layout.height_model - layout.text_height_model);
        for (size_t i = 0; i < layout.widths->count; ++i) {
            double cell_w = table_cell_w(&layout, i);
            const char *text = layout.texts->value.texts[i]
                                   ? layout.texts->value.texts[i]
                                   : "";
            double text_w = table_measure(metrics, text,
                                          layout.text_height_mm);
            double text_x = table_text_x(&layout, i, x, cell_w, text_w);
            if (table_emit_pick_obb(out, text_x, text_y, text_w,
                                    layout.text_height_model) != 0) {
                return -1;
            }
            x += cell_w;
        }
    }
    return 0;
}

static int table_row_init(isogl_symbol_params *params) {
    if (!params || params->param_count != TBL_PARAM_COUNT) return -1;

    params->params[TBL_POINTS].value.points[0].x = 0.0;
    params->params[TBL_POINTS].value.points[0].y = 0.0;
    params->params[TBL_HEIGHT_MM].value.number = 8.0;
    params->params[TBL_TEXT_HEIGHT_MM].value.number = 2.5;
    params->params[TBL_PADDING_MM].value.number = 0.5;
    params->params[TBL_WIDTHS].value.numbers[0] = 20.0;
    params->params[TBL_ALIGNMENTS].value.integers[0] = TABLE_ALIGN_LEFT;
    params->params[TBL_PICK_TEXT].value.boolean = 1;
    params->params[TBL_PATTERN].value.integer = LINE_TYPE_CONTINUOUS;
    params->params[TBL_WEIGHT].value.integer = LINE_THICKNESS_0_35;
    params->params[TBL_LAYER].value.integer = 0;
    params->params[TBL_COLOR].value.color[0] = 1.0f;
    params->params[TBL_COLOR].value.color[1] = 1.0f;
    params->params[TBL_COLOR].value.color[2] = 1.0f;
    params->params[TBL_COLOR].value.color[3] = 1.0f;
    return 0;
}

static int table_row_generate(const isogl_symbol_params *params,
                              const isogl_symbol_gen_context *ctx,
                              IsoglSymEmitter *render,
                              IsoglSymPickEmitter *pick) {
    if (!params || !ctx) return -1;

    IsoglSymMetrics metrics = {(void *)ctx, table_text_width};
    int rc = 0;
    if (render && render->emit) {
        rc = isogl_table_row_geometry(params, ctx, &metrics, render);
    }
    if (rc == 0 && pick && pick->emit) {
        rc = isogl_table_row_pick_geometry(params, ctx, &metrics, pick);
    }
    return rc == 0 ? 0 : -1;
}

static const isogl_symbol_type table_row_type = {
    .name = "table_row",
    .label = "Riga di tabella",
    .description = "Riga con celle, separatori e testo allineato",
    .params = TABLE_PARAMS,
    .param_count = TBL_PARAM_COUNT,
    .init_params = table_row_init,
    .validate = table_row_validate,
    .generate = table_row_generate
};

const isogl_symbol_type *isogl_symbol_table_row_type(void) {
    return &table_row_type;
}
