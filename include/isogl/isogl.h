#ifndef ISOGL_H
#define ISOGL_H

#include "glad.h"
#include <stddef.h>
#include <stdint.h>
#include "isogl_font.h"
#include "isogl_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public contract
 * ---------------
 * - The application owns the window and the OpenGL context; isogl never
 *   creates or destroys them.  Any toolkit that makes a GL 3.3 core context
 *   current can host isogl (SDL2, GTK4 GtkGLArea, Qt6 QOpenGLWidget, ...).
 * - gladLoadGLLoader() must be called while the context is current, before
 *   isogl_init() / isogl_init_with_asset_dir().
 * - The application installs the active viewport, in framebuffer pixels,
 *   with isogl_set_viewport() before drawing.  isogl does not swap buffers:
 *   presentation belongs to the host toolkit.
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

/* Default side, in framebuffer pixels, of the logical square box used by
 * isogl_pick_at().  Hosts on HiDPI displays pass a larger extent_px to keep
 * the box constant in logical points, e.g. lround(ISOGL_PICK_DEFAULT_EXTENT
 * * device_pixel_ratio). */
#define ISOGL_PICK_DEFAULT_EXTENT 8
/* The offscreen pick texture has this many texels per box pixel. */
#define ISOGL_PICK_TEXTURE_SCALE 2


typedef struct {
    float x;
    float y;
} isogl_point;

/* Axis-aligned box in model/document units. */
typedef struct {
    double xmin;
    double ymin;
    double xmax;
    double ymax;
} isogl_box;


typedef struct {
    int num;
    int den;
} isogl_scale;


/*
 * Coordinate spaces
 * -----------------
 * isogl_viewport uses framebuffer-absolute pixels with the origin at the
 * BOTTOM-LEFT, exactly like glViewport() and glScissor().
 *
 * Every other pixel parameter in this API is VIEWPORT-LOCAL with the origin
 * at the TOP-LEFT of the active viewport:
 *   - isogl_pick_at / isogl_symbol_pick_at        px, py
 *   - isogl_screen_to_world / isogl_world_to_screen
 *   - isogl_handle_zoom / isogl_handle_pan
 *   - isogl_pixel_to_point
 *   - isogl_set_selection_rect / isogl_get_selection_rect
 * isogl_world_to_screen returns viewport-local pixels, origin top-left.
 *
 * For a full-drawable viewport {0, 0, fb_w, fb_h}, viewport-local top-left
 * coincides with the window/drawable top-left.  For a sub-viewport, subtract
 * the widget rectangle's top-left (in framebuffer pixels) that the host
 * already knows from its layout.
 */
typedef struct {
    int x;       /* left edge, framebuffer pixels                   */
    int y;       /* bottom edge, framebuffer pixels (GL convention) */
    int width;   /* framebuffer pixels                              */
    int height;  /* framebuffer pixels                              */
} isogl_viewport;

/* Build a GL-convention viewport from a widget rectangle expressed in
 * top-left framebuffer pixels, given the total framebuffer height. */
isogl_viewport isogl_viewport_from_top_left(int x, int y, int width, int height,
                                            int framebuffer_height);

/* Install the active viewport and update glViewport() plus the aspect part
 * of isogl_view_matrix.  NULL clears the active viewport.  A viewport with a
 * non-positive width or height is rejected and leaves the state unchanged. */
void isogl_set_viewport(const isogl_viewport *viewport);

/* Copy the active viewport to *out.  Returns 0 when a valid viewport is
 * installed, -1 otherwise (out is left untouched). */
int isogl_get_viewport(isogl_viewport *out);

/* Re-assert the GL state isogl needs and re-apply the active viewport.
 * Call once per frame after the host toolkit has done its own GL work.
 * Returns 0 on success, -1 when no valid viewport is installed.
 * It does NOT bind framebuffer 0: GTK and Qt render into their own FBOs. */
