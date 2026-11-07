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

/* ---- a custom symbol type defined only in C ---------------------------- */

/* When set, generate() emits a full build and then fails.  Used to prove the
 * atomicity of params_update: the old symbol must survive untouched. */
static int custom_fail_generate;

enum {
    CUSTOM_POINTS = 0,
    CUSTOM_TEXTS,
    CUSTOM_SEGMENTS,
    CUSTOM_SCALE,
    CUSTOM_PARAM_COUNT
};

static const isogl_symbol_param_desc CUSTOM_PARAMS[CUSTOM_PARAM_COUNT] = {
    [CUSTOM_POINTS] = {
        "points", "Punti", "Due punti del segmento", "mm",
        ISOGL_SYM_PARAM_POINTS, 2, 2, 1
    },
    [CUSTOM_TEXTS] = {
        "text", "Etichetta", "Testo al primo punto", "",
        ISOGL_SYM_PARAM_TEXTS, 1, 1, 1
    },
    [CUSTOM_SEGMENTS] = {
        "extra", "Segmento extra", "Geometria fornita dal chiamante", "mm",
        ISOGL_SYM_PARAM_SEGMENTS, 1, 1, 1
    },
    [CUSTOM_SCALE] = {
        "scale", "Scala foglio", "Sola lettura", "1:n",
        ISOGL_SYM_PARAM_SCALE, 1, 1, 0
    }
};

static int custom_init(isogl_symbol_params *params) {
    if (!params || params->param_count != CUSTOM_PARAM_COUNT) return -1;
    params->params[CUSTOM_POINTS].value.points[0].x = 0.0;
    params->params[CUSTOM_POINTS].value.points[0].y = 0.0;
    params->params[CUSTOM_POINTS].value.points[1].x = 0.0;
    params->params[CUSTOM_POINTS].value.points[1].y = 0.0;
    if (isogl_symbol_params_set_text(params, CUSTOM_TEXTS, 0, "") != 0) {
        return -1;
    }
    memset(&params->params[CUSTOM_SEGMENTS].value.segments[0], 0,
           sizeof(isogl_symbol_segment));
    params->params[CUSTOM_SCALE].value.number = params->sheet_scale;
    return 0;
}

static int custom_validate(const isogl_symbol_params *params) {
    if (!params || params->param_count != CUSTOM_PARAM_COUNT) return -1;
    if (params->params[CUSTOM_POINTS].count != 2) return -1;
    if (params->params[CUSTOM_TEXTS].count != 1) return -1;
    if (params->params[CUSTOM_SEGMENTS].count != 1) return -1;
    return 0;
}

static int custom_emit_segment(IsoglSymEmitter *render,
                               double x0, double y0, double x1, double y1) {
    IsoglSymItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_SEGMENT;
    item.x0 = x0; item.y0 = y0; item.x1 = x1; item.y1 = y1;
    item.weight = LINE_THICKNESS_0_25;
    item.pattern = LINE_TYPE_CONTINUOUS;
    item.layer = 0;
    item.color[0] = 1.0f; item.color[1] = 1.0f;
    item.color[2] = 1.0f; item.color[3] = 1.0f;
    return render->emit(render->user, &item);
}

static int custom_emit_pick_segment(IsoglSymPickEmitter *pick,
                                    double x0, double y0,
                                    double x1, double y1) {
    IsoglSymPickItem item;
    memset(&item, 0, sizeof(item));
    item.kind = ISOGL_SYM_PICK_SEGMENT;
    item.x0 = x0; item.y0 = y0; item.x1 = x1; item.y1 = y1;
    return pick->emit(pick->user, &item);
}

