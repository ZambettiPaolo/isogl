#include "isogl.h"
#include "glad.h"
#include "cad.h"
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

/* Return the first lit pixel in screen coordinates (origin top-left), which
 * is the convention used by the pixel_near_color helpers. */
static int first_lit_pixel(const unsigned char *pixels, int *x, int *y) {
    for (int gy = 0; gy < FB_SIZE; ++gy) {
        for (int px = 0; px < FB_SIZE; ++px) {
            const unsigned char *p = &pixels[(gy * FB_SIZE + px) * 4];
            if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
                if (x) *x = px;
                if (y) *y = FB_SIZE - 1 - gy;
                return 1;
            }
        }
    }
    return 0;
}

static int pixel_near_color(const unsigned char *pixels, int x, int y,
                            int r, int g, int b) {
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int px = x + dx;
            int py = y + dy;
            if (px < 0 || py < 0 || px >= FB_SIZE || py >= FB_SIZE) continue;
            int gy = FB_SIZE - 1 - py;
            const unsigned char *p = &pixels[(gy * FB_SIZE + px) * 4];
            if (abs((int)p[0] - r) <= 25 &&
                abs((int)p[1] - g) <= 25 &&
                abs((int)p[2] - b) <= 25) {
                return 1;
            }
        }
    }
    return 0;
}

static int pixel_is_yellow(const unsigned char *pixels, int x, int y) {
    return pixel_near_color(pixels, x, y, 255, 217, 51);
}

