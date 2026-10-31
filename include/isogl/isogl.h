#ifndef ISOGL_H
#define ISOGL_H

#include "glad.h"
#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include "isogl_font.h"
#include "isogl_style.h"

/*
 * Public contract
 * ---------------
 * - The application owns the SDL window and the OpenGL context. isogl never
 *   creates or destroys them.
 * - gladLoadGLLoader() must be called while the context is current, before
 *   isogl_init() / isogl_init_with_asset_dir().
 * - All state is singleton state bound to the current context: not reentrant,
 *   not thread-safe, one context at a time.
 * - isogl_cleanup() must be called while the context is still current and is
 *   idempotent.
 * - Only ISOGL_VBO_SEGMENT is implemented in this version.
 */

// Shader program handles. Read-only for applications; owned by the library.
extern GLuint isogl_shader_program_segment;
extern GLuint isogl_shader_program_sheet;
extern GLuint isogl_shader_program_font;
extern GLuint isogl_shader_program_pick;
extern GLuint isogl_shader_program_font_pick;

/* Side, in window pixels, of the logical square box used by isogl_pick_at(). */
#define ISOGL_PICK_SIZE 8
/* Side, in texels, of the offscreen pick texture (2x definition). */
#define ISOGL_PICK_TEXTURE_SIZE (ISOGL_PICK_SIZE * 2)


typedef struct {
    float x;
    float y;
} isogl_point;


typedef struct {
    int num;
    int den;
} isogl_scale;


/**
 * @brief CPU-side temporary dynamic array used to fill a VBO with vertex pairs.
 *
 * Each pair of vertices represents a single line segment and is rendered accordingly.
 * This structure is intended to be consumed by the segment rendering pipeline
 * (vs_segment, gs_segment, fs_segment).
 *
 * Packed layout of pk_thickness_pattern_layer (see isogl_style.h):
 *   bits 0-4 thickness, bits 5-9 pattern, bits 10-14 layer.
 */
typedef struct {
    float   position[2];       // layout(location = 0) - 8 bytes
    float   color[4];          // layout(location = 1) - 16 bytes
    float   distanceFromStart; // layout(location = 2) - 4 bytes
    uint16_t pk_thickness_pattern_layer;       // layout(location = 3) - 2 bytes
    uint16_t padding;                          // keeps the stride at 32 bytes
} isogl_vertex; // Total: 32 bytes

_Static_assert(sizeof(isogl_vertex) == 32, "isogl_vertex must stay 32 bytes");
_Static_assert(offsetof(isogl_vertex, position) == 0, "unexpected vertex layout");
_Static_assert(offsetof(isogl_vertex, color) == 8, "unexpected vertex layout");
_Static_assert(offsetof(isogl_vertex, distanceFromStart) == 24, "unexpected vertex layout");
_Static_assert(offsetof(isogl_vertex, pk_thickness_pattern_layer) == 28,
               "unexpected vertex layout");

/**
 * @brief Dynamic array managing a collection of isogl_vertex objects.
 *
 * Used as a staging buffer before uploading data to the GPU.
 */
typedef struct {
    isogl_vertex *vertex;
    size_t size;
    size_t capacity;
} isogl_segment_list;

typedef enum {
    ISOGL_VBO_SEGMENT = 0,
    ISOGL_VBO_TEXT,
    ISOGL_VBO_COMMAND
} isogl_vbo_type;


/**
 * @brief Structure to store VBO information and transformations
 */
typedef struct {
    GLuint vbo_id;              // OpenGL VBO identifier
    size_t vertex_count;        // Number of vertices in this VBO
    int visible;                // Visibility flag (0 = hidden, 1 = visible)
    int selected;               // Persistent selection highlight (0/1)
    float transform[16];        // 4x4 transformation matrix (column-major for OpenGL)
    isogl_vbo_type type;        // selector for shader program
    uint32_t pick_id;           // opaque ID written by the pick pass (0 = not pickable)
} isogl_vbo_object;


/**
 * @brief Dynamic array to manage multiple VBO objects
 */
typedef struct {
    isogl_vbo_object *objects;  // Array of VBO objects
    size_t size;                // Current number of objects
    size_t capacity;            // Maximum capacity before reallocation
} isogl_vbo_array;


// Matrici di trasformazione
extern float isogl_model_matrix[16];
extern float isogl_view_matrix[16];

/**
 * @brief CPU staging list helpers (isogl_vertex pairs).
 *
 * The packed TPP layout and the ISO style helpers live in isogl_style.h.
 */
int isogl_segment_list_init(isogl_segment_list *list, size_t initial_capacity);
void isogl_segment_list_free(isogl_segment_list *list);
int isogl_segment_list_push_pair(isogl_segment_list *list, isogl_vertex v0, isogl_vertex v1);


// Inizializzazione e cleanup
int isogl_init(void);
int isogl_init_with_asset_dir(const char *asset_dir);
const char *isogl_asset_dir(void);
int isogl_asset_path(const char *relative, char *out, size_t out_size);
void isogl_cleanup(void);

GLuint isogl_load_shader(GLenum shader_type, char* shader_file_name);
GLuint isogl_compile_shader(GLenum shader_type, char* source);
GLuint isogl_create_shader_program(char *vertex_shader_source, char *geometry_shader_source, char *fragment_shader_source);