static int custom_generate(const isogl_symbol_params *params,
                           const isogl_symbol_gen_context *ctx,
                           IsoglSymEmitter *render,
                           IsoglSymPickEmitter *pick) {
    (void)ctx;
    if (!params || !render || !pick) return -1;

    const isogl_symbol_point *p = params->params[CUSTOM_POINTS].value.points;
    const isogl_symbol_segment *extra =
        params->params[CUSTOM_SEGMENTS].value.segments;

    int rc = 0;
    rc |= custom_emit_segment(render, p[0].x, p[0].y, p[1].x, p[1].y);
    rc |= custom_emit_segment(render, extra[0].x0, extra[0].y0,
                              extra[0].x1, extra[0].y1);

    IsoglSymItem text;
    memset(&text, 0, sizeof(text));
    text.kind = ISOGL_SYM_TEXT;
    text.utf8 = params->params[CUSTOM_TEXTS].value.texts[0];
    text.x = p[0].x;
    text.y = p[0].y;
    text.height_mm = 5.0;
    text.color[0] = 1.0f; text.color[1] = 1.0f;
    text.color[2] = 0.2f; text.color[3] = 1.0f;
    rc |= render->emit(render->user, &text);

    rc |= custom_emit_pick_segment(pick, p[0].x, p[0].y, p[1].x, p[1].y);
    rc |= custom_emit_pick_segment(pick, extra[0].x0, extra[0].y0,
                                   extra[0].x1, extra[0].y1);

    /* Fail after a complete emission: sym_update() must discard the build and
     * leave the previous VBO, strings, pick items and params in place. */
    if (custom_fail_generate) return -1;
    return rc == 0 ? 0 : -1;
}

