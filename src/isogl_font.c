// isogl_font.c
#include "isogl_font.h"
#include "isogl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "glad.h"
#include "toml.h"

#define ISOGL_TOML_SEPARATOR 0xFFFFFFu
#define ISOGL_REPLACEMENT_CP 0xFFFDu

// ============================================================================
// UTF-8
// ============================================================================
static uint32_t isogl_utf8_next(const char **cursor) {
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
        return ISOGL_REPLACEMENT_CP;
    }

    for (int i = 1; i <= extra; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            *cursor = (const char *)(s + 1);
            return ISOGL_REPLACEMENT_CP;
        }
        codepoint = (codepoint << 6) | (s[i] & 0x3Fu);
    }

    if ((extra == 1 && codepoint < 0x80) ||
        (extra == 2 && codepoint < 0x800) ||
        (extra == 3 && codepoint < 0x10000) ||
        codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        *cursor = (const char *)(s + 1);
        return ISOGL_REPLACEMENT_CP;
    }

    *cursor = (const char *)(s + extra + 1);
    return codepoint;
}

static const isogl_glyph *isogl_find_glyph(const isogl_font_data *font,
                                           uint32_t codepoint) {
    for (uint32_t i = 0; i < font->glyph_count; ++i) {
        if (font->glyphs[i].codepoint == codepoint) return &font->glyphs[i];
    }
    return NULL;
}

double isogl_font_text_advance(const isogl_font_data *font, const char *text) {
    if (!font || !text) return 0.0;

    double advance = 0.0;
    for (const char *cursor = text; *cursor; ) {
        uint32_t codepoint = isogl_utf8_next(&cursor);
        const isogl_glyph *glyph = isogl_find_glyph(font, codepoint);
        if (glyph) advance += (double)glyph->advance;
    }
    return advance;
}

// ============================================================================
// TOML PARSING
// ============================================================================
static void isogl_store_polyline(isogl_glyph *glyph, uint32_t *poly_idx,
                                 uint32_t start, uint32_t count) {
    if (count == 0) return;
    if (start >= glyph->vertex_count) return;
    if (count > glyph->vertex_count - start) {
        count = glyph->vertex_count - start;
    }

    glyph->polyline_indices[*poly_idx].index_start = start;
    glyph->polyline_indices[*poly_idx].index_count = count;
    (*poly_idx)++;
}

