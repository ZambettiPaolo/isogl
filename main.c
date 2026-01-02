#include <SDL2/SDL.h>
#include <stdio.h>
#include "R_GL.h"
#include "include/glad/glad.h"
#include "include/KHR/khrplatform.h"

// Dati di test per la geometria
static R_GL_Vertex test_vertices[4] = {
    {
        {-0.5f, 0.5f, 0.0f},
        {0.0f, 0.5f, 0.5f, 1.0f},
        2,
        1.0f,
        0.0f
    },
    {
        {0.5f, 0.5f, 0.0f},
        {0.5f, 0.0f, 0.5f, 1.0f},
        2,
        1.0f,
        1.0f
    },
    {
        {0.5f, -0.5f, 0.0f},
        {0.5f, 0.5f, 0.0f, 1.0f},
        3,
        1.0f,
        0.0f
    },
    {
        {-0.5f, -0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f},
        4,
        1.0f,
        0.0f
    }
};

static unsigned int test_indices[] = {
    0, 1,
    1, 2,
    2, 3,
    3, 0,
    0, 2,
    1, 3,
};

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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    
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
    R_GL_set_sheet(210.0f, 297.0f);
    R_GL_setup_geometry(test_vertices, 4, test_indices, 12);
    R_GL_zoom_all_sheet();
    
    // Setup viewport
    R_GL_resize_viewport(window);
    
    // Disabilita VSync per test
    SDL_GL_SetSwapInterval(0);
    
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
        R_GL_draw_geometry(12);
        R_GL_draw_sheet();
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
