#ifndef ISOGL_FONT_H
#define ISOGL_FONT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "glad.h"

// ============================================================================
// CPU-SIDE STRUCTURES
// ============================================================================
typedef struct {
    int16_t x;
    int16_t y;
} isogl_font_vertex;

typedef struct {
    GLuint index_start;
    GLuint index_count;
} isogl_font_index_polyline;

// Glyphs as char of font
typedef struct {
    GLuint codepoint;
    isogl_font_vertex* vertices;     // vertices of all polyline
    uint32_t vertex_count;
    isogl_font_index_polyline* polyline_indices;
    uint32_t polyline_count;
    uint16_t advance;
} isogl_glyph;

// font in RAM is an array of glyphs
typedef struct {
    float ascent;
    float descent;
    float height;
    float line_gap;
    isogl_glyph* glyphs;
    uint32_t glyph_count;
} isogl_font_data;

// string in RAM as a big glyph
typedef struct {
    char* text;                      // Text of the string
    uint32_t vertex_count;           // Total number of vertices
    uint32_t polyline_count;         // Total number of polylines

    isogl_font_vertex* vertices;     // Pointer to vertices (optional if using only VBO)
    isogl_font_index_polyline* polyline_indices; // Start and length indices for each polyline

    GLint* first;                    // Pre-compiled array for glMultiDrawArrays
    GLsizei* count;                  // Pre-compiled array for glMultiDrawArrays

    GLuint vbo;                      // VBO of vertices
    uint32_t pick_id;                // Opaque ID for the pick pass (0 = not pickable)
    int selected;                    // Persistent selection highlight (0/1)
    float string_matrix[16];         // String transformation matrix
} isogl_glyph_string;

typedef struct {
    isogl_glyph_string* glyph_strings;
    uint32_t glyph_string_count;
} isogl_string_list;

// ============================================================================
// PUBLIC API
// ============================================================================

isogl_font_data* isogl_font_load_from_toml(const char* filename);
void isogl_font_free_data(isogl_font_data* font);
isogl_glyph_string* isogl_create_glyph_string(const char* text, isogl_font_data* font);

/*
 * Sum of the glyph advances of a UTF-8 string, in font units.  Pure CPU;
 * missing codepoints are skipped exactly like in isogl_create_glyph_string().
 * Callers convert to model units with height / (font->height * sheet_scale).
 */
double isogl_font_text_advance(const isogl_font_data* font, const char* text);

// Text colour used by the font shader (RGBA, default white).
void isogl_font_set_color(const float rgba[4]);

// Draw a string list with the normal text fragment shader.
void isogl_draw_string_list(isogl_string_list* string_list, GLuint shader_program,
                           float model_matrix[16],      // Zoom
                           float view_matrix[16],       // Aspect ratio
                           float relative_matrix[16]);  // Block → World

/*
 * Draw only pickable strings (pick_id != 0) into the current pick target,
 * applying the pick scale/offset after the normal text transform.  The
 * fragment shader is expected to write uPickId as an integer.
 */
void isogl_draw_string_list_pick(isogl_string_list* string_list, GLuint shader_program,
                                 float model_matrix[16],
                                 float view_matrix[16],
                                 float relative_matrix[16],
                                 float pick_scale_x, float pick_scale_y,
                                 float pick_offset_x, float pick_offset_y);

/*
 * Append a string to the list.
 * Ownership of the glyph_string wrapper moves into the list: do not free it
 * afterwards.  On failure ownership stays with the caller.
 * pick_id is the opaque ID written by the pick pass (0 = not pickable).
 */
bool isogl_string_list_append(isogl_string_list* list, isogl_glyph_string* glyph_string,
                              float x, float y, float height, float global_scale,
                              float angle, isogl_font_data* font,
                              uint32_t pick_id);

/*
 * Set the persistent selection flag on the strings whose pick_id is in
 * entity_ids.  Strings not in the set are cleared first.  Returns 0 on
 * success, -1 on invalid arguments or allocation failure.
 */
int isogl_string_list_apply_selection(isogl_string_list* list,
                                      const uint32_t* entity_ids, size_t count);

// Free every string in the list, including their CPU arrays and VBOs.
void isogl_string_list_free(isogl_string_list* list);

void isogl_font_init_vao(void);
void isogl_font_cleanup(void);
void isogl_free_glyph_string(isogl_glyph_string* glyph_string);

#endif // ISOGL_FONT_H