isogl_font_data* isogl_font_load_from_toml(const char* filename) {
    char errbuf[256];
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "isogl_font: cannot open TOML file: %s\n", filename);
        return NULL;
    }

    toml_table_t* doc = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!doc) {
        fprintf(stderr, "isogl_font: TOML parse error: %s\n", errbuf);
        return NULL;
    }

    isogl_font_data* font = calloc(1, sizeof(isogl_font_data));
    if (!font) {
        toml_free(doc);
        return NULL;
    }

    toml_datum_t d;
    d = toml_double_in(doc, "ascent");
    if (d.ok) font->ascent = (float)d.u.d;
    d = toml_double_in(doc, "descent");
    if (d.ok) font->descent = (float)d.u.d;
    d = toml_double_in(doc, "height");
    if (d.ok) font->height = (float)d.u.d;
    d = toml_double_in(doc, "line_gap");
    if (d.ok) font->line_gap = (float)d.u.d;

    toml_array_t* glyphs = toml_array_in(doc, "glyphs");
    if (!glyphs) {
        fprintf(stderr, "isogl_font: no [[glyphs]] array found\n");
        toml_free(doc);
        free(font);
        return NULL;
    }

    int glyph_nelem = toml_array_nelem(glyphs);
    if (glyph_nelem <= 0) {
        fprintf(stderr, "isogl_font: empty [[glyphs]] array\n");
        toml_free(doc);
        free(font);
        return NULL;
    }

    uint32_t glyph_count = (uint32_t)glyph_nelem;
    font->glyphs = calloc(glyph_count, sizeof(isogl_glyph));
    font->glyph_count = glyph_count;
    if (!font->glyphs) {
        toml_free(doc);
        free(font);
        return NULL;
    }

    for (uint32_t i = 0; i < glyph_count; ++i) {
        toml_table_t* g = toml_table_at(glyphs, (int)i);
        if (!g) continue;

        isogl_glyph* glyph = &font->glyphs[i];

        toml_datum_t cp = toml_int_in(g, "codepoint");
        if (cp.ok && cp.u.i >= 0 && cp.u.i <= 0x10FFFF) {
            glyph->codepoint = (uint32_t)cp.u.i;
        }

        toml_datum_t adv = toml_double_in(g, "advance");
        if (adv.ok) {
            double value = adv.u.d;
            if (!(value >= 0.0)) value = 0.0;
            if (value > 65535.0) value = 65535.0;
            glyph->advance = (uint16_t)lround(value);
        }

        toml_array_t* verts = toml_array_in(g, "vertices");
        if (verts) {
            int nv = toml_array_nelem(verts);
            if (nv > 0) {
                glyph->vertices = calloc((size_t)nv, sizeof(isogl_font_vertex));
                if (!glyph->vertices) goto fail;
                glyph->vertex_count = (uint32_t)nv;

                for (int v = 0; v < nv; ++v) {
                    toml_array_t* pair = toml_array_at(verts, v);
                    if (!pair || toml_array_nelem(pair) < 2) continue;

                    toml_datum_t dx = toml_double_at(pair, 0);
                    toml_datum_t dy = toml_double_at(pair, 1);

                    float fx = dx.ok ? (float)dx.u.d : 0.0f;
                    float fy = dy.ok ? (float)dy.u.d : 0.0f;

                    if (!isfinite(fx)) fx = 0.0f;
                    if (!isfinite(fy)) fy = 0.0f;
                    if (fx >  32767.0f) fx =  32767.0f;
                    if (fx < -32768.0f) fx = -32768.0f;
                    if (fy >  32767.0f) fy =  32767.0f;
                    if (fy < -32768.0f) fy = -32768.0f;

                    glyph->vertices[v].x = (int16_t)lrintf(fx);
                    glyph->vertices[v].y = (int16_t)lrintf(fy);
                }
            }
        }

        toml_array_t* inds = toml_array_in(g, "indices");
        if (inds) {
            int ni = toml_array_nelem(inds);
            if (ni > 0) {
                // Pass 1: count non-empty runs (0xFFFFFF is the separator).
                uint32_t run_count = 0;
                int in_run = 0;
                for (int j = 0; j < ni; ++j) {
                    toml_datum_t id = toml_int_at(inds, j);
                    if (!id.ok) continue;
                    uint32_t v = (uint32_t)id.u.i;
                    if (v == ISOGL_TOML_SEPARATOR) {
                        if (in_run) {
                            run_count++;
                            in_run = 0;
                        }
                    } else {
                        in_run = 1;
                    }
                }
                if (in_run) run_count++;

                glyph->polyline_count = run_count;
                if (run_count > 0) {
                    glyph->polyline_indices =
                        calloc(run_count, sizeof(isogl_font_index_polyline));
                    if (!glyph->polyline_indices) goto fail;

                    // Pass 2: fill ranges, ignoring the index values themselves.
                    uint32_t poly_idx = 0;
                    uint32_t sequential = 0;
                    uint32_t start = 0;
                    uint32_t count = 0;

                    for (int j = 0; j < ni; ++j) {
                        toml_datum_t id = toml_int_at(inds, j);
                        if (!id.ok) continue;
                        uint32_t v = (uint32_t)id.u.i;

                        if (v == ISOGL_TOML_SEPARATOR) {
                            if (count > 0) {
                                isogl_store_polyline(glyph, &poly_idx, start, count);
                                start = sequential;
                                count = 0;
                            }
                        } else {
                            sequential++;
                            count++;
                        }
                    }
                    if (count > 0) {
                        isogl_store_polyline(glyph, &poly_idx, start, count);
                    }

                    glyph->polyline_count = poly_idx;
                }
            }
        }
    }

    toml_free(doc);

    if (!(font->height > 0.0f)) {
        font->height = font->ascent - font->descent;
    }
    if (!(font->height > 0.0f)) {
        fprintf(stderr, "isogl_font: missing or invalid 'height' in %s\n", filename);
        isogl_font_free_data(font);
        return NULL;
    }

