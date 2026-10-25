#include "isogl.h"
#include "glad.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FB_SIZE 256

static int failures = 0;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int count_lit_pixels(const unsigned char *pixels, int width, int height) {
    int count = 0;
    for (int i = 0; i < width * height; ++i) {
        const unsigned char *p = &pixels[i * 4];
        if (p[0] > 20 || p[1] > 20 || p[2] > 20) count++;
    }
    return count;
}

static isogl_vertex make_vertex(float x, float y, uint8_t weight, uint8_t pattern,
                                float distance) {
    isogl_vertex v;
    memset(&v, 0, sizeof(v));
    v.position[0] = x;
    v.position[1] = y;
    v.color[0] = 0.9f;
    v.color[1] = 0.9f;
    v.color[2] = 0.9f;
    v.color[3] = 1.0f;
    v.distanceFromStart = distance;
    v.pk_thickness_pattern_layer = isogl_pack(weight, pattern, 0);
    return v;
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : NULL;

    // ---- Pure style checks -------------------------------------------------
    int pack_ok = 1;
    for (int thickness = 0; thickness < LINE_THICKNESS_COUNT; ++thickness) {
        for (int pattern = 0; pattern < LINE_TYPE_COUNT; ++pattern) {
            for (int layer = 0; layer < 32; ++layer) {
                uint16_t packed = isogl_pack((uint8_t)thickness, (uint8_t)pattern,
                                             (uint8_t)layer);
                if (isogl_unpack_thickness(packed) != thickness ||
                    isogl_unpack_pattern(packed) != pattern ||
                    isogl_unpack_layer(packed) != layer) {
                    pack_ok = 0;
                }
            }
        }
    }
    check(pack_ok, "isogl_pack/unpack round-trip");
    check(isogl_line_weight_mm[LINE_THICKNESS_0_18] == 0.18f,
          "ISO 0.18 weight table");
    check(isogl_text_height_mm[TEXT_HEIGHT_20] == 20.0f,
          "ISO text height table");
    check(isogl_line_patterns[LINE_TYPE_DOTTED].count == 2,
          "ISO pattern ratio table");

    int patterns_ok = 1;
    for (int i = 0; i < LINE_TYPE_COUNT; ++i) {
        int n = isogl_line_patterns[i].count;
        if (n < 0 || n > ISOGL_MAX_PATTERN_RATIOS || (n & 1)) patterns_ok = 0;
    }
    check(patterns_ok, "pattern ratio counts valid");

    // ---- GL setup ----------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

    SDL_Window *window = SDL_CreateWindow("isogl-test", 0, 0, FB_SIZE, FB_SIZE,
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
        fprintf(stderr, "isogl_init_with_asset_dir failed (assets: %s)\n",
                assets ? assets : "(default)");
        return 2;
    }
    check(glIsProgram(isogl_shader_program_segment) == GL_TRUE,
          "segment program linked");
    check(glIsProgram(isogl_shader_program_sheet) == GL_TRUE,
          "sheet program linked");
    check(glIsProgram(isogl_shader_program_font) == GL_TRUE,
          "font program linked");

    isogl_set_sheet(210.0f, 297.0f, (isogl_scale){1, 2});
    isogl_zoom_all_sheet();
    isogl_font_init_vao();
    isogl_resize_viewport(window);

    // ---- Offscreen framebuffer --------------------------------------------
    GLuint fbo = 0, color_tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, FB_SIZE, FB_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "offscreen framebuffer complete");

    unsigned char pixels[FB_SIZE * FB_SIZE * 4];

    // ---- Segment pipeline --------------------------------------------------
    GLuint shader_segment = isogl_shader_program_segment;
    isogl_vbo_array objects;
    check(isogl_vbo_array_init(&objects) == 0, "vbo array init");

    isogl_segment_list segments;
    check(isogl_segment_list_init(&segments, 16) == 0, "segment list init");

    isogl_segment_list_push_pair(
        &segments,
        make_vertex(60.0f, 60.0f, LINE_THICKNESS_0_35, LINE_TYPE_DASHED, 0.0f),
        make_vertex(360.0f, 60.0f, LINE_THICKNESS_0_35, LINE_TYPE_DASHED, 300.0f));
    isogl_segment_list_push_pair(
        &segments,
        make_vertex(60.0f, 120.0f, LINE_THICKNESS_1_0, LINE_TYPE_LONG_DASH_DOT, 0.0f),
        make_vertex(360.0f, 120.0f, LINE_THICKNESS_1_0, LINE_TYPE_LONG_DASH_DOT, 300.0f));
    isogl_segment_list_push_pair(
        &segments,
        make_vertex(60.0f, 150.0f, LINE_THICKNESS_0_5,
                    LINE_TYPE_TRIPLE_DOT_DOUBLE_DASH, 0.0f),
        make_vertex(360.0f, 150.0f, LINE_THICKNESS_0_5,
                    LINE_TYPE_TRIPLE_DOT_DOUBLE_DASH, 300.0f));

    GLuint part = isogl_vbo_array_add(&objects, &segments, ISOGL_VBO_SEGMENT);
    check(part != 0, "segment VBO added");

    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    isogl_draw_all_vbo_objects(&objects);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    int lit_segments = count_lit_pixels(pixels, FB_SIZE, FB_SIZE);
    check(lit_segments > 0, "segment pipeline draws pixels");

    // ---- Incremental update ------------------------------------------------
    isogl_segment_list_free(&segments);
    isogl_segment_list_init(&segments, 16);
    isogl_segment_list_push_pair(
        &segments,
        make_vertex(60.0f, 180.0f, LINE_THICKNESS_2_0, LINE_TYPE_CONTINUOUS, 0.0f),
        make_vertex(360.0f, 180.0f, LINE_THICKNESS_2_0, LINE_TYPE_CONTINUOUS, 300.0f));
    check(isogl_vbo_array_update(&objects, part, &segments) == 0,
          "incremental VBO update");

    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    isogl_draw_all_vbo_objects(&objects);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(count_lit_pixels(pixels, FB_SIZE, FB_SIZE) > 0,
          "updated VBO draws pixels");

    // ---- Font pipeline -----------------------------------------------------
    char font_path[4096];
    check(isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                           font_path, sizeof(font_path)) == 0,
          "font asset path");

    isogl_font_data *font = isogl_font_load_from_toml(font_path);
    check(font != NULL, "font loads");
    if (font) {
        check(font->glyph_count == 423, "font has 423 glyphs");
        check(font->height > 0.0f, "font height is valid");

        isogl_string_list strings = {0};
        isogl_glyph_string *text = isogl_create_glyph_string("ISO 128 - \xC3\x98\xC2\xB0", font);
        check(text != NULL, "UTF-8 glyph string built");
        if (text) {
            check(text->vertex_count > 0, "glyph string has vertices");
            check(isogl_string_list_append(&strings, text, 60.0f, 240.0f, 20.0f,
                                           0.5f, 0.0f, font),
                  "string appended");
        }

        isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
        isogl_draw_string_list(&strings, isogl_shader_program_font,
                               isogl_model_matrix, isogl_view_matrix,
                               (float[16]){1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
        glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        check(count_lit_pixels(pixels, FB_SIZE, FB_SIZE) > 0,
              "font pipeline draws pixels");

        isogl_string_list_free(&strings);
        isogl_font_free_data(font);
    }

    // ---- Cleanup -----------------------------------------------------------
    isogl_segment_list_free(&segments);
    GLuint font_program = isogl_shader_program_font;
    isogl_vbo_array_free(&objects);
    isogl_cleanup();

    check(glIsBuffer(part) == GL_FALSE, "VBO deleted after cleanup");
    check(glIsProgram(shader_segment) == GL_FALSE, "segment program deleted");
    check(glIsProgram(font_program) == GL_FALSE, "font program deleted");

    // A second init/cleanup cycle must not leak or fail.
    check(isogl_init_with_asset_dir(assets) == 0, "second isogl_init");
    isogl_cleanup();

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &color_tex);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures == 0 ? 0 : 1;
}
