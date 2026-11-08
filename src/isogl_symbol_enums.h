#ifndef ISOGL_SYMBOL_ENUMS_H
#define ISOGL_SYMBOL_ENUMS_H

#include "isogl_symbol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal, shared enum tables for symbol types (not installed).
 *
 * The ISO line-type and line-weight tables, and the dimension arrow table,
 * live here once so every symbol type references the same descriptors.  The
 * counts are compile-time constants usable in static initializers; static
 * asserts in isogl_symbol_enums.c tie them to LINE_TYPE_COUNT,
 * LINE_THICKNESS_COUNT and IsoglDimensionArrow.
 */

#define ISOGL_SYM_ARROW_VALUE_COUNT 7
#define ISOGL_SYM_LINE_TYPE_VALUE_COUNT 15
#define ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT 8

extern const isogl_symbol_enum_value
    isogl_symbol_arrow_values[ISOGL_SYM_ARROW_VALUE_COUNT];
extern const isogl_symbol_enum_value
    isogl_symbol_line_type_values[ISOGL_SYM_LINE_TYPE_VALUE_COUNT];
extern const isogl_symbol_enum_value
    isogl_symbol_line_weight_values[ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* ISOGL_SYMBOL_ENUMS_H */
