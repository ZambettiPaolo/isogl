#include "isogl_style.h"

const float isogl_line_weight_mm[LINE_THICKNESS_COUNT] = {
    [LINE_THICKNESS_0_18] = 0.18f,
    [LINE_THICKNESS_0_25] = 0.25f,
    [LINE_THICKNESS_0_35] = 0.35f,
    [LINE_THICKNESS_0_5]  = 0.5f,
    [LINE_THICKNESS_0_7]  = 0.7f,
    [LINE_THICKNESS_1_0]  = 1.0f,
    [LINE_THICKNESS_1_4]  = 1.4f,
    [LINE_THICKNESS_2_0]  = 2.0f
};

const float isogl_text_height_mm[TEXT_HEIGHT_COUNT] = {
    [TEXT_HEIGHT_1_8] = 1.8f,
    [TEXT_HEIGHT_2_5] = 2.5f,
    [TEXT_HEIGHT_3_5] = 3.5f,
    [TEXT_HEIGHT_5]   = 5.0f,
    [TEXT_HEIGHT_7]   = 7.0f,
    [TEXT_HEIGHT_10]  = 10.0f,
    [TEXT_HEIGHT_14]  = 14.0f,
    [TEXT_HEIGHT_20]  = 20.0f
};

// ISO 216 portrait sizes in millimetres (width, height).
const float isogl_sheet_size_mm[ISOGL_SHEET_FORMAT_COUNT][2] = {
    [ISOGL_SHEET_A4] = {210.0f, 297.0f},
    [ISOGL_SHEET_A3] = {297.0f, 420.0f},
    [ISOGL_SHEET_A2] = {420.0f, 594.0f},
    [ISOGL_SHEET_A1] = {594.0f, 841.0f},
    [ISOGL_SHEET_A0] = {841.0f, 1189.0f}
};

const char *const isogl_line_type_names[LINE_TYPE_COUNT] = {
    [LINE_TYPE_CONTINUOUS]               = "Continuous",
    [LINE_TYPE_DASHED]                   = "Dashed",
    [LINE_TYPE_DASHED_SPACED]            = "Dashed Spaced",
    [LINE_TYPE_LONG_DASH_DOT]            = "Long Dash Dot",
    [LINE_TYPE_LONG_DASH_DOUBLE_DOT]     = "Long Dash Double Dot",
    [LINE_TYPE_LONG_DASH_TRIPLE_DOT]     = "Long Dash Triple Dot",
    [LINE_TYPE_DOTTED]                   = "Dotted",
    [LINE_TYPE_LONG_DASH_SHORT_DASH]     = "Long Dash Short Dash",
    [LINE_TYPE_LONG_DASH_DOUBLE_SHORT_DASH] = "Long Dash Double Short Dash",
    [LINE_TYPE_DASH_DOT]                 = "Dash Dot",
    [LINE_TYPE_DASH_DOUBLE_DOT]          = "Dash Double Dot",
    [LINE_TYPE_DOUBLE_DOT_DASH]          = "Double Dot Dash",
    [LINE_TYPE_DOUBLE_DOT_DOUBLE_DASH]   = "Double Dot Double Dash",
    [LINE_TYPE_TRIPLE_DOT_DASH]          = "Triple Dot Dash",
    [LINE_TYPE_TRIPLE_DOT_DOUBLE_DASH]   = "Triple Dot Double Dash"
};

// Ratios are multiples of the line thickness, per ISO 128-2.
const isogl_line_pattern isogl_line_patterns[LINE_TYPE_COUNT] = {
    [LINE_TYPE_CONTINUOUS] = {"Continuous", {0}, 0},
    [LINE_TYPE_DASHED] = {"Dashed", {12.0f, 3.0f}, 2},
    [LINE_TYPE_DASHED_SPACED] = {"Dashed Spaced", {12.0f, 6.0f}, 2},
    [LINE_TYPE_LONG_DASH_DOT] = {"Long Dash Dot", {20.0f, 3.0f, 2.0f, 3.0f}, 4},
    [LINE_TYPE_LONG_DASH_DOUBLE_DOT] =
        {"Long Dash Double Dot", {20.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f}, 6},
    [LINE_TYPE_LONG_DASH_TRIPLE_DOT] =
        {"Long Dash Triple Dot", {20.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f}, 8},
    [LINE_TYPE_DOTTED] = {"Dotted", {0.0f, 3.0f}, 2},
    [LINE_TYPE_LONG_DASH_SHORT_DASH] =
        {"Long Dash Short Dash", {20.0f, 3.0f, 6.0f, 3.0f}, 4},
    [LINE_TYPE_LONG_DASH_DOUBLE_SHORT_DASH] =
        {"Long Dash Double Short Dash", {20.0f, 3.0f, 6.0f, 3.0f, 6.0f, 3.0f}, 6},
    [LINE_TYPE_DASH_DOT] = {"Dash Dot", {12.0f, 3.0f, 0.0f, 3.0f}, 4},
    [LINE_TYPE_DASH_DOUBLE_DOT] =
        {"Dash Double Dot", {12.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f}, 6},
    [LINE_TYPE_DOUBLE_DOT_DASH] =
        {"Double Dot Dash", {0.0f, 3.0f, 0.0f, 3.0f, 12.0f, 3.0f}, 6},
    [LINE_TYPE_DOUBLE_DOT_DOUBLE_DASH] =
        {"Double Dot Double Dash", {0.0f, 3.0f, 0.0f, 3.0f, 6.0f, 3.0f, 6.0f, 3.0f}, 8},
    [LINE_TYPE_TRIPLE_DOT_DASH] =
        {"Triple Dot Dash", {0.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f, 12.0f, 3.0f}, 8},
    [LINE_TYPE_TRIPLE_DOT_DOUBLE_DASH] =
        {"Triple Dot Double Dash", {0.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f, 6.0f, 3.0f, 6.0f, 3.0f}, 10}
};

