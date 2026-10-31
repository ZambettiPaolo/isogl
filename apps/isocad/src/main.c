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

    /* Aligned dimension of the circle diameter, below the circle. */
    CadDimension dim;
    cad_dimension_init(&dim);
    dim.x0 = sheet_w * 0.5f - 60.0f;
    dim.y0 = sheet_h * 0.5f;
    dim.x1 = sheet_w * 0.5f + 60.0f;
    dim.y1 = sheet_h * 0.5f;
    dim.px = dim.x0;
    dim.py = dim.y0 - 55.0f;
    dim.text_height_mm = 5.0;
    dim.precision = 1;
    snprintf(dim.prefix, sizeof(dim.prefix), "\xC3\x98 ");
    cad_add_dimension(doc, 2, &dim);
}

static void pick_and_print(CadDocument *doc, SDL_Window *window, int x, int y,
                           size_t max_entities) {
    uint32_t ids[ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE];
    size_t count = 0;

    if (isogl_pick_at(window, &doc->parts, &doc->strings,
                      (float)x, (float)y,
                      ids, sizeof(ids) / sizeof(ids[0]), &count) != 0) {
        printf("pick at (%d,%d): errore\n", x, y);
        fflush(stdout);
        return;
    }

    printf("pick at (%d,%d): %zu id(s):", x, y, count);
    for (size_t i = 0; i < count; ++i) {
        printf(" %u", ids[i]);
    }
    if (count > max_entities) {
        printf(" — ambiguo (> %zu), zoom in\n", max_entities);
    } else {
        printf("\n");
    }
    fflush(stdout);
}

typedef struct {
    uint32_t *ids;
    size_t count;
    size_t capacity;
} CadSelection;

static uint32_t *selection_contained = NULL;
static uint32_t *selection_hit = NULL;
static size_t selection_work_capacity = 0;

static int selection_work_reserve(size_t count) {
    if (count <= selection_work_capacity) return 0;

    size_t capacity = selection_work_capacity ? selection_work_capacity : 32;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) return -1;
        capacity *= 2;
    }

    uint32_t *contained =
        realloc(selection_contained, capacity * sizeof(uint32_t));
    if (!contained) return -1;
    selection_contained = contained;

    uint32_t *hit = realloc(selection_hit, capacity * sizeof(uint32_t));
    if (!hit) return -1;
    selection_hit = hit;

    selection_work_capacity = capacity;
    return 0;
}

static void selection_set_assign(CadSelection *set, const uint32_t *ids,
                                 size_t count) {
    if (!set) return;
    if (count > set->capacity) {
        size_t capacity = set->capacity ? set->capacity : 32;
        while (capacity < count) {
            if (capacity > SIZE_MAX / 2) return;
            capacity *= 2;
        }
        uint32_t *grown = realloc(set->ids, capacity * sizeof(uint32_t));
        if (!grown) return;
        set->ids = grown;
        set->capacity = capacity;
    }
    if (count > 0) memcpy(set->ids, ids, count * sizeof(uint32_t));
    set->count = count;
}

static void selection_set_clear(CadSelection *set) {
    if (set) set->count = 0;
}

static void selection_set_free(CadSelection *set) {
    if (!set) return;
    free(set->ids);
    set->ids = NULL;
    set->count = 0;
    set->capacity = 0;
}

static int selection_compute(SDL_Window *window, isogl_point start_world,
                             int x, int y, isogl_point *end_world,
                             int *crossing, int update_highlight) {
    isogl_point start_screen;
    if (isogl_world_to_screen(window, start_world.x, start_world.y,
                              &start_screen) != 0) {
        return -1;
    }
    if (isogl_screen_to_world(window, (float)x, (float)y, end_world) != 0) {
        return -1;
    }

    *crossing = ((float)x < start_screen.x);

    if (update_highlight) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);

        float gx0 = fminf(start_screen.x, (float)x);
        float gx1 = fmaxf(start_screen.x, (float)x);
        float gy0 = fminf((float)(h - start_screen.y), (float)(h - y));
        float gy1 = fmaxf((float)(h - start_screen.y), (float)(h - y));
        isogl_set_selection_rect(gx0, gy0, gx1, gy1, *crossing);
    }

    return 0;
}

/*
 * Recompute the exact selection for the current rectangle and apply it to the
 * renderer.  The rectangle is only the preview; the IDs are the truth and stay
 * valid after pan/zoom and after VBO/text rebuilds (as long as they are
 * reapplied by the caller).
 */
static void selection_apply_rect(CadDocument *doc, isogl_point start_world,
                                 isogl_point end_world, int crossing,
                                 CadSelection *set) {
    if (!doc || !set) return;
    if (selection_work_reserve(doc->count) != 0) return;

    size_t n_contained = 0;
    size_t n_hit = 0;

    cad_select_rect(doc, start_world, end_world,
                    selection_contained, doc->count, &n_contained,
                    selection_hit, doc->count, &n_hit);

    const uint32_t *ids = crossing ? selection_hit : selection_contained;
    size_t count = crossing ? n_hit : n_contained;

    selection_set_assign(set, ids, count);
    cad_selection_apply(doc, ids, count);
}

