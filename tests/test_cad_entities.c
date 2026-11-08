/*
 * GL test for the CAD arc/ellipse entities: construction, derived VBOs, GPU
 * pick and rectangular selection.  The window is hidden; only the GL context
 * is needed.
 */
#include <isogl/isogl.h>
#include <isogl/glad.h>
#include <SDL2/SDL.h>

#include "cad.h"
#include "cad_geometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FB_SIZE 256
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int list_has(const uint32_t *ids, size_t count, uint32_t id) {
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) return 1;
    }
    return 0;
}

static int pick_has(CadDocument *doc, float wx, float wy, uint32_t id) {
    isogl_point screen;
    if (isogl_world_to_screen(wx, wy, &screen) != 0) return 0;

    uint32_t ids[4096];
    size_t count = 0;
    if (isogl_pick_at(&doc->parts, &doc->strings, screen.x, screen.y,
                      ISOGL_PICK_DEFAULT_EXTENT, ids,
                      sizeof(ids) / sizeof(ids[0]), &count) != 0) {
        return 0;
    }
    return list_has(ids, count, id);
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

    SDL_Window *window = SDL_CreateWindow("isocad-cad-entities-test", 0, 0,
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

    isogl_set_sheet(420.0f, 297.0f, (isogl_scale){1, 1});
    isogl_zoom_all_sheet();
    isogl_viewport vp = {0, 0, FB_SIZE, FB_SIZE};
    isogl_set_viewport(&vp);

    CadDocument doc;
    check(cad_document_init(&doc, NULL, 1.0f) == 0,
          "CAD document initialized");

    /* ---- constructors and derived VBOs ---------------------------------- */
    uint32_t arc = cad_add_arc_center_angles(
        &doc, 0, LINE_TYPE_CONTINUOUS, LINE_THICKNESS_0_5,
        200.0f, 150.0f, 40.0f, 0.0f, (float)(M_PI / 2.0));
    check(arc != 0, "center/angle arc added");
    check(doc.count == 1, "arc entity appended");
    check(doc.entities[0].type == CAD_ARC, "arc entity type");
    check(fabs(doc.entities[0].geom.arc.r - 40.0f) < 1e-5f &&
          fabs(doc.entities[0].geom.arc.a0) < 1e-6f &&
          fabs(doc.entities[0].geom.arc.sweep - (float)(M_PI / 2.0)) < 1e-6f,
          "arc analytic parameters recorded");
    check(doc.entities[0].vbo_id != 0, "arc VBO created");
    isogl_vbo_object *arc_object =
        isogl_vbo_array_get(&doc.parts, doc.entities[0].vbo_id);
    check(arc_object != NULL && arc_object->vertex_count > 0 &&
          (arc_object->vertex_count % 2) == 0,
          "arc VBO holds GL_LINES pairs");
    if (arc_object) {
        double segments = cad_chord_segment_count(
            40.0, M_PI / 2.0, CAD_CHORD_TOLERANCE_MM / doc.sheet_scale);
        check((double)arc_object->vertex_count == 2.0 * segments,
              "arc VBO uses the chord-tolerance segment count");
    }

    uint32_t ellipse = cad_add_ellipse(
        &doc, 0, LINE_TYPE_CONTINUOUS, LINE_THICKNESS_0_5,
        100.0f, 100.0f, 200.0f, 100.0f, 150.0f, 130.0f);
    check(ellipse != 0, "ellipse added from major axis and p3");
    check(doc.count == 2, "ellipse entity appended");
    check(doc.entities[1].type == CAD_ELLIPSE, "ellipse entity type");
    check(fabs(doc.entities[1].geom.ellipse.cx - 150.0f) < 1e-4f &&
          fabs(doc.entities[1].geom.ellipse.cy - 100.0f) < 1e-4f &&
          fabs(doc.entities[1].geom.ellipse.rx - 50.0f) < 1e-4f &&
          fabs(doc.entities[1].geom.ellipse.ry - 30.0f) < 1e-4f &&
          fabs(doc.entities[1].geom.ellipse.rotation) < 1e-4f,
          "ellipse analytic parameters recorded");
    check(doc.entities[1].vbo_id != 0, "ellipse VBO created");
    isogl_vbo_object *ellipse_object =
        isogl_vbo_array_get(&doc.parts, doc.entities[1].vbo_id);
    check(ellipse_object != NULL && ellipse_object->vertex_count > 0 &&
          (ellipse_object->vertex_count % 2) == 0,
          "ellipse VBO holds GL_LINES pairs");

    uint32_t arc3 = cad_add_arc_three_point(
        &doc, 0, LINE_TYPE_CONTINUOUS, LINE_THICKNESS_0_5,
        60.0f, 60.0f, 50.0f, 70.0f, 50.0f, 60.0f);
    check(arc3 != 0, "three-point arc added");
    check(doc.count == 3, "three-point arc entity appended");
    check(doc.entities[2].type == CAD_ARC &&
          fabs(doc.entities[2].geom.arc.r - 10.0f) < 1e-4f &&
          fabs(doc.entities[2].geom.arc.sweep - (float)(M_PI / 2.0)) < 1e-4f,
          "three-point arc is CCW with the solved parameters");

    /* ---- rejected constructors leave no partial entity ------------------ */
    size_t count_before = doc.count;
    check(cad_add_ellipse(&doc, 0, LINE_TYPE_CONTINUOUS, LINE_THICKNESS_0_5,
                          10.0f, 10.0f, 10.0f, 10.0f, 20.0f, 20.0f) == 0,
          "degenerate ellipse rejected");
    check(cad_add_arc_three_point(&doc, 0, LINE_TYPE_CONTINUOUS,
                                  LINE_THICKNESS_0_5,
                                  10.0f, 0.0f, 0.0f, 11.0f,
                                  0.0f, 0.0f) == 0,
          "three-point arc with end off the circle rejected");
    check(cad_add_arc_center_angles(&doc, 0, LINE_TYPE_CONTINUOUS,
                                    LINE_THICKNESS_0_5,
                                    0.0f, 0.0f, 10.0f, 1.0f, 1.0f) == 0,
          "center/angle arc with null sweep rejected");
    check(doc.count == count_before,
          "rejected constructors leave no partial entity");

    uint32_t line = cad_add_line(&doc, 0, LINE_TYPE_CONTINUOUS,
                                 LINE_THICKNESS_0_5, 0.0f, 0.0f,
                                 10.0f, 0.0f);
    check(line != 0 && arc < ellipse && ellipse < arc3 && arc3 < line,
          "successful entity IDs stay monotonic across constructors");

    /* ---- GPU pick ------------------------------------------------------- */
    /* A point on the quarter arc: centre (200,150), 45 degrees. */
    float arc_x = 200.0f + 40.0f * (float)cos(M_PI / 4.0);
    float arc_y = 150.0f + 40.0f * (float)sin(M_PI / 4.0);
    check(pick_has(&doc, arc_x, arc_y, arc), "GPU pick hits the arc");
    check(pick_has(&doc, 200.0f, 100.0f, ellipse),
          "GPU pick hits the ellipse at its major-axis vertex");
    check(pick_has(&doc, 55.0f, 65.0f, arc3),
          "GPU pick hits the three-point arc");
    check(!pick_has(&doc, 150.0f, 100.0f, ellipse),
          "GPU pick does not select the empty ellipse interior");
    check(!pick_has(&doc, 150.0f, 100.0f, arc),
          "GPU pick reports no arc away from its curve");

    /* ---- rectangular selection ----------------------------------------- */
    uint32_t contained[16];
    uint32_t hit[16];
    size_t n_contained = 0;
    size_t n_hit = 0;

    /* Window around the whole quarter arc. */
    isogl_point c0 = {190.0f, 140.0f};
    isogl_point c1 = {250.0f, 200.0f};
    cad_select_rect(&doc, c0, c1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 1 && contained[0] == arc,
          "window selection contains the arc");
    check(n_hit == 1 && hit[0] == arc,
          "crossing set matches the contained arc");

    /* Crossing rectangle over the arc's start region. */
    isogl_point x0 = {235.0f, 145.0f};
    isogl_point x1 = {255.0f, 160.0f};
    cad_select_rect(&doc, x0, x1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 0 && n_hit == 1 && hit[0] == arc,
          "crossing selection selects an arc crossing the rectangle");

    /* A window that clips part of the arc is not containment but is a hit. */
    isogl_point s0 = {230.0f, 145.0f};
    isogl_point s1 = {245.0f, 155.0f};
    cad_select_rect(&doc, s0, s1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 0 && n_hit == 1 && hit[0] == arc,
          "partially covered arc is a crossing hit, not contained");

    /* Window around the whole ellipse. */
    isogl_point e0 = {95.0f, 65.0f};
    isogl_point e1 = {205.0f, 135.0f};
    cad_select_rect(&doc, e0, e1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 1 && contained[0] == ellipse,
          "window selection contains the ellipse");
    check(n_hit == 1 && hit[0] == ellipse,
          "crossing set matches the contained ellipse");

    /* Rectangle crossing the ellipse boundary. */
    isogl_point ex0 = {195.0f, 95.0f};
    isogl_point ex1 = {215.0f, 135.0f};
    cad_select_rect(&doc, ex0, ex1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 0 && n_hit == 1 && hit[0] == ellipse,
          "crossing selection selects an ellipse crossing the rectangle");

    /* A rectangle wholly inside the ellipse selects nothing. */
    isogl_point in0 = {145.0f, 95.0f};
    isogl_point in1 = {155.0f, 105.0f};
    cad_select_rect(&doc, in0, in1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 0 && n_hit == 0,
          "rectangle inside the ellipse selects nothing");

    /* Empty region. */
    isogl_point o0 = {-200.0f, -200.0f};
    isogl_point o1 = {-150.0f, -150.0f};
    cad_select_rect(&doc, o0, o1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 0 && n_hit == 0,
          "empty rectangle selects no curve");

    /* Hidden layer filter applies to the new types. */
    doc.layers[0].visible = 0;
    cad_select_rect(&doc, c0, c1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 0 && n_hit == 0,
          "hidden layer hides the arc from selection");
    doc.layers[0].visible = 1;

    /* Hidden entity filter. */
    doc.entities[1].visible = 0;
    cad_select_rect(&doc, e0, e1, contained, 16, &n_contained,
                    hit, 16, &n_hit);
    check(n_contained == 0 && n_hit == 0,
          "hidden entity is excluded from selection");
    doc.entities[1].visible = 1;

    /* Persistent selection flags reach the entity VBO. */
    check(cad_selection_apply(&doc, &arc, 1) == 0,
          "selection applied to the arc");
    arc_object = isogl_vbo_array_get(&doc.parts, doc.entities[0].vbo_id);
    check(arc_object && arc_object->selected == 1,
          "arc VBO carries the selected flag");
    cad_selection_clear(&doc);
    check(arc_object && arc_object->selected == 0,
          "selection clear resets the arc flag");

    cad_document_free(&doc);
    isogl_cleanup();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASSED: CAD arc/ellipse entities\n");
        return 0;
    }
    printf("FAILED: %d CAD entity check(s)\n", failures);
    return 1;
}
