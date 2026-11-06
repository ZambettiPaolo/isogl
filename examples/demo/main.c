#include <SDL2/SDL.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "isogl.h"
#include "isogl-sdl/isogl_sdl.h"
#include "isogl_font.h"
#include "architecture.h"

/* isogl_zoom_all_sheet() keeps a fixed margin before the aspect correction,
 * which leaves the sheet small on wide windows.  Scale the model matrix around
 * the sheet centre so the drawing uses ~92% of the limiting NDC axis. */
static void demo_fit(const isogl_sdl_host *host, float sheet_w, float sheet_h) {
    isogl_zoom_all_sheet();
    if (!host || host->fb_width <= 0 || host->fb_height <= 0) return;

    isogl_viewport vp = {0, 0, host->fb_width, host->fb_height};
    isogl_set_viewport(&vp);

    float ex = fabsf(isogl_model_matrix[0]) * sheet_w * 0.5f *
               fabsf(isogl_view_matrix[0]);
    float ey = fabsf(isogl_model_matrix[5]) * sheet_h * 0.5f *
               fabsf(isogl_view_matrix[5]);
    float max_extent = ex > ey ? ex : ey;
    if (max_extent < 1e-6f) return;

    float factor = 0.92f / max_extent;
    if (factor <= 1.0f) return;

    isogl_model_matrix[0] *= factor;
    isogl_model_matrix[5] *= factor;
    isogl_model_matrix[12] *= factor;
    isogl_model_matrix[13] *= factor;
}

int main(int argc, char **argv) {
    const char *asset_dir = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            asset_dir = argv[++i];
        }
    }

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

    isogl_sdl_host host;
    isogl_sdl_host_desc desc = {
        .title = "isogl architecture",
        .width = dm.w * 3 / 4,
        .height = dm.h * 3 / 4,
        .gl_major = 3,
        .gl_minor = 3,
        .multisample = 0,
        .vsync = 1,
        .hidden = 0
    };
    if (isogl_sdl_host_init(&host, &desc) != 0) {
        printf("Error creating SDL/GL host: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (isogl_init_with_asset_dir(asset_dir) != 0) {
        printf("Failed to initialize ISOGL\n");
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }

    /* A3 landscape, 1:1.  The diagram is built in sheet millimetres. */
    if (isogl_set_sheet(420.0f, 297.0f, (isogl_scale){1, 1}) != 0) {
        printf("Failed to create the sheet\n");
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }

    char font_path[4096];
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                         font_path, sizeof(font_path)) != 0) {
        printf("Font asset path too long\n");
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }
    isogl_font_data *font = isogl_font_load_from_toml(font_path);
    if (!font) {
        printf("Failed to load font: %s\n", font_path);
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }
    isogl_font_init_vao();

    isogl_segment_list segments;
    if (isogl_segment_list_init(&segments, 512) != 0) {
        printf("Failed to allocate the segment list\n");
        isogl_font_free_data(font);
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }

    isogl_string_list strings = {0};
    ArchDrawing drawing = {
        .segments = &segments,
        .strings = &strings,
        .font = font,
        .global_scale = 1.0f
    };
    if (arch_build(&drawing) != 0) {
        printf("Failed to build the architecture drawing\n");
        isogl_segment_list_free(&segments);
        isogl_string_list_free(&strings);
        isogl_font_free_data(font);
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }

    isogl_vbo_array vbo_array;
    isogl_vbo_array_init(&vbo_array);
    GLuint drawing_id =
        isogl_vbo_array_add(&vbo_array, &segments, ISOGL_VBO_SEGMENT);
    isogl_segment_list_free(&segments);
    if (drawing_id == 0) {
        printf("Failed to upload the drawing\n");
        isogl_string_list_free(&strings);
        isogl_vbo_array_free(&vbo_array);
        isogl_font_free_data(font);
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }

    demo_fit(&host, 420.0f, 297.0f);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    SDL_Event event;
    int quit = 0;
    int drag = 0;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        isogl_sdl_host_update(&host);
                        demo_fit(&host, 420.0f, 297.0f);
                    }
                    break;

                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_z) {
                        demo_fit(&host, 420.0f, 297.0f);
                    } else if (event.key.keysym.sym == SDLK_t) {
                        isogl_toggle_color_thickness();
                    } else if (event.key.keysym.sym == SDLK_q ||
                               event.key.keysym.sym == SDLK_ESCAPE) {
                        quit = 1;
                    }
                    break;

                case SDL_MOUSEWHEEL: {
                    float px = 0.0f, py = 0.0f;
                    isogl_sdl_host_local_to_pixels(&host,
                        (float)event.wheel.mouseX,
                        (float)event.wheel.mouseY, &px, &py);
                    isogl_handle_zoom(event.wheel.y, px, py);
                    break;
                }

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) drag = 1;
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) drag = 0;
                    break;

                case SDL_MOUSEMOTION:
                    if (drag) {
                        float dx = 0.0f, dy = 0.0f;
                        isogl_sdl_host_delta_to_pixels(&host,
                            (float)event.motion.xrel,
                            (float)event.motion.yrel, &dx, &dy);
                        isogl_handle_pan(dx, dy);
                    }
                    break;

                case SDL_QUIT:
                    quit = 1;
                    break;
            }
        }

        isogl_sdl_host_begin_frame(&host);
        isogl_clear(0.10f, 0.10f, 0.10f, 1.0f);
        isogl_draw_sheet();
        isogl_draw_all_vbo_objects(&vbo_array);
        isogl_draw_string_list(&strings, isogl_shader_program_font,
                               isogl_model_matrix, isogl_view_matrix,
                               identity);
        isogl_sdl_host_swap(&host);
        SDL_Delay(20);
    }

    isogl_string_list_free(&strings);
    isogl_vbo_array_free(&vbo_array);
    isogl_font_free_data(font);
    isogl_cleanup();
    isogl_sdl_host_shutdown(&host);
    SDL_Quit();
    return 0;
}
