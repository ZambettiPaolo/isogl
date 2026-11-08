#ifndef ISOGL_FONT_DATA_H
#define ISOGL_FONT_DATA_H

#include "isogl_font.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal, pure CPU helpers over isogl_font_data (not installed).
 *
 * These live in isogl_font_data.c so they can be linked without GL or the
 * font/TOML loader.  isogl_font.c uses the same functions for glyph string
 * construction and for the public isogl_font_text_advance().
 */

#define ISOGL_FONT_REPLACEMENT_CP 0xFFFDu

uint32_t isogl_font_utf8_next(const char **cursor);

const isogl_glyph *isogl_font_find_glyph(const isogl_font_data *font,
                                         uint32_t codepoint);

#ifdef __cplusplus
}
#endif

#endif /* ISOGL_FONT_DATA_H */
