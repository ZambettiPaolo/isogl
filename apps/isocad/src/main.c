#include <isogl/isogl.h>
#include <isogl-sdl/isogl_sdl.h>

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cad.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void shutdown_all(isogl_sdl_host *host, CadDocument *doc,
                         isogl_font_data *font) {
    if (doc) cad_document_free(doc);
    isogl_symbol_cleanup();
    isogl_font_free_data(font);
    isogl_cleanup();
    if (host) isogl_sdl_host_shutdown(host);
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

    /* Aligned diameter dimension below the circle. */
    IsoglDimensionDesc dim;
    memset(&dim, 0, sizeof(dim));
    dim.x0 = sheet_w * 0.5f - 60.0f;
    dim.y0 = sheet_h * 0.5f;
    dim.x1 = sheet_w * 0.5f + 60.0f;
    dim.y1 = sheet_h * 0.5f;
    dim.px = dim.x0;
    dim.py = dim.y0 - 75.0f;
    dim.distance = 75.0f;
    dim.text_offset_mm = 0.0f;
    dim.text_height_mm = 5.0;
    dim.precision = 1;
    dim.pick_text = 1;
    dim.arrow_left = ISOGL_DIM_ARROW_CLOSED;
    dim.arrow_right = ISOGL_DIM_ARROW_CLOSED;
    snprintf(dim.text_left, sizeof(dim.text_left), "\xC3\x98 ");
    snprintf(dim.text_right, sizeof(dim.text_right), "");
    cad_add_dimension_symbol(doc, 2, &dim);

    /* Horizontal dimension with tick arrows. */
    IsoglDimensionDesc hdim;
    memset(&hdim, 0, sizeof(hdim));
    hdim.x0 = 30.0f;
    hdim.y0 = 250.0f;
    hdim.x1 = 150.0f;
    hdim.y1 = 250.0f;
    hdim.px = 90.0f;
    hdim.py = 230.0f;
    hdim.distance = 20.0f;
    hdim.text_offset_mm = 0.0f;
    hdim.text_height_mm = 4.0;
    hdim.precision = 1;
    hdim.pick_text = 1;
    hdim.arrow_left = ISOGL_DIM_ARROW_TICK;
    hdim.arrow_right = ISOGL_DIM_ARROW_TICK;
    snprintf(hdim.text_left, sizeof(hdim.text_left), "L ");
    cad_add_dimension_symbol(doc, 1, &hdim);

    /* Vertical dimension with dot arrows and the text shifted along the line. */
    IsoglDimensionDesc vdim;
    memset(&vdim, 0, sizeof(vdim));
    vdim.x0 = 330.0f;
    vdim.y0 = 60.0f;
    vdim.x1 = 330.0f;
    vdim.y1 = 180.0f;
    vdim.px = 360.0f;
    vdim.py = 120.0f;
    vdim.distance = 30.0f;
    vdim.text_offset_mm = 8.0f;
    vdim.text_height_mm = 4.0;
    vdim.precision = 1;
    vdim.pick_text = 1;
    vdim.arrow_left = ISOGL_DIM_ARROW_DOT;
    vdim.arrow_right = ISOGL_DIM_ARROW_DOT;
    snprintf(vdim.text_left, sizeof(vdim.text_left), "H ");
    cad_add_dimension_symbol(doc, 2, &vdim);

    /* Inclined dimension with mixed arrows and text on both sides. */
    IsoglDimensionDesc idim;
    memset(&idim, 0, sizeof(idim));
    idim.x0 = 60.0f;
    idim.y0 = 60.0f;
    idim.x1 = 120.0f;
    idim.y1 = 120.0f;
    idim.px = 40.0f;
    idim.py = 100.0f;
    idim.distance = 10.0f;
    idim.text_offset_mm = 12.0f;
    idim.text_height_mm = 4.0;
    idim.precision = 1;
    idim.pick_text = 1;
    idim.arrow_left = ISOGL_DIM_ARROW_OPEN;
    idim.arrow_right = ISOGL_DIM_ARROW_CLOSED;
    snprintf(idim.text_left, sizeof(idim.text_left), "L ");
    snprintf(idim.text_right, sizeof(idim.text_right), " 45\xC2\xB0");
    cad_add_dimension_symbol(doc, 1, &idim);

    /* Center mark at the circle centre: a second isogl symbol type. */
    IsoglCenterMarkDesc mark;
    memset(&mark, 0, sizeof(mark));
    mark.cx = sheet_w * 0.5f;
    mark.cy = sheet_h * 0.5f;
    mark.size_mm = 8.0;
    cad_add_center_mark_symbol(doc, 0, &mark);
}