static int pixel_is_green(const unsigned char *pixels, int x, int y) {
    return pixel_near_color(pixels, x, y, 77, 230, 102);
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
    check(glIsProgram(isogl_shader_program_font_pick) == GL_TRUE,
          "font pick program linked");

    isogl_set_sheet(210.0f, 297.0f, (isogl_scale){1, 2});
    isogl_zoom_all_sheet();
    isogl_font_init_vao();
    isogl_resize_viewport(window);

    isogl_point rt_world = {0.0f, 0.0f};
    isogl_point rt_screen = {0.0f, 0.0f};
    check(isogl_screen_to_world(window, 100.0f, 120.0f, &rt_world) == 0 &&
          isogl_world_to_screen(window, rt_world.x, rt_world.y,
                                &rt_screen) == 0 &&
          fabsf(rt_screen.x - 100.0f) < 0.01f &&
          fabsf(rt_screen.y - 120.0f) < 0.01f,
          "world/screen round-trip");

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

    // ---- Selection overlay ------------------------------------------------
    isogl_set_selection_rect(20.0f, 20.0f, 100.0f, 100.0f, 0);
    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    isogl_draw_selection_overlay();
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(count_lit_pixels(pixels, FB_SIZE, FB_SIZE) > 0,
          "selection overlay draws a rectangle");
    isogl_clear_selection_rect();

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

    // ---- Per-segment selection filter -------------------------------------
    isogl_vbo_array sel_objects;
    check(isogl_vbo_array_init(&sel_objects) == 0, "selection vbo array init");
    isogl_segment_list sel_segments;
    check(isogl_segment_list_init(&sel_segments, 8) == 0,
          "selection segment list init");

    struct {
        int x0, y0, x1, y1;
    } sel_lines[3] = {
        {70, 70, 130, 130},   /* entirely inside the box */
        {30, 100, 100, 100},  /* starts outside, crosses the box */
        {10, 10, 40, 40},     /* entirely outside */
    };

    for (size_t i = 0; i < sizeof(sel_lines) / sizeof(sel_lines[0]); ++i) {
        isogl_point a = {0.0f, 0.0f};
        isogl_point b = {0.0f, 0.0f};
        isogl_screen_to_world(window, (float)sel_lines[i].x0,
                              (float)sel_lines[i].y0, &a);
        isogl_screen_to_world(window, (float)sel_lines[i].x1,
                              (float)sel_lines[i].y1, &b);

        sel_segments.size = 0;
        isogl_segment_list_push_pair(
            &sel_segments,
            make_vertex(a.x, a.y, LINE_THICKNESS_0_5,
                        LINE_TYPE_CONTINUOUS, 0.0f),
            make_vertex(b.x, b.y, LINE_THICKNESS_0_5,
                        LINE_TYPE_CONTINUOUS, 1.0f));
        check(isogl_vbo_array_add(&sel_objects, &sel_segments,
                                  ISOGL_VBO_SEGMENT) != 0,
              "selection VBO added");
    }

    isogl_set_selection_rect(50.0f, (float)(FB_SIZE - 50),
                             150.0f, (float)(FB_SIZE - 150), 1);
    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    isogl_draw_all_vbo_objects(&sel_objects);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(pixel_is_yellow(pixels, 80, 80),
          "crossing highlights the inside segment");
    check(pixel_is_yellow(pixels, 35, 100),
          "crossing highlights the part outside the box");
    check(!pixel_is_yellow(pixels, 25, 25),
          "crossing excludes the outside segment");

    isogl_set_selection_rect(50.0f, (float)(FB_SIZE - 50),
                             150.0f, (float)(FB_SIZE - 150), 0);
    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    isogl_draw_all_vbo_objects(&sel_objects);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(pixel_is_green(pixels, 80, 80),
          "window highlights the contained segment");
    check(!pixel_is_green(pixels, 35, 100),
          "window excludes the crossing segment");
    check(!pixel_is_green(pixels, 25, 25),
          "window excludes the outside segment");

    isogl_clear_selection_rect();
    isogl_segment_list_free(&sel_segments);
    isogl_vbo_array_free(&sel_objects);

    // ---- Persistent selection by entity ID --------------------------------
    isogl_set_selection_preview(0);
    isogl_set_selection_color(1.0f, 0.85f, 0.2f, 1.0f);

    isogl_vbo_array persist_objects;
    check(isogl_vbo_array_init(&persist_objects) == 0,
          "persistent selection vbo array init");
    isogl_segment_list persist_segments;
    check(isogl_segment_list_init(&persist_segments, 8) == 0,
          "persistent selection segment list init");

    GLuint persist_vbo_a = 0;
    GLuint persist_vbo_b = 0;
    isogl_point pa = {0.0f, 0.0f};
    isogl_point pb = {0.0f, 0.0f};

    /* A crosses the test rectangle: its first part is outside it. */
    isogl_screen_to_world(window, 30.0f, 30.0f, &pa);
    isogl_screen_to_world(window, 130.0f, 130.0f, &pb);
    isogl_segment_list_push_pair(
        &persist_segments,
        make_vertex(pa.x, pa.y, LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 0.0f),
        make_vertex(pb.x, pb.y, LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 1.0f));
    persist_vbo_a = isogl_vbo_array_add(&persist_objects, &persist_segments,
                                        ISOGL_VBO_SEGMENT);
    check(persist_vbo_a != 0, "persistent selection VBO A added");

    /* B is entirely inside the test rectangle but is not selected. */
    persist_segments.size = 0;
    isogl_screen_to_world(window, 70.0f, 130.0f, &pa);
    isogl_screen_to_world(window, 130.0f, 70.0f, &pb);
    isogl_segment_list_push_pair(
        &persist_segments,
        make_vertex(pa.x, pa.y, LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 0.0f),
        make_vertex(pb.x, pb.y, LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 1.0f));
    persist_vbo_b = isogl_vbo_array_add(&persist_objects, &persist_segments,
                                        ISOGL_VBO_SEGMENT);
    check(persist_vbo_b != 0, "persistent selection VBO B added");

    check(isogl_vbo_array_set_pick_id(&persist_objects, persist_vbo_a, 501) == 0,
          "persistent selection pick id A");
    check(isogl_vbo_array_set_pick_id(&persist_objects, persist_vbo_b, 502) == 0,
          "persistent selection pick id B");

    uint32_t persistent_ids[2] = {501, 502};
    check(isogl_vbo_array_apply_selection(&persist_objects,
                                          persistent_ids, 0) == 0,
          "empty selection clears flags");
    check(isogl_vbo_array_get(&persist_objects, persist_vbo_a)->selected == 0,
          "empty selection leaves A unselected");

    check(isogl_vbo_array_apply_selection(&persist_objects,
                                          persistent_ids, 1) == 0,
          "apply selection by entity id");
    check(isogl_vbo_array_get(&persist_objects, persist_vbo_a)->selected == 1,
          "selected A is flagged");
    check(isogl_vbo_array_get(&persist_objects, persist_vbo_b)->selected == 0,
          "unlisted B is not flagged");

    /* Preview off: only the persistent flag may tint the object, and it
       tints the whole entity, including the part outside the rectangle. */
    isogl_set_selection_rect(50.0f, (float)(FB_SIZE - 50),
                             150.0f, (float)(FB_SIZE - 150), 1);
    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    isogl_draw_all_vbo_objects(&persist_objects);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(pixel_is_yellow(pixels, 35, 35),
          "persistent flag tints the selected line outside the rectangle");
    check(pixel_is_yellow(pixels, 120, 120),
          "persistent flag tints the selected line inside the rectangle");
    check(!pixel_is_yellow(pixels, 80, 120),
          "rect preview is disabled and B stays untinted");
    isogl_clear_selection_rect();

    check(isogl_vbo_array_set_selected(&persist_objects, persist_vbo_b, 1) == 0,
          "set selection for B");
    check(isogl_vbo_array_get(&persist_objects, persist_vbo_b)->selected == 1,
          "B is flagged");
    check(isogl_vbo_array_clear_selection(&persist_objects) == 0,
          "clear all selection flags");
    check(isogl_vbo_array_get(&persist_objects, persist_vbo_b)->selected == 0,
          "B flag cleared");

    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    isogl_draw_all_vbo_objects(&persist_objects);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(!pixel_is_yellow(pixels, 35, 35),
          "cleared selection returns to the layer colour");

    isogl_set_selection_preview(1);
    isogl_segment_list_free(&persist_segments);
    isogl_vbo_array_free(&persist_objects);

    // ---- Pick pass (8x8 box, 16x16 texture) --------------------------------
    isogl_point pick_center = {0.0f, 0.0f};
    check(isogl_screen_to_world(window, FB_SIZE / 2.0f, FB_SIZE / 2.0f,
                                &pick_center) == 0,
          "pick center world conversion");

    isogl_vbo_array pick_objects;
    check(isogl_vbo_array_init(&pick_objects) == 0, "pick vbo array init");

    isogl_segment_list pick_segments;
    check(isogl_segment_list_init(&pick_segments, 16) == 0,
          "pick segment list init");

    struct {
        float x0, y0, x1, y1;
        uint32_t id;
    } pick_lines[] = {
        {pick_center.x - 20.0f, pick_center.y,
         pick_center.x + 20.0f, pick_center.y, 101},
        {pick_center.x, pick_center.y - 20.0f,
         pick_center.x, pick_center.y + 20.0f, 102},
        {pick_center.x - 15.0f, pick_center.y - 15.0f,
         pick_center.x + 15.0f, pick_center.y + 15.0f, 103},
        {pick_center.x + 50.0f, pick_center.y,
         pick_center.x + 60.0f, pick_center.y, 104},
    };

    for (size_t i = 0; i < sizeof(pick_lines) / sizeof(pick_lines[0]); ++i) {
        pick_segments.size = 0;
        isogl_segment_list_push_pair(
            &pick_segments,
            make_vertex(pick_lines[i].x0, pick_lines[i].y0,
                        LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 0.0f),
            make_vertex(pick_lines[i].x1, pick_lines[i].y1,
                        LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 60.0f));

        GLuint pick_vbo = isogl_vbo_array_add(&pick_objects, &pick_segments,
                                              ISOGL_VBO_SEGMENT);
        check(pick_vbo != 0, "pick VBO added");
        check(isogl_vbo_array_set_pick_id(&pick_objects, pick_vbo,
                                          pick_lines[i].id) == 0,
              "pick ID set");
    }

    uint32_t picked_ids[ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE];
    size_t picked_count = 0;
    int pick_rc = isogl_pick_at(window, &pick_objects, NULL,
                                FB_SIZE / 2.0f, FB_SIZE / 2.0f,
                                picked_ids,
                                ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE,
                                &picked_count);
    check(pick_rc == 0, "pick pass succeeds");

    printf("pick at (%d,%d): %zu id(s):", FB_SIZE / 2, FB_SIZE / 2,
           picked_count);
    for (size_t i = 0; i < picked_count; ++i) {
        printf(" %u", picked_ids[i]);
    }
    printf("\n");

    int has_101 = 0, has_102 = 0, has_103 = 0, has_104 = 0;
    for (size_t i = 0; i < picked_count; ++i) {
        if (picked_ids[i] == 101) has_101 = 1;
        if (picked_ids[i] == 102) has_102 = 1;
        if (picked_ids[i] == 103) has_103 = 1;
        if (picked_ids[i] == 104) has_104 = 1;
    }
    check(has_101 && has_102 && has_103,
          "pick finds the three crossing objects");
    check(!has_104, "pick excludes the far object");
    check(picked_count == 3, "pick returns exactly three distinct IDs");

    // Off-center pick: validates the pick matrix translation, not just the
    // scale with the cursor in the middle of the window.
    isogl_point off_center = {0.0f, 0.0f};
    check(isogl_screen_to_world(window, FB_SIZE / 4.0f, FB_SIZE / 4.0f,
                                &off_center) == 0,
          "off-center world conversion");

    pick_segments.size = 0;
    isogl_segment_list_push_pair(
        &pick_segments,
        make_vertex(off_center.x - 10.0f, off_center.y,
                    LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 0.0f),
        make_vertex(off_center.x + 10.0f, off_center.y,
                    LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 20.0f));
    GLuint off_vbo = isogl_vbo_array_add(&pick_objects, &pick_segments,
                                         ISOGL_VBO_SEGMENT);
    check(off_vbo != 0, "off-center pick VBO added");
    check(isogl_vbo_array_set_pick_id(&pick_objects, off_vbo, 105) == 0,
          "off-center pick ID set");

    uint32_t off_ids[ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE];
    size_t off_count = 0;
    pick_rc = isogl_pick_at(window, &pick_objects, NULL,
                           FB_SIZE / 4.0f, FB_SIZE / 4.0f,
                           off_ids,
                           ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE,
                           &off_count);
    check(pick_rc == 0, "off-center pick succeeds");
    printf("pick at (%d,%d): %zu id(s):", FB_SIZE / 4, FB_SIZE / 4, off_count);
    for (size_t i = 0; i < off_count; ++i) {
        printf(" %u", off_ids[i]);
    }
    printf("\n");
    check(off_count == 1 && off_ids[0] == 105,
          "off-center pick returns only the expected ID");

    // Spiral order: a line through the centre must be returned before a line
    // three screen pixels away, even though both are inside the 8x8 box.
    isogl_point offset_center = {0.0f, 0.0f};
    check(isogl_screen_to_world(window, FB_SIZE / 2.0f, FB_SIZE / 2.0f + 3.0f,
                                &offset_center) == 0,
          "spiral offset world conversion");

    pick_segments.size = 0;
    isogl_segment_list_push_pair(
        &pick_segments,
        make_vertex(pick_center.x - 5.0f, pick_center.y,
                    LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 0.0f),
        make_vertex(pick_center.x + 5.0f, pick_center.y,
                    LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 10.0f));
    GLuint center_vbo = isogl_vbo_array_add(&pick_objects, &pick_segments,
                                            ISOGL_VBO_SEGMENT);
    check(center_vbo != 0, "spiral centre VBO added");
    check(isogl_vbo_array_set_pick_id(&pick_objects, center_vbo, 106) == 0,
          "spiral centre ID set");

    pick_segments.size = 0;
    isogl_segment_list_push_pair(
        &pick_segments,
        make_vertex(offset_center.x - 5.0f, offset_center.y,
                    LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 0.0f),
        make_vertex(offset_center.x + 5.0f, offset_center.y,
                    LINE_THICKNESS_0_5, LINE_TYPE_CONTINUOUS, 10.0f));
    GLuint offset_vbo = isogl_vbo_array_add(&pick_objects, &pick_segments,
                                            ISOGL_VBO_SEGMENT);
    check(offset_vbo != 0, "spiral offset VBO added");
    check(isogl_vbo_array_set_pick_id(&pick_objects, offset_vbo, 107) == 0,
          "spiral offset ID set");

    uint32_t order_ids[ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE];
    size_t order_count = 0;
    pick_rc = isogl_pick_at(window, &pick_objects, NULL,
                           FB_SIZE / 2.0f, FB_SIZE / 2.0f,
                           order_ids,
                           ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE,
                           &order_count);
    check(pick_rc == 0, "spiral pick succeeds");
    printf("spiral pick: %zu id(s):", order_count);
    for (size_t i = 0; i < order_count; ++i) printf(" %u", order_ids[i]);
    printf("\n");

    size_t index_center = SIZE_MAX;
    size_t index_offset = SIZE_MAX;
    for (size_t i = 0; i < order_count; ++i) {
        if (order_ids[i] == 106) index_center = i;
        if (order_ids[i] == 107) index_offset = i;
    }
    check(index_center != SIZE_MAX && index_offset != SIZE_MAX,
          "spiral pick returns both lines");
    check(index_center < index_offset,
          "pick scans centre-outwards, centre line first");

    isogl_segment_list_free(&pick_segments);
    isogl_vbo_array_free(&pick_objects);

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
                                           0.5f, 0.0f, font, 0),
                  "string appended");
        }

        isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
        isogl_draw_string_list(&strings, isogl_shader_program_font,
                               isogl_model_matrix, isogl_view_matrix,
                               (float[16]){1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
        glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        check(count_lit_pixels(pixels, FB_SIZE, FB_SIZE) > 0,
              "font pipeline draws pixels");

        // Persistent text selection by entity ID.
        if (strings.glyph_string_count > 0) {
            int lit_x = 0, lit_y = 0;
            check(first_lit_pixel(pixels, &lit_x, &lit_y),
                  "font has a lit pixel to sample");

            isogl_set_selection_color(1.0f, 0.85f, 0.2f, 1.0f);
            strings.glyph_strings[0].pick_id = 301;
            uint32_t text_selected_id = 301;
            check(isogl_string_list_apply_selection(&strings,
                                                    &text_selected_id, 1) == 0,
                  "apply text selection by entity id");
            check(strings.glyph_strings[0].selected == 1,
                  "text string is flagged");

            isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
            isogl_draw_string_list(&strings, isogl_shader_program_font,
                                   isogl_model_matrix, isogl_view_matrix,
                                   (float[16]){1,0,0,0, 0,1,0,0,
                                               0,0,1,0, 0,0,0,1});
            glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE,
                         pixels);
            check(pixel_is_yellow(pixels, lit_x, lit_y),
                  "text selection tints the whole string");

            check(isogl_string_list_apply_selection(&strings, NULL, 0) == 0,
                  "clear text selection");
            check(strings.glyph_strings[0].selected == 0,
                  "text flag cleared");
        }

        // ---- Incremental string update -----------------------------------
        isogl_string_list updates = {0};
        isogl_glyph_string *u1 = isogl_create_glyph_string("AB", font);
        isogl_glyph_string *u2 = isogl_create_glyph_string("CD", font);
        check(u1 != NULL && u2 != NULL, "update glyph strings built");
        if (u1 && u2) {
            check(isogl_string_list_append(&updates, u1, 10.0f, 10.0f,
                                           10.0f, 0.5f, 0.0f, font, 401),
                  "update string 1 appended");
            check(isogl_string_list_append(&updates, u2, 10.0f, 30.0f,
                                           10.0f, 0.5f, 0.0f, font, 402),
                  "update string 2 appended");

            GLuint update_vbo1 = updates.glyph_strings[0].vbo;
            GLuint update_vbo2 = updates.glyph_strings[1].vbo;
            uint32_t update_selected = 401;
            check(isogl_string_list_apply_selection(&updates,
                                                    &update_selected, 1) == 0,
                  "update selection applied");
            check(updates.glyph_strings[0].selected == 1 &&
                  updates.glyph_strings[1].selected == 0,
                  "update selection flags correct");

            check(isogl_string_list_update_by_pick_id(
                      &updates, 401, "XYZ", font,
                      10.0f, 10.0f, 10.0f, 0.5f, 0.0f) == 0,
                  "string content update succeeds");
            check(updates.glyph_strings[0].vbo != update_vbo1 &&
                  updates.glyph_strings[0].vbo != 0,
                  "content update creates a new string VBO");
            check(strcmp(updates.glyph_strings[0].text, "XYZ") == 0,
                  "content update replaces the text");
            check(updates.glyph_strings[0].selected == 1,
                  "content update preserves selection");
            check(updates.glyph_strings[1].vbo == update_vbo2 &&
                  strcmp(updates.glyph_strings[1].text, "CD") == 0 &&
                  updates.glyph_strings[1].selected == 0,
                  "content update leaves the other string untouched");

            GLuint update_vbo1b = updates.glyph_strings[0].vbo;
            float old_matrix_x = updates.glyph_strings[0].string_matrix[12];
            check(isogl_string_list_set_transform_by_pick_id(
                      &updates, 401, font, 25.0f, 10.0f,
                      10.0f, 0.5f, 0.0f) == 0,
                  "string transform update succeeds");
            check(updates.glyph_strings[0].vbo == update_vbo1b,
                  "transform update keeps the string VBO");
            check(updates.glyph_strings[0].string_matrix[12] != old_matrix_x,
                  "transform update changes the matrix");
            check(updates.glyph_strings[0].selected == 1 &&
                  updates.glyph_strings[1].vbo == update_vbo2,
                  "transform update preserves selection and the other string");

            check(isogl_string_list_update_by_pick_id(
                      &updates, 401, "", font,
                      10.0f, 10.0f, 10.0f, 0.5f, 0.0f) == 0,
                  "empty string update succeeds");
            check(updates.glyph_strings[0].vertex_count == 0 &&
                  updates.glyph_strings[0].vbo == 0,
                  "empty string has no VBO");
            check(updates.glyph_strings[1].vbo == update_vbo2,
                  "empty update leaves the other string untouched");

            isogl_string_list_free(&updates);
        } else {
            if (u1) isogl_free_glyph_string(u1);
            if (u2) isogl_free_glyph_string(u2);
        }

        // Text pick: a single glyph at the pick center with its own ID.
        isogl_string_list pick_strings = {0};
        isogl_glyph_string *pick_text = isogl_create_glyph_string("I", font);
        check(pick_text != NULL, "pick glyph string built");
        if (pick_text) {
            /* The "I" glyph has its vertical stroke at x=140 font units.
               Shift the string origin so that stroke lands on the pick
               center instead of a few pixels to its right. */
            float text_scale = 20.0f / (font->height * 0.5f);
            float glyph_x = 140.0f * text_scale;
            check(isogl_string_list_append(&pick_strings, pick_text,
                                           pick_center.x - glyph_x,
                                           pick_center.y,
                                           20.0f, 0.5f, 0.0f, font, 201),
                  "pick text appended");

            uint32_t text_ids[ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE];
            size_t text_count = 0;
            int text_rc = isogl_pick_at(window, NULL, &pick_strings,
                                        FB_SIZE / 2.0f, FB_SIZE / 2.0f,
                                        text_ids,
                                        ISOGL_PICK_TEXTURE_SIZE * ISOGL_PICK_TEXTURE_SIZE,
                                        &text_count);
            check(text_rc == 0, "text pick succeeds");
            printf("pick text at (%d,%d): %zu id(s):",
                   FB_SIZE / 2, FB_SIZE / 2, text_count);
            for (size_t i = 0; i < text_count; ++i) {
                printf(" %u", text_ids[i]);
            }
            printf("\n");
            check(text_count == 1 && text_ids[0] == 201,
                  "text pick returns the string ID");

            isogl_string_list_free(&pick_strings);
        }

        // ---- isocad selection integration ---------------------------------
        CadDocument cad;
        check(cad_document_init(&cad, font, 1.0f) == 0,
              "cad document init");

        uint32_t line_a = cad_add_line(&cad, 0, LINE_TYPE_CONTINUOUS,
                                       LINE_THICKNESS_0_5, 0.0f, 0.0f,
                                       10.0f, 0.0f);
        uint32_t line_b = cad_add_line(&cad, 0, LINE_TYPE_CONTINUOUS,
                                       LINE_THICKNESS_0_5, 5.0f, 0.0f,
                                       15.0f, 0.0f);
        uint32_t circle_id = cad_add_circle(&cad, 0, LINE_TYPE_CONTINUOUS,
                                            LINE_THICKNESS_0_5, 50.0f, 50.0f,
                                            10.0f);
        /* Height 3 keeps the now-exact text OBB (real advances) inside the
           window rectangle used below. */
        uint32_t text_id = cad_add_text(&cad, 0, 0.0f, 20.0f, 3.0f, 0.0f,
                                        "ABC");
        check(line_a && line_b && circle_id && text_id,
              "cad entities added");

        uint32_t cad_contained[8];
        uint32_t cad_hit[8];
        size_t cad_n_contained = 0;
        size_t cad_n_hit = 0;
        isogl_point cad_p0 = {-5.0f, -5.0f};
        isogl_point cad_p1 = {12.0f, 25.0f};

        cad_select_rect(&cad, cad_p0, cad_p1,
                        cad_contained, 8, &cad_n_contained,
                        cad_hit, 8, &cad_n_hit);
        check(cad_n_contained == 2 && cad_contained[0] == line_a &&
              cad_contained[1] == text_id,
              "cad window selection returns contained line and text");
        check(cad_n_hit == 3, "cad crossing selection includes the crossing line");

        check(cad_selection_apply(&cad, cad_contained, cad_n_contained) == 0,
              "cad selection apply");
        check(isogl_vbo_array_get(&cad.parts, cad.entities[0].vbo_id)->selected == 1,
              "cad flags the contained line");
        check(isogl_vbo_array_get(&cad.parts, cad.entities[1].vbo_id)->selected == 0,
              "cad leaves the crossing line unselected in window mode");
        check(cad.strings.glyph_string_count == 1 &&
              cad.strings.glyph_strings[0].pick_id == text_id &&
              cad.strings.glyph_strings[0].selected == 1,
              "cad flags the contained text");

        // ---- Incremental CAD text updates ---------------------------------
        uint32_t text2_id = cad_add_text(&cad, 0, 30.0f, 20.0f, 3.0f, 0.0f,
                                         "XYZ");
        check(text2_id != 0, "second text added");

        isogl_glyph_string *text1_str = NULL;
        isogl_glyph_string *text2_str = NULL;
        for (uint32_t i = 0; i < cad.strings.glyph_string_count; ++i) {
            if (cad.strings.glyph_strings[i].pick_id == text_id) {
                text1_str = &cad.strings.glyph_strings[i];
            }
            if (cad.strings.glyph_strings[i].pick_id == text2_id) {
                text2_str = &cad.strings.glyph_strings[i];
            }
        }
        check(text1_str && text2_str, "both text strings present");

        if (text1_str && text2_str) {
            GLuint text1_vbo = text1_str->vbo;
            GLuint text2_vbo = text2_str->vbo;
            check(text1_vbo != 0 && text2_vbo != 0, "text VBOs created");

            check(cad_selection_apply(&cad, &text_id, 1) == 0,
                  "select first text");
            check(text1_str->selected == 1 && text2_str->selected == 0,
                  "first text selected, second not");

            check(cad_update_text(&cad, text2_id, "UVW") == 0,
                  "update second text content");
            check(strcmp(text2_str->text, "UVW") == 0 &&
                  text2_str->vbo != text2_vbo,
                  "second text content and VBO updated");
            check(text1_str->vbo == text1_vbo && text1_str->selected == 1,
                  "first text untouched by the second update");

            text2_vbo = text2_str->vbo;
            check(cad_update_text(&cad, text_id, "ABC2") == 0,
                  "update first text content");
            check(strcmp(text1_str->text, "ABC2") == 0 &&
                  text1_str->vbo != text1_vbo,
                  "first text content and VBO updated");
            check(text1_str->selected == 1,
                  "first text selection preserved by content update");
            check(text2_str->vbo == text2_vbo && text2_str->selected == 0,
                  "second text untouched by the first update");

            text1_vbo = text1_str->vbo;
            float old_matrix_x = text1_str->string_matrix[12];
            check(cad_set_text_transform(&cad, text_id, 5.0f, 25.0f,
                                         4.0f, 0.0f) == 0,
                  "set first text transform");
            check(text1_str->vbo == text1_vbo,
                  "transform update keeps the text VBO");
            check(text1_str->string_matrix[12] != old_matrix_x,
                  "transform update changes the matrix");
            check(text1_str->selected == 1 && text2_str->vbo == text2_vbo,
                  "transform preserves selection and the other text");
        }

        // A rectangle wholly inside the circle selects nothing: the interior
        // is not part of the entity.
        isogl_point circle_in0 = {45.0f, 45.0f};
        isogl_point circle_in1 = {55.0f, 55.0f};
        cad_select_rect(&cad, circle_in0, circle_in1,
                        cad_contained, 8, &cad_n_contained,
                        cad_hit, 8, &cad_n_hit);
        check(cad_n_contained == 0 && cad_n_hit == 0,
              "rect inside the circle selects nothing");

        // A rectangle covering the circle contains it.
        isogl_point circle_out0 = {35.0f, 35.0f};
        isogl_point circle_out1 = {65.0f, 65.0f};
        cad_select_rect(&cad, circle_out0, circle_out1,
                        cad_contained, 8, &cad_n_contained,
                        cad_hit, 8, &cad_n_hit);
        check(cad_n_contained == 1 && cad_contained[0] == circle_id,
              "rect covering the circle contains it");
        check(cad_selection_apply(&cad, cad_contained, cad_n_contained) == 0,
              "apply circle selection");
        check(isogl_vbo_array_get(&cad.parts, cad.entities[2].vbo_id)->selected == 1,
              "cad flags the covered circle");

        // Rectangle entity: contained and crossing semantics.
        uint32_t rect_id = cad_add_rectangle(&cad, 0, LINE_TYPE_CONTINUOUS,
                                             LINE_THICKNESS_0_5,
                                             100.0f, 0.0f, 110.0f, 10.0f);
        check(rect_id != 0, "cad rectangle added");

        isogl_point rect_cover0 = {90.0f, -5.0f};
        isogl_point rect_cover1 = {120.0f, 15.0f};
        cad_select_rect(&cad, rect_cover0, rect_cover1,
                        cad_contained, 8, &cad_n_contained,
                        cad_hit, 8, &cad_n_hit);
        check(cad_n_contained == 1 && cad_contained[0] == rect_id,
              "rect covering the rectangle contains it");

        isogl_point rect_cross0 = {105.0f, -5.0f};
        isogl_point rect_cross1 = {120.0f, 15.0f};
        cad_select_rect(&cad, rect_cross0, rect_cross1,
                        cad_contained, 8, &cad_n_contained,
                        cad_hit, 8, &cad_n_hit);
        check(cad_n_contained == 0 && cad_n_hit == 1 &&
              cad_hit[0] == rect_id,
              "rect crossing the rectangle selects it as hit");

        cad_selection_clear(&cad);
        check(isogl_vbo_array_get(&cad.parts, cad.entities[0].vbo_id)->selected == 0,
              "cad selection clear resets the flags");

        cad_document_free(&cad);

        isogl_string_list_free(&strings);
        isogl_font_free_data(font);
    }

    // ---- Cleanup -----------------------------------------------------------
    isogl_segment_list_free(&segments);
    GLuint font_program = isogl_shader_program_font;
    GLuint pick_program = isogl_shader_program_pick;
    GLuint font_pick_program = isogl_shader_program_font_pick;
    isogl_vbo_array_free(&objects);
    isogl_cleanup();

    check(glIsBuffer(part) == GL_FALSE, "VBO deleted after cleanup");
    check(glIsProgram(shader_segment) == GL_FALSE, "segment program deleted");
    check(glIsProgram(font_program) == GL_FALSE, "font program deleted");
    check(glIsProgram(pick_program) == GL_FALSE, "pick program deleted");
    check(glIsProgram(font_pick_program) == GL_FALSE,
          "font pick program deleted");

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
