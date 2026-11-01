#include <isogl/isogl.h>
#include <isogl/glad.h>
#include <SDL2/SDL.h>

#include "fake_cad.h"

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

static int pixel_is_green(const unsigned char *pixels, int x, int y) {
    return pixel_near_color(pixels, x, y, 77, 230, 102);
}

static int collect_symbol_items(void *user, const IsoglSymItem *item) {
    size_t *count = user;
    (void)item;
    (*count)++;
    return 0;
}

static int fail_symbol_item(void *user, const IsoglSymItem *item) {
    (void)user;
    (void)item;
    return -1;
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".";
    const char *fixture = argc > 2 ? argv[2]
        : "tests/fixtures/test_drawing.toml";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

    SDL_Window *window = SDL_CreateWindow("isogl-symbols-test", 0, 0,
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
        fprintf(stderr, "isogl_init_with_asset_dir failed (assets: %s)\n",
                assets);
        return 2;
    }

    char font_path[4096];
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                         font_path, sizeof(font_path)) != 0) {
        fprintf(stderr, "font asset path failed\n");
        return 2;
    }

    isogl_font_data *font = isogl_font_load_from_toml(font_path);
    check(font != NULL, "font loads");
    if (!font) return 2;

    check(isogl_symbol_init(font) == 0, "symbol registry init");

    isogl_set_sheet(420.0f, 297.0f, (isogl_scale){1, 1});
    isogl_zoom_all_sheet();
    isogl_font_init_vao();
    isogl_resize_viewport(window);

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

    // ---- Load the structured test drawing ---------------------------------
    FakeDrawingSpec spec;
    check(fake_drawing_load_toml(&spec, fixture) == 0,
          "test drawing TOML loads");
    check(spec.group_count == 2, "drawing has two groups");
    check(spec.primitive_count == 6, "drawing has six primitives");
    check(spec.symbol_count == 2, "drawing has two symbol specs");

    FakeDrawing fake;
    check(fake_drawing_build(&fake, &spec, font) == 0,
          "fake CAD graphics build");
    check(fake_drawing_create_symbols(&fake) == 0,
          "symbols created from the drawing");
    check(fake.ref_count == 2, "two symbol references");
    if (fake.ref_count != 2) return 2;

    isogl_symbol_id dim_id = fake.refs[0].symbol;
    isogl_symbol_id center_id = fake.refs[1].symbol;
    check(dim_id != 0 && center_id != 0 && dim_id != center_id,
          "symbol IDs are distinct and non-zero");

    // ---- Bounds and hit ----------------------------------------------------
    isogl_box dim_all = {30.0, 50.0, 170.0, 100.0};
    check(isogl_symbol_hit(dim_id, &dim_all, 0) == 1,
          "dimension window hit");
    isogl_box dim_bounds;
    check(isogl_symbol_bounds(dim_id, &dim_bounds) == 0 &&
          dim_bounds.xmin <= 40.0 && dim_bounds.xmax >= 160.0 &&
          dim_bounds.ymin <= 60.0 && dim_bounds.ymax >= 93.0,
          "dimension bounds cover pick geometry");

    isogl_box dim_no_text = {30.0, 50.0, 170.0, 93.0};
    check(isogl_symbol_hit(dim_id, &dim_no_text, 0) == 0,
          "dimension window excludes the text OBB");
    check(isogl_symbol_hit(dim_id, &dim_no_text, 1) == 1,
          "dimension crossing includes the text OBB");

    isogl_box dim_empty = {60.0, 70.0, 80.0, 80.0};
    check(isogl_symbol_hit(dim_id, &dim_empty, 1) == 0,
          "dimension empty interior is not selectable");

    isogl_box dim_cross = {90.0, 89.0, 110.0, 91.0};
    check(isogl_symbol_hit(dim_id, &dim_cross, 1) == 1,
          "dimension line crossing hit");
    check(isogl_symbol_hit(dim_id, &dim_cross, 0) == 0,
          "dimension line crossing is not contained");

    isogl_box center_box = {140.0, 50.0, 160.0, 70.0};
    check(isogl_symbol_hit(center_id, &center_box, 0) == 1,
          "center mark contained");
    isogl_box center_empty = {100.0, 30.0, 120.0, 40.0};
    check(isogl_symbol_hit(center_id, &center_empty, 1) == 0,
          "center mark excludes empty space");

    // ---- Symbol rectangle selection in the fake CAD ------------------------
    uint32_t selected[8];
    size_t selected_count = 0;
    check(fake_drawing_select_symbols(&fake, &dim_all, 0,
                                      selected, 8, &selected_count) == 0 &&
          selected_count == 2,
          "fake CAD window selects both symbols");
    isogl_box dim_only = {30.0, 80.0, 170.0, 100.0};
    check(fake_drawing_select_symbols(&fake, &dim_only, 1,
                                      selected, 8, &selected_count) == 0 &&
          selected_count == 1 && selected[0] == dim_id,
          "fake CAD crossing isolates the dimension");
    isogl_box center_only = {140.0, 50.0, 158.0, 70.0};
    check(fake_drawing_select_symbols(&fake, &center_only, 1,
                                      selected, 8, &selected_count) == 0 &&
          selected_count == 1 && selected[0] == center_id,
          "fake CAD crossing isolates the center mark");

    // ---- Draw and pick -----------------------------------------------------
    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    fake_drawing_draw(&fake);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(count_lit_pixels(pixels, FB_SIZE, FB_SIZE) > 0,
          "fake CAD and symbols draw pixels");

    isogl_point dim_screen = {0.0f, 0.0f};
    check(isogl_world_to_screen(window, 100.0f, 90.0f, &dim_screen) == 0,
          "dimension screen conversion");
    uint32_t pick_ids[64];
    size_t pick_count = 0;
    check(fake_drawing_pick_symbols(&fake, window,
                                    dim_screen.x, dim_screen.y,
                                    pick_ids, 64, &pick_count) == 0,
          "symbol pick succeeds");
    check(pick_count == 1 && pick_ids[0] == dim_id,
          "pick returns the dimension symbol ID");

    isogl_point center_screen = {0.0f, 0.0f};
    check(isogl_world_to_screen(window, 150.0f, 60.0f, &center_screen) == 0,
          "center mark screen conversion");
    check(fake_drawing_pick_symbols(&fake, window,
                                    center_screen.x, center_screen.y,
                                    pick_ids, 64, &pick_count) == 0,
          "center mark pick succeeds");
    check(pick_count == 1 && pick_ids[0] == center_id,
          "pick returns the center mark ID");

    // The fake CAD primitive at world y=160 proves the group transform.
    isogl_point child_screen = {0.0f, 0.0f};
    check(isogl_world_to_screen(window, 45.0f, 160.0f, &child_screen) == 0,
          "child group screen conversion");
    check(isogl_pick_at(window, &fake.parts, &fake.strings,
                        child_screen.x, child_screen.y,
                        pick_ids, 64, &pick_count) == 0,
          "CAD primitive pick succeeds");
    check(pick_count >= 1 &&
          pick_ids[0] == FAKE_CAD_PICK_BASE + 1u,
          "child group transform is applied to CAD geometry");

    // ---- Symbol selection highlight ---------------------------------------
    isogl_set_selection_color(0.3f, 0.9f, 0.4f, 1.0f);
    check(isogl_symbol_set_selected(dim_id, 1) == 0,
          "set dimension selection");
    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    fake_drawing_draw(&fake);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(pixel_is_green(pixels, (int)dim_screen.x, (int)dim_screen.y),
          "selected symbol is tinted");
    check(isogl_symbol_set_selected(dim_id, 0) == 0,
          "clear dimension selection");
    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    fake_drawing_draw(&fake);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(!pixel_is_green(pixels, (int)dim_screen.x, (int)dim_screen.y),
          "cleared symbol returns to its style colour");

    // ---- Explode the center mark ------------------------------------------
    size_t seg = 0, txt = 0;
    size_t prim_before = fake.spec.primitive_count;
    check(fake_drawing_explode_symbol(&fake, 1, &seg, &txt) == 0,
          "center mark explode succeeds");
    check(seg == 2 && txt == 0, "center mark emits two segments");
    check(fake.ref_count == 1, "center mark reference removed");
    check(fake.spec.primitive_count == prim_before + 2,
          "center mark segments transferred to the CAD");
    check(isogl_symbol_hit(center_id, &center_box, 0) == -1,
          "exploded center mark ID is dead");
    check(isogl_symbol_set_selected(center_id, 1) == -1,
          "dead symbol cannot be selected");

    // ---- Explode the dimension --------------------------------------------
    prim_before = fake.spec.primitive_count;
    check(fake_drawing_explode_symbol(&fake, 0, &seg, &txt) == 0,
          "dimension explode succeeds");
    check(seg == 7 && txt == 1, "dimension emits 7 segments + 1 text");
    check(fake.ref_count == 0, "dimension reference removed");
    check(fake.spec.primitive_count == prim_before + 8,
          "dimension geometry transferred to the CAD");
    check(isogl_symbol_hit(dim_id, &dim_all, 0) == -1,
          "exploded dimension ID is dead");

    int found_value = 0;
    for (size_t i = 0; i < fake.spec.primitive_count; ++i) {
        const FakePrimitive *p = &fake.spec.primitives[i];
        if (p->type == FAKE_PRIM_TEXT && strcmp(p->text, "120.0 mm") == 0) {
            found_value = 1;
        }
    }
    check(found_value, "dimension text became a CAD text primitive");

    isogl_clear(0.05f, 0.05f, 0.05f, 1.0f);
    fake_drawing_draw(&fake);
    glReadPixels(0, 0, FB_SIZE, FB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(count_lit_pixels(pixels, FB_SIZE, FB_SIZE) > 0,
          "exploded geometry renders through the fake CAD");

    // The exploded dimension line is now CAD geometry, not a symbol.
    check(isogl_pick_at(window, &fake.parts, &fake.strings,
                        dim_screen.x, dim_screen.y,
                        pick_ids, 64, &pick_count) == 0,
          "exploded geometry pick succeeds");
    check(pick_count >= 1 &&
          pick_ids[0] == FAKE_CAD_PICK_BASE,
          "exploded geometry is picked as CAD geometry");

    // ---- Transactional explode failure ------------------------------------
    IsoglCenterMarkDesc fail_mark;
    memset(&fail_mark, 0, sizeof(fail_mark));
    fail_mark.cx = 50.0;
    fail_mark.cy = 50.0;
    fail_mark.size_mm = 5.0;
    fail_mark.sheet_scale = 1.0;
    fail_mark.weight = 1;
    fail_mark.color[0] = fail_mark.color[1] = 1.0f;
    fail_mark.color[2] = fail_mark.color[3] = 1.0f;

    isogl_symbol_id fail_id = isogl_symbol_create_center_mark(&fail_mark);
    check(fail_id != 0, "failure-test center mark created");
    isogl_box fail_box = {45.0, 45.0, 55.0, 55.0};

    IsoglSymEmitter failing = {NULL, fail_symbol_item};
    check(isogl_symbol_explode(fail_id, &failing) == -1,
          "failed explode returns error");
    check(isogl_symbol_hit(fail_id, &fail_box, 0) == 1,
          "failed explode leaves the symbol alive");

    size_t item_count = 0;
    IsoglSymEmitter collector = {&item_count, collect_symbol_items};
    check(isogl_symbol_explode(fail_id, &collector) == 0,
          "retry explode succeeds");
    check(item_count == 2, "retry emitted both center-mark segments");
    check(isogl_symbol_hit(fail_id, &fail_box, 0) == -1,
          "retry destroyed the symbol");

    isogl_symbol_id next_id = isogl_symbol_create_center_mark(&fail_mark);
    check(next_id > fail_id, "symbol IDs are monotonic and not reused");
    check(isogl_symbol_destroy(next_id) == 0, "destroy new symbol");
    check(isogl_symbol_hit(next_id, &fail_box, 0) == -1,
          "destroyed symbol is invalid");

    // ---- Cleanup -----------------------------------------------------------
    fake_drawing_free(&fake);
    isogl_symbol_cleanup();
    isogl_font_free_data(font);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &color_tex);
    isogl_cleanup();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASSED: 0 failure(s)\n");
        return 0;
    }
    printf("FAILED: %d failure(s)\n", failures);
    return 1;
}
