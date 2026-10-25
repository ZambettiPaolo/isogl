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
    float transform[16];        // 4x4 transformation matrix (column-major for OpenGL)
    isogl_vbo_type type;        // selector for shader program
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
isogl_vbo_object* isogl_vbo_array_get(isogl_vbo_array* vbo_array, GLuint id);
int isogl_vbo_array_remove(isogl_vbo_array* vbo_array, GLuint id);
void isogl_vbo_array_free(isogl_vbo_array* vbo_array);
void isogl_draw_vbo_object(isogl_vbo_array* vbo_array, size_t index);
void isogl_draw_all_vbo_objects(isogl_vbo_array* vbo_array);

// Gestione viewport e trasformazioni
void isogl_resize_viewport(SDL_Window* window);
void isogl_pixel_to_point(SDL_Window *window, isogl_point *pixelP, isogl_point *modelP);
int isogl_ndc_to_world(float ndc_x, float ndc_y, isogl_point *world);
int isogl_screen_to_world(SDL_Window *window, float px, float py, isogl_point *world);

// Gestione zoom e pan
void isogl_handle_zoom(SDL_Window* window, int wheel_y, float mouseX, float mouseY);
void isogl_handle_pan(SDL_Window* window, int xrel, int yrel);

// Rendering
void isogl_clear(float r, float g, float b, float a);
void isogl_swap_buffers(SDL_Window* window);

#endif // ISOGL_H
