#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "isogl.h"
#include "isogl-sdl/isogl_sdl.h"
#include "isogl_font.h"
#include "drawing_load.h"

static const char *sheet_format_name(int format) {
    switch (format) {
        case ISOGL_SHEET_A4: return "A4";
        case ISOGL_SHEET_A3: return "A3";
        case ISOGL_SHEET_A2: return "A2";
        case ISOGL_SHEET_A1: return "A1";
        case ISOGL_SHEET_A0: return "A0";
        default: return "?";
    }
}

static const char *sheet_orientation_name(int orientation) {
    return orientation == ISOGL_ORIENTATION_PORTRAIT ? "portrait" : "landscape";
}

int main(int argc, char **argv) {
    const char *asset_dir = NULL;
    const char *drawing_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            asset_dir = argv[++i];
        } else if (strcmp(argv[i], "--drawing") == 0 && i + 1 < argc) {
            drawing_path = argv[++i];
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

    char asset_drawing[4096];
    char local_drawing[] = "examples/demo/architecture.toml";
    if (!drawing_path) {
        FILE *probe = fopen(local_drawing, "r");
        if (probe) {
            fclose(probe);
            drawing_path = local_drawing;
        } else if (isogl_asset_path("drawings/architecture.toml",
                                    asset_drawing,
                                    sizeof(asset_drawing)) == 0) {
            drawing_path = asset_drawing;
        } else {
            drawing_path = local_drawing;
        }
    }

    DrawingSheetInfo sheet;
    if (drawing_load(drawing_path, font, &segments, &strings, &sheet) != 0) {
        printf("Failed to load drawing: %s\n", drawing_path);
        isogl_segment_list_free(&segments);
        isogl_string_list_free(&strings);
        isogl_font_free_data(font);
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }

    isogl_scale scale = {sheet.scale_num, sheet.scale_den};
    if (isogl_set_sheet(sheet.width_mm, sheet.height_mm, scale) != 0) {
        printf("Failed to create the sheet\n");
        isogl_segment_list_free(&segments);
        isogl_string_list_free(&strings);
        isogl_font_free_data(font);
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        SDL_Quit();
        return 1;
    }

    printf("drawing: %s\n", drawing_path);
    printf("sheet: %s %s%s (%.0f x %.0f mm)\n",
           sheet_format_name(sheet.format),
           sheet_orientation_name(sheet.orientation),
           sheet.promoted ? " - promoted from A3" : "",
           sheet.width_mm, sheet.height_mm);
    if (sheet.bounds.has_bounds) {
        printf("drawing bounds: %.1f x %.1f mm\n",
               sheet.bounds.xmax - sheet.bounds.xmin,
               sheet.bounds.ymax - sheet.bounds.ymin);
    }
    fflush(stdout);

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

    isogl_zoom_all_sheet();

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
                        isogl_zoom_all_sheet();
                    }
                    break;

                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_z) {
                        isogl_zoom_all_sheet();
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