#ifdef ISOGL_FONT_DEBUG
    printf("isogl_font: %u glyphs, height %.1f, ascent %.1f, descent %.1f\n",
           font->glyph_count, font->height, font->ascent, font->descent);
#endif

    return font;

fail:
    toml_free(doc);
    isogl_font_free_data(font);
    return NULL;
}

void isogl_font_free_data(isogl_font_data* font) {
    if (!font) return;

    if (font->glyphs) {
        for (uint32_t i = 0; i < font->glyph_count; ++i) {
            free(font->glyphs[i].vertices);
            free(font->glyphs[i].polyline_indices);
        }
    }

    free(font->glyphs);
    free(font);
}

// ============================================================================
// GPU UPLOAD
// ============================================================================

static GLuint isogl_font_vao;
static float isogl_font_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

void isogl_font_init_vao(void) {
    if (isogl_font_vao != 0) return;
    glGenVertexArrays(1, &isogl_font_vao);
}

void isogl_font_cleanup(void) {
    if (isogl_font_vao != 0) {
        glDeleteVertexArrays(1, &isogl_font_vao);
        isogl_font_vao = 0;
    }
}

void isogl_font_set_color(const float rgba[4]) {
    if (!rgba) return;
    memcpy(isogl_font_color, rgba, sizeof(isogl_font_color));
}

isogl_glyph_string* isogl_create_glyph_string(const char* text, isogl_font_data* font) {
    if (!text || !font) return NULL;

    isogl_glyph_string* glyph_string = calloc(1, sizeof(isogl_glyph_string));
    if (!glyph_string) return NULL;

    size_t text_len = strlen(text);
    glyph_string->text = malloc(text_len + 1);
    if (!glyph_string->text) {
        free(glyph_string);
        return NULL;
    }
    memcpy(glyph_string->text, text, text_len + 1);

    // Pass 1: total vertices and polylines.
    uint32_t total_vertices = 0;
    uint32_t total_polylines = 0;
    for (const char *cursor = text; *cursor; ) {
        uint32_t codepoint = isogl_utf8_next(&cursor);
        const isogl_glyph *glyph = isogl_find_glyph(font, codepoint);
        if (!glyph) continue;
        total_vertices += glyph->vertex_count;
        total_polylines += glyph->polyline_count;
    }

    glyph_string->vertex_count = total_vertices;
    glyph_string->polyline_count = total_polylines;

    // Nothing to draw (e.g. a string of spaces): valid empty string.
    if (total_vertices == 0 || total_polylines == 0) {
        glyph_string->vertex_count = 0;
        glyph_string->polyline_count = 0;
        return glyph_string;
    }

    isogl_font_vertex* all_vertices =
        calloc(total_vertices, sizeof(isogl_font_vertex));
    if (!all_vertices) goto fail;

    glyph_string->polyline_indices =
        calloc(total_polylines, sizeof(isogl_font_index_polyline));
    if (!glyph_string->polyline_indices) goto fail_vertices;

    glyph_string->first = calloc(total_polylines, sizeof(GLint));
    glyph_string->count = calloc(total_polylines, sizeof(GLsizei));
    if (!glyph_string->first || !glyph_string->count) goto fail_indices;

    // Pass 2: place glyphs and build the glMultiDrawArrays ranges.
    uint32_t vertex_offset = 0;
    uint32_t polyline_offset = 0;
    float pen_x = 0.0f;
    int clamped = 0;

    for (const char *cursor = text; *cursor; ) {
        uint32_t codepoint = isogl_utf8_next(&cursor);
        const isogl_glyph *glyph = isogl_find_glyph(font, codepoint);
        if (!glyph) continue;

        for (uint32_t v = 0; v < glyph->vertex_count; ++v) {
            float x = (float)glyph->vertices[v].x + pen_x;
            if (x > 32767.0f) { x = 32767.0f; clamped = 1; }
            if (x < -32768.0f) { x = -32768.0f; clamped = 1; }
            all_vertices[vertex_offset + v].x = (int16_t)lrintf(x);
            all_vertices[vertex_offset + v].y = glyph->vertices[v].y;
        }

        for (uint32_t p = 0; p < glyph->polyline_count; ++p) {
            uint32_t start = vertex_offset + glyph->polyline_indices[p].index_start;
            uint32_t count = glyph->polyline_indices[p].index_count;

            glyph_string->polyline_indices[polyline_offset].index_start = start;
            glyph_string->polyline_indices[polyline_offset].index_count = count;
            glyph_string->first[polyline_offset] = (GLint)start;
            glyph_string->count[polyline_offset] = (GLsizei)count;
            polyline_offset++;
        }

        vertex_offset += glyph->vertex_count;
        pen_x += (float)glyph->advance;
    }

    if (clamped) {
        fprintf(stderr,
                "isogl_font: string too wide for int16 vertices; coordinates clamped\n");
    }

    glGenBuffers(1, &glyph_string->vbo);
    if (glyph_string->vbo == 0) goto fail_indices;

    glBindBuffer(GL_ARRAY_BUFFER, glyph_string->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(total_vertices * sizeof(isogl_font_vertex)),
                 all_vertices,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    free(all_vertices);
    return glyph_string;

fail_indices:
    free(glyph_string->polyline_indices);
    glyph_string->polyline_indices = NULL;
    free(glyph_string->first);
    glyph_string->first = NULL;
    free(glyph_string->count);
    glyph_string->count = NULL;
fail_vertices:
    free(all_vertices);
fail:
    free(glyph_string->text);
    free(glyph_string);
    return NULL;
}

void isogl_create_string_matrix(float* m, float x, float y, float scale, float angle) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;

    float c = cosf(angle);
    float s = sinf(angle);

    m[0] =  scale * c;
    m[1] =  scale * s;
    m[4] = -scale * s;
    m[5] =  scale * c;
    m[10] = 1.0f;
    m[12] = x;
    m[13] = y;
    m[15] = 1.0f;
}

