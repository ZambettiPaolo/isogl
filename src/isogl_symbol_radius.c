/*
 * Radius dimension symbol type ("dimension_radius").
 *
 * Same three points as the diameter symbol: points[0] is the circle centre,
 * points[1] a point on the circle and points[2] the centre of the text box in
 * model units.  The radius line runs from the centre to the circle point and
 * the automatic value is r = |points[1] - points[0]| formatted with
 * `precision` decimals.  texts = [prefix, suffix]; init defaults are "R" and
 * "".  The single arrow sits on the circle point with its legs pointing toward
 * the centre (default CLOSED).  A leader runs from the circle point to the
 * text anchor only when the anchor is not already on the radius segment.
 *
 * This file is pure C: no GL, no SDL, no CAD document access.
 */

#include "isogl_symbol_builtins.h"
#include "isogl_symbol_enums.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    RAD_POINTS = 0,
    RAD_TEXTS,
    RAD_TEXT_HEIGHT,
    RAD_PRECISION,
    RAD_PICK_TEXT,
    RAD_ARROW,
    RAD_PATTERN,
    RAD_WEIGHT,
    RAD_LAYER,
    RAD_COLOR,
    RAD_SCALE,
    RAD_PARAM_COUNT
};

static const isogl_symbol_param_desc RAD_PARAMS[RAD_PARAM_COUNT] = {
    [RAD_POINTS] = {
        "points", "Punti", "Centro, punto sul cerchio, ancoraggio testo", "mm",
        ISOGL_SYM_PARAM_POINTS, 3, 3, 1
    },
    [RAD_TEXTS] = {
        "texts", "Testi", "Prefisso e suffisso del valore", "",
        ISOGL_SYM_PARAM_TEXTS, 2, 2, 1
    },
    [RAD_TEXT_HEIGHT] = {
        "text_height_mm", "Altezza testo", "Altezza ISO su carta", "mm",
        ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [RAD_PRECISION] = {
        "precision", "Decimali", "Cifre del valore automatico", "",
        ISOGL_SYM_PARAM_INTEGER, 1, 1, 1
    },
    [RAD_PICK_TEXT] = {
        "pick_text", "Testo selezionabile", "Include l'OBB del testo", "",
        ISOGL_SYM_PARAM_BOOLEAN, 1, 1, 1
    },
    [RAD_ARROW] = {
        "arrow", "Freccia", "Punta sul punto del cerchio", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_arrow_values, ISOGL_SYM_ARROW_VALUE_COUNT
    },
    [RAD_PATTERN] = {
        "pattern", "Tipo linea ISO", "Pattern del raggio", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_type_values, ISOGL_SYM_LINE_TYPE_VALUE_COUNT
    },
    [RAD_WEIGHT] = {
        "weight", "Peso ISO", "Spessore del raggio", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_weight_values, ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT
    },
    [RAD_LAYER] = {
        "layer", "Layer", "Layer di stile", "",
        ISOGL_SYM_PARAM_INTEGER, 1, 1, 1
    },
    [RAD_COLOR] = {
        "color", "Colore", "Colore di stampa", "",
        ISOGL_SYM_PARAM_COLOR, 1, 1, 1
    },
    [RAD_SCALE] = {
        "scale", "Scala foglio", "Sola lettura; rigenera il simbolo", "1:n",
        ISOGL_SYM_PARAM_SCALE, 1, 1, 0
    }
};

static int rad_validate(const isogl_symbol_params *params) {
    if (!params || params->param_count != RAD_PARAM_COUNT) return -1;

    const isogl_symbol_param *points = &params->params[RAD_POINTS];
    const isogl_symbol_param *texts = &params->params[RAD_TEXTS];
    if (points->count != 3 || !points->value.points || texts->count != 2) {
        return -1;
    }
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(points->value.points[i].x) ||
            !isfinite(points->value.points[i].y)) {
            return -1;
        }
    }
    double dx = points->value.points[1].x - points->value.points[0].x;
    double dy = points->value.points[1].y - points->value.points[0].y;
    double radius = hypot(dx, dy);
    if (!isfinite(radius) || !(radius > 0.0)) return -1;

    double height = params->params[RAD_TEXT_HEIGHT].value.number;
    if (!isfinite(height) || !(height > 0.0)) return -1;

    int precision = params->params[RAD_PRECISION].value.integer;
    if (precision < 0 || precision > 12) return -1;

    int arrow = params->params[RAD_ARROW].value.integer;
    if (arrow < ISOGL_DIM_ARROW_NONE ||
        arrow > ISOGL_DIM_ARROW_CLOSED_OUTSIDE) {
        return -1;
    }

    int pattern = params->params[RAD_PATTERN].value.integer;
    int weight = params->params[RAD_WEIGHT].value.integer;
    int layer = params->params[RAD_LAYER].value.integer;
    if (pattern < 0 || pattern >= LINE_TYPE_COUNT) return -1;
    if (weight < 0 || weight >= LINE_THICKNESS_COUNT) return -1;
    if (layer < 0 || layer > 31) return -1;
    return 0;
}

