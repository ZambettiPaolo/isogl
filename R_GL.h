#ifndef R_GL_H
#define R_GL_H

#include "include/glad/glad.h"
#include "include/KHR/khrplatform.h"
#include <SDL2/SDL.h>

typedef struct {
    float x;
    float y;
} R_GL_Point;


typedef struct{
	int num;
	int den;
} R_GL_Scale;


typedef struct {
    float position[3];
    float color[4];
	float thickness;
    int patternType;
    int is_end_polyline;
    float distanceFromStart;
} R_GL_Vertex;

// Inizializzazione e cleanup
int R_GL_init(void);
void R_GL_cleanup(void);

// Gestione shader
GLuint R_GL_compile_shader(GLenum type, const char* source);
GLuint R_GL_create_shader_program(const char *vertex_shader_source, const char *fragment_shader_source);

// Gestione sheet (foglio)
int R_GL_set_sheet(float width, float height, R_GL_Scale scale);
void R_GL_draw_sheet(void);
void R_GL_zoom_all_sheet(void);
void R_GL_toggle_color_thickness();


// Gestione geometrie
void R_GL_setup_geometry(R_GL_Vertex* vertices, size_t vertex_count, unsigned int* indices, size_t index_count);
void R_GL_draw_geometry(size_t index_count);

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
