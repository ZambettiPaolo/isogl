/*
 * Diameter dimension symbol type ("dimension_diameter").
 *
 * Interpretation of the parameters (agreed with the CAD model):
 *
 *   - points[0] is the circle centre in model units;
 *   - points[1] is a point on the circle; r = |p1 - p0| must be positive and
 *     the diameter line runs from p0 - r*u to p0 + r*u, u = (p1-p0)/r;
 *   - points[2] is the centre of the text box in model units;
 *   - the automatic value is 2*r, formatted with `precision` decimals;
 *   - texts = [prefix, suffix]; init defaults are "Ø" and "";
 *   - text_height_mm, arrows and the dot size are paper sizes converted with
 *     the global sheet scale;
 *   - arrow_left/arrow_right place the heads on the two diameter ends with
 *     the legs pointing toward the centre (default CLOSED);
 *   - a leader connects the text centre to the closest point of the diameter
 *     segment when the anchor is not already on it.
 *
 * This file is pure C: no GL, no SDL, no CAD document access.  The geometry
 * itself lives in isogl_symbol_geometry.c and is shared with the aligned
 * dimension helpers (arrows, style, metrics).
 */

#include "isogl_symbol_builtins.h"
#include "isogl_symbol_enums.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    DIA_POINTS = 0,
    DIA_TEXTS,
    DIA_TEXT_HEIGHT,
    DIA_PRECISION,
    DIA_PICK_TEXT,
    DIA_ARROW_LEFT,
    DIA_ARROW_RIGHT,
    DIA_PATTERN,
    DIA_WEIGHT,
    DIA_LAYER,
    DIA_COLOR,
    DIA_SCALE,
    DIA_PARAM_COUNT
};

