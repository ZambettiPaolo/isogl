// isogl_font.c
#include "isogl_font.h"
#include "isogl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <locale.h>
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

static isogl_font_data* isogl_font_load_from_toml_impl(const char* filename) {
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

/*
 * tomlc99 parses floating point values with the C library's strtod(), which is
 * locale-sensitive.  In a comma-decimal locale (it_IT, de_DE, fr_FR, ...)
 * "advance = 320.0" fails to parse and the glyph advances stay zero, so every
 * string collapses onto itself.  Parse under the C numeric locale and restore
 * the host locale afterwards.  The library is documented as single-threaded.
 */
isogl_font_data* isogl_font_load_from_toml(const char* filename) {
    char saved[128];
    int restore = 0;
    const char *current = setlocale(LC_NUMERIC, NULL);
    if (current) {
        snprintf(saved, sizeof(saved), "%s", current);
        if (setlocale(LC_NUMERIC, "C")) restore = 1;
    }

    isogl_font_data *font = isogl_font_load_from_toml_impl(filename);

    if (restore) setlocale(LC_NUMERIC, saved);
    return font;
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

    // Pass 1: count the vertices of the complete GL_LINES string.
    // Each polyline of n points becomes 2*(n-1) independent vertices.
    uint32_t total_vertices = 0;
    for (const char *cursor = text; *cursor; ) {
        uint32_t codepoint = isogl_utf8_next(&cursor);
        const isogl_glyph *glyph = isogl_find_glyph(font, codepoint);
        if (!glyph) continue;

        for (uint32_t p = 0; p < glyph->polyline_count; ++p) {
            uint32_t count = glyph->polyline_indices[p].index_count;
            if (count < 2) continue;
            uint32_t add = 2u * (count - 1u);
            if (total_vertices > UINT32_MAX - add) {
                free(glyph_string->text);
                free(glyph_string);
                return NULL;
            }
            total_vertices += add;
        }
    }

    glyph_string->vertex_count = total_vertices;
    glyph_string->polyline_count = 0;

    // Nothing to draw (e.g. a string of spaces): valid empty string.
    if (total_vertices == 0) return glyph_string;

    isogl_font_vertex* all_vertices =
        calloc(total_vertices, sizeof(isogl_font_vertex));
    if (!all_vertices) {
        free(glyph_string->text);
        free(glyph_string);
        return NULL;
    }

    // Pass 2: place glyphs and emit independent segment pairs.
    uint32_t out = 0;
    float pen_x = 0.0f;
    int clamped = 0;

    for (const char *cursor = text; *cursor; ) {
        uint32_t codepoint = isogl_utf8_next(&cursor);
        const isogl_glyph *glyph = isogl_find_glyph(font, codepoint);
        if (!glyph) continue;

        for (uint32_t p = 0; p < glyph->polyline_count; ++p) {
            uint32_t start = glyph->polyline_indices[p].index_start;
            uint32_t count = glyph->polyline_indices[p].index_count;
            if (count < 2 || start >= glyph->vertex_count) continue;
            if (count > glyph->vertex_count - start) {
                count = glyph->vertex_count - start;
            }

            for (uint32_t j = 0; j + 1 < count; ++j) {
                const isogl_font_vertex *v0 = &glyph->vertices[start + j];
                const isogl_font_vertex *v1 = &glyph->vertices[start + j + 1];
                float x0 = (float)v0->x + pen_x;
                float x1 = (float)v1->x + pen_x;

                if (x0 > 32767.0f) { x0 = 32767.0f; clamped = 1; }
                if (x0 < -32768.0f) { x0 = -32768.0f; clamped = 1; }
                if (x1 > 32767.0f) { x1 = 32767.0f; clamped = 1; }
                if (x1 < -32768.0f) { x1 = -32768.0f; clamped = 1; }

                all_vertices[out].x = (int16_t)lrintf(x0);
                all_vertices[out].y = v0->y;
                all_vertices[out + 1].x = (int16_t)lrintf(x1);
                all_vertices[out + 1].y = v1->y;
                out += 2;
            }
        }

        pen_x += (float)glyph->advance;
    }

    if (out != total_vertices) {
        free(all_vertices);
        free(glyph_string->text);
        free(glyph_string);
        return NULL;
    }

    if (clamped) {
        fprintf(stderr,
                "isogl_font: string too wide for int16 vertices; coordinates clamped\n");
    }

    glGenBuffers(1, &glyph_string->vbo);
    if (glyph_string->vbo == 0) {
        free(all_vertices);
        free(glyph_string->text);
        free(glyph_string);
        return NULL;
    }

    glBindBuffer(GL_ARRAY_BUFFER, glyph_string->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(total_vertices * sizeof(isogl_font_vertex)),
                 all_vertices,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    free(all_vertices);
    return glyph_string;
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

static void isogl_glyph_string_free_contents(isogl_glyph_string *s) {
    if (!s) return;
    free(s->text);
    free(s->vertices);
    free(s->polyline_indices);
    free(s->first);
    free(s->count);
    s->text = NULL;
    s->vertices = NULL;
    s->polyline_indices = NULL;
    s->first = NULL;
    s->count = NULL;
    if (s->vbo != 0) {
        glDeleteBuffers(1, &s->vbo);
        s->vbo = 0;
    }
}

static int isogl_text_scale(const isogl_font_data *font, float height,
                            float global_scale, float *scale) {
    if (!font || !scale || !(font->height > 0.0f) ||
        !(height > 0.0f) || !(global_scale > 0.0f)) {
        return -1;
    }

    float value = height / (font->height * global_scale);
    if (!isfinite(value) || value <= 0.0f) return -1;

    *scale = value;
    return 0;
}

int isogl_string_list_update_by_pick_id(isogl_string_list *list,
                                        uint32_t pick_id,
                                        const char *text,
                                        isogl_font_data *font,
                                        float x, float y, float height,
                                        float global_scale, float angle) {
    if (!list || !text || !font) return -1;

    float scale = 0.0f;
    if (isogl_text_scale(font, height, global_scale, &scale) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < list->glyph_string_count; ++i) {
        if (list->glyph_strings[i].pick_id != pick_id) continue;

        isogl_glyph_string *fresh = isogl_create_glyph_string(text, font);
        if (!fresh) return -1;

        fresh->pick_id = pick_id;
        fresh->selected = list->glyph_strings[i].selected;
        isogl_create_string_matrix(fresh->string_matrix, x, y, scale, angle);

        isogl_glyph_string_free_contents(&list->glyph_strings[i]);
        list->glyph_strings[i] = *fresh;
        free(fresh);
        return 0;
    }
    return -1;
}

int isogl_string_list_set_transform_by_pick_id(isogl_string_list *list,
                                               uint32_t pick_id,
                                               isogl_font_data *font,
                                               float x, float y, float height,
                                               float global_scale,
                                               float angle) {
    if (!list) return -1;

    float scale = 0.0f;
    if (isogl_text_scale(font, height, global_scale, &scale) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < list->glyph_string_count; ++i) {
        if (list->glyph_strings[i].pick_id != pick_id) continue;
        isogl_create_string_matrix(list->glyph_strings[i].string_matrix,
                                   x, y, scale, angle);
        return 0;
    }
    return -1;
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
    int sel_active = 0;
    float sel_rect_fb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (isogl_get_selection_active() && isogl_get_selection_preview()) {
        isogl_viewport vp;
        if (isogl_get_viewport(&vp) == 0) {
            const float *r = isogl_get_selection_rect();
            sel_active = 1;
            /* Local top-left -> framebuffer-absolute bottom-left, the space
               gl_FragCoord uses in fs_font.glsl. */
            sel_rect_fb[0] = (float)vp.x + r[0];
            sel_rect_fb[1] = (float)vp.y + (float)vp.height - r[3];
            sel_rect_fb[2] = (float)vp.x + r[2];
            sel_rect_fb[3] = (float)vp.y + (float)vp.height - r[1];
        }
    }
    if (isogl_font_u.sel_active != -1)
        glUniform1i(isogl_font_u.sel_active, sel_active);
    if (isogl_font_u.sel_rect != -1)
        glUniform4fv(isogl_font_u.sel_rect, 1, sel_rect_fb);
    if (isogl_font_u.sel_color != -1)
        glUniform4fv(isogl_font_u.sel_color, 1, isogl_get_selection_color());

    glBindVertexArray(isogl_font_vao);
    glEnableVertexAttribArray(0);

    for (uint32_t s = 0; s < string_list->glyph_string_count; ++s) {
        isogl_glyph_string* glyph_string = &string_list->glyph_strings[s];

        if (glyph_string->vertex_count == 0 || glyph_string->vbo == 0) continue;

        if (isogl_font_u.string != -1)
            glUniformMatrix4fv(isogl_font_u.string, 1, GL_FALSE, glyph_string->string_matrix);
        if (isogl_font_u.selected != -1)
            glUniform1i(isogl_font_u.selected, glyph_string->selected ? 1 : 0);

        glBindBuffer(GL_ARRAY_BUFFER, glyph_string->vbo);
        glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE,
                              sizeof(isogl_font_vertex), (void*)0);

        glDrawArrays(GL_LINES, 0, (GLsizei)glyph_string->vertex_count);
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
        if (glyph_string->vertex_count == 0 || glyph_string->vbo == 0) continue;

        if (isogl_font_u.string != -1)
            glUniformMatrix4fv(isogl_font_u.string, 1, GL_FALSE,
                               glyph_string->string_matrix);
        if (isogl_font_u.pick_id != -1)
            glUniform1ui(isogl_font_u.pick_id, glyph_string->pick_id);

        glBindBuffer(GL_ARRAY_BUFFER, glyph_string->vbo);
        glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE,
                              sizeof(isogl_font_vertex), (void*)0);

        glDrawArrays(GL_LINES, 0, (GLsizei)glyph_string->vertex_count);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

int isogl_string_list_remove_by_pick_id(isogl_string_list *list,
                                        uint32_t pick_id) {
    if (!list) return -1;

    for (uint32_t i = 0; i < list->glyph_string_count; ++i) {
        if (list->glyph_strings[i].pick_id != pick_id) continue;

        isogl_glyph_string *s = &list->glyph_strings[i];
        isogl_glyph_string_free_contents(s);

        for (uint32_t j = i; j + 1 < list->glyph_string_count; ++j) {
            list->glyph_strings[j] = list->glyph_strings[j + 1];
        }
        list->glyph_string_count--;
        return 0;
    }
    return 1;
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
