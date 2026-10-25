#include <isogl/isogl.h>
#include <isogl/glad.h>

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cad.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void shutdown_all(SDL_Window *window, SDL_GLContext context,
                         CadDocument *doc, isogl_font_data *font) {
    if (doc) cad_document_free(doc);
    isogl_font_free_data(font);
    isogl_cleanup();
    if (context) SDL_GL_DeleteContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

static void build_demo_document(CadDocument *doc, float sheet_w, float sheet_h) {
    float margin = 10.0f;

    cad_add_rectangle(doc, 0, LINE_TYPE_CONTINUOUS, LINE_THICKNESS_0_7,
                      margin, margin, sheet_w - margin, sheet_h - margin);

    cad_add_line(doc, 1, LINE_TYPE_LONG_DASH_DOT, LINE_THICKNESS_0_18,
                 margin + 10.0f, sheet_h * 0.5f,
                 sheet_w - margin - 10.0f, sheet_h * 0.5f);
    cad_add_line(doc, 1, LINE_TYPE_LONG_DASH_DOT, LINE_THICKNESS_0_18,
                 sheet_w * 0.5f, margin + 10.0f,
                 sheet_w * 0.5f, sheet_h - margin - 10.0f);

    cad_add_circle(doc, 1, LINE_TYPE_LONG_DASH_DOT, LINE_THICKNESS_0_18,
                   sheet_w * 0.5f, sheet_h * 0.5f, 60.0f);

    cad_add_line(doc, 2, LINE_TYPE_DASHED, LINE_THICKNESS_0_18,
                 margin + 10.0f, margin + 20.0f,
                 sheet_w - margin - 10.0f, margin + 20.0f);

    cad_add_text(doc, 3, margin + 10.0f, margin + 35.0f, 7.0f, 0.0f,
                 "ISO 128 - \xC3\x98 20");
    cad_add_text(doc, 3, sheet_w - margin - 80.0f, margin + 35.0f, 5.0f, 0.0f,
                 "isocad - A3 1:1");
}

int main(int argc, char **argv) {
    const char *asset_dir = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            asset_dir = argv[++i];
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow("isocad", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 1280, 800,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        shutdown_all(window, context, NULL, NULL);
        return 1;
    }

    if (isogl_init_with_asset_dir(asset_dir) != 0) {
        fprintf(stderr, "Failed to initialize isogl (assets: %s)\n",
                asset_dir ? asset_dir : isogl_asset_dir());
        shutdown_all(window, context, NULL, NULL);
        return 1;
    }

    char font_path[4096];
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                         font_path, sizeof(font_path)) != 0) {
        fprintf(stderr, "Font asset path too long\n");
        shutdown_all(window, context, NULL, NULL);
        return 1;
    }

    isogl_font_data *font = isogl_font_load_from_toml(font_path);
    if (!font) {
        fprintf(stderr, "Failed to load font: %s\n", font_path);
        shutdown_all(window, context, NULL, NULL);
        return 1;
    }
    isogl_font_init_vao();

    float sheet_w = 0.0f, sheet_h = 0.0f;
    isogl_sheet_size(ISOGL_SHEET_A3, ISOGL_ORIENTATION_LANDSCAPE, &sheet_w, &sheet_h);

    if (isogl_set_sheet(sheet_w, sheet_h, (isogl_scale){1, 1}) != 0) {
        fprintf(stderr, "Failed to create sheet\n");
        shutdown_all(window, context, NULL, font);
        return 1;
    }
    isogl_zoom_all_sheet();
    isogl_resize_viewport(window);

    CadDocument doc;
    if (cad_document_init(&doc, font, 1.0f) != 0) {
        fprintf(stderr, "Failed to create document\n");
        shutdown_all(window, context, NULL, font);
        return 1;
    }

    build_demo_document(&doc, sheet_w, sheet_h);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    int running = 1;
    int dragging = 0;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = 0;
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        isogl_resize_viewport(window);
                    }
                    break;

                case SDL_KEYDOWN:
                    if (event.key.repeat) break;
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                        case SDLK_q:
                            running = 0;
                            break;
                        case SDLK_f:
                            isogl_zoom_all_sheet();
                            break;
                        case SDLK_t:
                            isogl_toggle_color_thickness();
                            break;
                        default:
                            break;
                    }
                    break;

                case SDL_MOUSEWHEEL:
                    isogl_handle_zoom(window, event.wheel.y,
                                      (float)event.wheel.mouseX,
                                      (float)event.wheel.mouseY);
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) dragging = 1;
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) dragging = 0;
                    break;

                case SDL_MOUSEMOTION:
                    if (dragging) {
                        isogl_handle_pan(window, event.motion.xrel, event.motion.yrel);
                    }
                    break;

                default:
                    break;
            }
        }

        isogl_clear(0.08f, 0.09f, 0.10f, 1.0f);
        isogl_draw_sheet();
        isogl_draw_all_vbo_objects(&doc.parts);
        isogl_draw_string_list(&doc.strings, isogl_shader_program_font,
                               isogl_model_matrix, isogl_view_matrix, identity);
        isogl_swap_buffers(window);
    }

    shutdown_all(window, context, &doc, font);
    return 0;
}
