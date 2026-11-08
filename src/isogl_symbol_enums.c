#include "isogl_symbol_enums.h"

/*
 * The normative ranges come from isogl_style.h and isogl_symbol_geometry.h.
 * These asserts catch a table that drifts from a normative count or from the
 * last IsoglDimensionArrow value.
 */
_Static_assert(ISOGL_SYM_ARROW_VALUE_COUNT ==
                   ISOGL_DIM_ARROW_CLOSED_OUTSIDE + 1,
               "arrow table must cover every IsoglDimensionArrow value");
_Static_assert(ISOGL_SYM_LINE_TYPE_VALUE_COUNT == LINE_TYPE_COUNT,
               "line-type table must match LINE_TYPE_COUNT");
_Static_assert(ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT == LINE_THICKNESS_COUNT,
               "line-weight table must match LINE_THICKNESS_COUNT");

const isogl_symbol_enum_value
    isogl_symbol_arrow_values[ISOGL_SYM_ARROW_VALUE_COUNT] = {
    {ISOGL_DIM_ARROW_NONE, "none", "Nessuna"},
    {ISOGL_DIM_ARROW_OPEN, "open", "Aperta"},
    {ISOGL_DIM_ARROW_CLOSED, "closed", "Chiusa"},
    {ISOGL_DIM_ARROW_TICK, "tick", "Trattino"},
    {ISOGL_DIM_ARROW_DOT, "dot", "Pallino"},
    {ISOGL_DIM_ARROW_OPEN_OUTSIDE, "open_outside", "Aperta esterna"},
    {ISOGL_DIM_ARROW_CLOSED_OUTSIDE, "closed_outside", "Chiusa esterna"}
};

const isogl_symbol_enum_value
    isogl_symbol_line_type_values[ISOGL_SYM_LINE_TYPE_VALUE_COUNT] = {
    {LINE_TYPE_CONTINUOUS, "continuous", "Continuous"},
    {LINE_TYPE_DASHED, "dashed", "Dashed"},
    {LINE_TYPE_DASHED_SPACED, "dashed_spaced", "Dashed Spaced"},
    {LINE_TYPE_LONG_DASH_DOT, "long_dash_dot", "Long Dash Dot"},
    {LINE_TYPE_LONG_DASH_DOUBLE_DOT, "long_dash_double_dot",
     "Long Dash Double Dot"},
    {LINE_TYPE_LONG_DASH_TRIPLE_DOT, "long_dash_triple_dot",
     "Long Dash Triple Dot"},
    {LINE_TYPE_DOTTED, "dotted", "Dotted"},
    {LINE_TYPE_LONG_DASH_SHORT_DASH, "long_dash_short_dash",
     "Long Dash Short Dash"},
    {LINE_TYPE_LONG_DASH_DOUBLE_SHORT_DASH, "long_dash_double_short_dash",
     "Long Dash Double Short Dash"},
    {LINE_TYPE_DASH_DOT, "dash_dot", "Dash Dot"},
    {LINE_TYPE_DASH_DOUBLE_DOT, "dash_double_dot", "Dash Double Dot"},
    {LINE_TYPE_DOUBLE_DOT_DASH, "double_dot_dash", "Double Dot Dash"},
    {LINE_TYPE_DOUBLE_DOT_DOUBLE_DASH, "double_dot_double_dash",
     "Double Dot Double Dash"},
    {LINE_TYPE_TRIPLE_DOT_DASH, "triple_dot_dash", "Triple Dot Dash"},
    {LINE_TYPE_TRIPLE_DOT_DOUBLE_DASH, "triple_dot_double_dash",
     "Triple Dot Double Dash"}
};

const isogl_symbol_enum_value
    isogl_symbol_line_weight_values[ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT] = {
    {LINE_THICKNESS_0_18, "0.18", "0.18 mm"},
    {LINE_THICKNESS_0_25, "0.25", "0.25 mm"},
    {LINE_THICKNESS_0_35, "0.35", "0.35 mm"},
    {LINE_THICKNESS_0_5, "0.5", "0.5 mm"},
    {LINE_THICKNESS_0_7, "0.7", "0.7 mm"},
    {LINE_THICKNESS_1_0, "1.0", "1.0 mm"},
    {LINE_THICKNESS_1_4, "1.4", "1.4 mm"},
    {LINE_THICKNESS_2_0, "2.0", "2.0 mm"}
};
