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

    SDL_Window *window = SDL_CreateWindow("isocad-table-row-test", 0, 0,
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

    /* ---- type descriptor and schema ------------------------------------- */
    const isogl_symbol_type *type = isogl_symbol_find_type("table_row");
    check(type != NULL, "table_row is registered as a built-in type");
    if (!type) return 2;

    check(type->name && strcmp(type->name, "table_row") == 0,
          "type name is stable");
    check(type->label && type->label[0] != '\0' &&
          type->description && type->description[0] != '\0',
          "type exposes editor metadata");
    check(isogl_symbol_find_type("dimension") != NULL &&
          isogl_symbol_find_type("center_mark") != NULL,
          "existing built-ins remain registered");

    const isogl_symbol_param_desc *points = type_desc(type, "points");
    const isogl_symbol_param_desc *height = type_desc(type, "height_mm");
    const isogl_symbol_param_desc *text_height =
        type_desc(type, "text_height_mm");
    const isogl_symbol_param_desc *padding = type_desc(type, "padding_mm");
    const isogl_symbol_param_desc *widths = type_desc(type, "widths");
    const isogl_symbol_param_desc *alignments =
        type_desc(type, "alignments");
    const isogl_symbol_param_desc *texts = type_desc(type, "texts");
    const isogl_symbol_param_desc *pick_text = type_desc(type, "pick_text");
    const isogl_symbol_param_desc *pattern = type_desc(type, "pattern");
    const isogl_symbol_param_desc *weight = type_desc(type, "weight");
    const isogl_symbol_param_desc *layer = type_desc(type, "layer");
    const isogl_symbol_param_desc *color = type_desc(type, "color");
    const isogl_symbol_param_desc *scale = type_desc(type, "scale");

    check(points && points->kind == ISOGL_SYM_PARAM_POINTS &&
          points->min_count == 1 && points->max_count == 1,
          "points is a single required point");
    check(height && height->kind == ISOGL_SYM_PARAM_NUMBER &&
          text_height && text_height->kind == ISOGL_SYM_PARAM_NUMBER &&
          padding && padding->kind == ISOGL_SYM_PARAM_NUMBER,
          "row, text and padding sizes are NUMBER parameters");
    check(widths && widths->kind == ISOGL_SYM_PARAM_NUMBERS &&
          widths->min_count == 1 && widths->max_count == 0,
          "widths is an unbounded NUMBERS list");
    check(alignments && alignments->kind == ISOGL_SYM_PARAM_ENUM_LIST &&
          alignments->min_count == 1 && alignments->max_count == 0 &&
          alignments->enum_values && alignments->enum_count == 3,
          "alignments is an ENUM_LIST with left/center/right");
    check(alignments &&
          isogl_symbol_param_enum_value(alignments, "left", NULL) == -1,
          "enum metadata helpers reject a NULL output");
    int align_value = -1;
    check(alignments &&
          isogl_symbol_param_enum_value(alignments, "right",
                                        &align_value) == 0 &&
          align_value == 2,
          "ENUM_LIST tokens resolve against the descriptor table");
    check(texts && texts->kind == ISOGL_SYM_PARAM_TEXTS &&
          texts->min_count == 1 && texts->max_count == 0,
          "texts is an unbounded TEXTS list");
    check(pick_text && pick_text->kind == ISOGL_SYM_PARAM_BOOLEAN,
          "pick_text is a BOOLEAN");
    check(pattern && pattern->kind == ISOGL_SYM_PARAM_ENUM &&
          pattern->enum_count == LINE_TYPE_COUNT,
          "pattern carries the ISO line-type table");
    check(weight && weight->kind == ISOGL_SYM_PARAM_ENUM &&
          weight->enum_count == LINE_THICKNESS_COUNT,
          "weight carries the ISO thickness table");
    check(layer && layer->kind == ISOGL_SYM_PARAM_INTEGER &&
          color && color->kind == ISOGL_SYM_PARAM_COLOR,
          "style parameters are exposed");
    check(scale && scale->kind == ISOGL_SYM_PARAM_SCALE &&
          scale->editable == 0,
          "scale is read-only");

    /* ---- creation through the generic parameter API --------------------- */
    isogl_symbol_params *params = isogl_symbol_params_create("table_row");
    check(params != NULL, "table_row parameters created");
    if (!params) return 2;

    int wi = param_index(params, "widths");
    int ai = param_index(params, "alignments");
    int ti = param_index(params, "texts");
    int pi = param_index(params, "pick_text");
    check(wi >= 0 && ai >= 0 && ti >= 0 && pi >= 0,
          "default parameter set exposes the cell keys");
    check(params->params[wi].count == 1 &&
          params->params[wi].value.numbers[0] == 20.0,
          "default width is 20 mm");
    check(params->params[ai].count == 1 &&
          params->params[ai].value.integers[0] == 0,
          "default alignment is left");
    check(params->params[ti].count == 1 &&
          strcmp(params->params[ti].value.texts[0], "") == 0,
          "default text is empty");
    check(params->params[pi].value.boolean == 1,
          "default pick_text is true");

    check(isogl_symbol_params_resize(params, (size_t)wi, 3) == 0,
          "widths resized to three cells");
    check(isogl_symbol_params_resize(params, (size_t)ai, 3) == 0,
          "alignments resized to three cells");
    check(isogl_symbol_params_resize(params, (size_t)ti, 3) == 0,
          "texts resized to three cells");
    check(isogl_symbol_params_set_number(params, (size_t)wi, 0, 20.0) == 0 &&
          isogl_symbol_params_set_number(params, (size_t)wi, 1, 30.0) == 0 &&
          isogl_symbol_params_set_number(params, (size_t)wi, 2, 15.0) == 0,
          "NUMBERS elements set");
    check(isogl_symbol_params_set_number(params, (size_t)wi, 0, NAN) == -1,
          "NUMBERS rejects a non-finite value");
    check(isogl_symbol_params_set_integer(params, (size_t)ai, 0, 0) == 0 &&
          isogl_symbol_params_set_integer(params, (size_t)ai, 1, 1) == 0 &&
          isogl_symbol_params_set_integer(params, (size_t)ai, 2, 2) == 0,
          "ENUM_LIST elements set");
    check(isogl_symbol_params_set_integer(params, (size_t)ai, 0, 99) == -1,
          "ENUM_LIST rejects a value outside the table");
    check(isogl_symbol_params_set_text(params, (size_t)ti, 0, "AB") == 0 &&
          isogl_symbol_params_set_text(params, (size_t)ti, 1, "C") == 0 &&
          isogl_symbol_params_set_text(params, (size_t)ti, 2, "DE") == 0,
          "cell texts set");

    isogl_symbol_id id = isogl_symbol_create("table_row", params, 1.0);
    check(id != 0, "table_row symbol created from generic params");
    isogl_symbol_params_free(params);
    if (!id) return 2;

    isogl_box bounds;
    check(isogl_symbol_bounds(id, &bounds) == 0,
          "table_row bounds available");
    check(near(bounds.xmin, 0.0) && near(bounds.ymin, 0.0) &&
          near(bounds.xmax, 65.0) && near(bounds.ymax, 8.0),
          "bounds follow widths and row height");

    isogl_box all = {-1.0, -1.0, 66.0, 9.0};
    check(isogl_symbol_hit(id, &all, 0) == 1,
          "window hit contains the whole row");
    isogl_box inside = {10.0, 2.0, 12.0, 4.0};
    check(isogl_symbol_hit(id, &inside, 0) == 0,
          "window hit excludes a rectangle inside a cell");
    isogl_box edge = {10.0, -1.0, 12.0, 1.0};
    check(isogl_symbol_hit(id, &edge, 1) == 1 &&
          isogl_symbol_hit(id, &edge, 0) == 0,
          "crossing hit sees the border, window does not");

    /* ---- GPU pick ------------------------------------------------------- */
    isogl_set_viewport(&vp);
    isogl_begin_frame();
    isogl_zoom_all_sheet();
    isogl_clear(0.02f, 0.02f, 0.02f, 1.0f);
    isogl_symbol_draw_all();

    isogl_point screen;
    check(isogl_world_to_screen(20.0, 4.0, &screen) == 0,
          "cell separator maps to screen");
    uint32_t picked[64];
    size_t picked_count = 0;
    check(isogl_symbol_pick_at(screen.x, screen.y,
                               ISOGL_PICK_DEFAULT_EXTENT,
                               picked, 64, &picked_count) == 0 &&
          picked_count > 0 && picked[0] == id,
          "GPU pick returns the table_row ID");

    /* ---- params_update preserves ID, pick and selection ----------------- */
    check(isogl_symbol_set_selected(id, 1) == 0,
          "table_row selected before update");
    isogl_symbol_params *copy = NULL;
    check(isogl_symbol_params_get(id, &copy) == 0 && copy != NULL,
          "params_get returns the table_row copy");
    if (copy) {
        int cai = param_index(copy, "alignments");
        int cti = param_index(copy, "texts");
        check(cai >= 0 && cti >= 0, "copy exposes cell keys");
        copy->params[cai].value.integers[1] = 2;
        check(isogl_symbol_params_set_text(copy, (size_t)cti, 1, "ZZ") == 0,
              "copy text changed");
        check(isogl_symbol_params_update(id, copy) == 0,
              "params_update regenerates the table_row");
        isogl_symbol_params_free(copy);
    }
    check(isogl_symbol_get_selected(id) == 1,
          "params_update preserves the selection");
    isogl_box after_update;
    check(isogl_symbol_bounds(id, &after_update) == 0 &&
          near(after_update.xmax, 65.0) && near(after_update.ymax, 8.0),
          "params_update preserves the row bounds");
    size_t after_pick_count = 0;
    check(isogl_symbol_pick_at(screen.x, screen.y,
                               ISOGL_PICK_DEFAULT_EXTENT,
                               picked, 64, &after_pick_count) == 0 &&
          after_pick_count > 0 && picked[0] == id,
          "params_update preserves the pick ID");

    /* ---- document record and console commands --------------------------- */
    check(cad_document_add_symbol(&doc, id) == 0,
          "table_row reference appended to the CAD document");
    CadSymbolRef *ref = cad_symbol_find(&doc, id);
    check(ref && ref->type == type && ref->params,
          "CAD reference keeps type and parameter copy");

    IsocadConsole console;
    memset(&console, 0, sizeof(console));
    char command[ISOCAD_CONSOLE_MAX_INPUT + 1];

    snprintf(command, sizeof(command), "sym info %u", id);
    check(run_command(&doc, &console, command) == 0,
          "console sym info prints list elements");
    check(console.history_count > 0, "console echoes a summary");

    snprintf(command, sizeof(command), "sym set %u widths 1 40", id);
    check(run_command(&doc, &console, command) == 0,
          "console edits a NUMBERS element");
    ref = cad_symbol_find(&doc, id);
    wi = ref ? param_index(ref->params, "widths") : -1;
    check(wi >= 0 &&
          near(ref->params->params[wi].value.numbers[1], 40.0),
          "NUMBERS edit reaches the document copy");
    check(isogl_symbol_bounds(id, &bounds) == 0 && near(bounds.xmax, 75.0),
          "NUMBERS edit regenerates the geometry");

    snprintf(command, sizeof(command), "sym set %u alignments 2 right", id);
    check(run_command(&doc, &console, command) == 0,
          "console edits an ENUM_LIST token");
    ref = cad_symbol_find(&doc, id);
    ai = ref ? param_index(ref->params, "alignments") : -1;
    check(ai >= 0 && ref->params->params[ai].value.integers[2] == 2,
          "ENUM_LIST token resolves to its integer value");

    snprintf(command, sizeof(command), "sym set %u alignments 2 middle", id);
    check(run_command(&doc, &console, command) == -1,
          "console rejects an unknown ENUM_LIST token");

    snprintf(command, sizeof(command), "sym set %u texts 2 X", id);
    check(run_command(&doc, &console, command) == 0,
          "console edits a TEXTS element");
    ref = cad_symbol_find(&doc, id);
    ti = ref ? param_index(ref->params, "texts") : -1;
    check(ti >= 0 &&
          strcmp(ref->params->params[ti].value.texts[2], "X") == 0,
          "text edit reaches the document copy");

    snprintf(command, sizeof(command), "sym set %u widths 0 0", id);
    check(run_command(&doc, &console, command) == -1,
          "console rejects a non-positive cell width");
    ref = cad_symbol_find(&doc, id);
    wi = ref ? param_index(ref->params, "widths") : -1;
    check(wi >= 0 &&
          near(ref->params->params[wi].value.numbers[0], 20.0),
          "rejected edit keeps the previous document copy");

    /* ---- schema-v2 writer and loader round-trip ------------------------- */
    char save_path[] = "build/test-table-row-save-XXXXXX";
    int save_fd = mkstemp(save_path);
    check(save_fd >= 0, "temporary save path created");
    if (save_fd >= 0) {
        close(save_fd);
        check(cad_save_symbols_v2(save_path, &doc) == 0,
              "schema-v2 symbol section saved");
        char *section = read_text_file(save_path);
        check(section != NULL, "saved section readable");
        if (section) {
            check(strstr(section, "type = \"table_row\"") != NULL,
                  "writer emits the table_row type");
            check(strstr(section, "widths = [20.0, 40.0, 15.0]") != NULL,
                  "writer emits the NUMBERS list");
            check(strstr(section, "alignments = [0, 2, 2]") != NULL,
                  "writer emits the ENUM_LIST");
            check(strstr(section, "texts = [\"AB\", \"ZZ\", \"X\"]") != NULL,
                  "writer emits the TEXTS list");
            check(strstr(section, "scale =") == NULL,
                  "writer omits the per-symbol SCALE");

            char full_path[] = "build/test-table-row-full-XXXXXX";
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
                    check(rt_ok, "v2 loader reads the written table_row");
                    check(rt_symbols.count == 1,
                          "round-trip keeps the table_row entry");

                    int row_index = -1;
                    for (size_t i = 0; i < rt_symbols.count; ++i) {
                        if (rt_symbols.items[i].type &&
                            strcmp(rt_symbols.items[i].type,
                                   "table_row") == 0) {
                            row_index = (int)i;
                        }
                    }
                    check(row_index >= 0, "round-trip resolves table_row");
                    if (row_index >= 0) {
                        const isogl_symbol_params *rt =
                            rt_symbols.items[row_index].params;
                        int rw = param_index(rt, "widths");
                        int ra = param_index(rt, "alignments");
                        int rt_texts = param_index(rt, "texts");
                        check(rw >= 0 && rt->params[rw].count == 3 &&
                              near(rt->params[rw].value.numbers[0], 20.0) &&
                              near(rt->params[rw].value.numbers[1], 40.0) &&
                              near(rt->params[rw].value.numbers[2], 15.0),
                              "round-trip preserves the NUMBERS list");
                        check(ra >= 0 && rt->params[ra].count == 3 &&
                              rt->params[ra].value.integers[0] == 0 &&
                              rt->params[ra].value.integers[1] == 2 &&
                              rt->params[ra].value.integers[2] == 2,
                              "round-trip preserves the ENUM_LIST");
                        check(rt_texts >= 0 && rt->params[rt_texts].count == 3 &&
                              strcmp(rt->params[rt_texts].value.texts[0],
                                     "AB") == 0 &&
                              strcmp(rt->params[rt_texts].value.texts[1],
                                     "ZZ") == 0 &&
                              strcmp(rt->params[rt_texts].value.texts[2],
                                     "X") == 0,
                              "round-trip preserves the TEXTS list");
                        check(rt->params[param_index(rt, "pick_text")]
                                  .value.boolean == 1,
                              "round-trip preserves pick_text");

                        isogl_symbol_id rt_id = isogl_symbol_create(
                            rt_symbols.items[row_index].type,
                            rt_symbols.items[row_index].params, 1.0);
                        check(rt_id != 0, "round-trip symbol created");
                        isogl_box rt_bounds;
                        check(rt_id != 0 &&
                              isogl_symbol_bounds(rt_id, &rt_bounds) == 0 &&
                              near(rt_bounds.xmin, bounds.xmin) &&
                              near(rt_bounds.xmax, bounds.xmax) &&
                              near(rt_bounds.ymin, bounds.ymin) &&
                              near(rt_bounds.ymax, bounds.ymax),
                              "round-trip bounds match the live symbol");
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
    check(cad_remove_symbol(&doc, id) == 0, "table_row removed");
    check(doc.symbol_count == 0, "removal clears the reference");
    check(cad_symbol_find(&doc, id) == NULL,
          "removed reference is no longer found");
    isogl_box dead = {-1.0, -1.0, 100.0, 20.0};
    check(isogl_symbol_hit(id, &dead, 0) == -1,
          "removed symbol ID is dead in the registry");

    cad_document_free(&doc);
    isogl_symbol_cleanup();
    isogl_font_free_data(font);
    isogl_cleanup();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASSED: table row tests\n");
        return 0;
    }
    printf("FAILED: %d table row check(s)\n", failures);
    return 1;
}
