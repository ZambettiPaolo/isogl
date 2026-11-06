#include "isogl.h"
#include "isogl_font.h"
#include "isogl_overlay.h"
#include "isogl_panel.h"
#include "glad.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FB_SIZE 256
#define BG_R 0
#define BG_G 0
#define BG_B 0

static int failures;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static unsigned char pixels[FB_SIZE * FB_SIZE * 4];

static void read_frame(void) {
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

static const unsigned char *pixel_at(int x, int y) {
    if (x < 0 || y < 0 || x >= FB_SIZE || y >= FB_SIZE) return NULL;
    int gy = FB_SIZE - 1 - y;   /* top-left test coordinates */
    return &pixels[(gy * FB_SIZE + x) * 4];
}

static int near_color(int x, int y, int r, int g, int b, int tolerance) {
    const unsigned char *p = pixel_at(x, y);
    if (!p) return 0;
    return abs((int)p[0] - r) <= tolerance &&
           abs((int)p[1] - g) <= tolerance &&
           abs((int)p[2] - b) <= tolerance;
}

static int region_has_color(int x0, int y0, int x1, int y1,
                            int r, int g, int b, int tolerance) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (near_color(x, y, r, g, b, tolerance)) return 1;
        }
    }
    return 0;
}

static void clear_scene(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    isogl_set_viewport(&(isogl_viewport){0, 0, FB_SIZE, FB_SIZE});
    isogl_begin_frame();
    isogl_clear(BG_R / 255.0f, BG_G / 255.0f, BG_B / 255.0f, 1.0f);
}

static void set_full_viewport(void) {
    isogl_viewport vp = {0, 0, FB_SIZE, FB_SIZE};
    isogl_set_viewport(&vp);
}

