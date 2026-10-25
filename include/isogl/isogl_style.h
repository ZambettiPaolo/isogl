#ifndef ISOGL_STYLE_H
#define ISOGL_STYLE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Normative tables for technical drawing.
 *
 * This header is the single source of truth for ISO line types, line weights,
 * text heights, sheet formats and the predefined weight -> colour map used by
 * the "thickness colours" viewing mode.  The shaders consume the same data:
 * colour tables are uploaded as uniforms and pattern ratios are kept in sync
 * by tests (see tests/test_style.c).
 */

// Packed attribute layout (isogl_vertex.pk_thickness_pattern_layer).
#define ISOGL_TPP_THICKNESS_SHIFT 0u
#define ISOGL_TPP_PATTERN_SHIFT   5u
#define ISOGL_TPP_LAYER_SHIFT    10u
#define ISOGL_TPP_FIELD_MASK     0x1Fu

// ISO 128 line types.
typedef enum {
    LINE_TYPE_CONTINUOUS = 0,
    LINE_TYPE_DASHED,
    LINE_TYPE_DASHED_SPACED,
    LINE_TYPE_LONG_DASH_DOT,
    LINE_TYPE_LONG_DASH_DOUBLE_DOT,
    LINE_TYPE_LONG_DASH_TRIPLE_DOT,
    LINE_TYPE_DOTTED,
    LINE_TYPE_LONG_DASH_SHORT_DASH,
    LINE_TYPE_LONG_DASH_DOUBLE_SHORT_DASH,
    LINE_TYPE_DASH_DOT,
    LINE_TYPE_DASH_DOUBLE_DOT,
    LINE_TYPE_DOUBLE_DOT_DASH,
    LINE_TYPE_DOUBLE_DOT_DOUBLE_DASH,
    LINE_TYPE_TRIPLE_DOT_DASH,
    LINE_TYPE_TRIPLE_DOT_DOUBLE_DASH,
    LINE_TYPE_COUNT
} isogl_line_type;

// ISO 128 line weights (mm).
typedef enum {
    LINE_THICKNESS_0_18 = 0,
    LINE_THICKNESS_0_25,
    LINE_THICKNESS_0_35,
    LINE_THICKNESS_0_5,
    LINE_THICKNESS_0_7,
    LINE_THICKNESS_1_0,
    LINE_THICKNESS_1_4,
    LINE_THICKNESS_2_0,
    LINE_THICKNESS_COUNT
} isogl_line_weight;

// ISO 3098 text heights (mm).
typedef enum {
    TEXT_HEIGHT_1_8 = 0,
    TEXT_HEIGHT_2_5,
    TEXT_HEIGHT_3_5,
    TEXT_HEIGHT_5,
    TEXT_HEIGHT_7,
    TEXT_HEIGHT_10,
    TEXT_HEIGHT_14,
    TEXT_HEIGHT_20,
    TEXT_HEIGHT_COUNT
} isogl_text_height;

typedef enum {
    ISOGL_SHEET_A4 = 0,
    ISOGL_SHEET_A3,
    ISOGL_SHEET_A2,
    ISOGL_SHEET_A1,
    ISOGL_SHEET_A0,
    ISOGL_SHEET_FORMAT_COUNT
} isogl_sheet_format;

typedef enum {
    ISOGL_ORIENTATION_PORTRAIT = 0,
    ISOGL_ORIENTATION_LANDSCAPE
} isogl_sheet_orientation;

#define ISOGL_MAX_PATTERN_RATIOS 10

typedef struct {
    const char *name;
    float ratios[ISOGL_MAX_PATTERN_RATIOS]; // multiples of line thickness
    uint8_t count;                          // 0 = continuous
} isogl_line_pattern;

extern const float isogl_line_weight_mm[LINE_THICKNESS_COUNT];
extern const float isogl_text_height_mm[TEXT_HEIGHT_COUNT];
extern const float isogl_sheet_size_mm[ISOGL_SHEET_FORMAT_COUNT][2]; // portrait
extern const char *const isogl_line_type_names[LINE_TYPE_COUNT];
extern const isogl_line_pattern isogl_line_patterns[LINE_TYPE_COUNT];

/*
 * Predefined colour for each ISO weight, used by the "thickness colours" mode.
 * Indexed by isogl_line_weight.  This is the AutoCAD-like CTB equivalent.
 */
extern const float isogl_weight_color[LINE_THICKNESS_COUNT][4];

// 0-based field extraction / packing helpers.
uint16_t isogl_pack(uint8_t thickness, uint8_t pattern, uint8_t layer);
uint8_t isogl_unpack_thickness(uint16_t pk_TPL);
uint8_t isogl_unpack_pattern(uint16_t pk_TPL);
uint8_t isogl_unpack_layer(uint16_t pk_TPL);

// Validation at the API boundary.
bool isogl_line_type_valid(int type);
bool isogl_line_weight_valid(int weight);
bool isogl_text_height_valid(int height);

// Sheet size in mm for a normalised format and orientation.
void isogl_sheet_size(isogl_sheet_format format, isogl_sheet_orientation orientation,
                      float *width_mm, float *height_mm);

#endif // ISOGL_STYLE_H