static int selection_update(CadDocument *doc, SDL_Window *window,
                            isogl_point start_world, int x, int y,
                            int update_highlight, CadSelection *set) {
    isogl_point end_world = start_world;
    int crossing = 0;

    if (selection_compute(window, start_world, x, y, &end_world,
                          &crossing, update_highlight) != 0) {
        return -1;
    }

    selection_apply_rect(doc, start_world, end_world, crossing, set);
    return crossing;
}

static void selection_finish(CadDocument *doc, SDL_Window *window,
                             isogl_point start_world, int x, int y,
                             CadSelection *set) {
    /* Update the rectangle too, so the persistent colour matches the final
       window/crossing mode; the overlay is cleared immediately after. */
    int crossing = selection_update(doc, window, start_world, x, y, 1, set);

    if (crossing >= 0) {
        printf("select %s: %zu id(s):", crossing ? "crossing" : "window",
               set->count);
        for (size_t i = 0; i < set->count; ++i) printf(" %u", set->ids[i]);
        printf("\n");
        fflush(stdout);
    }

    isogl_clear_selection_rect();
}

int main(int argc, char **argv) {
    const char *asset_dir = NULL;
    size_t pick_max_entities = 5;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            asset_dir = argv[++i];
        } else if (strcmp(argv[i], "--pick-max") == 0 && i + 1 < argc) {
            long value = strtol(argv[++i], NULL, 10);
            if (value > 0) pick_max_entities = (size_t)value;
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

    /* isocad applies the exact per-entity selection through cad; the
       per-segment rectangle tint is only a preview and is not used here. */
    isogl_set_selection_preview(0);

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
    int selecting = 0;
    int sel_moved = 0;
    int sel_start_px_x = 0;
    int sel_start_px_y = 0;
    int panning = 0;
    isogl_point sel_start_world = {0.0f, 0.0f};
    CadSelection selection = {0};
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
                        if (selecting && sel_moved) {
                            int mx = 0, my = 0;
                            SDL_GetMouseState(&mx, &my);
                            selection_update(&doc, window, sel_start_world,
                                             mx, my, 1, &selection);
                        }
                    }
                    break;

                case SDL_KEYDOWN:
                    if (event.key.repeat) break;
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            if (selecting || isogl_get_selection_active() ||
                                selection.count > 0) {
                                selecting = 0;
                                sel_moved = 0;
                                isogl_clear_selection_rect();
                                selection_set_clear(&selection);
                                cad_selection_clear(&doc);
                            }
                            break;
                        case SDLK_q:
                            running = 0;
                            break;
                        case SDLK_f: {
                            isogl_zoom_all_sheet();
                            if (selecting && sel_moved) {
                                int mx = 0, my = 0;
                                SDL_GetMouseState(&mx, &my);
                                selection_update(&doc, window, sel_start_world,
                                                 mx, my, 1, &selection);
                            }
                            break;
                        }
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
                    if (selecting && sel_moved) {
                        int mx = 0, my = 0;
                        SDL_GetMouseState(&mx, &my);
                        selection_update(&doc, window, sel_start_world,
                                         mx, my, 1, &selection);
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (isogl_screen_to_world(window,
                                                  (float)event.button.x,
                                                  (float)event.button.y,
                                                  &sel_start_world) == 0) {
                            selecting = 1;
                            sel_moved = 0;
                            sel_start_px_x = event.button.x;
                            sel_start_px_y = event.button.y;
                            isogl_clear_selection_rect();
                        }
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        pick_and_print(&doc, window,
                                       event.button.x, event.button.y,
                                       pick_max_entities);
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        panning = 1;
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT && selecting) {
                        if (!sel_moved) {
                            pick_and_print(&doc, window,
                                           event.button.x, event.button.y,
                                           pick_max_entities);
                            isogl_clear_selection_rect();
                        } else {
                            selection_finish(&doc, window, sel_start_world,
                                             event.button.x, event.button.y,
                                             &selection);
                        }
                        selecting = 0;
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        panning = 0;
                        if (selecting && sel_moved) {
                            int mx = 0, my = 0;
                            SDL_GetMouseState(&mx, &my);
                            selection_update(&doc, window, sel_start_world,
                                             mx, my, 1, &selection);
                        }
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (panning) {
                        isogl_handle_pan(window,
                                         event.motion.xrel,
                                         event.motion.yrel);
                        if (selecting && sel_moved) {
                            selection_update(&doc, window, sel_start_world,
                                             event.motion.x, event.motion.y,
                                             1, &selection);
                        }
                    } else if (selecting) {
                        if (!sel_moved &&
                            abs(event.motion.x - sel_start_px_x) +
                            abs(event.motion.y - sel_start_px_y) > 4) {
                            sel_moved = 1;
                            selection_set_clear(&selection);
                            cad_selection_clear(&doc);
                        }
                        if (sel_moved) {
                            selection_update(&doc, window, sel_start_world,
                                             event.motion.x, event.motion.y,
                                             1, &selection);
                        }
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
        isogl_draw_selection_overlay();
        isogl_swap_buffers(window);
    }

    selection_set_free(&selection);
    free(selection_contained);
    free(selection_hit);
    selection_contained = NULL;
    selection_hit = NULL;

    shutdown_all(window, context, &doc, font);
    return 0;
}
