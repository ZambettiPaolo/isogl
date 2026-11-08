#define _POSIX_C_SOURCE 200809L

#include <isogl/isogl.h>
#include <isogl/glad.h>
#include <SDL2/SDL.h>

#include "cad.h"
#include "cad_commands.h"
#include "console.h"
#include "console_cmd.h"
#include "drawing_load.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FB_SIZE 256
#define TAN15 0.26794919243112270647

static int failures;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int near(double a, double b) {
    return fabs(a - b) < 1e-6;
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

static const isogl_symbol_param_desc *type_desc(const isogl_symbol_type *type,
                                                const char *key) {
    if (!type || !type->params || !key) return NULL;
    for (size_t i = 0; i < type->param_count; ++i) {
        if (type->params[i].key && strcmp(type->params[i].key, key) == 0) {
            return &type->params[i];
        }
    }
    return NULL;
}

static int run_command(CadDocument *doc, IsocadConsole *console,
                       const char *line) {
    IsocadCommand cmd;
    if (isocad_command_parse(line, &cmd) == ISOCAD_CMD_NONE) return -1;
    return isocad_command_execute(doc, console, &cmd);
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "r");
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

static IsoglDiameterDesc make_diameter_desc(void) {
    IsoglDiameterDesc d;
    memset(&d, 0, sizeof(d));
    d.cx = 100.0;
    d.cy = 100.0;
    d.px = 110.0;
    d.py = 100.0;
    d.tx = 100.0;
    d.ty = 120.0;
    d.text_height_mm = 3.5;
    d.sheet_scale = 1.0;
    snprintf(d.text_left, sizeof(d.text_left), "\xC3\x98");
    d.text_right[0] = '\0';
    d.precision = 1;
    d.pick_text = 1;
    d.arrow_left = ISOGL_DIM_ARROW_CLOSED;
    d.arrow_right = ISOGL_DIM_ARROW_CLOSED;
    return d;
}

static IsoglRadiusDesc make_radius_desc(void) {
    IsoglRadiusDesc d;
    memset(&d, 0, sizeof(d));
    d.cx = 200.0;
    d.cy = 100.0;
    d.px = 210.0;
    d.py = 100.0;
    d.tx = 215.0;
    d.ty = 105.0;
    d.text_height_mm = 3.5;
    d.sheet_scale = 1.0;
    snprintf(d.text_left, sizeof(d.text_left), "R");
    d.text_right[0] = '\0';
    d.precision = 1;
    d.pick_text = 1;
    d.arrow = ISOGL_DIM_ARROW_CLOSED;
    return d;
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : ".";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

    SDL_Window *window = SDL_CreateWindow("isocad-diameter-radius-test", 0, 0,
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
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                         font_path, sizeof(font_path)) != 0) {
        fprintf(stderr, "font asset path failed\n");
        return 2;
    }
    isogl_font_data *font = isogl_font_load_from_toml(font_path);
    check(font != NULL, "font loaded");
    if (!font) return 2;
    isogl_font_init_vao();
    check(isogl_symbol_init(font) == 0, "symbol registry initialized");

    isogl_set_sheet(420.0f, 297.0f, (isogl_scale){1, 1});
    isogl_zoom_all_sheet();
    isogl_viewport vp = {0, 0, FB_SIZE, FB_SIZE};
    isogl_set_viewport(&vp);

    CadDocument doc;
    check(cad_document_init(&doc, font, 1.0f) == 0, "CAD document initialized");

    /* ---- type descriptors and schema ------------------------------------ */
    const isogl_symbol_type *dia_type =
        isogl_symbol_find_type("dimension_diameter");
    const isogl_symbol_type *rad_type =
        isogl_symbol_find_type("dimension_radius");
    check(dia_type != NULL, "dimension_diameter is a built-in type");
    check(rad_type != NULL, "dimension_radius is a built-in type");
    if (!dia_type || !rad_type) return 2;

    check(dia_type->label && dia_type->label[0] != '\0' &&
          dia_type->description && dia_type->description[0] != '\0',
          "diameter type exposes editor metadata");
    check(rad_type->label && rad_type->label[0] != '\0' &&
          rad_type->description && rad_type->description[0] != '\0',
          "radius type exposes editor metadata");
    check(isogl_symbol_find_type("dimension") != NULL &&
          isogl_symbol_find_type("center_mark") != NULL &&
          isogl_symbol_find_type("table_row") != NULL,
          "existing built-ins remain registered");

    for (int i = 0; i < 2; ++i) {
        const isogl_symbol_type *type = i == 0 ? dia_type : rad_type;
        const isogl_symbol_param_desc *points = type_desc(type, "points");
        const isogl_symbol_param_desc *texts = type_desc(type, "texts");
        const isogl_symbol_param_desc *height =
            type_desc(type, "text_height_mm");
        const isogl_symbol_param_desc *precision =
            type_desc(type, "precision");
        const isogl_symbol_param_desc *pick_text =
            type_desc(type, "pick_text");
        const isogl_symbol_param_desc *pattern = type_desc(type, "pattern");
        const isogl_symbol_param_desc *weight = type_desc(type, "weight");
        const isogl_symbol_param_desc *layer = type_desc(type, "layer");
        const isogl_symbol_param_desc *color = type_desc(type, "color");
        const isogl_symbol_param_desc *scale = type_desc(type, "scale");

        check(points && points->kind == ISOGL_SYM_PARAM_POINTS &&
              points->min_count == 3 && points->max_count == 3,
              i == 0 ? "diameter points = centre, circle point, anchor"
                     : "radius points = centre, circle point, anchor");
        check(texts && texts->kind == ISOGL_SYM_PARAM_TEXTS &&
              texts->min_count == 2 && texts->max_count == 2,
              i == 0 ? "diameter texts = prefix, suffix"
                     : "radius texts = prefix, suffix");
        check(height && height->kind == ISOGL_SYM_PARAM_NUMBER &&
              precision && precision->kind == ISOGL_SYM_PARAM_INTEGER &&
              pick_text && pick_text->kind == ISOGL_SYM_PARAM_BOOLEAN,
              i == 0 ? "diameter size/precision/pick parameters"
                     : "radius size/precision/pick parameters");
        check(pattern && pattern->kind == ISOGL_SYM_PARAM_ENUM &&
              pattern->enum_count == LINE_TYPE_COUNT &&
              weight && weight->enum_count == LINE_THICKNESS_COUNT,
              i == 0 ? "diameter reuses the ISO style tables"
                     : "radius reuses the ISO style tables");
        check(layer && layer->kind == ISOGL_SYM_PARAM_INTEGER &&
              color && color->kind == ISOGL_SYM_PARAM_COLOR &&
              scale && scale->kind == ISOGL_SYM_PARAM_SCALE &&
              scale->editable == 0,
              i == 0 ? "diameter exposes style and read-only scale"
                     : "radius exposes style and read-only scale");

        if (i == 0) {
            const isogl_symbol_param_desc *left =
                type_desc(type, "arrow_left");
            const isogl_symbol_param_desc *right =
                type_desc(type, "arrow_right");
            check(left && left->kind == ISOGL_SYM_PARAM_ENUM &&
                  left->enum_count == 7 &&
                  right && right->kind == ISOGL_SYM_PARAM_ENUM &&
                  right->enum_count == 7,
                  "diameter arrow_left/right use the shared arrow table");
        } else {
            const isogl_symbol_param_desc *arrow = type_desc(type, "arrow");
            check(arrow && arrow->kind == ISOGL_SYM_PARAM_ENUM &&
                  arrow->enum_count == 7,
                  "radius arrow uses the shared arrow table");
        }
    }

    /* ---- defaults -------------------------------------------------------- */
    isogl_symbol_params *params =
        isogl_symbol_params_create("dimension_diameter");
    check(params != NULL, "diameter params created");
    if (params) {
        int ti = param_index(params, "texts");
        int hi = param_index(params, "text_height_mm");
        int pi = param_index(params, "precision");
        int pti = param_index(params, "pick_text");
        int li = param_index(params, "arrow_left");
        int ri = param_index(params, "arrow_right");
        check(ti >= 0 && strcmp(params->params[ti].value.texts[0],
                                "\xC3\x98") == 0 &&
              strcmp(params->params[ti].value.texts[1], "") == 0,
              "diameter defaults to the Ø prefix with no suffix");
        check(hi >= 0 && near(params->params[hi].value.number, 3.5) &&
              pi >= 0 && params->params[pi].value.integer == 1,
              "diameter defaults: 3.5 mm text, one decimal");
        check(pti >= 0 && params->params[pti].value.boolean == 1,
              "diameter default pick_text is true");
        check(li >= 0 && ri >= 0 &&
              params->params[li].value.integer == ISOGL_DIM_ARROW_CLOSED &&
              params->params[ri].value.integer == ISOGL_DIM_ARROW_CLOSED,
              "diameter defaults to closed arrows");
        isogl_symbol_params_free(params);
    }

    params = isogl_symbol_params_create("dimension_radius");
    check(params != NULL, "radius params created");
    if (params) {
        int ti = param_index(params, "texts");
        int ai = param_index(params, "arrow");
        int hi = param_index(params, "text_height_mm");
        check(ti >= 0 && strcmp(params->params[ti].value.texts[0], "R") == 0 &&
              strcmp(params->params[ti].value.texts[1], "") == 0,
              "radius defaults to the R prefix with no suffix");
        check(ai >= 0 &&
              params->params[ai].value.integer == ISOGL_DIM_ARROW_CLOSED,
              "radius defaults to a closed arrow");
        check(hi >= 0 && near(params->params[hi].value.number, 3.5),
              "radius default text height is 3.5 mm");
        isogl_symbol_params_free(params);
    }

    /* ---- creation through the CAD wrappers ------------------------------ */
    IsoglDiameterDesc dia_desc = make_diameter_desc();
    IsoglRadiusDesc rad_desc = make_radius_desc();
    isogl_symbol_id dia_id = cad_add_diameter_symbol(&doc, 2, &dia_desc);
    isogl_symbol_id rad_id = cad_add_radius_symbol(&doc, 2, &rad_desc);
    check(dia_id != 0, "diameter symbol created through the CAD wrapper");
    check(rad_id != 0, "radius symbol created through the CAD wrapper");
    if (!dia_id || !rad_id) return 2;
    check(doc.symbol_count == 2, "both references are registered");

    CadSymbolRef *dia_ref = cad_symbol_find(&doc, dia_id);
    CadSymbolRef *rad_ref = cad_symbol_find(&doc, rad_id);
    check(dia_ref && dia_ref->type == dia_type && dia_ref->params,
          "diameter reference keeps type and parameter copy");
    check(rad_ref && rad_ref->type == rad_type && rad_ref->params,
          "radius reference keeps type and parameter copy");
    check(dia_ref && dia_ref->params->params[param_index(
              dia_ref->params, "layer")].value.integer == 2,
          "diameter layer style is applied by the wrapper");
    check(rad_ref && rad_ref->params->params[param_index(
              rad_ref->params, "layer")].value.integer == 2,
          "radius layer style is applied by the wrapper");

    isogl_box dia_bounds;
    isogl_box rad_bounds;
    check(isogl_symbol_bounds(dia_id, &dia_bounds) == 0,
          "diameter bounds available");
    check(isogl_symbol_bounds(rad_id, &rad_bounds) == 0,
          "radius bounds available");
    check(near(dia_bounds.xmin, 90.0) &&
          near(dia_bounds.ymin, 100.0 - 2.2 * TAN15) &&
          dia_bounds.xmax >= 110.0 && dia_bounds.ymax >= 121.75,
          "diameter bounds cover the line, both heads and the text");
    check(near(rad_bounds.xmin, 200.0) &&
          near(rad_bounds.ymin, 100.0 - 2.2 * TAN15) &&
          rad_bounds.xmax >= 215.0 && rad_bounds.ymax >= 106.75,
          "radius bounds cover the line, head, leader and text");

    isogl_box dia_all = {89.0, 98.0, 120.0, 123.0};
    check(isogl_symbol_hit(dia_id, &dia_all, 0) == 1,
          "window hit contains the whole diameter symbol");
    isogl_box dia_inner = {99.0, 99.0, 101.0, 101.0};
    check(isogl_symbol_hit(dia_id, &dia_inner, 0) == 0,
          "window hit excludes the empty area around the line");
    isogl_box dia_line = {104.0, 99.0, 106.0, 101.0};
    check(isogl_symbol_hit(dia_id, &dia_line, 1) == 1,
          "crossing hit sees the diameter line");

    /* ---- GPU pick ------------------------------------------------------- */
    isogl_set_viewport(&vp);
    isogl_begin_frame();
    isogl_zoom_all_sheet();
    isogl_clear(0.02f, 0.02f, 0.02f, 1.0f);
    isogl_symbol_draw_all();

    isogl_point screen;
    uint32_t picked[64];
    size_t picked_count = 0;
    check(isogl_world_to_screen(105.0, 100.0, &screen) == 0,
          "diameter line maps to screen");
    check(isogl_symbol_pick_at(screen.x, screen.y,
                               ISOGL_PICK_DEFAULT_EXTENT,
                               picked, 64, &picked_count) == 0 &&
          picked_count > 0 && picked[0] == dia_id,
          "GPU pick returns the diameter ID");

    check(isogl_world_to_screen(205.0, 100.0, &screen) == 0,
          "radius line maps to screen");
    picked_count = 0;
    check(isogl_symbol_pick_at(screen.x, screen.y,
                               ISOGL_PICK_DEFAULT_EXTENT,
                               picked, 64, &picked_count) == 0 &&
          picked_count > 0 && picked[0] == rad_id,
          "GPU pick returns the radius ID");

    /* ---- params_get / params_update preserve ID, pick and selection ----- */
    check(isogl_symbol_set_selected(dia_id, 1) == 0,
          "diameter selected before update");
    isogl_symbol_params *copy = NULL;
    check(isogl_symbol_params_get(dia_id, &copy) == 0 && copy != NULL,
          "params_get returns the diameter copy");
    if (copy) {
        int ti = param_index(copy, "texts");
        int pi = param_index(copy, "precision");
        check(ti >= 0 && pi >= 0, "diameter copy exposes texts/precision");
        copy->params[pi].value.integer = 2;
        /* set_text owns the string, so replace it through the API. */
        check(isogl_symbol_params_set_text(copy, (size_t)ti, 0, "D") == 0,
              "diameter copy prefix set");
        check(isogl_symbol_params_update(dia_id, copy) == 0,
              "params_update regenerates the diameter in place");
        isogl_symbol_params_free(copy);
    }
    check(isogl_symbol_get_selected(dia_id) == 1,
          "params_update preserves the diameter selection");
    isogl_box dia_after;
    check(isogl_symbol_bounds(dia_id, &dia_after) == 0,
          "diameter bounds survive the update");
    check(isogl_world_to_screen(105.0, 100.0, &screen) == 0,
          "diameter line still maps to screen after the update");
    picked_count = 0;
    check(isogl_symbol_pick_at(screen.x, screen.y,
                               ISOGL_PICK_DEFAULT_EXTENT,
                               picked, 64, &picked_count) == 0 &&
          picked_count > 0 && picked[0] == dia_id,
          "params_update preserves the diameter pick ID");
    dia_ref = cad_symbol_find(&doc, dia_id);
    check(dia_ref && strcmp(dia_ref->params->params[
              param_index(dia_ref->params, "texts")].value.texts[0],
              "Ø") == 0,
          "CAD copy still holds the previous prefix (registry updated)");

    /* ---- console sym info / sym set ------------------------------------- */
    IsocadConsole console;
    memset(&console, 0, sizeof(console));
    char command[ISOCAD_CONSOLE_MAX_INPUT + 1];

    snprintf(command, sizeof(command), "sym info %u", dia_id);
    check(run_command(&doc, &console, command) == 0,
          "console sym info prints the diameter parameters");
    check(console.history_count > 0, "console echoes a summary");

    snprintf(command, sizeof(command), "sym set %u texts 0 D", dia_id);
    check(run_command(&doc, &console, command) == 0,
          "console edits the diameter prefix");
    dia_ref = cad_symbol_find(&doc, dia_id);
    check(dia_ref && strcmp(dia_ref->params->params[
              param_index(dia_ref->params, "texts")].value.texts[0],
              "D") == 0,
          "text edit reaches the document copy");

    snprintf(command, sizeof(command), "sym set %u precision 2", dia_id);
    check(run_command(&doc, &console, command) == 0,
          "console edits the diameter precision");
    dia_ref = cad_symbol_find(&doc, dia_id);
    check(dia_ref && dia_ref->params->params[
              param_index(dia_ref->params, "precision")].value.integer == 2,
          "precision edit reaches the document copy");

    snprintf(command, sizeof(command), "sym set %u arrow_left open", dia_id);
    check(run_command(&doc, &console, command) == 0,
          "console edits the diameter arrow enum");
    dia_ref = cad_symbol_find(&doc, dia_id);
    check(dia_ref && dia_ref->params->params[
              param_index(dia_ref->params, "arrow_left")].value.integer ==
              ISOGL_DIM_ARROW_OPEN,
          "enum token resolves to the shared arrow value");

    snprintf(command, sizeof(command),
             "sym set %u arrow_left nonsuch", dia_id);
    check(run_command(&doc, &console, command) == -1,
          "console rejects an unknown arrow token");

    snprintf(command, sizeof(command), "sym set %u precision 2", rad_id);
    check(run_command(&doc, &console, command) == 0,
          "console edits the radius precision");
    rad_ref = cad_symbol_find(&doc, rad_id);
    check(rad_ref && rad_ref->params->params[
              param_index(rad_ref->params, "precision")].value.integer == 2,
          "radius precision reaches the document copy");

    /* Keep the diameter type alive for the round-trip comparison. */
    dia_ref = cad_symbol_find(&doc, dia_id);

    /* ---- schema-v2 writer and loader round-trip ------------------------- */
    char save_path[] = "build/test-diameter-radius-save-XXXXXX";
    int save_fd = mkstemp(save_path);
    check(save_fd >= 0, "temporary save path created");
    if (save_fd >= 0) {
        close(save_fd);
        check(cad_save_symbols_v2(save_path, &doc) == 0,
              "schema-v2 symbol section saved");
        char *section = read_text_file(save_path);
        check(section != NULL, "saved section readable");
        if (section) {
            check(strstr(section, "type = \"dimension_diameter\"") != NULL,
                  "writer emits the diameter type");
            check(strstr(section, "type = \"dimension_radius\"") != NULL,
                  "writer emits the radius type");
            check(strstr(section, "precision = 2") != NULL,
                  "writer emits the integer parameters");
            check(strstr(section, "arrow = 2") != NULL ||
                  strstr(section, "arrow_left = 1") != NULL,
                  "writer emits the enum parameters");
            check(strstr(section, "scale =") == NULL,
                  "writer omits the per-symbol SCALE");

            char full_path[] = "build/test-diameter-radius-full-XXXXXX";
            int full_fd = mkstemp(full_path);
            check(full_fd >= 0, "round-trip path created");
            if (full_fd >= 0) {
                close(full_fd);
                FILE *full = fopen(full_path, "w");
                int wrote = full != NULL;
                if (full) {
                    if (fputs(
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
                            "\n",
                            full) < 0 ||
                        fputs(section, full) < 0) {
                        wrote = 0;
                    }
                    if (fclose(full) != 0) wrote = 0;
                }
                check(wrote, "round-trip drawing composed");

                if (wrote) {
                    isogl_segment_list rt_segments;
                    memset(&rt_segments, 0, sizeof(rt_segments));
                    isogl_string_list rt_strings = {0};
                    DrawingSheetInfo rt_sheet;
                    DrawingSymbols rt_symbols;
                    memset(&rt_symbols, 0, sizeof(rt_symbols));
                    int rt_ok =
                        isogl_segment_list_init(&rt_segments, 64) == 0;
                    if (rt_ok) {
                        rt_ok = drawing_load(full_path, font, &rt_segments,
                                             &rt_strings, &rt_sheet,
                                             &rt_symbols) == 0;
                    }
                    check(rt_ok, "v2 loader reads the written symbols");
                    check(rt_symbols.count == 2,
                          "round-trip keeps both symbol entries");

                    for (int i = 0; i < 2 && rt_ok; ++i) {
                        const char *wanted =
                            i == 0 ? "dimension_diameter"
                                   : "dimension_radius";
                        int index = -1;
                        for (size_t j = 0; j < rt_symbols.count; ++j) {
                            if (rt_symbols.items[j].type &&
                                strcmp(rt_symbols.items[j].type,
                                       wanted) == 0) {
                                index = (int)j;
                            }
                        }
                        check(index >= 0, i == 0
                                  ? "round-trip resolves the diameter"
                                  : "round-trip resolves the radius");
                        if (index < 0) continue;

                        const isogl_symbol_params *rt =
                            rt_symbols.items[index].params;
                        int rp = param_index(rt, "points");
                        int rt_texts = param_index(rt, "texts");
                        int rpr = param_index(rt, "precision");
                        check(rp >= 0 && rt->params[rp].count == 3 &&
                              near(rt->params[rp].value.points[0].x,
                                   i == 0 ? 100.0 : 200.0) &&
                              near(rt->params[rp].value.points[1].x,
                                   i == 0 ? 110.0 : 210.0),
                              i == 0
                                  ? "round-trip preserves the diameter points"
                                  : "round-trip preserves the radius points");
                        check(rt_texts >= 0 &&
                              strcmp(rt->params[rt_texts].value.texts[0],
                                     i == 0 ? "D" : "R") == 0,
                              i == 0
                                  ? "round-trip preserves the diameter prefix"
                                  : "round-trip preserves the radius prefix");
                        check(rpr >= 0 &&
                              rt->params[rpr].value.integer == 2,
                              "round-trip preserves the precision");

                        isogl_symbol_id rt_id = isogl_symbol_create(
                            rt_symbols.items[index].type,
                            rt_symbols.items[index].params, 1.0);
                        check(rt_id != 0, i == 0
                                  ? "round-trip diameter symbol created"
                                  : "round-trip radius symbol created");
                        isogl_box rt_bounds;
                        isogl_box live_bounds;
                        isogl_symbol_id live = i == 0 ? dia_id : rad_id;
                        check(rt_id != 0 &&
                              isogl_symbol_bounds(rt_id, &rt_bounds) == 0 &&
                              isogl_symbol_bounds(live, &live_bounds) == 0 &&
                              near(rt_bounds.xmin, live_bounds.xmin) &&
                              near(rt_bounds.ymin, live_bounds.ymin) &&
                              near(rt_bounds.xmax, live_bounds.xmax) &&
                              near(rt_bounds.ymax, live_bounds.ymax),
                              i == 0
                                  ? "round-trip diameter bounds match"
                                  : "round-trip radius bounds match");
                        if (rt_id) isogl_symbol_destroy(rt_id);
                    }
                    drawing_symbols_free(&rt_symbols);
                    isogl_segment_list_free(&rt_segments);
                    isogl_string_list_free(&rt_strings);
                }
                unlink(full_path);
            }
            free(section);
        }
        unlink(save_path);
    }

    /* ---- removal -------------------------------------------------------- */
    check(cad_remove_symbol(&doc, dia_id) == 0, "diameter removed");
    check(cad_remove_symbol(&doc, rad_id) == 0, "radius removed");
    check(doc.symbol_count == 0, "removal clears both references");
    check(cad_symbol_find(&doc, dia_id) == NULL &&
          cad_symbol_find(&doc, rad_id) == NULL,
          "removed references are no longer found");
    isogl_box dead = {-1.0, -1.0, 100.0, 200.0};
    check(isogl_symbol_hit(dia_id, &dead, 0) == -1 &&
          isogl_symbol_hit(rad_id, &dead, 0) == -1,
          "removed symbol IDs are dead in the registry");

    cad_document_free(&doc);
    isogl_symbol_cleanup();
    isogl_font_free_data(font);
    isogl_cleanup();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASSED: diameter and radius tests\n");
        return 0;
    }
    printf("FAILED: %d diameter/radius check(s)\n", failures);
    return 1;
}
