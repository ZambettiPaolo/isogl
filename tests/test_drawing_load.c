#define _POSIX_C_SOURCE 200809L

#include "drawing_load.h"
#include "isogl.h"
#include "isogl_font.h"
#include "isogl_symbol.h"
#include "glad.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FB_SIZE 256

static int failures;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int count_bright(const unsigned char *pixels) {
    int count = 0;
    for (int i = 0; i < FB_SIZE * FB_SIZE; ++i) {
        const unsigned char *p = &pixels[i * 4];
        if (p[0] > 70 || p[1] > 70 || p[2] > 70) count++;
    }
    return count;
}

/* ---- params helpers ---------------------------------------------------- */

static int param_index(const isogl_symbol_params *params, const char *key) {
    if (!params || !key) return -1;
    for (size_t i = 0; i < params->param_count; ++i) {
        if (params->params[i].desc &&
            strcmp(params->params[i].desc->key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int entry_index(const DrawingSymbols *symbols, const char *type) {
    if (!symbols || !type) return -1;
    for (size_t i = 0; i < symbols->count; ++i) {
        if (symbols->items[i].type &&
            strcmp(symbols->items[i].type, type) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int near(double a, double b) {
    return fabs(a - b) < 1e-6;
}

static int params_equal(const isogl_symbol_params *a,
                        const isogl_symbol_params *b) {
    if (!a || !b) return a == b;
    if (a->param_count != b->param_count) return 0;

    for (size_t i = 0; i < a->param_count; ++i) {
        const isogl_symbol_param *pa = &a->params[i];
        const isogl_symbol_param *pb = &b->params[i];
        if (!pa->desc || !pb->desc) return 0;
        if (strcmp(pa->desc->key, pb->desc->key) != 0) return 0;
        if (pa->desc->kind != pb->desc->kind) return 0;

        switch (pa->desc->kind) {
            case ISOGL_SYM_PARAM_POINTS:
                if (pa->count != pb->count) return 0;
                for (size_t j = 0; j < pa->count; ++j) {
                    if (!near(pa->value.points[j].x,
                              pb->value.points[j].x) ||
                        !near(pa->value.points[j].y,
                              pb->value.points[j].y)) {
                        return 0;
                    }
                }
                break;
            case ISOGL_SYM_PARAM_TEXTS:
                if (pa->count != pb->count) return 0;
                for (size_t j = 0; j < pa->count; ++j) {
                    const char *ta = pa->value.texts[j];
                    const char *tb = pb->value.texts[j];
                    if (strcmp(ta ? ta : "", tb ? tb : "") != 0) return 0;
                }
                break;
            case ISOGL_SYM_PARAM_SEGMENTS:
                if (pa->count != pb->count) return 0;
                for (size_t j = 0; j < pa->count; ++j) {
                    const isogl_symbol_segment *sa = &pa->value.segments[j];
                    const isogl_symbol_segment *sb = &pb->value.segments[j];
                    if (!near(sa->x0, sb->x0) || !near(sa->y0, sb->y0) ||
                        !near(sa->x1, sb->x1) || !near(sa->y1, sb->y1)) {
                        return 0;
                    }
                }
                break;
            case ISOGL_SYM_PARAM_NUMBER:
            case ISOGL_SYM_PARAM_SCALE:
                if (!near(pa->value.number, pb->value.number)) return 0;
                break;
            case ISOGL_SYM_PARAM_INTEGER:
            case ISOGL_SYM_PARAM_ENUM:
                if (pa->value.integer != pb->value.integer) return 0;
                break;
            case ISOGL_SYM_PARAM_BOOLEAN:
                if ((pa->value.boolean != 0) != (pb->value.boolean != 0)) {
                    return 0;
                }
                break;
            case ISOGL_SYM_PARAM_COLOR:
                for (int j = 0; j < 4; ++j) {
                    if (fabs((double)pa->value.color[j] -
                             (double)pb->value.color[j]) > 1e-6) {
                        return 0;
                    }
                }
                break;
        }
    }
    return 1;
}

static int boxes_equal(const isogl_box *a, const isogl_box *b) {
    return near(a->xmin, b->xmin) && near(a->ymin, b->ymin) &&
           near(a->xmax, b->xmax) && near(a->ymax, b->ymax);
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buffer = malloc((size_t)length + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buffer, 1, (size_t)length, f);
    buffer[got] = '\0';
    fclose(f);
    return buffer;
}

static const char RT_PREFIX[] =
    "[sheet]\n"
    "format = \"A3\"\n"
    "orientation = \"landscape\"\n"
    "scale_num = 1\n"
    "scale_den = 1\n"
    "schema_version = 2\n"
    "\n"
    "[[groups]]\n"
    "id = 0\n"
    "parent = -1\n"
    "x = 0.0\n"
    "y = 0.0\n"
    "angle_deg = 0.0\n"
    "scale = 1.0\n"
    "\n"
    "[[groups]]\n"
    "id = 1\n"
    "parent = 0\n"
    "x = 0.0\n"
    "y = 0.0\n"
    "angle_deg = 0.0\n"
    "scale = 1.0\n"
    "\n";

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : NULL;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

    SDL_Window *window = SDL_CreateWindow("isogl-drawing-load-test", 0, 0,
                                          FB_SIZE, FB_SIZE,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        return 2;
    }
    if (isogl_init_with_asset_dir(assets) != 0) {
        fprintf(stderr, "isogl_init_with_asset_dir failed\n");
        return 2;
    }

    char font_path[4096];
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml", font_path,
                         sizeof(font_path)) != 0) {
        fprintf(stderr, "font asset path failed\n");
        return 2;
    }
    isogl_font_data *font = isogl_font_load_from_toml(font_path);
    check(font != NULL, "font loaded");
    if (!font) return 2;
    isogl_font_init_vao();

    /* The loader resolves type names against the C registry, so the registry
     * must be initialized before drawing_load(). */
    check(isogl_symbol_init(font) == 0, "symbol registry initialized");
    double sheet_scale = 1.0;

    /* ---- legacy flat schema (p1x / cx) --------------------------------- */
    isogl_segment_list legacy_segments;
    check(isogl_segment_list_init(&legacy_segments, 256) == 0,
          "legacy segment list initialized");
    isogl_string_list legacy_strings = {0};
    DrawingSheetInfo legacy_sheet;
    DrawingSymbols legacy_symbols;

    int legacy_loaded = drawing_load("tests/fixtures/test_drawing.toml",
                                     font, &legacy_segments, &legacy_strings,
                                     &legacy_sheet, &legacy_symbols);
    check(legacy_loaded == 0, "legacy hierarchical drawing loaded");
    check(legacy_sheet.schema_version == 1,
          "legacy drawing defaults to schema version 1");
    check(legacy_symbols.count == 2, "legacy: two symbol entries parsed");

    int legacy_dim = entry_index(&legacy_symbols, "dimension");
    int legacy_mark = entry_index(&legacy_symbols, "center_mark");
    check(legacy_dim >= 0 && legacy_mark >= 0,
          "legacy: both known type names resolved");
    if (legacy_dim >= 0) {
        const isogl_symbol_params *p = legacy_symbols.items[legacy_dim].params;
        int points = param_index(p, "points");
        int texts = param_index(p, "texts");
        int weight = param_index(p, "weight");
        int layer = param_index(p, "layer");
        int color = param_index(p, "color");
        check(points >= 0 && p->params[points].count == 3,
              "legacy dimension maps p1/p2/p3 to points");
        check(near(p->params[points].value.points[0].x, 40.0) &&
              near(p->params[points].value.points[0].y, 60.0),
              "legacy dimension p1 survives the group transform");
        check(near(p->params[points].value.points[1].x, 160.0) &&
              near(p->params[points].value.points[1].y, 60.0),
              "legacy dimension p2 survives the group transform");
        check(texts >= 0 &&
              strcmp(p->params[texts].value.texts[1], " mm") == 0,
              "legacy text_right maps to the second text parameter");
        check(weight >= 0 && p->params[weight].value.integer == 1,
              "legacy weight maps to the typed parameter");
        check(layer >= 0 && p->params[layer].value.integer == 2,
              "legacy layer maps to the typed parameter");
        check(color >= 0 &&
              fabs((double)p->params[color].value.color[1] - 0.85) < 1e-6,
              "legacy color maps to the typed parameter");
    }
    if (legacy_mark >= 0) {
        const isogl_symbol_params *p = legacy_symbols.items[legacy_mark].params;
        int points = param_index(p, "points");
        int size = param_index(p, "size_mm");
        check(points >= 0 && p->params[points].count == 1 &&
              near(p->params[points].value.points[0].x, 150.0) &&
              near(p->params[points].value.points[0].y, 60.0),
              "legacy center mark cx/cy maps to points");
        check(size >= 0 && near(p->params[size].value.number, 8.0),
              "legacy center mark size maps to size_mm");
    }
    check(legacy_sheet.bounds.has_bounds &&
          legacy_sheet.bounds.xmax >= 160.0f &&
          legacy_sheet.bounds.ymax >= 90.0f,
          "legacy symbol geometry participates in bounds");

    isogl_segment_list_free(&legacy_segments);
    isogl_string_list_free(&legacy_strings);
    drawing_symbols_free(&legacy_symbols);

    /* ---- schema v2: typed params, group transform, unknown skip -------- */
    isogl_segment_list segments;
    check(isogl_segment_list_init(&segments, 256) == 0,
          "segment list initialized");
    isogl_string_list strings = {0};
    DrawingSheetInfo sheet;
    DrawingSymbols symbols;

    int loaded = drawing_load("tests/fixtures/test_drawing_v2.toml",
                              font, &segments, &strings, &sheet, &symbols);
    check(loaded == 0, "v2 hierarchical drawing loaded");
    check(sheet.schema_version == 2, "v2 schema version parsed");
    check(symbols.count == 3, "v2: three symbol entries parsed");

    int dim = entry_index(&symbols, "dimension");
    int mark = entry_index(&symbols, "center_mark");
    int unknown = entry_index(&symbols, "unknown_marker");
    check(dim >= 0 && mark >= 0, "v2: known types resolved from the registry");
    check(unknown >= 0 && symbols.items[unknown].params == NULL,
          "v2: unknown type kept with NULL params and skipped");
    check(dim >= 0 && symbols.items[dim].group == 1,
          "v2: symbol group preserved");

    if (dim >= 0 && symbols.items[dim].params) {
        const isogl_symbol_params *p = symbols.items[dim].params;
        int points = param_index(p, "points");
        int texts = param_index(p, "texts");
        int segments_p = param_index(p, "segments");
        int color = param_index(p, "color");
        check(points >= 0 && p->params[points].count == 3,
              "v2 dimension points parsed");
        check(near(p->params[points].value.points[0].x, 40.0) &&
              near(p->params[points].value.points[0].y, 45.0) &&
              near(p->params[points].value.points[1].x, 90.0) &&
              near(p->params[points].value.points[1].y, 45.0) &&
              near(p->params[points].value.points[2].x, 40.0) &&
              near(p->params[points].value.points[2].y, 55.0),
              "v2 dimension points survive the group transform");
        check(texts >= 0 &&
              strcmp(p->params[texts].value.texts[0], "") == 0 &&
              strcmp(p->params[texts].value.texts[1], " mm") == 0,
              "v2 text array parsed");
        check(segments_p >= 0 && p->params[segments_p].count == 0,
              "v2 empty segment array parsed");
        check(color >= 0 &&
              fabs((double)p->params[color].value.color[0] - 1.0) < 1e-6 &&
              fabs((double)p->params[color].value.color[1] - 0.85) < 1e-6,
              "v2 color array parsed");
    }
    if (mark >= 0 && symbols.items[mark].params) {
        const isogl_symbol_params *p = symbols.items[mark].params;
        int points = param_index(p, "points");
        check(points >= 0 && p->params[points].count == 1 &&
              near(p->params[points].value.points[0].x, 150.0) &&
              near(p->params[points].value.points[0].y, 60.0),
              "v2 center mark point parsed");
    }
    check(sheet.bounds.has_bounds &&
          sheet.bounds.xmax >= 154.0f &&
          sheet.bounds.ymax >= 64.0f,
          "v2 symbol pick geometry participates in bounds");

    /* ---- creation from the generic params + pick ----------------------- */
    isogl_symbol_id ids[2] = {0, 0};
    isogl_box boxes[2];
    memset(boxes, 0, sizeof(boxes));
    if (dim >= 0 && symbols.items[dim].params) {
        ids[0] = isogl_symbol_create(symbols.items[dim].type,
                                     symbols.items[dim].params, sheet_scale);
        check(ids[0] != 0, "v2 dimension created from params");
        check(isogl_symbol_bounds(ids[0], &boxes[0]) == 0,
              "v2 dimension bounds available");
    }
    if (mark >= 0 && symbols.items[mark].params) {
        ids[1] = isogl_symbol_create(symbols.items[mark].type,
                                     symbols.items[mark].params, sheet_scale);
        check(ids[1] != 0, "v2 center mark created from params");
        check(isogl_symbol_bounds(ids[1], &boxes[1]) == 0,
              "v2 center mark bounds available");
    }

    isogl_scale scale = {sheet.scale_num, sheet.scale_den};
    check(isogl_set_sheet(sheet.width_mm, sheet.height_mm, scale) == 0,
          "sheet created from the loader result");

    isogl_vbo_array vbo;
    isogl_vbo_array_init(&vbo);
    GLuint part = isogl_vbo_array_add(&vbo, &segments, ISOGL_VBO_SEGMENT);
    isogl_segment_list_free(&segments);
    check(part != 0, "drawing VBO uploaded");

    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, FB_SIZE, FB_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "offscreen framebuffer complete");

    isogl_viewport vp = {0, 0, FB_SIZE, FB_SIZE};
    isogl_set_viewport(&vp);
    isogl_begin_frame();
    isogl_zoom_all_sheet();
    isogl_clear(0.02f, 0.02f, 0.02f, 1.0f);
    isogl_draw_sheet();
    isogl_draw_all_vbo_objects(&vbo);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    isogl_draw_string_list(&strings, isogl_shader_program_font,
                           isogl_model_matrix, isogl_view_matrix, identity);
    isogl_symbol_draw_all();

    unsigned char pixels[FB_SIZE * FB_SIZE * 4];
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(count_bright(pixels) > 10, "symbols and drawing render pixels");

    isogl_point screen;
    check(isogl_world_to_screen(150.0f, 60.0f, &screen) == 0,
          "center mark maps to screen");
    uint32_t picked[64];
    size_t picked_count = 0;
    int pick_ok = isogl_symbol_pick_at(screen.x, screen.y,
                                       ISOGL_PICK_DEFAULT_EXTENT,
                                       picked, 64, &picked_count);
    check(pick_ok == 0 && picked_count > 0,
          "symbol pick returns at least one ID");
    check(picked_count > 0 && picked[0] == ids[1],
          "symbol pick returns the center mark ID first");

    /* ---- writer round-trip -------------------------------------------- */
    char symbols_path[] = "build/test-drawing-load-symbols-XXXXXX";
    int symbols_fd = mkstemp(symbols_path);
    check(symbols_fd >= 0, "temporary symbol section created");
    if (symbols_fd >= 0) {
        close(symbols_fd);
        check(drawing_save_symbols(symbols_path, &symbols) == 0,
              "symbol section saved");
        char *section = read_text_file(symbols_path);
        check(section != NULL, "saved symbol section readable");
        if (section) {
            check(strstr(section, "[[symbols]]") != NULL &&
                  strstr(section, "[symbols.params]") != NULL,
                  "writer emits [[symbols]] and [symbols.params]");
            check(strstr(section, "type = \"dimension\"") != NULL &&
                  strstr(section, "type = \"center_mark\"") != NULL,
                  "writer emits the registered type names");
            check(strstr(section, "scale =") == NULL,
                  "writer omits the per-symbol SCALE parameter");
            check(strstr(section, "unknown_marker") == NULL,
                  "writer omits unknown types (no raw representation)");
            free(section);
        }
        unlink(symbols_path);
    }

    char roundtrip_path[] = "build/test-drawing-load-rt-XXXXXX";
    int rt_fd = mkstemp(roundtrip_path);
    check(rt_fd >= 0, "temporary round-trip drawing created");
    if (rt_fd >= 0) {
        FILE *out = fdopen(rt_fd, "w");
        int write_ok = 0;
        if (out) {
            write_ok = fputs(RT_PREFIX, out) >= 0 &&
                       drawing_write_symbols(out, &symbols) == 0;
            if (fclose(out) != 0) write_ok = 0;
        }
        check(write_ok != 0, "symbol section written for round-trip");

        isogl_segment_list rt_segments;
        memset(&rt_segments, 0, sizeof(rt_segments));
        isogl_string_list rt_strings = {0};
        DrawingSheetInfo rt_sheet;
        DrawingSymbols rt_symbols;
        memset(&rt_symbols, 0, sizeof(rt_symbols));
        int rt_loaded = isogl_segment_list_init(&rt_segments, 64) == 0;
        if (rt_loaded) {
            rt_loaded = drawing_load(roundtrip_path, font, &rt_segments,
                                     &rt_strings, &rt_sheet, &rt_symbols) == 0;
        }
        check(rt_loaded, "round-trip drawing reloaded");
        check(rt_symbols.count == 2,
              "round-trip keeps the two known symbols");

        int rt_dim = entry_index(&rt_symbols, "dimension");
        int rt_mark = entry_index(&rt_symbols, "center_mark");
        check(rt_dim >= 0 && rt_mark >= 0,
              "round-trip type names reloaded");
        if (rt_dim >= 0 && dim >= 0) {
            check(params_equal(symbols.items[dim].params,
                               rt_symbols.items[rt_dim].params),
                  "round-trip dimension params match (points/texts/scalars/color)");
        }
        if (rt_mark >= 0 && mark >= 0) {
            check(params_equal(symbols.items[mark].params,
                               rt_symbols.items[rt_mark].params),
                  "round-trip center mark params match");
        }

        isogl_symbol_id rt_ids[2] = {0, 0};
        isogl_box rt_boxes[2];
        memset(rt_boxes, 0, sizeof(rt_boxes));
        if (rt_dim >= 0 && rt_symbols.items[rt_dim].params) {
            rt_ids[0] = isogl_symbol_create(rt_symbols.items[rt_dim].type,
                                            rt_symbols.items[rt_dim].params,
                                            sheet_scale);
            check(rt_ids[0] != 0, "round-trip dimension created");
            check(isogl_symbol_bounds(rt_ids[0], &rt_boxes[0]) == 0 &&
                  ids[0] != 0 && boxes_equal(&rt_boxes[0], &boxes[0]),
                  "round-trip dimension bounds match");
        }
        if (rt_mark >= 0 && rt_symbols.items[rt_mark].params) {
            rt_ids[1] = isogl_symbol_create(rt_symbols.items[rt_mark].type,
                                            rt_symbols.items[rt_mark].params,
                                            sheet_scale);
            check(rt_ids[1] != 0, "round-trip center mark created");
            check(isogl_symbol_bounds(rt_ids[1], &rt_boxes[1]) == 0 &&
                  ids[1] != 0 && boxes_equal(&rt_boxes[1], &boxes[1]),
                  "round-trip center mark bounds match");
        }

        for (int i = 0; i < 2; ++i) {
            if (rt_ids[i]) isogl_symbol_destroy(rt_ids[i]);
        }
        isogl_segment_list_free(&rt_segments);
        isogl_string_list_free(&rt_strings);
        drawing_symbols_free(&rt_symbols);
        unlink(roundtrip_path);
    }

    for (int i = 0; i < 2; ++i) {
        if (ids[i]) isogl_symbol_destroy(ids[i]);
    }
    drawing_symbols_free(&symbols);
    isogl_string_list_free(&strings);
    isogl_vbo_array_free(&vbo);
    glDeleteTextures(1, &tex);
    glDeleteFramebuffers(1, &fbo);
    isogl_symbol_cleanup();
    isogl_font_free_data(font);
    isogl_cleanup();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASSED: drawing load tests\n");
        return 0;
    }
    printf("FAILED: %d drawing load check(s)\n", failures);
    return 1;
}
