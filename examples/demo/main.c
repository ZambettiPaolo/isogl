#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "isogl.h"
#include "glad.h"
#include "khrplatform.h"
#include "primitive.h" 
#include <math.h>
#include <string.h>
#include "isogl_font.h"

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

isogl_vbo_array vbo_array;

// Test data for geometry
static Primitive line1 = {
    .next = NULL,
    .type = PRIM_LINE,
    .pattern = LINE_TYPE_DASHED,
    .thickness = LINE_THICKNESS_0_18,
    .visible = 1,
    .line = {
        .start = {.x = 50.0, .y = 50.0},
        .end = {.x = 150.0, .y = 150.0}
    }
};

static Primitive circle1 = {
    .next = &line1,
    .type = PRIM_CIRCLE,
    .pattern = LINE_TYPE_LONG_DASH_DOT,
    .thickness = LINE_THICKNESS_0_35,
    .visible = 1,
    .circle = {
        .center = {.x = 50.0, .y = 50.0},
        .radius = 100.0
    }
};

static Primitive rectangle = {
    .next = &circle1,
    .type = PRIM_RECTANGLE,
    .pattern = LINE_TYPE_CONTINUOUS,
    .thickness = LINE_THICKNESS_0_18,
    .visible = 1,
    .rectangle = {
        .start = {.x = 50.0, .y = 50.0},
        .angle = 3.14 / 6.0,
        .end = {.x = 100.0, .y = 0.0}
    }
};

// Test data for geometry
static Primitive line2 = {
    .next = NULL,
    .type = PRIM_LINE,
    .pattern = LINE_TYPE_CONTINUOUS,
    .thickness = LINE_THICKNESS_0_25,
    .visible = 1,
    .line = {
        .start = {.x = -50.0, .y = 50.0},
        .end = {.x = -150.0, .y = 150.0}
    }
};

static Primitive circle2 = {
    .next = &line2,
    .type = PRIM_CIRCLE,
    .pattern = LINE_TYPE_DASHED,
    .thickness = LINE_THICKNESS_0_35,
    .visible = 1,
    .circle = {
        .center = {.x = -150.0, .y = 150.0},
        .radius = 30.0
    }
};

static Primitive rectangle2 = {
    .next = &circle2,
    .type = PRIM_RECTANGLE,
    .pattern = LINE_TYPE_CONTINUOUS,
    .thickness = LINE_THICKNESS_0_18,
    .visible = 1,
    .rectangle = {
        .start = {.x = 50.0, .y = 50.0},
        .angle = 3.14 / 6.0,
        .end = {.x = 100.0, .y = 0.0}
    }
};

void append_primitive(isogl_segment_list *segment_list, Primitive *primitive) {
    while (primitive != NULL) {
        switch (primitive->type) {
        case PRIM_LINE: {
            unsigned int thickness = (unsigned int)primitive->thickness;
            unsigned int pattern   = (unsigned int)primitive->pattern;
            unsigned int layer = 0;
            unsigned int pk = isogl_pack(thickness, pattern, layer);
            
            isogl_vertex vertex0 = {
                .position = {(float)primitive->line.start.x, (float)primitive->line.start.y},
                .color = {1.0, 0.0, 0.0, 1.0},
                .distanceFromStart = 0.0f,
                .pk_thickness_pattern_layer = pk
            };
            
            float vx = (float)primitive->line.end.x;
            float vy = (float)primitive->line.end.y;
            float dx = vx - vertex0.position[0];
            float dy = vy - vertex0.position[1];
            
            isogl_vertex vertex1 = {
                .position = {vx, vy},
                .color = {1.0, 0.0, 0.0, 1.0},
                .distanceFromStart = vertex0.distanceFromStart + sqrtf(dx*dx + dy*dy),
                .pk_thickness_pattern_layer = pk
            };
            isogl_segment_list_push_pair(segment_list, vertex0, vertex1);
            break;
        }

        case PRIM_CIRCLE: {
            unsigned int thickness = (unsigned int)primitive->thickness;
            unsigned int pattern   = (unsigned int)primitive->pattern;
            unsigned int layer = 0;
            unsigned int pk = isogl_pack(thickness, pattern, layer);
            float distance = 0.0f;
            float angle = 0.0;
            float vx0 = 0.0;
            float vy0 = 0.0;
            float vx1 = 0.0;
            float vy1 = 0.0;
            float dx = 0.0;
            float dy = 0.0;
            for (int i = 0; i < 24; i++) {
                vx0 = (float)primitive->circle.center.x + (float)primitive->circle.radius * cosf(angle);
                vy0 = (float)primitive->circle.center.y + (float)primitive->circle.radius * sinf(angle);
                isogl_vertex vertex0 = {
                    .position = {vx0, vy0},
                    .color = {0.0, 1.0, 0.0, 1.0},
                    .distanceFromStart = distance,
                    .pk_thickness_pattern_layer = pk
                };
            
                angle = angle + M_PI/12.0;
                vx1 = (float)primitive->circle.center.x + (float)primitive->circle.radius * cosf(angle);
                vy1 = (float)primitive->circle.center.y + (float)primitive->circle.radius * sinf(angle);
                dx = vx1 - vx0;
                dy = vy1 - vy0;
                distance = distance + sqrtf(dx*dx + dy*dy);
                isogl_vertex vertex1 = {
                    .position = {vx1, vy1},
                    .color = {0.0, 1.0, 0.0, 1.0},
                    .distanceFromStart = distance,
                    .pk_thickness_pattern_layer = pk
                };
                isogl_segment_list_push_pair(segment_list, vertex0, vertex1);
            }
            break;
        }

        case PRIM_RECTANGLE: {
            // TODO: implementazione per rettangolo
            break;
        }
          
        case PRIM_ARC: {
            // TODO: implementazione per arco
            break;
        }

        default:
            break;
        }
        primitive = primitive->next;  // Avanza alla prossima primitiva
    }
}

