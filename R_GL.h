#ifndef R_GL_H
#define R_GL_H

#include "include/glad/glad.h"
#include "include/KHR/khrplatform.h"
#include <SDL2/SDL.h>
#include <stddef.h>


typedef struct {
    float x;
    float y;
} R_GL_Point;


typedef struct{
	int num;
	int den;
} R_GL_Scale;


/**
 * @brief CPU-side temporary dynamic array used to fill a VBO with vertex pairs.
 *
 * Each pair of vertices represents a single line segment and is rendered accordingly.
 * This structure is intended to be consumed by the segment rendering pipeline
 * (vs_segment, gs_segment, fs_segment).
 */
typedef struct LineVertex {
    float   position[2];       // layout(location = 0) - 8 bytes
    float   color[4];          // layout(location = 1) - 16 bytes
    float   distanceFromStart; // layout(location = 2) - 4 bytes
    unsigned int pk_thickness_pattern_layer;       // layout(location = 3) - 4 bytes (packed: thickness|pattern|layer)
} R_GL_Vertex; // Total: 32 bytes

/**
 * @brief Dynamic array managing a collection of R_GL_Vertex objects.
 *
 * Used as a staging buffer before uploading data to the GPU.
 */
typedef struct{
	R_GL_Vertex *vertex;
	size_t size;
	size_t capacity;
} Segment_list;

/**
 * @brief Structure to store VBO information and transformations
 */
typedef struct {
    GLuint vbo_id;              // OpenGL VBO identifier
    size_t vertex_count;        // Number of vertices in this VBO
    int visible;                // Visibility flag (0 = hidden, 1 = visible)
    float transform[16];        // 4x4 transformation matrix (column-major for OpenGL)
} VBO_Object;

/**
 * @brief Dynamic array to manage multiple VBO objects
 */
typedef struct {
    VBO_Object *objects;        // Array of VBO objects
    size_t size;                // Current number of objects
    size_t capacity;            // Maximum capacity before reallocation
} VBO_Array;

/**
 * @brief Pack three 5-bit values into a single 32-bit unsigned integer
 * 
 * This layout is optimized for filtering in the geometry shader using
 * 32-bit bitmasks. Each value (0-31) can be quickly tested against
 * a mask using bitwise operations.
 * 
 * Bit layout (32 bits total):
 * - Bits 0-4   (5 bits): thickness (0-31)
 * - Bits 5-9   (5 bits): pattern (0-31)
 * - Bits 10-14 (5 bits): layer (0-31)
 * - Bits 15-31: unused (reserved for future use)
 * 
 * @param thickness Thickness value (0-31)
 * @param pattern Pattern index (0-31)
 * @param layer Layer index (0-31)
 * @return Packed 32-bit value
 */
unsigned int R_GL_pack(unsigned int thickness, unsigned int pattern, unsigned int layer);

/**
 * @brief Unpack thickness from packed value
 * 
 * @param packed Packed 32-bit value
 * @return Thickness value (0-31)
 */
unsigned int R_GL_unpack_thickness(unsigned int packed);

/**
 * @brief Unpack pattern from packed value
 * 
 * @param packed Packed 32-bit value
 * @return Pattern value (0-31)
 */
unsigned int R_GL_unpack_pattern(unsigned int packed);

/**
 * @brief Unpack layer from packed value
 * 
 * @param packed Packed 32-bit value
 * @return Layer value (0-31)
 */
unsigned int R_GL_unpack_layer(unsigned int packed);


// Inizializzazione e cleanup
int R_GL_init(void);
void R_GL_cleanup(void);

// Gestione shader
GLuint R_GL_load_shader(GLenum shader_type  , char *shader_file_name);
GLuint R_GL_compile_shader(GLenum hader_ype, char *source);
GLuint R_GL_create_shader_program(char *vertex_shader_source, char *geometry_shader_source, char *fragment_shader_source);

// Gestione sheet (foglio)
int R_GL_set_sheet(float width, float height, R_GL_Scale scale);
void R_GL_draw_sheet(void);
void R_GL_zoom_all_sheet(void);
void R_GL_toggle_color_thickness();

int vbo_array_init(VBO_Array *vbo_array); 		
int vbo_array_add(VBO_Array *vbo_array, GLuint vbo_id, size_t vertex_count);
int vbo_array_set_visible(VBO_Array *vbo_array, GLuint id, int visible); 
int vbo_array_remove(VBO_Array *vbo_array, GLuint id);
VBO_Object* vbo_array_get(VBO_Array *vbo_array, GLuint id); 
void vbo_array_free(VBO_Array *vbo_array); 
int upload_VBO(VBO_Array *vbo_array, Segment_list *segment_list);
void R_GL_draw_vbo_object(VBO_Array *vbo_array, size_t index); 
void R_GL_draw_geometry(size_t index_count);
void R_GL_draw_all_vbo_objects(VBO_Array *vbo_array);

// Gestione viewport e trasformazioni
void R_GL_resize_viewport(SDL_Window* window);
void R_GL_pixel_to_point(SDL_Window *window, R_GL_Point *pixelP, R_GL_Point *modelP);

// Gestione zoom e pan
void R_GL_handle_zoom(SDL_Window* window, int wheel_y, float mouseX, float mouseY);
void R_GL_handle_pan(SDL_Window* window, int xrel, int yrel);

// Rendering
void R_GL_clear(float r, float g, float b, float a);
void R_GL_swap_buffers(SDL_Window* window);

// Shader sources esterni
extern const char* R_GL_vertex_shader_source_sheet;
extern const char* R_GL_fragment_shader_source_sheet;
extern const char* R_GL_vertex_shader_source_segment;
extern const char* R_GL_fragment_shader_source_segment;

#endif // R_GL_H