bool isogl_string_list_append(isogl_string_list* list, isogl_glyph_string* glyph_string,
                              float x, float y, float height, float global_scale,
                              float angle, isogl_font_data* font,
                              uint32_t pick_id) {
    if (!list || !glyph_string || !font) return false;
    if (!(font->height > 0.0f) || !(global_scale > 0.0f) || !(height > 0.0f)) {
        return false;
    }

    float scale = height / (font->height * global_scale);

    isogl_glyph_string* grown = realloc(
        list->glyph_strings,
        (list->glyph_string_count + 1) * sizeof(isogl_glyph_string));
    if (!grown) {
        fprintf(stderr, "isogl_string_list_append: realloc failed\n");
        return false;
    }

    list->glyph_strings = grown;

    // Ownership moves into the list: the struct content is copied and only
    // the heap wrapper is released.  On failure above the caller keeps it.
    list->glyph_strings[list->glyph_string_count] = *glyph_string;
    free(glyph_string);

    isogl_create_string_matrix(list->glyph_strings[list->glyph_string_count].string_matrix,
                               x, y, scale, angle);
    list->glyph_strings[list->glyph_string_count].pick_id = pick_id;
    list->glyph_strings[list->glyph_string_count].selected = 0;
    list->glyph_string_count++;
    return true;
}

static int isogl_font_compare_u32(const void *a, const void *b) {
    uint32_t va = *(const uint32_t *)a;
    uint32_t vb = *(const uint32_t *)b;
    return (va > vb) - (va < vb);
}