static const isogl_symbol_param_desc DIA_PARAMS[DIA_PARAM_COUNT] = {
    [DIA_POINTS] = {
        "points", "Punti", "Centro, punto sul cerchio, ancoraggio testo", "mm",
        ISOGL_SYM_PARAM_POINTS, 3, 3, 1
    },
    [DIA_TEXTS] = {
        "texts", "Testi", "Prefisso e suffisso del valore", "",
        ISOGL_SYM_PARAM_TEXTS, 2, 2, 1
    },
    [DIA_TEXT_HEIGHT] = {
        "text_height_mm", "Altezza testo", "Altezza ISO su carta", "mm",
        ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [DIA_PRECISION] = {
        "precision", "Decimali", "Cifre del valore automatico", "",
        ISOGL_SYM_PARAM_INTEGER, 1, 1, 1
    },
    [DIA_PICK_TEXT] = {
        "pick_text", "Testo selezionabile", "Include l'OBB del testo", "",
        ISOGL_SYM_PARAM_BOOLEAN, 1, 1, 1
    },
    [DIA_ARROW_LEFT] = {
        "arrow_left", "Freccia sinistra", "Punta sull'estremo -r*u", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_arrow_values, ISOGL_SYM_ARROW_VALUE_COUNT
    },
    [DIA_ARROW_RIGHT] = {
        "arrow_right", "Freccia destra", "Punta sull'estremo +r*u", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_arrow_values, ISOGL_SYM_ARROW_VALUE_COUNT
    },
    [DIA_PATTERN] = {
        "pattern", "Tipo linea ISO", "Pattern del diametro", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_type_values, ISOGL_SYM_LINE_TYPE_VALUE_COUNT
    },
    [DIA_WEIGHT] = {
        "weight", "Peso ISO", "Spessore del diametro", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_weight_values, ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT
    },
    [DIA_LAYER] = {
        "layer", "Layer", "Layer di stile", "",
        ISOGL_SYM_PARAM_INTEGER, 1, 1, 1
    },
    [DIA_COLOR] = {
        "color", "Colore", "Colore di stampa", "",
        ISOGL_SYM_PARAM_COLOR, 1, 1, 1
    },
    [DIA_SCALE] = {
        "scale", "Scala foglio", "Sola lettura; rigenera il simbolo", "1:n",
        ISOGL_SYM_PARAM_SCALE, 1, 1, 0
    }
};

static int dia_validate(const isogl_symbol_params *params) {
    if (!params || params->param_count != DIA_PARAM_COUNT) return -1;

    const isogl_symbol_param *points = &params->params[DIA_POINTS];
    const isogl_symbol_param *texts = &params->params[DIA_TEXTS];
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

    double height = params->params[DIA_TEXT_HEIGHT].value.number;
    if (!isfinite(height) || !(height > 0.0)) return -1;

    int precision = params->params[DIA_PRECISION].value.integer;
    if (precision < 0 || precision > 12) return -1;

    int left = params->params[DIA_ARROW_LEFT].value.integer;
    int right = params->params[DIA_ARROW_RIGHT].value.integer;
    if (left < ISOGL_DIM_ARROW_NONE ||
        left > ISOGL_DIM_ARROW_CLOSED_OUTSIDE ||
        right < ISOGL_DIM_ARROW_NONE ||
        right > ISOGL_DIM_ARROW_CLOSED_OUTSIDE) {
        return -1;
    }

    int pattern = params->params[DIA_PATTERN].value.integer;
    int weight = params->params[DIA_WEIGHT].value.integer;
    int layer = params->params[DIA_LAYER].value.integer;
    if (pattern < 0 || pattern >= LINE_TYPE_COUNT) return -1;
    if (weight < 0 || weight >= LINE_THICKNESS_COUNT) return -1;
    if (layer < 0 || layer > 31) return -1;
    return 0;
}

static int dia_generate(const isogl_symbol_params *params,
                        const isogl_symbol_gen_context *ctx,
                        IsoglSymEmitter *render,
                        IsoglSymPickEmitter *pick) {
    if (!params || !ctx || dia_validate(params) != 0) return -1;

    IsoglDiameterDesc d;
    memset(&d, 0, sizeof(d));

    const isogl_symbol_param *points = &params->params[DIA_POINTS];
    const isogl_symbol_param *texts = &params->params[DIA_TEXTS];
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

    d.text_height_mm = params->params[DIA_TEXT_HEIGHT].value.number;
    d.sheet_scale = ctx->sheet_scale;
    d.precision = params->params[DIA_PRECISION].value.integer;
    d.pick_text = params->params[DIA_PICK_TEXT].value.boolean;
    d.arrow_left =
        (IsoglDimensionArrow)params->params[DIA_ARROW_LEFT].value.integer;
    d.arrow_right =
        (IsoglDimensionArrow)params->params[DIA_ARROW_RIGHT].value.integer;
    d.pattern = (uint8_t)params->params[DIA_PATTERN].value.integer;
    d.weight = (uint8_t)params->params[DIA_WEIGHT].value.integer;
    d.layer = (uint8_t)params->params[DIA_LAYER].value.integer;
    memcpy(d.color, params->params[DIA_COLOR].value.color, sizeof(d.color));

    IsoglSymMetrics metrics = {(void *)ctx, isogl_symbol_gen_text_width};
    int rc = 0;
    if (render && render->emit) {
        rc = isogl_diameter_geometry(&d, &metrics, render);
    }
    if (rc == 0 && pick && pick->emit) {
        rc = isogl_diameter_pick_geometry(&d, &metrics, pick);
    }
    return rc == 0 ? 0 : -1;
}

static int dia_init(isogl_symbol_params *params) {
    if (!params || params->param_count != DIA_PARAM_COUNT) return -1;

    for (int i = 0; i < 3; ++i) {
        params->params[DIA_POINTS].value.points[i].x = 0.0;
        params->params[DIA_POINTS].value.points[i].y = 0.0;
    }
    if (isogl_symbol_params_set_text(params, DIA_TEXTS, 0, "\xC3\x98") != 0 ||
        isogl_symbol_params_set_text(params, DIA_TEXTS, 1, "") != 0) {
        return -1;
    }
    params->params[DIA_TEXT_HEIGHT].value.number = 3.5;
    params->params[DIA_PRECISION].value.integer = 1;
    params->params[DIA_PICK_TEXT].value.boolean = 1;
    params->params[DIA_ARROW_LEFT].value.integer = ISOGL_DIM_ARROW_CLOSED;
    params->params[DIA_ARROW_RIGHT].value.integer = ISOGL_DIM_ARROW_CLOSED;
    params->params[DIA_PATTERN].value.integer = LINE_TYPE_CONTINUOUS;
    params->params[DIA_WEIGHT].value.integer = LINE_THICKNESS_0_35;
    params->params[DIA_LAYER].value.integer = 0;
    params->params[DIA_COLOR].value.color[0] = 1.0f;
    params->params[DIA_COLOR].value.color[1] = 1.0f;
    params->params[DIA_COLOR].value.color[2] = 1.0f;
    params->params[DIA_COLOR].value.color[3] = 1.0f;
    return 0;
}

static const isogl_symbol_type diameter_type = {
    .name = "dimension_diameter",
    .label = "Quota diametro",
    .description = "Diametro con linea, frecce e testo",
    .params = DIA_PARAMS,
    .param_count = DIA_PARAM_COUNT,
    .init_params = dia_init,
    .validate = dia_validate,
    .generate = dia_generate
};

const isogl_symbol_type *isogl_symbol_diameter_type(void) {
    return &diameter_type;
}