/*
 * Predefined thickness -> colour map ("colori-spessore" mode).
 * ACI-inspired: 1 red, 2 yellow, 3 green, 4 cyan, 5 blue, 6 magenta, 7 white,
 * 8 grey.  The shader receives this table as a uniform array, so this is the
 * only definition of the mapping.
 */
const float isogl_weight_color[LINE_THICKNESS_COUNT][4] = {
    [LINE_THICKNESS_0_18] = {1.00f, 0.00f, 0.00f, 1.0f},
    [LINE_THICKNESS_0_25] = {1.00f, 1.00f, 0.00f, 1.0f},
    [LINE_THICKNESS_0_35] = {0.00f, 1.00f, 0.00f, 1.0f},
    [LINE_THICKNESS_0_5]  = {0.00f, 1.00f, 1.00f, 1.0f},
    [LINE_THICKNESS_0_7]  = {0.30f, 0.55f, 1.00f, 1.0f},
    [LINE_THICKNESS_1_0]  = {1.00f, 0.00f, 1.00f, 1.0f},
    [LINE_THICKNESS_1_4]  = {1.00f, 1.00f, 1.00f, 1.0f},
    [LINE_THICKNESS_2_0]  = {0.55f, 0.55f, 0.55f, 1.0f}
};

uint16_t isogl_pack(uint8_t thickness, uint8_t pattern, uint8_t layer) {
    if (thickness > ISOGL_TPP_FIELD_MASK ||
        pattern > ISOGL_TPP_FIELD_MASK ||
        layer > ISOGL_TPP_FIELD_MASK) {
        return 0;
    }

    uint16_t result = 0;
    result |= (uint16_t)((uint16_t)thickness << ISOGL_TPP_THICKNESS_SHIFT);
    result |= (uint16_t)((uint16_t)pattern << ISOGL_TPP_PATTERN_SHIFT);
    result |= (uint16_t)((uint16_t)layer << ISOGL_TPP_LAYER_SHIFT);
    return result;
}

uint8_t isogl_unpack_thickness(uint16_t packed) {
    return (uint8_t)((packed >> ISOGL_TPP_THICKNESS_SHIFT) & ISOGL_TPP_FIELD_MASK);
}

uint8_t isogl_unpack_pattern(uint16_t packed) {
    return (uint8_t)((packed >> ISOGL_TPP_PATTERN_SHIFT) & ISOGL_TPP_FIELD_MASK);
}

uint8_t isogl_unpack_layer(uint16_t packed) {
    return (uint8_t)((packed >> ISOGL_TPP_LAYER_SHIFT) & ISOGL_TPP_FIELD_MASK);
}

bool isogl_line_type_valid(int type) {
    return type >= 0 && type < LINE_TYPE_COUNT;
}

bool isogl_line_weight_valid(int weight) {
    return weight >= 0 && weight < LINE_THICKNESS_COUNT;
}

bool isogl_text_height_valid(int height) {
    return height >= 0 && height < TEXT_HEIGHT_COUNT;
}

void isogl_sheet_size(isogl_sheet_format format, isogl_sheet_orientation orientation,
                      float *width_mm, float *height_mm) {
    if (!width_mm || !height_mm) return;

    if (format < 0 || format >= ISOGL_SHEET_FORMAT_COUNT) {
        *width_mm = 0.0f;
        *height_mm = 0.0f;
        return;
    }

    float w = isogl_sheet_size_mm[format][0];
    float h = isogl_sheet_size_mm[format][1];
    if (orientation == ISOGL_ORIENTATION_LANDSCAPE) {
        float tmp = w;
        w = h;
        h = tmp;
    }
    *width_mm = w;
    *height_mm = h;
}