int isogl_string_list_apply_selection(isogl_string_list *list,
                                      const uint32_t *entity_ids,
                                      size_t count) {
    if (!list) return -1;
    if (count > 0 && !entity_ids) return -1;
    if (count > SIZE_MAX / sizeof(uint32_t)) return -1;

    for (uint32_t i = 0; i < list->glyph_string_count; ++i) {
        list->glyph_strings[i].selected = 0;
    }
    if (count == 0) return 0;

    uint32_t *sorted = malloc(count * sizeof(uint32_t));
    if (!sorted) return -1;
    memcpy(sorted, entity_ids, count * sizeof(uint32_t));
    qsort(sorted, count, sizeof(uint32_t), isogl_font_compare_u32);

    for (uint32_t i = 0; i < list->glyph_string_count; ++i) {
        uint32_t pick_id = list->glyph_strings[i].pick_id;
        if (pick_id == 0) continue;
        if (bsearch(&pick_id, sorted, count, sizeof(uint32_t),
                    isogl_font_compare_u32) != NULL) {
            list->glyph_strings[i].selected = 1;
        }
    }

    free(sorted);
    return 0;
}

// Cached font uniforms.
typedef struct {
    GLuint program;
    GLint model;
    GLint view;
    GLint relative;
    GLint string;
    GLint color;
    GLint pick_scale;
    GLint pick_offset;
    GLint pick_id;
    GLint sel_active;
    GLint sel_rect;
    GLint sel_color;
    GLint selected;
} isogl_font_uniforms;

static isogl_font_uniforms isogl_font_u;

static void isogl_cache_font_uniforms(GLuint program) {
    if (isogl_font_u.program == program) return;
    isogl_font_u.program = program;
    isogl_font_u.model = glGetUniformLocation(program, "model_matrix");
    isogl_font_u.view = glGetUniformLocation(program, "view_matrix");
    isogl_font_u.relative = glGetUniformLocation(program, "relative_matrix");
    isogl_font_u.string = glGetUniformLocation(program, "string_matrix");
    isogl_font_u.color = glGetUniformLocation(program, "uColor");
    isogl_font_u.pick_scale = glGetUniformLocation(program, "uPickScale");
    isogl_font_u.pick_offset = glGetUniformLocation(program, "uPickOffset");
    isogl_font_u.pick_id = glGetUniformLocation(program, "uPickId");
    isogl_font_u.sel_active = glGetUniformLocation(program, "uSelActive");
    isogl_font_u.sel_rect = glGetUniformLocation(program, "uSelRect");
    isogl_font_u.sel_color = glGetUniformLocation(program, "uSelColor");
    isogl_font_u.selected = glGetUniformLocation(program, "uSelected");
}

