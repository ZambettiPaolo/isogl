#define _POSIX_C_SOURCE 200809L

#include <isogl/isogl.h>
#include <isogl/glad.h>
#include <SDL2/SDL.h>

#include "cad.h"
#include "cad_commands.h"
#include "console.h"
#include "console_cmd.h"

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

    SDL_Window *window = SDL_CreateWindow("isocad-cad-symbols-test", 0, 0,
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
    check(doc.symbol_count == 0, "new document has no symbols");

    /* ---- create through the existing dimension/center-mark adapters ----- */
    IsoglDimensionDesc dim;
    memset(&dim, 0, sizeof(dim));
    dim.x0 = 30.0f;
    dim.y0 = 250.0f;
    dim.x1 = 150.0f;
    dim.y1 = 250.0f;
    dim.px = 90.0f;
    dim.py = 230.0f;
    dim.distance = 20.0f;
    dim.text_offset_mm = 0.0f;
    dim.text_height_mm = 2.5f;
    dim.precision = 1;
    dim.pick_text = 1;
    dim.mark_points = 1;
    dim.arrow_left = ISOGL_DIM_ARROW_CLOSED;
    dim.arrow_right = ISOGL_DIM_ARROW_CLOSED;
    snprintf(dim.text_left, sizeof(dim.text_left), "L ");
    snprintf(dim.text_right, sizeof(dim.text_right), " mm");

    isogl_symbol_id dim_id = cad_add_dimension_symbol(&doc, 2, &dim);
    check(dim_id != 0, "dimension symbol created");
    check(doc.symbol_count == 1, "dimension reference appended");

    CadSymbolRef *dim_ref = cad_symbol_find(&doc, dim_id);
    check(dim_ref != NULL, "dimension reference found by ID");
    check(dim_ref && dim_ref->id == dim_id, "reference keeps the symbol ID");
    check(dim_ref && dim_ref->type != NULL &&
          strcmp(dim_ref->type->name, "dimension") == 0,
          "reference exposes the C type descriptor");
    check(dim_ref && dim_ref->params != NULL,
          "reference owns a parameter copy");
    check(dim_ref && dim_ref->params->type == dim_ref->type,
          "parameter copy points at the same type");
    check(dim_ref && dim_ref->params->param_count ==
          dim_ref->type->param_count,
          "parameter copy has the full schema");
    check(cad_symbol_params(dim_ref) == (dim_ref ? dim_ref->params : NULL),
          "cad_symbol_params accessor returns the owned copy");
    check(cad_symbol_find(&doc, dim_id + 1000u) == NULL,
          "unknown symbol ID is not found");

    int points = dim_ref ? param_index(dim_ref->params, "points") : -1;
    int texts = dim_ref ? param_index(dim_ref->params, "texts") : -1;
    check(points >= 0 && dim_ref->params->params[points].count == 3,
          "dimension points parameter recorded");
    check(points >= 0 &&
          fabs(dim_ref->params->params[points].value.points[0].x - 30.0) < 1e-6 &&
          fabs(dim_ref->params->params[points].value.points[1].x - 150.0) < 1e-6,
          "dimension point values recorded");
    check(texts >= 0 &&
          strcmp(dim_ref->params->params[texts].value.texts[1], " mm") == 0,
          "dimension text values recorded");

    IsoglCenterMarkDesc mark;
    memset(&mark, 0, sizeof(mark));
    mark.cx = 210.0f;
    mark.cy = 148.0f;
    mark.size_mm = 8.0;
    isogl_symbol_id mark_id = cad_add_center_mark_symbol(&doc, 0, &mark);
    check(mark_id != 0 && mark_id != dim_id, "center mark symbol created");
    check(doc.symbol_count == 2, "two symbol references");

    const CadSymbolRef *mark_ref = cad_symbol_find_const(&doc, mark_id);
    check(mark_ref && mark_ref->type &&
          strcmp(mark_ref->type->name, "center_mark") == 0,
          "center mark reference exposes its type");

    /* ---- rejected append does not leak a reference ---------------------- */
    check(cad_document_add_symbol(&doc, 0) == -1,
          "adding symbol ID 0 fails");
    check(cad_document_add_symbol(&doc, 999999u) == -1,
          "adding a dead symbol ID fails");
    check(doc.symbol_count == 2, "failed appends leave the references intact");

    /* ---- update a point through the document --------------------------- */
    check(isogl_symbol_set_selected(dim_id, 1) == 0,
          "dimension selected before parameter update");
    isogl_box before;
    check(isogl_symbol_bounds(dim_id, &before) == 0,
          "dimension bounds before update");

    isogl_symbol_params *work = NULL;
    check(isogl_symbol_params_get(dim_id, &work) == 0 && work != NULL,
          "working params copy obtained");
    if (work) {
        int wp = param_index(work, "points");
        int wt = param_index(work, "texts");
        check(wp >= 0 && wt >= 0, "working copy exposes keys");
        work->params[wp].value.points[1].x = 180.0;
        check(isogl_symbol_params_set_text(work, (size_t)wt, 1, " cm") == 0,
              "working copy text updated");
        check(cad_symbol_update_params(&doc, dim_id, work) == 0,
              "cad_symbol_update_params succeeds");
        isogl_symbol_params_free(work);
    }

    check(isogl_symbol_get_selected(dim_id) == 1,
          "params update preserves the symbol selection");

    isogl_box after;
    check(isogl_symbol_bounds(dim_id, &after) == 0 && after.xmax >= 180.0,
          "bounds follow the updated point");

    dim_ref = cad_symbol_find(&doc, dim_id);
    check(dim_ref != NULL, "reference survives the update");
    points = dim_ref ? param_index(dim_ref->params, "points") : -1;
    texts = dim_ref ? param_index(dim_ref->params, "texts") : -1;
    check(points >= 0 &&
          fabs(dim_ref->params->params[points].value.points[1].x - 180.0) < 1e-6,
          "document parameter copy refreshed after update");
    check(texts >= 0 &&
          strcmp(dim_ref->params->params[texts].value.texts[1], " cm") == 0,
          "document text copy refreshed after update");

    /* ---- rejected update keeps the previous params and geometry --------- */
    isogl_symbol_params *bad = NULL;
    check(isogl_symbol_params_get(dim_id, &bad) == 0 && bad != NULL,
          "second working copy obtained");
    if (bad) {
        int arrow = param_index(bad, "arrow_left");
        if (arrow >= 0) bad->params[arrow].value.integer = 999;
        check(cad_symbol_update_params(&doc, dim_id, bad) == -1,
              "invalid parameters are rejected");
        isogl_symbol_params_free(bad);
    }
    isogl_box after_bad;
    check(isogl_symbol_bounds(dim_id, &after_bad) == 0 &&
          fabs(after_bad.xmax - after.xmax) < 1e-6,
          "rejected update keeps the previous geometry");
    dim_ref = cad_symbol_find(&doc, dim_id);
    points = dim_ref ? param_index(dim_ref->params, "points") : -1;
    check(points >= 0 &&
          fabs(dim_ref->params->params[points].value.points[1].x - 180.0) < 1e-6,
          "rejected update keeps the previous parameter copy");

    /* ---- console command execution against CadSymbolRef ---------------- */
    IsocadConsole console;
    memset(&console, 0, sizeof(console));

    check(run_command(&doc, &console, "sym list") == 0,
          "console sym list executes");
    check(console.history_count > 0, "console sym list echoes a summary");

    char command[ISOCAD_CONSOLE_MAX_INPUT + 1];
    snprintf(command, sizeof(command), "sym info %u", dim_id);
    check(run_command(&doc, &console, command) == 0,
          "console sym info executes");

    snprintf(command, sizeof(command), "sym points %u", dim_id);
    check(run_command(&doc, &console, command) == 0,
          "console sym points executes");

    snprintf(command, sizeof(command), "sym set %u texts 1 mm", dim_id);
    check(run_command(&doc, &console, command) == 0,
          "console sym set text executes");
    dim_ref = cad_symbol_find(&doc, dim_id);
    texts = dim_ref ? param_index(dim_ref->params, "texts") : -1;
    check(texts >= 0 &&
          strcmp(dim_ref->params->params[texts].value.texts[1], "mm") == 0,
          "console sym set updates the document parameter copy");

    snprintf(command, sizeof(command), "sym set %u scale 2", dim_id);
    check(run_command(&doc, &console, command) == -1,
          "console rejects the read-only SCALE parameter");

    snprintf(command, sizeof(command), "sym set %u points 1 175 250", dim_id);
    check(run_command(&doc, &console, command) == 0,
          "console sym set point executes");
    dim_ref = cad_symbol_find(&doc, dim_id);
    points = dim_ref ? param_index(dim_ref->params, "points") : -1;
    check(points >= 0 &&
          fabs(dim_ref->params->params[points].value.points[1].x - 175.0) < 1e-6,
          "console point edit reaches the symbol parameters");

    /* ---- schema-v2 save writer ----------------------------------------- */
    char save_path[] = "build/test-cad-symbols-XXXXXX";
    int save_fd = mkstemp(save_path);
    check(save_fd >= 0, "temporary save path created");
    if (save_fd >= 0) {
        close(save_fd);
        snprintf(command, sizeof(command), "save %s", save_path);
        check(run_command(&doc, &console, command) == 0,
              "console save executes");
        check(cad_save_symbols_v2(save_path, &doc) == 0,
              "symbol section saved");
        char *section = read_text_file(save_path);
        check(section != NULL, "saved section readable");
        if (section) {
            check(strstr(section, "[[symbols]]") != NULL &&
                  strstr(section, "[symbols.params]") != NULL,
                  "writer emits [[symbols]] and [symbols.params]");
            check(strstr(section, "type = \"dimension\"") != NULL &&
                  strstr(section, "type = \"center_mark\"") != NULL,
                  "writer emits both registered type names");
            check(strstr(section, "group = 0") != NULL,
                  "writer emits group = 0");
            check(strstr(section, "points = ") != NULL &&
                  strstr(section, "texts = ") != NULL,
                  "writer emits typed parameter values");
            check(strstr(section, "scale =") == NULL,
                  "writer omits the per-symbol SCALE parameter");
            free(section);
        }
        unlink(save_path);
    }
    check(cad_save_symbols_v2("/nonexistent/isocad/symbols.toml", &doc) == -1,
          "save reports an unopenable path");

    /* ---- remove bookkeeping -------------------------------------------- */
    check(cad_remove_symbol(&doc, dim_id) == 0, "dimension removed");
    check(doc.symbol_count == 1, "one reference remains after removal");
    check(cad_symbol_find(&doc, dim_id) == NULL,
          "removed reference is no longer found");
    check(doc.symbols[0].id == mark_id, "remaining reference is the center mark");
    isogl_box dead_box = {20.0, 240.0, 200.0, 270.0};
    check(isogl_symbol_hit(dim_id, &dead_box, 0) == -1,
          "removed symbol ID is dead in the registry");
    check(isogl_symbol_get_selected(dim_id) == -1,
          "removed symbol has no selection flag");
    check(cad_remove_symbol(&doc, dim_id) == -1,
          "removing the same symbol twice fails");
    check(doc.symbol_count == 1, "double removal does not change the count");

    /* ---- document free destroys the remaining symbol -------------------- */
    cad_document_free(&doc);
    check(isogl_symbol_hit(mark_id, &dead_box, 0) == -1,
          "document free destroys isogl symbols");

    isogl_symbol_cleanup();
    isogl_font_free_data(font);
    isogl_cleanup();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASSED: CAD symbol records\n");
        return 0;
    }
    printf("FAILED: %d CAD symbol check(s)\n", failures);
    return 1;
}