static int isocad_pick_extent(const isogl_sdl_host *host) {
    double s = host->scale_x > host->scale_y ? host->scale_x : host->scale_y;
    if (s < 1.0) s = 1.0;
    return (int)lround((double)ISOGL_PICK_DEFAULT_EXTENT * s);
}

static void pick_and_print(CadDocument *doc, isogl_sdl_host *host,
                           float px, float py, size_t max_entities) {
    uint32_t ids[4096];
    size_t count = 0;
    int extent = isocad_pick_extent(host);

    if (isogl_pick_at(&doc->parts, &doc->strings, px, py, extent,
                      ids, sizeof(ids) / sizeof(ids[0]), &count) == 0) {
        printf("pick at (%.1f,%.1f): %zu entity id(s):", px, py, count);
        for (size_t i = 0; i < count; ++i) printf(" %u", ids[i]);
        if (count > max_entities) {
            printf(" — ambiguo (> %zu), zoom in", max_entities);
        }
        printf("\n");
    } else {
        printf("pick at (%.1f,%.1f): errore entità\n", px, py);
    }

    uint32_t symbol_ids[64];
    size_t symbol_count = 0;
    if (isogl_symbol_pick_at(px, py, extent,
                             symbol_ids, 64, &symbol_count) == 0 &&
        symbol_count > 0) {
        printf("pick at (%.1f,%.1f): %zu symbol id(s):", px, py, symbol_count);
        for (size_t i = 0; i < symbol_count; ++i) {
            printf(" %u", symbol_ids[i]);
        }
        printf("\n");
    }
    fflush(stdout);
}

typedef enum {
    CAD_SEL_ENTITY = 0,
    CAD_SEL_SYMBOL = 1
} CadSelKind;

typedef struct {
    CadSelKind kind;
    uint32_t id;
} CadSelRef;