void isogl_draw_string_list(isogl_string_list* string_list, GLuint shader_program,
                            float model_matrix[16],
                            float view_matrix[16],
                            float relative_matrix[16]) {
    if (!string_list || string_list->glyph_string_count == 0) return;
    if (shader_program == 0) return;
    if (isogl_font_vao == 0) isogl_font_init_vao();

    glUseProgram(shader_program);
    isogl_cache_font_uniforms(shader_program);

    if (isogl_font_u.view != -1)
        glUniformMatrix4fv(isogl_font_u.view, 1, GL_FALSE, view_matrix);
    if (isogl_font_u.model != -1)
        glUniformMatrix4fv(isogl_font_u.model, 1, GL_FALSE, model_matrix);
    if (isogl_font_u.relative != -1)
        glUniformMatrix4fv(isogl_font_u.relative, 1, GL_FALSE, relative_matrix);
    if (isogl_font_u.color != -1)
        glUniform4fv(isogl_font_u.color, 1, isogl_font_color);
    if (isogl_font_u.pick_scale != -1)
        glUniform2f(isogl_font_u.pick_scale, 1.0f, 1.0f);
    if (isogl_font_u.pick_offset != -1)
        glUniform2f(isogl_font_u.pick_offset, 0.0f, 0.0f);
    if (isogl_font_u.sel_active != -1)
        glUniform1i(isogl_font_u.sel_active,
                    isogl_get_selection_active() &&
                    isogl_get_selection_preview());
    if (isogl_font_u.sel_rect != -1)
        glUniform4fv(isogl_font_u.sel_rect, 1, isogl_get_selection_rect());
    if (isogl_font_u.sel_color != -1)
        glUniform4fv(isogl_font_u.sel_color, 1, isogl_get_selection_color());

    glBindVertexArray(isogl_font_vao);
    glEnableVertexAttribArray(0);

    for (uint32_t s = 0; s < string_list->glyph_string_count; ++s) {
        isogl_glyph_string* glyph_string = &string_list->glyph_strings[s];

        if (glyph_string->polyline_count == 0 || glyph_string->vbo == 0) continue;

        if (isogl_font_u.string != -1)
            glUniformMatrix4fv(isogl_font_u.string, 1, GL_FALSE, glyph_string->string_matrix);
        if (isogl_font_u.selected != -1)
            glUniform1i(isogl_font_u.selected, glyph_string->selected ? 1 : 0);

        glBindBuffer(GL_ARRAY_BUFFER, glyph_string->vbo);
        glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE,
                              sizeof(isogl_font_vertex), (void*)0);

        glMultiDrawArrays(GL_LINE_STRIP, glyph_string->first, glyph_string->count,
                          (GLsizei)glyph_string->polyline_count);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void isogl_draw_string_list_pick(isogl_string_list* string_list, GLuint shader_program,
                                 float model_matrix[16],
                                 float view_matrix[16],
                                 float relative_matrix[16],
                                 float pick_scale_x, float pick_scale_y,
                                 float pick_offset_x, float pick_offset_y) {
    if (!string_list || string_list->glyph_string_count == 0) return;
    if (shader_program == 0) return;
    if (isogl_font_vao == 0) isogl_font_init_vao();

    glUseProgram(shader_program);
    isogl_cache_font_uniforms(shader_program);

    if (isogl_font_u.view != -1)
        glUniformMatrix4fv(isogl_font_u.view, 1, GL_FALSE, view_matrix);
    if (isogl_font_u.model != -1)
        glUniformMatrix4fv(isogl_font_u.model, 1, GL_FALSE, model_matrix);
    if (isogl_font_u.relative != -1)
        glUniformMatrix4fv(isogl_font_u.relative, 1, GL_FALSE, relative_matrix);
    if (isogl_font_u.pick_scale != -1)
        glUniform2f(isogl_font_u.pick_scale, pick_scale_x, pick_scale_y);
    if (isogl_font_u.pick_offset != -1)
        glUniform2f(isogl_font_u.pick_offset, pick_offset_x, pick_offset_y);

    glBindVertexArray(isogl_font_vao);
    glEnableVertexAttribArray(0);

    for (uint32_t s = 0; s < string_list->glyph_string_count; ++s) {
        isogl_glyph_string* glyph_string = &string_list->glyph_strings[s];
        if (glyph_string->pick_id == 0) continue;
        if (glyph_string->polyline_count == 0 || glyph_string->vbo == 0) continue;

        if (isogl_font_u.string != -1)
            glUniformMatrix4fv(isogl_font_u.string, 1, GL_FALSE,
                               glyph_string->string_matrix);
        if (isogl_font_u.pick_id != -1)
            glUniform1ui(isogl_font_u.pick_id, glyph_string->pick_id);

        glBindBuffer(GL_ARRAY_BUFFER, glyph_string->vbo);
        glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE,
                              sizeof(isogl_font_vertex), (void*)0);

        glMultiDrawArrays(GL_LINE_STRIP, glyph_string->first, glyph_string->count,
                          (GLsizei)glyph_string->polyline_count);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void isogl_free_glyph_string(isogl_glyph_string* glyph_string) {
    if (!glyph_string) return;

    free(glyph_string->text);
    free(glyph_string->polyline_indices);
    free(glyph_string->first);
    free(glyph_string->count);
    if (glyph_string->vbo != 0) {
        glDeleteBuffers(1, &glyph_string->vbo);
    }
    free(glyph_string);
}

void isogl_string_list_free(isogl_string_list* list) {
    if (!list) return;

    for (uint32_t i = 0; i < list->glyph_string_count; ++i) {
        isogl_glyph_string* glyph_string = &list->glyph_strings[i];
        free(glyph_string->text);
        free(glyph_string->polyline_indices);
        free(glyph_string->first);
        free(glyph_string->count);
        if (glyph_string->vbo != 0) {
            glDeleteBuffers(1, &glyph_string->vbo);
        }
    }

    free(list->glyph_strings);
    list->glyph_strings = NULL;
    list->glyph_string_count = 0;
}