int main(int argc, char **argv) {
    printf("start\n");

    // Inizializzazione SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL Init error: %s\n", SDL_GetError());
        return 1;
    }
    
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0) {
        printf("SDL Get Current Display Mode: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    // Imposta attributi OpenGL 4.3
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    
    // Debug    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    
    // Double buffering
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);   
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    // SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4); 
    
    // Creazione finestra
    SDL_Window* window = SDL_CreateWindow(
        "ISO CAD",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        dm.w / 2,
        dm.h / 2,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    
    if (window == NULL) {
        printf("Error creating window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    // Creazione contesto OpenGL
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == NULL) {
        printf("Error GL context creation: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Inizializzazione GLAD
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        printf("Failed to initialize GLAD\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    printf("OpenGL context: %d.%d\n", major, minor);
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    printf("GLSL: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    
    const char *asset_dir = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            asset_dir = argv[++i];
        }
    }

    // Inizializzazione ISOGL
    if (isogl_init_with_asset_dir(asset_dir) != 0) {
        printf("Failed to initialize ISOGL\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    glEnable(GL_MULTISAMPLE); 
    
    // Setup geometrie
    isogl_scale scale = {1, 2};
    isogl_set_sheet(210.0f, 297.0f, scale);
    isogl_segment_list segment_list;
    if (isogl_segment_list_init(&segment_list, 100) != 0) {
        fprintf(stderr, "Failed to allocate segment list\n");
        isogl_cleanup();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    append_primitive(&segment_list, &rectangle);
    isogl_vbo_array_init(&vbo_array);
    isogl_vbo_array_add(&vbo_array, &segment_list, ISOGL_VBO_SEGMENT);

    isogl_segment_list_free(&segment_list);
    isogl_segment_list_init(&segment_list, 100);
    append_primitive(&segment_list, &rectangle2);
    GLuint id = isogl_vbo_array_add(&vbo_array, &segment_list, ISOGL_VBO_SEGMENT);
    isogl_vbo_array_set_transform(&vbo_array, id, 50.0f, -200.0, M_PI/4, 0.5f);
    isogl_segment_list_free(&segment_list);

    isogl_zoom_all_sheet();
    
    // Setup viewport
    isogl_resize_viewport(window);
    
    // Disabilita VSync per test
    SDL_GL_SetSwapInterval(1);
    
    // Font setup
    char font_path[4096];
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                         font_path, sizeof(font_path)) != 0) {
        fprintf(stderr, "Font asset path too long\n");
        return 1;
    }
    isogl_font_data* font_data = isogl_font_load_from_toml(font_path);
    if (!font_data) {
        fprintf(stderr, "Failed to load font: %s\n", font_path);
        return 1;
    }
    isogl_font_init_vao();

    isogl_string_list string_list = {0};    
    isogl_glyph_string* s = isogl_create_glyph_string("EUREKA!!!!", font_data);

    isogl_string_list_append(&string_list, s, 210, 0, 2.5, ((float)scale.num/scale.den), 0, font_data); 

    s = isogl_create_glyph_string("Paolo Zambetti", font_data);

    isogl_string_list_append(&string_list, s, 100, -50, 5.0, ((float)scale.num/scale.den), 1, font_data); 
    
    // Loop principale
    SDL_Event event;
    int quit = 0;
    int drag = 0;
    
    while (!quit) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        isogl_resize_viewport(window);
                    }
                    break;
                
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_z) {
                        isogl_zoom_all_sheet();
                    }
                    if (event.key.keysym.sym == SDLK_t) {
                        isogl_toggle_color_thickness();
                    }
                    break;
                
                case SDL_MOUSEWHEEL:
                    isogl_handle_zoom(window, event.wheel.y, 
                                     (float)event.wheel.mouseX, 
                                     (float)event.wheel.mouseY);
                    break;                                                                               
                
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        drag = 1;
                    }
                    break;
                
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        drag = 0;
                    }
                    break;
                
                case SDL_MOUSEMOTION:
                    if (drag) {
                        isogl_handle_pan(window, event.motion.xrel, event.motion.yrel);
                    }
                    break;
                
                case SDL_QUIT:
                    quit = 1;
                    break;
            }
        }
        
        // Rendering
        isogl_clear(0.1f, 0.1f, 0.1f, 1.0f);
        isogl_draw_sheet();
        isogl_draw_all_vbo_objects(&vbo_array);

        float relative_matrix[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };

        isogl_draw_string_list(&string_list, 
                              isogl_shader_program_font,
                              isogl_model_matrix,      // Zoom
                              isogl_view_matrix,       // Aspect ratio
                              relative_matrix);        // Blocco → Mondo

        isogl_swap_buffers(window);
        SDL_Delay(20);
    }
   
    // Cleanup (GPU resources first, while the context is current)
    isogl_string_list_free(&string_list);
    isogl_vbo_array_free(&vbo_array);
    isogl_cleanup();
    isogl_font_free_data(font_data);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}