static int rad_generate(const isogl_symbol_params *params,
                        const isogl_symbol_gen_context *ctx,
                        IsoglSymEmitter *render,
                        IsoglSymPickEmitter *pick) {
    if (!params || !ctx || rad_validate(params) != 0) return -1;

    IsoglRadiusDesc d;
    memset(&d, 0, sizeof(d));

    const isogl_symbol_param *points = &params->params[RAD_POINTS];
    const isogl_symbol_param *texts = &params->params[RAD_TEXTS];
    d.cx = points->value.points[0].x;
    d.cy = points->value.points[0].y;
    d.px = points->value.points[1].x;
    d.py = points->value.points[1].y;
    d.tx = points->value.points[2].x;
    d.ty = points->value.points[2].y;

    snprintf(d.text_left, sizeof(d.text_left), "%s",
             texts->value.texts[0] ? texts->value.texts[0] : "");
    snprintf(d.text_right, sizeof(d.text_right), "%s",
             texts->value.texts[1] ? texts->value.texts[1] : "");

    d.text_height_mm = params->params[RAD_TEXT_HEIGHT].value.number;
    d.sheet_scale = ctx->sheet_scale;
    d.precision = params->params[RAD_PRECISION].value.integer;
    d.pick_text = params->params[RAD_PICK_TEXT].value.boolean;
    d.arrow = (IsoglDimensionArrow)params->params[RAD_ARROW].value.integer;
    d.pattern = (uint8_t)params->params[RAD_PATTERN].value.integer;
    d.weight = (uint8_t)params->params[RAD_WEIGHT].value.integer;
    d.layer = (uint8_t)params->params[RAD_LAYER].value.integer;
    memcpy(d.color, params->params[RAD_COLOR].value.color, sizeof(d.color));

    IsoglSymMetrics metrics = {(void *)ctx, isogl_symbol_gen_text_width};
    int rc = 0;
    if (render && render->emit) {
        rc = isogl_radius_geometry(&d, &metrics, render);
    }
    if (rc == 0 && pick && pick->emit) {
        rc = isogl_radius_pick_geometry(&d, &metrics, pick);
    }
    return rc == 0 ? 0 : -1;
}

static int rad_init(isogl_symbol_params *params) {
    if (!params || params->param_count != RAD_PARAM_COUNT) return -1;

    for (int i = 0; i < 3; ++i) {
        params->params[RAD_POINTS].value.points[i].x = 0.0;
        params->params[RAD_POINTS].value.points[i].y = 0.0;
    }
    if (isogl_symbol_params_set_text(params, RAD_TEXTS, 0, "R") != 0 ||
        isogl_symbol_params_set_text(params, RAD_TEXTS, 1, "") != 0) {
        return -1;
    }
    params->params[RAD_TEXT_HEIGHT].value.number = 3.5;
    params->params[RAD_PRECISION].value.integer = 1;
    params->params[RAD_PICK_TEXT].value.boolean = 1;
    params->params[RAD_ARROW].value.integer = ISOGL_DIM_ARROW_CLOSED;
    params->params[RAD_PATTERN].value.integer = LINE_TYPE_CONTINUOUS;
    params->params[RAD_WEIGHT].value.integer = LINE_THICKNESS_0_35;
    params->params[RAD_LAYER].value.integer = 0;
    params->params[RAD_COLOR].value.color[0] = 1.0f;
    params->params[RAD_COLOR].value.color[1] = 1.0f;
    params->params[RAD_COLOR].value.color[2] = 1.0f;
    params->params[RAD_COLOR].value.color[3] = 1.0f;
    return 0;
}

static const isogl_symbol_type radius_type = {
    .name = "dimension_radius",
    .label = "Quota raggio",
    .description = "Raggio con linea, freccia e testo",
    .params = RAD_PARAMS,
    .param_count = RAD_PARAM_COUNT,
    .init_params = rad_init,
    .validate = rad_validate,
    .generate = rad_generate
};

const isogl_symbol_type *isogl_symbol_radius_type(void) {
    return &radius_type;
}
