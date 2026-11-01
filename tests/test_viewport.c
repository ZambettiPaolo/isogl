#include <isogl/isogl.h>
#include <isogl/isogl_symbol.h>
#include <isogl-sdl/isogl_sdl.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FB_SIZE 256
#define PICK_EXTENT ISOGL_PICK_DEFAULT_EXTENT
#define PICK_TEXELS ((PICK_EXTENT * ISOGL_PICK_TEXTURE_SCALE) * \
                     (PICK_EXTENT * ISOGL_PICK_TEXTURE_SCALE))

static int failures = 0;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int near_f(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

static void set_model(float zoom, float tx, float ty) {
    memset(isogl_model_matrix, 0, sizeof(float) * 16);
    isogl_model_matrix[0] = zoom;
    isogl_model_matrix[5] = zoom;
    isogl_model_matrix[10] = 1.0f;
    isogl_model_matrix[15] = 1.0f;
    isogl_model_matrix[12] = tx;
    isogl_model_matrix[13] = ty;
}

static isogl_vertex make_vertex(float x, float y, uint8_t weight,
                                uint8_t pattern, float distance) {
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

static GLuint add_line(isogl_vbo_array *array,
                       float x0, float y0, float x1, float y1,
                       uint32_t pick_id) {
    isogl_segment_list list;
    if (isogl_segment_list_init(&list, 8) != 0) return 0;
    isogl_segment_list_push_pair(
        &list,
        make_vertex(x0, y0, LINE_THICKNESS_2_0, LINE_TYPE_CONTINUOUS, 0.0f),
        make_vertex(x1, y1, LINE_THICKNESS_2_0, LINE_TYPE_CONTINUOUS, 1.0f));
    GLuint id = isogl_vbo_array_add(array, &list, ISOGL_VBO_SEGMENT);
    isogl_segment_list_free(&list);
    if (id != 0 && pick_id != 0) {
        isogl_vbo_array_set_pick_id(array, id, pick_id);
    }
    return id;
}

static void clear_whole_fb(float r, float g, float b, float a) {
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

static void read_fb(unsigned char *pixels) {
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

static const unsigned char *pixel_at(const unsigned char *pixels, int x, int y) {
    static const unsigned char zero[4] = {0, 0, 0, 0};
    if (x < 0 || x >= FB_SIZE || y < 0 || y >= FB_SIZE) return zero;
    return &pixels[(y * FB_SIZE + x) * 4];
}

static int pixel_lit(const unsigned char *pixels, int x, int y) {
    const unsigned char *p = pixel_at(pixels, x, y);
    return p[0] > 20 || p[1] > 20 || p[2] > 20;
}

static int pixel_lit_near(const unsigned char *pixels, int x, int y, int r) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (pixel_lit(pixels, x + dx, y + dy)) return 1;
        }
    }
    return 0;
}

static int pixel_near_color(const unsigned char *pixels, int x, int y,
                            int r, int g, int b, int tolerance) {
    const unsigned char *p = pixel_at(pixels, x, y);
    return abs((int)p[0] - r) <= tolerance &&
           abs((int)p[1] - g) <= tolerance &&
           abs((int)p[2] - b) <= tolerance;
}

static int first_lit_local(const unsigned char *pixels, int vp_x, int vp_y,
                           int vp_w, int vp_h, int *lx, int *ly) {
    for (int fy = vp_y; fy < vp_y + vp_h; ++fy) {
        for (int fx = vp_x; fx < vp_x + vp_w; ++fx) {
            if (pixel_lit(pixels, fx, fy)) {
                if (lx) *lx = fx - vp_x;
                if (ly) *ly = vp_h - 1 - (fy - vp_y);
                return 1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".";

    isogl_sdl_host_desc desc = {
        .title = "isogl-viewport-test",
        .width = FB_SIZE,
        .height = FB_SIZE,
        .gl_major = 3,
        .gl_minor = 3,
        .multisample = 0,
        .vsync = 0,
        .hidden = 1
    };
    isogl_sdl_host host;
    if (isogl_sdl_host_init(&host, &desc) != 0) {
        fprintf(stderr, "isogl_sdl_host_init failed: %s\n", SDL_GetError());
        return 2;
    }

    if (isogl_init_with_asset_dir(assets) != 0) {
        fprintf(stderr, "isogl_init_with_asset_dir failed\n");
        isogl_sdl_host_shutdown(&host);
        return 2;
    }

    char font_path[4096];
    isogl_font_data *font = NULL;
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                         font_path, sizeof(font_path)) == 0) {
        font = isogl_font_load_from_toml(font_path);
    }
    if (!font) {
        fprintf(stderr, "font load failed\n");
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        return 2;
    }
    isogl_font_init_vao();
    if (isogl_symbol_init(font) != 0) {
        fprintf(stderr, "isogl_symbol_init failed\n");
        isogl_font_free_data(font);
        isogl_cleanup();
        isogl_sdl_host_shutdown(&host);
        return 2;
    }

    isogl_set_sheet(210.0f, 297.0f, (isogl_scale){1, 2});
    isogl_zoom_all_sheet();

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
    isogl_point w = {0.0f, 0.0f};
    isogl_point s = {0.0f, 0.0f};

    /* 1. isogl_begin_frame re-asserts the GL state and the viewport. */
    check(isogl_sdl_host_begin_frame(&host) == 0, "host begin-frame succeeds");
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);
    glScissor(1, 2, 3, 4);

    check(isogl_begin_frame() == 0, "isogl_begin_frame succeeds");
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    check(vp[0] == 0 && vp[1] == 0 && vp[2] == FB_SIZE && vp[3] == FB_SIZE,
          "begin_frame re-applies the viewport");
    check(glIsEnabled(GL_BLEND), "begin_frame enables blending");
    GLint blend_src = 0, blend_dst = 0;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst);
    check(blend_src == GL_SRC_ALPHA && blend_dst == GL_ONE_MINUS_SRC_ALPHA,
          "begin_frame restores the blend function");
    check(!glIsEnabled(GL_DEPTH_TEST) && !glIsEnabled(GL_CULL_FACE) &&
          !glIsEnabled(GL_SCISSOR_TEST),
          "begin_frame disables depth, cull and scissor");

    /* 2. isogl_clear is confined to the viewport and restores scissor. */
    clear_whole_fb(0.2f, 0.1f, 0.05f, 1.0f);          /* colour A */
    isogl_viewport vp2 = {32, 48, 96, 64};
    isogl_set_viewport(&vp2);
    isogl_clear(0.05f, 0.2f, 0.1f, 1.0f);             /* colour B */
    read_fb(pixels);
    check(pixel_near_color(pixels, 40, 60, 13, 51, 26, 3),
          "clear paints inside the viewport");
    check(pixel_near_color(pixels, 10, 10, 51, 26, 13, 3) &&
          pixel_near_color(pixels, 31, 48, 51, 26, 13, 3) &&
          pixel_near_color(pixels, 128, 48, 51, 26, 13, 3) &&
          pixel_near_color(pixels, 32, 112, 51, 26, 13, 3),
          "clear leaves pixels outside the viewport untouched");

    glEnable(GL_SCISSOR_TEST);
    glScissor(1, 2, 3, 4);
    isogl_set_viewport(&vp2);
    isogl_clear(0.9f, 0.9f, 0.1f, 1.0f);
    GLint box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, box);
    check(glIsEnabled(GL_SCISSOR_TEST) && box[0] == 1 && box[1] == 2 &&
          box[2] == 3 && box[3] == 4,
          "clear restores a pre-existing scissor box");
    glDisable(GL_SCISSOR_TEST);

    /* 3. Screen/world round trip inside a sub-viewport. */
    isogl_viewport vp3 = {32, 48, 128, 96};
    isogl_set_viewport(&vp3);
    set_model(1.0f, 0.0f, 0.0f);
    static const float rt_points[][2] = {
        {0.0f, 0.0f}, {128.0f, 0.0f}, {0.0f, 96.0f},
        {128.0f, 96.0f}, {37.0f, 19.0f}
    };
    int rt_ok = 1;
    for (size_t i = 0; i < sizeof(rt_points) / sizeof(rt_points[0]); ++i) {
        if (isogl_screen_to_world(rt_points[i][0], rt_points[i][1], &w) != 0 ||
            isogl_world_to_screen(w.x, w.y, &s) != 0 ||
            !near_f(s.x, rt_points[i][0], 0.01f) ||
            !near_f(s.y, rt_points[i][1], 0.01f)) {
            rt_ok = 0;
        }
    }
    check(rt_ok, "sub-viewport screen/world round trip");

    /* 4. Per-axis formulas on non-square viewports. */
    isogl_viewport wide = {0, 0, 200, 100};
    isogl_set_viewport(&wide);
    set_model(1.0f, 0.0f, 0.0f);
    isogl_world_to_screen(1.0f, 0.0f, &s);
    check(near_f(s.x, 150.0f, 0.05f) && near_f(s.y, 50.0f, 0.05f),
          "wide viewport: world +x maps to the expected pixel delta");
    isogl_world_to_screen(0.0f, 1.0f, &s);
    check(near_f(s.x, 100.0f, 0.05f) && near_f(s.y, 0.0f, 0.05f),
          "wide viewport: world +y maps to the expected pixel delta");
    isogl_screen_to_world(60.0f, 25.0f, &w);
    isogl_world_to_screen(w.x, w.y, &s);
    check(near_f(s.x, 60.0f, 0.05f) && near_f(s.y, 25.0f, 0.05f),
          "wide viewport round trip");

    isogl_viewport tall = {0, 0, 100, 200};
    isogl_set_viewport(&tall);
    set_model(1.0f, 0.0f, 0.0f);
    isogl_world_to_screen(1.0f, 0.0f, &s);
    check(near_f(s.x, 100.0f, 0.05f) && near_f(s.y, 100.0f, 0.05f),
          "tall viewport: world +x maps to the expected pixel delta");
    isogl_world_to_screen(0.0f, 1.0f, &s);
    check(near_f(s.x, 50.0f, 0.05f) && near_f(s.y, 50.0f, 0.05f),
          "tall viewport: world +y maps to the expected pixel delta");
    isogl_screen_to_world(30.0f, 120.0f, &w);
    isogl_world_to_screen(w.x, w.y, &s);
    check(near_f(s.x, 30.0f, 0.05f) && near_f(s.y, 120.0f, 0.05f),
          "tall viewport round trip");

    /* 5. Zoom keeps the world point under the cursor fixed. */
    isogl_set_viewport(&wide);
    set_model(1.0f, 0.0f, 0.0f);
    isogl_screen_to_world(30.0f, 20.0f, &w);
    isogl_handle_zoom(1, 30.0f, 20.0f);
    isogl_screen_to_world(30.0f, 20.0f, &s);
    check(near_f(isogl_model_matrix[0], 1.41f, 0.001f),
          "zoom-in factor is 1.41");
    check(near_f(s.x, w.x, 1e-4f) && near_f(s.y, w.y, 1e-4f),
          "zoom-in keeps the cursor anchor fixed");

    set_model(1.0f, 0.0f, 0.0f);
    isogl_screen_to_world(150.0f, 70.0f, &w);
    isogl_handle_zoom(-1, 150.0f, 70.0f);
    isogl_screen_to_world(150.0f, 70.0f, &s);
    check(near_f(s.x, w.x, 1e-4f) && near_f(s.y, w.y, 1e-4f),
          "zoom-out keeps the cursor anchor fixed");

    isogl_set_viewport(&tall);
    set_model(1.0f, 0.0f, 0.0f);
    isogl_screen_to_world(25.0f, 150.0f, &w);
    isogl_handle_zoom(1, 25.0f, 150.0f);
    isogl_screen_to_world(25.0f, 150.0f, &s);
    check(near_f(s.x, w.x, 1e-4f) && near_f(s.y, w.y, 1e-4f),
          "tall viewport zoom keeps the cursor anchor fixed");

    /* 6. Drawing is confined to the sub-viewport rectangle. */
    isogl_vbo_array draw_objects;
    check(isogl_vbo_array_init(&draw_objects) == 0, "draw vbo array init");
    isogl_set_viewport(&vp3);
    set_model(1.0f, 0.0f, 0.0f);
    isogl_point a = {0.0f, 0.0f};
    isogl_point b = {0.0f, 0.0f};
    isogl_screen_to_world(0.0f, 48.0f, &a);
    isogl_screen_to_world(128.0f, 48.0f, &b);
    check(add_line(&draw_objects, a.x, a.y, b.x, b.y, 0) != 0,
          "sub-viewport line added");

    clear_whole_fb(0.2f, 0.1f, 0.05f, 1.0f);          /* A */
    isogl_clear(0.05f, 0.2f, 0.1f, 1.0f);             /* B, viewport only */
    isogl_draw_all_vbo_objects(&draw_objects);
    read_fb(pixels);
    check(pixel_lit(pixels, 96, 96), "sub-viewport draw is visible");
    check(pixel_near_color(pixels, 40, 60, 13, 51, 26, 3),
          "sub-viewport clear is visible inside");
    check(pixel_near_color(pixels, 10, 10, 51, 26, 13, 3) &&
          pixel_near_color(pixels, 200, 100, 51, 26, 13, 3),
          "sub-viewport draw and clear stay inside");

    /* 7. Pick uses viewport-local pixels and ignores the viewport origin. */
    isogl_vbo_array pick_objects;
    check(isogl_vbo_array_init(&pick_objects) == 0, "pick vbo array init");
    isogl_set_viewport(&vp3);
    set_model(1.0f, 0.0f, 0.0f);
    isogl_screen_to_world(0.0f, 48.0f, &a);
    isogl_screen_to_world(128.0f, 48.0f, &b);
    check(add_line(&pick_objects, a.x, a.y, b.x, b.y, 77) != 0,
          "pick line added");

    uint32_t ids[4096];
    size_t count = 0;
    check(isogl_pick_at(&pick_objects, NULL, 64.0f, 48.0f, PICK_EXTENT,
                        ids, sizeof(ids) / sizeof(ids[0]), &count) == 0,
          "sub-viewport pick succeeds");
    check(count >= 1 && ids[0] == 77, "sub-viewport pick returns the entity");

    isogl_viewport vp_origin = {0, 0, 128, 96};
    isogl_set_viewport(&vp_origin);
    count = 0;
    check(isogl_pick_at(&pick_objects, NULL, 64.0f, 48.0f, PICK_EXTENT,
                        ids, sizeof(ids) / sizeof(ids[0]), &count) == 0 &&
          count >= 1 && ids[0] == 77,
          "pick ignores the viewport origin");

    isogl_viewport vp_tall_pick = {32, 48, 96, 128};
    isogl_set_viewport(&vp_tall_pick);
    set_model(1.0f, 0.0f, 0.0f);
    isogl_screen_to_world(0.0f, 64.0f, &a);
    isogl_screen_to_world(96.0f, 64.0f, &b);
    check(add_line(&pick_objects, a.x, a.y, b.x, b.y, 78) != 0,
          "tall pick line added");
    count = 0;
    check(isogl_pick_at(&pick_objects, NULL, 48.0f, 64.0f, PICK_EXTENT,
                        ids, PICK_TEXELS, &count) == 0 &&
          count >= 1 && ids[0] == 78,
          "tall viewport pick honours the aspect");

    /* 8. Pick extent is in framebuffer pixels: HiDPI callers scale it. */
    isogl_vbo_array hidpi_objects;
    check(isogl_vbo_array_init(&hidpi_objects) == 0, "hidpi vbo array init");
    isogl_viewport full = {0, 0, FB_SIZE, FB_SIZE};
    isogl_set_viewport(&full);
    set_model(1.0f, 0.0f, 0.0f);
    isogl_screen_to_world(100.0f, 122.0f, &a);
    isogl_screen_to_world(156.0f, 122.0f, &b);
    check(add_line(&hidpi_objects, a.x, a.y, b.x, b.y, 88) != 0,
          "hidpi pick line added");

    count = 0;
    check(isogl_pick_at(&hidpi_objects, NULL, 128.0f, 128.0f, 8,
                        ids, sizeof(ids) / sizeof(ids[0]), &count) == 0,
          "small extent pick succeeds");
    int small_hit = 0;
    for (size_t i = 0; i < count; ++i) if (ids[i] == 88) small_hit = 1;
    check(!small_hit, "8 px box misses a line 6 px away");

    count = 0;
    check(isogl_pick_at(&hidpi_objects, NULL, 128.0f, 128.0f, 16,
                        ids, sizeof(ids) / sizeof(ids[0]), &count) == 0,
          "large extent pick succeeds");
    int large_hit = 0;
    for (size_t i = 0; i < count; ++i) if (ids[i] == 88) large_hit = 1;
    check(large_hit, "16 px box hits a line 6 px away");

    /* The symbol wrapper forwards the same viewport-local extent. */
    isogl_point center_world = {0.0f, 0.0f};
    isogl_screen_to_world(128.0f, 128.0f, &center_world);
    IsoglCenterMarkDesc mark;
    memset(&mark, 0, sizeof(mark));
    mark.cx = center_world.x;
    mark.cy = center_world.y;
    mark.size_mm = 40.0f;
    isogl_symbol_id mark_id = isogl_symbol_create_center_mark(&mark);
    check(mark_id != 0, "center mark created for the extent test");

    uint32_t symbol_ids[64];
    size_t symbol_count = 0;
    check(isogl_symbol_pick_at(134.0f, 122.0f, 8,
                               symbol_ids, 64, &symbol_count) == 0,
          "symbol pick with small extent succeeds");
    check(symbol_count == 0, "symbol small extent misses the offset target");

    symbol_count = 0;
    check(isogl_symbol_pick_at(134.0f, 122.0f, 16,
                               symbol_ids, 64, &symbol_count) == 0,
          "symbol pick with large extent succeeds");
    check(symbol_count >= 1 && symbol_ids[0] == mark_id,
          "symbol large extent hits the offset target");
    check(isogl_symbol_destroy(mark_id) == 0, "center mark destroyed");

    /* 9. Selection overlay maps local top-left coordinates. */
    isogl_set_viewport(&vp3);
    set_model(1.0f, 0.0f, 0.0f);
    clear_whole_fb(0.02f, 0.02f, 0.02f, 1.0f);
    isogl_set_selection_rect(10.0f, 10.0f, 40.0f, 30.0f, 0);
    isogl_draw_selection_overlay();
    read_fb(pixels);
    check(pixel_lit_near(pixels, 42, 124, 1) &&
          pixel_lit_near(pixels, 72, 124, 1) &&
          pixel_lit_near(pixels, 57, 114, 1) &&
          pixel_lit_near(pixels, 57, 134, 1),
          "selection overlay uses the viewport origin");
    check(!pixel_lit(pixels, 10, 10) && !pixel_lit(pixels, 200, 100),
          "selection overlay stays inside the viewport");
    isogl_clear_selection_rect();

    /* 10. Text selection converts local top-left to gl_FragCoord pixels. */
    isogl_viewport vp_text = {32, 48, 192, 160};
    isogl_set_viewport(&vp_text);
    isogl_zoom_all_sheet();
    isogl_string_list strings = {0};
    isogl_glyph_string *glyph = isogl_create_glyph_string("I", font);
    check(glyph != NULL, "glyph string created");
    if (glyph) {
        isogl_point text_world = {0.0f, 0.0f};
        isogl_screen_to_world(60.0f, 80.0f, &text_world);
        check(isogl_string_list_append(&strings, glyph,
                                       text_world.x, text_world.y,
                                       20.0f, 0.5f, 0.0f, font, 0),
              "glyph string appended in the sub-viewport");

        float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        isogl_set_selection_preview(1);
        isogl_clear_selection_rect();
        clear_whole_fb(0.02f, 0.02f, 0.02f, 1.0f);
        isogl_clear(0.02f, 0.02f, 0.02f, 1.0f);
        isogl_draw_string_list(&strings, isogl_shader_program_font,
                               isogl_model_matrix, isogl_view_matrix,
                               identity);
        read_fb(pixels);

        int lx = 0, ly = 0;
        int found = first_lit_local(pixels, vp_text.x, vp_text.y,
                                    vp_text.width, vp_text.height, &lx, &ly);
        check(found, "text renders inside the sub-viewport");
        if (found) {
            int fx = vp_text.x + lx;
            int fy = vp_text.y + (vp_text.height - 1 - ly);
            check(!pixel_near_color(pixels, fx, fy, 77, 230, 102, 40),
                  "text is not tinted before selecting");

            isogl_set_selection_rect((float)(lx - 10), (float)(ly - 10),
                                     (float)(lx + 20), (float)(ly + 20), 0);
            isogl_clear(0.02f, 0.02f, 0.02f, 1.0f);
            isogl_draw_string_list(&strings, isogl_shader_program_font,
                                   isogl_model_matrix, isogl_view_matrix,
                                   identity);
            read_fb(pixels);
            check(pixel_near_color(pixels, fx, fy, 77, 230, 102, 40),
                  "text selection converts local coordinates correctly");

            isogl_clear_selection_rect();
            isogl_clear(0.02f, 0.02f, 0.02f, 1.0f);
            isogl_draw_string_list(&strings, isogl_shader_program_font,
                                   isogl_model_matrix, isogl_view_matrix,
                                   identity);
            read_fb(pixels);
            check(!pixel_near_color(pixels, fx, fy, 77, 230, 102, 40),
                  "clearing the rectangle removes the text tint");
        }
        isogl_string_list_free(&strings);
    }

    isogl_vbo_array_free(&draw_objects);
    isogl_vbo_array_free(&pick_objects);
    isogl_vbo_array_free(&hidpi_objects);
    isogl_symbol_cleanup();
    isogl_cleanup();
    isogl_font_free_data(font);
    glDeleteTextures(1, &color_tex);
    glDeleteFramebuffers(1, &fbo);
    isogl_sdl_host_shutdown(&host);

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