typedef struct {
    CadSelRef *refs;
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

static void selection_set_append(CadSelection *set, CadSelKind kind,
                                 uint32_t id) {
    if (!set) return;
    if (set->count == set->capacity) {
        size_t capacity = set->capacity ? set->capacity * 2 : 32;
        CadSelRef *grown = realloc(set->refs, capacity * sizeof(*grown));
        if (!grown) return;
        set->refs = grown;
        set->capacity = capacity;
    }
    set->refs[set->count].kind = kind;
    set->refs[set->count].id = id;
    set->count++;
}

static void selection_set_clear(CadSelection *set) {
    if (set) set->count = 0;
}

static void selection_set_free(CadSelection *set) {
    if (!set) return;
    free(set->refs);
    set->refs = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void selection_clear_all(CadDocument *doc, CadSelection *set) {
    selection_set_clear(set);
    if (!doc) return;
    cad_selection_clear(doc);
    for (size_t i = 0; i < doc->symbol_count; ++i) {
        isogl_symbol_set_selected(doc->symbols[i], 0);
    }
}

static int selection_compute(isogl_sdl_host *host, isogl_point start_world,
                             int x, int y, isogl_point *end_world,
                             int *crossing, int update_highlight) {
    float px = 0.0f, py = 0.0f;
    isogl_sdl_host_local_to_pixels(host, (float)x, (float)y, &px, &py);

    isogl_point start_screen;
    if (isogl_world_to_screen(start_world.x, start_world.y,
                              &start_screen) != 0) {
        return -1;
    }
    if (isogl_screen_to_world(px, py, end_world) != 0) {
        return -1;
    }

    *crossing = (px < start_screen.x);

    if (update_highlight) {
        float gx0 = fminf(start_screen.x, px);
        float gx1 = fmaxf(start_screen.x, px);
        float gy0 = fminf(start_screen.y, py);
        float gy1 = fmaxf(start_screen.y, py);
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

    cad_selection_apply(doc, ids, count);
    selection_set_clear(set);
    for (size_t i = 0; i < count; ++i) {
        selection_set_append(set, CAD_SEL_ENTITY, ids[i]);
    }

    /* isogl symbols use their own ID namespace and pick geometry. */
    isogl_box query = {
        fmin((double)start_world.x, (double)end_world.x),
        fmin((double)start_world.y, (double)end_world.y),
        fmax((double)start_world.x, (double)end_world.x),
        fmax((double)start_world.y, (double)end_world.y)
    };

    for (size_t i = 0; i < doc->symbol_count; ++i) {
        isogl_symbol_id symbol = doc->symbols[i];
        isogl_symbol_set_selected(symbol, 0);
        if (isogl_symbol_hit(symbol, &query, crossing) == 1) {
            isogl_symbol_set_selected(symbol, 1);
            selection_set_append(set, CAD_SEL_SYMBOL, symbol);
        }
    }
}

static int selection_update(CadDocument *doc, isogl_sdl_host *host,
                            isogl_point start_world, int x, int y,
                            int update_highlight, CadSelection *set) {
    isogl_point end_world = start_world;
    int crossing = 0;

    if (selection_compute(host, start_world, x, y, &end_world,
                          &crossing, update_highlight) != 0) {
        return -1;
    }

    selection_apply_rect(doc, start_world, end_world, crossing, set);
    return crossing;
}

static void selection_finish(CadDocument *doc, isogl_sdl_host *host,
                             isogl_point start_world, int x, int y,
                             CadSelection *set) {
    /* Update the rectangle too, so the persistent colour matches the final
       window/crossing mode; the overlay is cleared immediately after. */
    int crossing = selection_update(doc, host, start_world, x, y, 1, set);

    if (crossing >= 0) {
        printf("select %s: %zu ref(s):", crossing ? "crossing" : "window",
               set->count);
        for (size_t i = 0; i < set->count; ++i) {
            printf(" %c%u",
                   set->refs[i].kind == CAD_SEL_ENTITY ? 'E' : 'S',
                   set->refs[i].id);
        }
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

    isogl_sdl_host host;
    isogl_sdl_host_desc desc = {
        .title = "isocad",
        .width = 1280,
        .height = 800,
        .gl_major = 3,
        .gl_minor = 3,
        .multisample = 0,
        .vsync = 1,
        .hidden = 0
    };
    if (isogl_sdl_host_init(&host, &desc) != 0) {
        fprintf(stderr, "isogl_sdl_host_init: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (isogl_init_with_asset_dir(asset_dir) != 0) {
        fprintf(stderr, "Failed to initialize isogl (assets: %s)\n",
                asset_dir ? asset_dir : isogl_asset_dir());
        shutdown_all(&host, NULL, NULL);
        return 1;
    }

    /* isocad applies the exact per-entity selection through cad; the
       per-segment rectangle tint is only a preview and is not used here. */
    isogl_set_selection_preview(0);

    char font_path[4096];
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                         font_path, sizeof(font_path)) != 0) {
        fprintf(stderr, "Font asset path too long\n");
        shutdown_all(&host, NULL, NULL);
        return 1;
    }

    isogl_font_data *font = isogl_font_load_from_toml(font_path);
    if (!font) {
        fprintf(stderr, "Failed to load font: %s\n", font_path);
        shutdown_all(&host, NULL, NULL);
        return 1;
    }
    isogl_font_init_vao();

    if (isogl_symbol_init(font) != 0) {
        fprintf(stderr, "Failed to initialize symbol registry\n");
        shutdown_all(&host, NULL, font);
        return 1;
    }

    float sheet_w = 0.0f, sheet_h = 0.0f;
    isogl_sheet_size(ISOGL_SHEET_A3, ISOGL_ORIENTATION_LANDSCAPE, &sheet_w, &sheet_h);

    if (isogl_set_sheet(sheet_w, sheet_h, (isogl_scale){1, 1}) != 0) {
        fprintf(stderr, "Failed to create sheet\n");
        shutdown_all(&host, NULL, font);
        return 1;
    }
    isogl_zoom_all_sheet();
    isogl_sdl_host_update(&host);

    CadDocument doc;
    if (cad_document_init(&doc, font, 1.0f) != 0) {
        fprintf(stderr, "Failed to create document\n");
        shutdown_all(&host, NULL, font);
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
                        isogl_sdl_host_update(&host);
                        if (selecting && sel_moved) {
                            int mx = 0, my = 0;
                            SDL_GetMouseState(&mx, &my);
                            selection_update(&doc, &host, sel_start_world,
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
                                selection_clear_all(&doc, &selection);
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
                                selection_update(&doc, &host, sel_start_world,
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

                case SDL_MOUSEWHEEL: {
                    float px = 0.0f, py = 0.0f;
                    isogl_sdl_host_local_to_pixels(&host,
                        (float)event.wheel.mouseX,
                        (float)event.wheel.mouseY, &px, &py);
                    isogl_handle_zoom(event.wheel.y, px, py);
                    if (selecting && sel_moved) {
                        int mx = 0, my = 0;
                        SDL_GetMouseState(&mx, &my);
                        selection_update(&doc, &host, sel_start_world,
                                         mx, my, 1, &selection);
                    }
                    break;
                }

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        float px = 0.0f, py = 0.0f;
                        isogl_sdl_host_local_to_pixels(&host,
                            (float)event.button.x, (float)event.button.y,
                            &px, &py);
                        if (isogl_screen_to_world(px, py,
                                                  &sel_start_world) == 0) {
                            selecting = 1;
                            sel_moved = 0;
                            sel_start_px_x = event.button.x;
                            sel_start_px_y = event.button.y;
                            isogl_clear_selection_rect();
                        }
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        float px = 0.0f, py = 0.0f;
                        isogl_sdl_host_local_to_pixels(&host,
                            (float)event.button.x, (float)event.button.y,
                            &px, &py);
                        pick_and_print(&doc, &host, px, py,
                                       pick_max_entities);
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        panning = 1;
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT && selecting) {
                        if (!sel_moved) {
                            float px = 0.0f, py = 0.0f;
                            isogl_sdl_host_local_to_pixels(&host,
                                (float)event.button.x,
                                (float)event.button.y, &px, &py);
                            pick_and_print(&doc, &host, px, py,
                                           pick_max_entities);
                            isogl_clear_selection_rect();
                        } else {
                            selection_finish(&doc, &host, sel_start_world,
                                             event.button.x, event.button.y,
                                             &selection);
                        }
                        selecting = 0;
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        panning = 0;
                        if (selecting && sel_moved) {
                            int mx = 0, my = 0;
                            SDL_GetMouseState(&mx, &my);
                            selection_update(&doc, &host, sel_start_world,
                                             mx, my, 1, &selection);
                        }
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (panning) {
                        float dx = 0.0f, dy = 0.0f;
                        isogl_sdl_host_delta_to_pixels(&host,
                            (float)event.motion.xrel,
                            (float)event.motion.yrel, &dx, &dy);
                        isogl_handle_pan(dx, dy);
                        if (selecting && sel_moved) {
                            selection_update(&doc, &host, sel_start_world,
                                             event.motion.x, event.motion.y,
                                             1, &selection);
                        }
                    } else if (selecting) {
                        if (!sel_moved &&
                            abs(event.motion.x - sel_start_px_x) +
                            abs(event.motion.y - sel_start_px_y) > 4) {
                            sel_moved = 1;
                            selection_clear_all(&doc, &selection);
                        }
                        if (sel_moved) {
                            selection_update(&doc, &host, sel_start_world,
                                             event.motion.x, event.motion.y,
                                             1, &selection);
                        }
                    }
                    break;

                default:
                    break;
            }
        }

        isogl_sdl_host_begin_frame(&host);
        isogl_clear(0.08f, 0.09f, 0.10f, 1.0f);
        isogl_draw_sheet();
        isogl_draw_all_vbo_objects(&doc.parts);
        isogl_draw_string_list(&doc.strings, isogl_shader_program_font,
                               isogl_model_matrix, isogl_view_matrix, identity);
        isogl_symbol_draw_all();
        isogl_draw_selection_overlay();
        isogl_sdl_host_swap(&host);
    }

    selection_set_free(&selection);
    free(selection_contained);
    free(selection_hit);
    selection_contained = NULL;
    selection_hit = NULL;

    shutdown_all(&host, &doc, font);
    return 0;
}