static void destroy_group(isogl_overlay *o, isogl_overlay_group *g) {
    if (*g != ISOGL_OVERLAY_GROUP_INVALID) {
        isogl_overlay_group_destroy(o, *g);
        *g = ISOGL_OVERLAY_GROUP_INVALID;
    }
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

    SDL_Window *window = SDL_CreateWindow("isogl-overlay-test", 0, 0,
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

    set_full_viewport();
    isogl_begin_frame();

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
    isogl_begin_frame();

    isogl_overlay *overlay = isogl_overlay_create(font);
    check(overlay != NULL, "overlay created");
    if (!overlay) return 2;

    /* ---- quad batch, top-left coordinates, stats ----------------------- */
    isogl_overlay_group red = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group green = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group blue = isogl_overlay_group_create(overlay, 0);
    check(red && green && blue, "overlay groups created");

    const float R[4] = {1, 0, 0, 1};
    const float G[4] = {0, 1, 0, 1};
    const float B[4] = {0, 0, 1, 1};

    isogl_overlay_group_quad(overlay, red, 10, 10, 30, 30, R);
    isogl_overlay_group_quad(overlay, green, 50, 10, 30, 30, G);
    isogl_overlay_group_quad(overlay, blue, 90, 10, 30, 30, B);

    clear_scene();
    check(isogl_overlay_draw(overlay) == 0, "overlay draw returns success");
    read_frame();
    isogl_overlay_stats stats = isogl_overlay_get_stats(overlay);
    check(stats.quads == 3, "three quads baked");
    check(stats.draw_calls == 1, "three quads are one draw call");
    check(near_color(20, 20, 255, 0, 0, 10), "first quad is red at top-left");
    check(near_color(60, 20, 0, 255, 0, 10), "second quad is green");
    check(near_color(100, 20, 0, 0, 255, 10), "third quad is blue");
    check(!near_color(45, 45, 255, 0, 0, 10), "quad does not bleed outside");

    /* ---- painter order inside one batch ------------------------------- */
    isogl_overlay_group_quad(overlay, red, 20, 50, 40, 40, R);
    isogl_overlay_group_quad(overlay, green, 30, 60, 40, 40, G);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(near_color(40, 70, 0, 255, 0, 10), "later quad wins the overlap");

    destroy_group(overlay, &red);
    destroy_group(overlay, &green);
    destroy_group(overlay, &blue);

    /* ---- group transform ---------------------------------------------- */
    isogl_overlay_group moved = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_quad(overlay, moved, 0, 0, 10, 10, R);
    isogl_overlay_group_set_transform(overlay, moved, 100, 100, 2.0f);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(near_color(110, 110, 255, 0, 0, 10),
          "group transform moves and scales the quad");
    check(!near_color(5, 5, 255, 0, 0, 10),
          "local quad origin is not drawn at the viewport origin");
    destroy_group(overlay, &moved);

    /* ---- line batch ---------------------------------------------------- */
    isogl_overlay_group lines = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_line(overlay, lines, 10, 200, 200, 200, R);
    isogl_overlay_group_line(overlay, lines, 20, 220, 200, 240, G);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    stats = isogl_overlay_get_stats(overlay);
    check(stats.lines == 2, "two segments baked");
    check(stats.draw_calls == 1, "segments are one draw call");
    check(region_has_color(9, 198, 202, 202, 255, 0, 0, 20),
          "first segment is red");
    check(region_has_color(19, 218, 202, 242, 0, 255, 0, 20),
          "second segment is green");
    destroy_group(overlay, &lines);

    /* ---- clipping ------------------------------------------------------ */
    isogl_overlay_group clipped = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_set_clip(overlay, clipped, 1, 50, 50, 40, 40);
    isogl_overlay_group_quad(overlay, clipped, 0, 0, 200, 200, R);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(near_color(60, 60, 255, 0, 0, 10), "clip keeps the inside");
    check(!near_color(10, 10, 255, 0, 0, 10), "clip discards the outside");
    destroy_group(overlay, &clipped);

    /* ---- layers: a tooltip covers lower primitives --------------------- */
    isogl_overlay_group base = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group top = isogl_overlay_group_create(overlay, 1);
    isogl_overlay_group_quad(overlay, base, 20, 20, 80, 80, R);
    isogl_overlay_group_quad(overlay, top, 40, 40, 40, 40, G);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(near_color(50, 50, 0, 255, 0, 10),
          "higher layer covers the lower layer");
    stats = isogl_overlay_get_stats(overlay);
    check(stats.draw_calls == 2, "two layers cost two draw calls");
    destroy_group(overlay, &base);
    destroy_group(overlay, &top);

    /* ---- text, per-string colour, clip --------------------------------- */
    isogl_overlay_group text = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_text(overlay, text, "TEST", 10, 30, 18, 0, R);
    isogl_overlay_group_text(overlay, text, "TEST", 10, 60, 18, 0, G);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    stats = isogl_overlay_get_stats(overlay);
    check(stats.texts == 2, "two strings baked");
    check(stats.draw_calls == 2, "one draw per string");
    check(region_has_color(9, 5, 90, 32, 255, 0, 0, 30),
          "first string uses its red colour");
    check(region_has_color(9, 35, 90, 62, 0, 255, 0, 30),
          "second string uses its green colour");

    isogl_overlay_group_set_clip(overlay, text, 1, 0, 0, 30, 75);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(region_has_color(9, 5, 29, 32, 255, 0, 0, 30),
          "text inside the clip survives");
    check(!region_has_color(31, 5, 90, 32, 255, 0, 0, 30),
          "text outside the clip is discarded");

    /* ---- hover colour override and restore ----------------------------- */
    isogl_overlay_group_set_clip(overlay, text, 0, 0, 0, 0, 0);
    isogl_overlay_group_set_color(overlay, text, B);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(region_has_color(9, 5, 90, 62, 0, 0, 255, 30),
          "group colour override tints text");
    isogl_overlay_group_set_color(overlay, text, NULL);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(region_has_color(9, 5, 90, 32, 255, 0, 0, 30),
          "clearing the override restores local colours");

    /* ---- text move and content change ---------------------------------- */
    isogl_overlay_group_set_transform(overlay, text, 60, 0, 1.0f);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(region_has_color(69, 5, 150, 32, 255, 0, 0, 30),
          "group transform moves text without rebuilding it");
    check(!region_has_color(9, 5, 50, 32, 255, 0, 0, 30),
          "old text position is empty after the move");

    isogl_overlay_group_set_transform(overlay, text, 60, 20, 1.0f);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(region_has_color(69, 25, 150, 60, 255, 0, 0, 30),
          "group transform moves text vertically without rebuilding it");
    check(!region_has_color(69, 5, 150, 20, 255, 0, 0, 30),
          "old vertical text position is empty after the move");

    isogl_overlay_group_clear(overlay, text);
    isogl_overlay_group_set_transform(overlay, text, 0.0f, 0.0f, 1.0f);
    isogl_overlay_group_text(overlay, text, "ABC", 10, 30, 18, 0, B);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(region_has_color(9, 5, 70, 32, 0, 0, 255, 30),
          "icon/text content change is re-emitted");
    destroy_group(overlay, &text);

    /* ---- icon-like geometry change (clear + re-add) -------------------- */
    isogl_overlay_group icon = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_line(overlay, icon, 10, 100, 60, 100, R);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(region_has_color(9, 98, 62, 102, 255, 0, 0, 20),
          "icon line drawn");
    isogl_overlay_group_clear(overlay, icon);
    isogl_overlay_group_line(overlay, icon, 10, 100, 60, 100, R);
    isogl_overlay_group_line(overlay, icon, 10, 110, 60, 110, R);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    stats = isogl_overlay_get_stats(overlay);
    check(stats.lines == 2, "icon change re-emits both lines");
    check(region_has_color(9, 108, 62, 112, 255, 0, 0, 20),
          "new icon line is visible");
    destroy_group(overlay, &icon);

    /* ---- selection tint suppression ------------------------------------ */
    isogl_overlay_group panel_text = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_text(overlay, panel_text, "TEST", 10, 30, 18, 0, B);
    isogl_set_selection_preview(1);
    isogl_set_selection_rect(0.0f, 0.0f, 128.0f, 128.0f, 0);
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(region_has_color(9, 5, 70, 32, 0, 0, 255, 30),
          "overlay text keeps its colour during a selection drag");
    isogl_clear_selection_rect();
    destroy_group(overlay, &panel_text);

    /* ---- matrices are untouched ---------------------------------------- */
    clear_scene();
    float model_before[16], view_before[16];
    for (int i = 0; i < 16; ++i) {
        isogl_model_matrix[i] = (float)i + 0.5f;
        isogl_view_matrix[i] = (float)i + 10.5f;
    }
    memcpy(model_before, isogl_model_matrix, sizeof(model_before));
    memcpy(view_before, isogl_view_matrix, sizeof(view_before));
    isogl_overlay_group marker = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_quad(overlay, marker, 0, 0, 5, 5, R);
    isogl_overlay_draw(overlay);
    read_frame();
    check(memcmp(model_before, isogl_model_matrix, sizeof(model_before)) == 0,
          "overlay leaves the model matrix untouched");
    check(memcmp(view_before, isogl_view_matrix, sizeof(view_before)) == 0,
          "overlay leaves the view matrix untouched");
    destroy_group(overlay, &marker);
    set_full_viewport();
    isogl_begin_frame();

    /* ---- sub-viewport --------------------------------------------------- */
    clear_scene();                 /* clear the whole framebuffer first */
    isogl_viewport sub = {16, 32, 128, 128};
    isogl_set_viewport(&sub);
    isogl_begin_frame();
    isogl_clear(0, 0, 0, 1);
    isogl_overlay_group subquad = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_quad(overlay, subquad, 0, 0, 20, 20, R);
    isogl_overlay_draw(overlay);
    read_frame();
    /* Viewport top-left in framebuffer top-left coordinates is
       (16, 256 - (32 + 128)) = (16, 96); local (10,10) is (26,106). */
    check(near_color(26, 106, 255, 0, 0, 10),
          "overlay uses viewport-local top-left coordinates");
    check(!near_color(4, 4, 255, 0, 0, 10),
          "overlay does not draw outside the sub-viewport");
    destroy_group(overlay, &subquad);
    set_full_viewport();
    isogl_begin_frame();

    /* ---- panel emit smoke test ----------------------------------------- */
    isogl_panel_edges edges = {32, 32, 32, 100, 1};
    isogl_panel_layout layout;
    check(isogl_panel_compute(&layout, FB_SIZE, FB_SIZE, &edges) == 0,
          "panel layout computed");
    check(layout.content.x == 32 && layout.content.y == 32 &&
          layout.content.width == 192 && layout.content.height == 124,
          "panel content rectangle is correct");

    isogl_panel_style style;
    const float bg[4] = {0.1f, 0.2f, 0.3f, 1.0f};
    const float edge[4] = {0.8f, 0.8f, 0.2f, 1.0f};
    memcpy(style.background, bg, sizeof(bg));
    memcpy(style.edge, edge, sizeof(edge));

    isogl_overlay_group panels = isogl_overlay_group_create(overlay, 0);
    check(isogl_panel_emit(&layout, &style, overlay, panels) == 0,
          "panel emit succeeds");
    clear_scene();
    isogl_overlay_draw(overlay);
    read_frame();
    check(near_color(100, 10, 26, 51, 77, 10), "top strip background");
    check(near_color(100, 31, 204, 204, 51, 10), "top separator");
    check(near_color(10, 100, 26, 51, 77, 10), "left strip background");
    check(near_color(246, 100, 26, 51, 77, 10), "right strip background");
    check(near_color(100, 250, 26, 51, 77, 10), "bottom strip background");
    check(!near_color(100, 100, 26, 51, 77, 10),
          "content rectangle is left untouched");
    destroy_group(overlay, &panels);

    /* ---- cleanup order -------------------------------------------------- */
    isogl_overlay_destroy(overlay);
    isogl_font_free_data(font);
    isogl_cleanup();
    glDeleteTextures(1, &color_tex);
    glDeleteFramebuffers(1, &fbo);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASSED: overlay tests\n");
        return 0;
    }
    printf("FAILED: %d overlay check(s)\n", failures);
    return 1;
}
