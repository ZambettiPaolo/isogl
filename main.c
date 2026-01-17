#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "R_GL.h"
#include "include/glad/glad.h"
#include "include/KHR/khrplatform.h"
#include "primitive.h" 
#include <math.h>
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

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
        .end = 100.0
    }
};

int segment_list_init(Segment_list *segment_list){
	segment_list->size = 0;
	segment_list->capacity = 100;
	segment_list->vertex = (R_GL_Vertex*) malloc(segment_list->capacity * sizeof(R_GL_Vertex));
	if (!segment_list->vertex) {
		return -1; 
	} else {
		return 0;  
	}
}

int append_segment(Segment_list *segment_list, R_GL_Vertex v0, R_GL_Vertex v1){
	// Verifica se serve espandere l'array (serve spazio per 2 vertici)
	if (segment_list->size + 2 > segment_list->capacity){
		size_t new_capacity = segment_list->capacity * 2;
		R_GL_Vertex *ptr = realloc(segment_list->vertex, new_capacity * sizeof(R_GL_Vertex));
		if (!ptr){
			return -1;
		}
		segment_list->vertex = ptr;
		segment_list->capacity = new_capacity;
	}
	
	// Aggiungi i due vertici
	segment_list->vertex[segment_list->size] = v0;
	segment_list->vertex[segment_list->size + 1] = v1;
	segment_list->size += 2;
	
	return 0;  
}



void append_primitive(Segment_list *segment_list, Primitive *primitive){
	while (primitive != NULL){
		switch (primitive->type) {
		case PRIM_LINE:{
			unsigned int thickness = (unsigned int)primitive->thickness;
			unsigned int pattern   = (unsigned int)primitive->pattern;
			unsigned int layer = 0;
			unsigned int pk = R_GL_pack(thickness, pattern, layer);
			
			R_GL_Vertex vertex0 = {
				.position = {(float)primitive->line.start.x, (float)primitive->line.start.y},
				.color = {1.0, 0.0, 0.0, 1.0},
				.distanceFromStart = 0.0f,
			.pk_thickness_pattern_layer = pk
			};
			
			float vx = (float)primitive->line.end.x;
			float vy = (float)primitive->line.end.y;
			float dx = vx - vertex0.position[0];
			float dy = vy - vertex0.position[1];
			
			R_GL_Vertex vertex1 = {
				.position = {vx, vy},
				.color = {1.0, 0.0, 0.0, 1.0},
				.distanceFromStart = vertex0.distanceFromStart + sqrtf(dx*dx + dy*dy),
				.pk_thickness_pattern_layer = pk
			};
			append_segment(segment_list, vertex0, vertex1);
	break;
	}

	case PRIM_CIRCLE:{
			R_GL_Point c_point[24];
			unsigned int thickness = (unsigned int)primitive->thickness;
			unsigned int pattern   = (unsigned int)primitive->pattern;
			unsigned int layer = 0;
			unsigned int pk = R_GL_pack(thickness, pattern, layer);
			float distance = 0.0f;
			float angle = 0.0;
			float vx0 = 0.0;
			float vy0 = 0.0;
			float vx1 = 0.0;
			float vy1 = 0.0;
			float dx = 0.0;
			float dy = 0.0;
			for (int i = 0; i<24; i++){
				vx0 = (float)primitive->circle.center.x + (float)primitive->circle.radius * cosf(angle);
				vy0 = (float)primitive->circle.center.y + (float)primitive->circle.radius * sinf(angle);
				R_GL_Vertex vertex0 = {
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
				R_GL_Vertex vertex1 = {
					.position = {vx1, vy1},
					.color = {0.0, 1.0, 0.0, 1.0},
					.distanceFromStart = distance,
					.pk_thickness_pattern_layer = pk
				};
				append_segment(segment_list, vertex0, vertex1);
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
					 
	}
	primitive = primitive->next;  // Avanza alla prossima primitiva
	}
}


int main(void) {
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
    
    // Configurazione attributi OpenGL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    
    // Creazione finestra
    SDL_Window* window = SDL_CreateWindow(
        "Finestra massima",
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
    
    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    
    // Inizializzazione R_GL
    if (R_GL_init() != 0) {
        printf("Failed to initialize R_GL\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Setup geometrie
    R_GL_Scale scale = {2, 1};
	R_GL_set_sheet(210.0f, 297.0f, scale);
	Segment_list segment_list;
	segment_list_init( &segment_list);
	append_primitive(&segment_list, &rectangle);
	size_t number_of_segments = upload_VBO(&segment_list);
	
	//upload_VBO(&rectangle, VBO);
    //R_GL_setup_geometry(test_vertices, 4);
    R_GL_zoom_all_sheet();
    
    // Setup viewport
    R_GL_resize_viewport(window);
    
    // Disabilita VSync per test
    SDL_GL_SetSwapInterval(1);\
    
    // Loop principale
    SDL_Event event;
    int quit = 0;
    int drag = 0;
   

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        R_GL_resize_viewport(window);
                    }
                    break;
                
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_z) {
                        R_GL_zoom_all_sheet();
                    }
                    if (event.key.keysym.sym == SDLK_t) {
                        R_GL_toggle_color_thickness();
                    }
                    break;
                
                case SDL_MOUSEWHEEL:
                    R_GL_handle_zoom(window, event.wheel.y, 
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
                        R_GL_handle_pan(window, event.motion.xrel, event.motion.yrel);
                    }
                    break;
                
                case SDL_QUIT:
                    quit = 1;
                    break;
            }
        }
        
        // Rendering
        R_GL_clear(0.1f, 0.1f, 0.1f, 1.0f);
        R_GL_draw_sheet();
        R_GL_draw_geometry(number_of_segments);
        R_GL_swap_buffers(window);
        SDL_Delay(20);
    }
    
    // Cleanup
    R_GL_cleanup();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}