static const isogl_symbol_type CUSTOM_TYPE = {
    .name = "custom_test",
    .label = "Custom test",
    .description = "Symbol generated by a C function",
    .params = CUSTOM_PARAMS,
    .param_count = CUSTOM_PARAM_COUNT,
    .init_params = custom_init,
    .validate = custom_validate,
    .generate = custom_generate
};

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

    SDL_Window *window = SDL_CreateWindow("isogl-symbol-params-test", 0, 0,
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
        return 2;
    }
    isogl_font_data *font = isogl_font_load_from_toml(font_path);
    check(font != NULL, "font loaded");
    if (!font) return 2;
    isogl_font_init_vao();
    check(isogl_symbol_init(font) == 0, "symbol registry initialized");

    /* ---- module-wide read-only sheet scale ----------------------------- */
    check(isogl_symbol_get_sheet_scale() == 1.0,
          "sheet scale defaults to 1.0");
    check(isogl_symbol_set_sheet_scale(0.0) == -1,
          "sheet scale rejects zero");
    check(isogl_symbol_set_sheet_scale(-2.0) == -1,
          "sheet scale rejects negatives");
    check(isogl_symbol_set_sheet_scale(NAN) == -1,
          "sheet scale rejects NaN");
    check(isogl_symbol_set_sheet_scale(INFINITY) == -1,
          "sheet scale rejects infinity");
    check(isogl_symbol_get_sheet_scale() == 1.0,
          "invalid sheet scale leaves the global unchanged");

    check(isogl_symbol_find_type("dimension") != NULL,
          "built-in dimension type registered");
    check(isogl_symbol_find_type("center_mark") != NULL,
          "built-in center mark type registered");
    size_t type_count = isogl_symbol_type_count();
    check(type_count >= 2, "type registry enumerates built-ins");
    check(isogl_symbol_type_at(0) != NULL, "type registry index access");

    /* ---- metadata and built-in params ---------------------------------- */
    isogl_symbol_params *dim = isogl_symbol_params_create("dimension");
    check(dim != NULL, "dimension params created");
    if (dim) {
        check(dim->param_count >= 4, "dimension parameter schema populated");
        int points = param_index(dim, "points");
        int texts = param_index(dim, "texts");
        int scale = param_index(dim, "scale");
        check(points >= 0 && texts >= 0 && scale >= 0,
              "stable parameter keys exposed");
        check(dim->params[points].desc->label != NULL &&
              dim->params[points].desc->kind == ISOGL_SYM_PARAM_POINTS,
              "point list metadata");
        check(dim->params[texts].desc->kind == ISOGL_SYM_PARAM_TEXTS,
              "text list metadata");
        check(dim->params[scale].desc->editable == 0,
              "sheet scale is read-only");

        dim->params[points].value.points[0].x = 10.0;
        dim->params[points].value.points[0].y = 10.0;
        dim->params[points].value.points[1].x = 60.0;
        dim->params[points].value.points[1].y = 10.0;
        dim->params[points].value.points[2].x = 10.0;
        dim->params[points].value.points[2].y = 20.0;
        isogl_symbol_params_set_text(dim, texts, 1, " mm");

        isogl_symbol_id dim_id = isogl_symbol_create("dimension", dim, 1.0);
        check(dim_id != 0, "dimension created from generic params");

        isogl_symbol_params *copy = NULL;
        check(isogl_symbol_params_get(dim_id, &copy) == 0,
              "params_get returns a deep copy");
        if (copy) {
            int cp = param_index(copy, "points");
            int ct = param_index(copy, "texts");
            check(cp >= 0 && fabs(copy->params[cp].value.points[1].x - 60.0) < 1e-6,
                  "params_get returns point values");
            check(ct >= 0 && strcmp(copy->params[ct].value.texts[1], " mm") == 0,
                  "params_get returns text values");

            copy->params[cp].value.points[1].x = 80.0;
            isogl_symbol_set_selected(dim_id, 1);
            check(isogl_symbol_params_update(dim_id, copy) == 0,
                  "params_update regenerates the dimension");
            check(isogl_symbol_get_selected(dim_id) == 1,
                  "params_update preserves selection");
            isogl_box bounds;
            check(isogl_symbol_bounds(dim_id, &bounds) == 0 &&
                  bounds.xmax >= 80.0,
                  "updated geometry follows the new point");
            isogl_symbol_params_free(copy);
        }
        isogl_symbol_params_free(dim);
    }

    /* ---- custom C type -------------------------------------------------- */
    check(isogl_symbol_register_type(&CUSTOM_TYPE) == 0,
          "custom type registered");
    check(isogl_symbol_register_type(&CUSTOM_TYPE) != 0,
          "duplicate type registration rejected");
    check(isogl_symbol_find_type("custom_test") == &CUSTOM_TYPE,
          "custom type found by name");

    isogl_symbol_params *custom = isogl_symbol_params_create("custom_test");
    check(custom != NULL, "custom params created");
    isogl_symbol_id custom_id = 0;
    if (custom) {
        int points = param_index(custom, "points");
        int texts = param_index(custom, "text");
        int extra = param_index(custom, "extra");
        check(points >= 0 && texts >= 0 && extra >= 0,
              "custom schema exposes lists");

        custom->params[points].value.points[0].x = 10.0;
        custom->params[points].value.points[0].y = 10.0;
        custom->params[points].value.points[1].x = 40.0;
        custom->params[points].value.points[1].y = 10.0;
        isogl_symbol_params_set_text(custom, texts, 0, "C");
        custom->params[extra].value.segments[0] =
            (isogl_symbol_segment){10.0, 20.0, 40.0, 20.0};

        custom_id = isogl_symbol_create("custom_test", custom, 1.0);
        check(custom_id != 0, "custom symbol created from C type");
        isogl_symbol_params_free(custom);
    }

    if (custom_id) {
        isogl_set_sheet(100.0f, 100.0f, (isogl_scale){1, 1});
        isogl_viewport vp = {0, 0, FB_SIZE, FB_SIZE};
        isogl_set_viewport(&vp);
        isogl_begin_frame();
        isogl_zoom_all_sheet();
        isogl_clear(0.02f, 0.02f, 0.02f, 1.0f);
        isogl_symbol_draw_all();

        isogl_point screen;
        check(isogl_world_to_screen(10.0f, 10.0f, &screen) == 0,
              "custom point maps to screen");
        uint32_t picked[64];
        size_t picked_count = 0;
        check(isogl_symbol_pick_at(screen.x, screen.y,
                                   ISOGL_PICK_DEFAULT_EXTENT,
                                   picked, 64, &picked_count) == 0 &&
              picked_count > 0, "custom symbol pick returns an ID");
        check(picked_count > 0 && picked[0] == custom_id,
              "custom pick returns the custom symbol ID");

        isogl_box query = {5.0, 5.0, 45.0, 25.0};
        check(isogl_symbol_hit(custom_id, &query, 0) == 1,
              "custom symbol window hit");
        isogl_box bounds;
        check(isogl_symbol_bounds(custom_id, &bounds) == 0 &&
              bounds.xmax >= 40.0 && bounds.ymax >= 20.0,
              "custom bounds include points and segments");

        isogl_symbol_set_selected(custom_id, 1);
        isogl_symbol_params *copy = NULL;
        check(isogl_symbol_params_get(custom_id, &copy) == 0,
              "custom params_get");
        if (copy) {
            int points = param_index(copy, "points");
            int texts = param_index(copy, "text");
            int extra = param_index(copy, "extra");
            check(points >= 0 && fabs(copy->params[points].value.points[1].x -
                                      40.0) < 1e-6,
                  "custom points copied");
            check(texts >= 0 &&
                  strcmp(copy->params[texts].value.texts[0], "C") == 0,
                  "custom text copied");
            check(extra >= 0 &&
                  fabs(copy->params[extra].value.segments[0].x1 - 40.0) < 1e-6,
                  "custom segment copied");

            copy->params[points].value.points[1].x = 70.0;
            isogl_symbol_params_set_text(copy, texts, 0, "CHANGED");
            check(isogl_symbol_params_update(custom_id, copy) == 0,
                  "custom params_update succeeds");
            check(isogl_symbol_get_selected(custom_id) == 1,
                  "custom update preserves selection");
            check(isogl_symbol_bounds(custom_id, &bounds) == 0 &&
                  bounds.xmax >= 70.0,
                  "custom bounds follow the update");
            isogl_symbol_params_free(copy);
        }

        /* ---- atomic failure: a later update fails after a full build ---- */
        isogl_box before_fail;
        check(isogl_symbol_bounds(custom_id, &before_fail) == 0,
              "custom bounds before failed update");

        isogl_symbol_params *fail_copy = NULL;
        check(isogl_symbol_params_get(custom_id, &fail_copy) == 0,
              "params_get before failed update");
        if (fail_copy) {
            int fp = param_index(fail_copy, "points");
            int ft = param_index(fail_copy, "text");
            fail_copy->params[fp].value.points[1].x = 200.0;
            isogl_symbol_params_set_text(fail_copy, ft, 0, "BOOM");

            custom_fail_generate = 1;
            check(isogl_symbol_params_update(custom_id, fail_copy) == -1,
                  "failed generation returns -1");
            custom_fail_generate = 0;
            isogl_symbol_params_free(fail_copy);
        }

        isogl_box after_fail;
        check(isogl_symbol_bounds(custom_id, &after_fail) == 0 &&
              fabs(after_fail.xmin - before_fail.xmin) < 1e-9 &&
              fabs(after_fail.xmax - before_fail.xmax) < 1e-9 &&
              fabs(after_fail.ymin - before_fail.ymin) < 1e-9 &&
              fabs(after_fail.ymax - before_fail.ymax) < 1e-9,
              "failed update keeps the old bounds");
        check(isogl_symbol_get_selected(custom_id) == 1,
              "failed update keeps the selection");

        isogl_symbol_params *intact = NULL;
        check(isogl_symbol_params_get(custom_id, &intact) == 0,
              "params_get after failed update");
        if (intact) {
            int ip = param_index(intact, "points");
            int it = param_index(intact, "text");
            check(fabs(intact->params[ip].value.points[1].x - 70.0) < 1e-6,
                  "failed update keeps the old params");
            check(strcmp(intact->params[it].value.texts[0], "CHANGED") == 0,
                  "failed update keeps the old text");
            isogl_symbol_params_free(intact);
        }

        isogl_point old_screen;
        isogl_world_to_screen(10.0f, 10.0f, &old_screen);
        size_t fail_pick_count = 0;
        check(isogl_symbol_pick_at(old_screen.x, old_screen.y,
                                   ISOGL_PICK_DEFAULT_EXTENT,
                                   picked, 64, &fail_pick_count) == 0 &&
              fail_pick_count > 0 && picked[0] == custom_id,
              "failed update keeps the old pick geometry");

        /* ---- a symbol that fails to regenerate keeps its applied scale -- */
        custom_fail_generate = 1;
        check(isogl_symbol_set_sheet_scale(3.0) == -1,
              "global scale change reports a failing symbol");
        custom_fail_generate = 0;
        check(isogl_symbol_get_sheet_scale() == 3.0,
              "global scale is stored even when a symbol fails");
        check(isogl_symbol_bounds(custom_id, &after_fail) == 0 &&
              fabs(after_fail.xmax - before_fail.xmax) < 1e-9,
              "failing symbol keeps its previous geometry");
        isogl_symbol_params *stale = NULL;
        check(isogl_symbol_params_get(custom_id, &stale) == 0,
              "params_get after failed scale regeneration");
        if (stale) {
            int sp = param_index(stale, "scale");
            check(sp >= 0 && stale->params[sp].value.number == 1.0,
                  "failing symbol keeps its previous applied scale");
            isogl_symbol_params_free(stale);
        }
        check(isogl_symbol_set_sheet_scale(1.0) == 0,
              "scale restored after the failure");

        check(isogl_symbol_destroy(custom_id) == 0, "custom symbol destroyed");
    }

    /* ---- successful global scale regeneration -------------------------- */
    IsoglCenterMarkDesc mark;
    memset(&mark, 0, sizeof(mark));
    mark.cx = 50.0;
    mark.cy = 50.0;
    mark.size_mm = 8.0;
    mark.sheet_scale = 1.0;
    mark.weight = 1;
    mark.color[0] = mark.color[1] = 1.0f;
    mark.color[2] = mark.color[3] = 1.0f;

    isogl_symbol_id mark_id = isogl_symbol_create_center_mark(&mark);
    check(mark_id != 0, "center mark created for the scale test");
    if (mark_id) {
        isogl_box mark_before;
        check(isogl_symbol_bounds(mark_id, &mark_before) == 0,
              "center mark bounds at scale 1");
        isogl_symbol_set_selected(mark_id, 1);

        isogl_point center_screen;
        isogl_world_to_screen(50.0f, 50.0f, &center_screen);
        uint32_t mark_pick[64];
        size_t mark_pick_count = 0;
        check(isogl_symbol_pick_at(center_screen.x, center_screen.y,
                                   ISOGL_PICK_DEFAULT_EXTENT,
                                   mark_pick, 64, &mark_pick_count) == 0 &&
              mark_pick_count > 0 && mark_pick[0] == mark_id,
              "center mark picked before the scale change");

        check(isogl_symbol_set_sheet_scale(2.0) == 0,
              "set_sheet_scale regenerates live symbols");
        check(isogl_symbol_get_sheet_scale() == 2.0,
              "get_sheet_scale reports the new scale");
        check(isogl_symbol_get_selected(mark_id) == 1,
              "scale regeneration preserves selection");

        isogl_symbol_params *scaled = NULL;
        check(isogl_symbol_params_get(mark_id, &scaled) == 0,
              "params_get after scale regeneration");
        if (scaled) {
            int sp = param_index(scaled, "scale");
            check(sp >= 0 && scaled->params[sp].value.number == 2.0,
                  "params_get SCALE reflects the global scale");
            check(sp >= 0 && scaled->params[sp].desc->editable == 0,
                  "params_get SCALE remains read-only");
            isogl_symbol_params_free(scaled);
        }

        isogl_box mark_after;
        check(isogl_symbol_bounds(mark_id, &mark_after) == 0 &&
              mark_after.xmax - mark_after.xmin <
                  mark_before.xmax - mark_before.xmin - 1e-6,
              "paper-sized geometry shrinks in model units at scale 2");

        /* An incoming SCALE value must be ignored by params_update. */
        isogl_symbol_params *tamper = NULL;
        check(isogl_symbol_params_get(mark_id, &tamper) == 0,
              "params_get before SCALE tamper");
        if (tamper) {
            int sp = param_index(tamper, "scale");
            tamper->params[sp].value.number = 99.0;
            check(isogl_symbol_params_update(mark_id, tamper) == 0,
                  "params_update succeeds while ignoring SCALE");
            isogl_symbol_params_free(tamper);
        }
        check(isogl_symbol_get_sheet_scale() == 2.0,
              "params_update cannot change the global scale");
        isogl_box tampered;
        check(isogl_symbol_bounds(mark_id, &tampered) == 0 &&
              fabs(tampered.xmax - mark_after.xmax) < 1e-9 &&
              fabs(tampered.ymax - mark_after.ymax) < 1e-9,
              "params_update ignores the incoming SCALE value");

        isogl_symbol_params *reported = NULL;
        check(isogl_symbol_params_get(mark_id, &reported) == 0,
              "params_get after SCALE tamper");
        if (reported) {
            int sp = param_index(reported, "scale");
            check(sp >= 0 && reported->params[sp].value.number == 2.0,
                  "stored SCALE is still the global value");
            isogl_symbol_params_free(reported);
        }

        size_t after_pick_count = 0;
        check(isogl_symbol_pick_at(center_screen.x, center_screen.y,
                                   ISOGL_PICK_DEFAULT_EXTENT,
                                   mark_pick, 64, &after_pick_count) == 0 &&
              after_pick_count > 0 && mark_pick[0] == mark_id,
              "scale regeneration preserves the pick ID");

        check(isogl_symbol_set_sheet_scale(1.0) == 0,
              "scale reset to 1.0");
        check(isogl_symbol_destroy(mark_id) == 0, "scale-test center mark destroyed");
    }

    isogl_symbol_cleanup();
    isogl_font_free_data(font);
    isogl_cleanup();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASSED: symbol params tests\n");
        return 0;
    }
    printf("FAILED: %d symbol params check(s)\n", failures);
    return 1;
}
