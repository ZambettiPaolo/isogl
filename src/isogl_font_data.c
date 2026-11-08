/*
 * Pure CPU helpers over isogl_font_data.
 *
 * No GL and no TOML: this translation unit is linked by the font renderer and
 * by symbol generators that need real advances (e.g. table_row) without
 * pulling the GL font pipeline.
 */

#include "isogl_font_data.h"

uint32_t isogl_font_utf8_next(const char **cursor) {
    const unsigned char *s = (const unsigned char *)*cursor;
    if (!s || *s == 0) return 0;

    uint32_t codepoint;
    int extra;

    if (s[0] < 0x80) {
        codepoint = s[0];
        extra = 0;
    } else if ((s[0] & 0xE0) == 0xC0) {
        codepoint = s[0] & 0x1Fu;
        extra = 1;
    } else if ((s[0] & 0xF0) == 0xE0) {
        codepoint = s[0] & 0x0Fu;
        extra = 2;
    } else if ((s[0] & 0xF8) == 0xF0) {
        codepoint = s[0] & 0x07u;
        extra = 3;
    } else {
        *cursor = (const char *)(s + 1);
        return ISOGL_FONT_REPLACEMENT_CP;
    }

    for (int i = 1; i <= extra; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            *cursor = (const char *)(s + 1);
            return ISOGL_FONT_REPLACEMENT_CP;
        }
        codepoint = (codepoint << 6) | (s[i] & 0x3Fu);
    }

    if ((extra == 1 && codepoint < 0x80) ||
        (extra == 2 && codepoint < 0x800) ||
        (extra == 3 && codepoint < 0x10000) ||
        codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        *cursor = (const char *)(s + 1);
        return ISOGL_FONT_REPLACEMENT_CP;
    }

    *cursor = (const char *)(s + extra + 1);
    return codepoint;
}

const isogl_glyph *isogl_font_find_glyph(const isogl_font_data *font,
                                         uint32_t codepoint) {
    if (!font || !font->glyphs) return NULL;
    for (uint32_t i = 0; i < font->glyph_count; ++i) {
        if (font->glyphs[i].codepoint == codepoint) return &font->glyphs[i];
    }
    return NULL;
}

double isogl_font_text_advance(const isogl_font_data *font, const char *text) {
    if (!font || !text) return 0.0;

    double advance = 0.0;
    for (const char *cursor = text; *cursor; ) {
        uint32_t codepoint = isogl_font_utf8_next(&cursor);
        const isogl_glyph *glyph = isogl_font_find_glyph(font, codepoint);
        if (glyph) advance += (double)glyph->advance;
    }
    return advance;
}