int isogl_begin_frame(void);


#if defined(__cplusplus)
#  define ISOGL_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#  define ISOGL_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

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

ISOGL_STATIC_ASSERT(sizeof(isogl_vertex) == 32, "isogl_vertex must stay 32 bytes");
ISOGL_STATIC_ASSERT(offsetof(isogl_vertex, position) == 0, "unexpected vertex layout");
ISOGL_STATIC_ASSERT(offsetof(isogl_vertex, color) == 8, "unexpected vertex layout");
ISOGL_STATIC_ASSERT(offsetof(isogl_vertex, distanceFromStart) == 24,
                    "unexpected vertex layout");
ISOGL_STATIC_ASSERT(offsetof(isogl_vertex, pk_thickness_pattern_layer) == 28,
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
/* Non-zero when colour-thickness mode is active; zero selects print colours. */
int isogl_color_thickness_enabled(void);

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
 * @brief Pick the opaque IDs visible in a square box around a cursor.
 *
 * The box is extent_px framebuffer pixels wide and is rendered into an
 * (extent_px * ISOGL_PICK_TEXTURE_SCALE) square GL_R32UI texture, so thin
 * lines get more than one texel per screen pixel.  Texels are read back and
 * scanned from the centre outwards in a square spiral: the returned distinct
 * IDs are ordered from the closest to the cursor outwards, so out_ids[0] is
 * the centre-most hit.
 *
 * The viewport origin does not enter the pick mapping: px/py are local to the
 * active viewport.
 *
 * It does not change the current view/model or the objects.
 *
 * @param vbo_array  segment objects to test (may be NULL)
 * @param strings    text strings to test (may be NULL)
 * @param px, py     cursor in viewport-local pixels, origin top-left
 * @param extent_px  side of the pick box in framebuffer pixels; hosts keep a
 *                   constant logical size by passing
 *                   lround(ISOGL_PICK_DEFAULT_EXTENT * device_pixel_ratio)
 * @param out_ids    caller array receiving distinct IDs, centre-out
 * @param max_ids    capacity of out_ids; use the pick texture size squared
 *                   to receive every possible distinct ID
 * @param out_count  number of distinct IDs written
 * @return 0 on success, -1 on invalid arguments or GL failure
 */
int isogl_pick_at(isogl_vbo_array* vbo_array, isogl_string_list* strings,
                  float px, float py, int extent_px,
                  uint32_t* out_ids, size_t max_ids, size_t* out_count);

// Gestione viewport e trasformazioni
/* Convert a viewport-local top-left pixel to true normalised device
 * coordinates.  Returns 0 on success, -1 when no viewport is installed. */
int isogl_pixel_to_point(float px, float py, isogl_point *ndc);
int isogl_ndc_to_world(float ndc_x, float ndc_y, isogl_point *world);
int isogl_screen_to_world(float px, float py, isogl_point *world);
int isogl_world_to_screen(float wx, float wy, isogl_point *screen);

/*
 * Selection highlight for the next display frames.  The rectangle is in
 * viewport-local pixels, origin top-left, like every other input in this API.
 * It is converted internally to the coordinates used by the segment and text
 * fragment shaders.  crossing chooses the highlight colour (yellow for
 * crossing, green for window).  This is a preview only; it does not change
 * the pick IDs.
 *
 * isogl_get_selection_rect() returns the rectangle in the same local top-left
 * space that was passed to isogl_set_selection_rect().
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

// Gestione zoom e pan (coordinate locali al viewport, origine top-left)
void isogl_handle_zoom(int wheel_y, float px, float py);
void isogl_handle_pan(float dx, float dy);

// Rendering
/* Clear the active viewport only, honouring and restoring a pre-existing
 * scissor box.  No-op when no valid viewport is installed. */
void isogl_clear(float r, float g, float b, float a);
/* isogl_swap_buffers() was removed: presentation belongs to the host. */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // ISOGL_H