// Gestione sheet (foglio)
int isogl_set_sheet(float width, float height, isogl_scale scale);
void isogl_draw_sheet(void);
void isogl_zoom_all_sheet(void);
void isogl_toggle_color_thickness(void);

// Gestione VBO array
int isogl_vbo_array_init(isogl_vbo_array* vbo_array);
GLuint isogl_vbo_array_add(isogl_vbo_array* vbo_array, const isogl_segment_list* segment_list, isogl_vbo_type type);
int isogl_vbo_array_update(isogl_vbo_array* vbo_array, GLuint id, const isogl_segment_list* segment_list);
int isogl_vbo_array_set_transform(isogl_vbo_array* vbo_array, GLuint id,
                                  float tx, float ty, float rotation, float scale);
int isogl_vbo_array_set_visible(isogl_vbo_array* vbo_array, GLuint id, int visible);
int isogl_vbo_array_set_pick_id(isogl_vbo_array* vbo_array, GLuint id, uint32_t pick_id);
int isogl_vbo_array_set_selected(isogl_vbo_array* vbo_array, GLuint id, int selected);
int isogl_vbo_array_clear_selection(isogl_vbo_array* vbo_array);
int isogl_vbo_array_apply_selection(isogl_vbo_array* vbo_array,
                                    const uint32_t* entity_ids, size_t count);
isogl_vbo_object* isogl_vbo_array_get(isogl_vbo_array* vbo_array, GLuint id);
int isogl_vbo_array_remove(isogl_vbo_array* vbo_array, GLuint id);
void isogl_vbo_array_free(isogl_vbo_array* vbo_array);
void isogl_draw_vbo_object(isogl_vbo_array* vbo_array, size_t index);
void isogl_draw_all_vbo_objects(isogl_vbo_array* vbo_array);

/**
 * @brief Pick the opaque IDs visible in an ISOGL_PICK_SIZE square box
 *        around a cursor position.
 *
 * The logical box is ISOGL_PICK_SIZE window pixels wide.  It is rendered
 * into an ISOGL_PICK_TEXTURE_SIZE square GL_R32UI texture (2x definition),
 * so thin lines get two texels per screen pixel.  Texels are read back and
 * scanned from the centre outwards in a square spiral: the returned distinct
 * IDs are ordered from the closest to the cursor outwards, so out_ids[0] is
 * the centre-most hit.
 *
 * It does not change the current view/model or the objects.
 *
 * @param window     window whose size defines the pixel mapping
 * @param vbo_array  segment objects to test (may be NULL)
 * @param strings    text strings to test (may be NULL)
 * @param px, py     cursor in window pixels, origin top-left (SDL convention)
 * @param out_ids    caller array receiving distinct IDs, centre-out
 * @param max_ids    capacity of out_ids; use ISOGL_PICK_TEXTURE_SIZE squared
 *                   to receive every possible distinct ID
 * @param out_count  number of distinct IDs written
 * @return 0 on success, -1 on invalid arguments or GL failure
 */
int isogl_pick_at(SDL_Window* window, isogl_vbo_array* vbo_array,
                  isogl_string_list* strings,
                  float px, float py,
                  uint32_t* out_ids, size_t max_ids, size_t* out_count);

// Gestione viewport e trasformazioni
void isogl_resize_viewport(SDL_Window* window);
void isogl_pixel_to_point(SDL_Window *window, isogl_point *pixelP, isogl_point *modelP);
int isogl_ndc_to_world(float ndc_x, float ndc_y, isogl_point *world);
int isogl_screen_to_world(SDL_Window *window, float px, float py, isogl_point *world);
int isogl_world_to_screen(SDL_Window *window, float wx, float wy, isogl_point *screen);

/*
 * Selection highlight for the next display frames.  The rectangle is in
 * framebuffer pixels, origin bottom-left, and is used by the segment and
 * text fragment shaders to tint the fragments inside it.  crossing chooses
 * the highlight colour (yellow for crossing, green for window).
 * This is a preview only; it does not change the pick IDs.
 */
void isogl_set_selection_rect(float x0, float y0, float x1, float y1, int crossing);
void isogl_clear_selection_rect(void);
void isogl_draw_selection_overlay(void);
int isogl_get_selection_active(void);
const float *isogl_get_selection_rect(void);
const float *isogl_get_selection_color(void);

/*
 * Persistent selection.  isogl stores only a flag per VBO object (and per
 * text string); the application owns the set of entity IDs.  Apply the set
 * with isogl_vbo_array_apply_selection() / isogl_string_list_apply_selection()
 * and the selected objects are tinted on every following frame, independently
 * of the transient rectangle preview.
 *
 * The rectangle preview still uses the same colour.  isogl_set_selection_preview(0)
 * disables the per-segment rectangle tint while keeping the rubber-band
 * overlay drawn by isogl_draw_selection_overlay().
 */
void isogl_set_selection_color(float r, float g, float b, float a);
void isogl_set_selection_preview(int enabled);
int isogl_get_selection_preview(void);

// Gestione zoom e pan
void isogl_handle_zoom(SDL_Window* window, int wheel_y, float mouseX, float mouseY);
void isogl_handle_pan(SDL_Window* window, int xrel, int yrel);

// Rendering
void isogl_clear(float r, float g, float b, float a);
void isogl_swap_buffers(SDL_Window* window);

#endif // ISOGL_H
