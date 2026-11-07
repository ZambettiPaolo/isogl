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

    isogl_segment_list segments;
    check(isogl_segment_list_init(&segments, 256) == 0,
          "segment list initialized");
    isogl_string_list strings = {0};
    DrawingSheetInfo sheet;
    DrawingSymbols symbols;

    int loaded = drawing_load("tests/fixtures/test_drawing.toml",
                              font, &segments, &strings, &sheet, &symbols);
    check(loaded == 0, "hierarchical drawing loaded");
    check(symbols.count == 2, "two symbol descriptors parsed");

    if (symbols.count == 2) {
        const IsoglDimensionDesc *d = &symbols.items[0].dimension;
        check(symbols.items[0].type == DRAW_SYMBOL_DIMENSION,
              "first symbol is a dimension");
        check(fabs(d->x0 - 40.0) < 0.001 && fabs(d->y0 - 60.0) < 0.001,
              "dimension p1 survives the group transform");
        check(fabs(d->x1 - 160.0) < 0.001 && fabs(d->y1 - 60.0) < 0.001,
              "dimension p2 survives the group transform");
        check(fabs(symbols.items[1].center_mark.cx - 150.0) < 0.001 &&
              fabs(symbols.items[1].center_mark.cy - 60.0) < 0.001,
              "center mark control point survives the group transform");
    }
    check(sheet.bounds.has_bounds, "drawing bounds computed");
    check(sheet.bounds.xmax >= 160.0f && sheet.bounds.ymax >= 90.0f,
          "symbol geometry participates in bounds");

    check(isogl_symbol_init(font) == 0, "symbol registry initialized");
    isogl_symbol_id ids[2] = {0, 0};
    for (size_t i = 0; i < symbols.count && i < 2; ++i) {
        if (symbols.items[i].type == DRAW_SYMBOL_DIMENSION) {
            ids[i] = isogl_symbol_create_dimension(
                &symbols.items[i].dimension);
        } else {
            ids[i] = isogl_symbol_create_center_mark(
                &symbols.items[i].center_mark);
        }
    }
    check(ids[0] != 0 && ids[1] != 0, "both symbols created");

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

    isogl_symbol_cleanup();
    isogl_string_list_free(&strings);
    isogl_vbo_array_free(&vbo);
    glDeleteTextures(1, &tex);
    glDeleteFramebuffers(1, &fbo);
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
