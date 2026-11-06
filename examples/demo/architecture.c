#include "architecture.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Colours are used in print-colour mode; in thickness-colour mode the
 * fragment shader replaces them with the ISO weight colours. */
static const float C_FRAME[4]  = {0.80f, 0.84f, 0.90f, 1.0f};
static const float C_BOX[4]    = {0.55f, 0.75f, 0.98f, 1.0f};
static const float C_SUB[4]    = {0.40f, 0.88f, 1.00f, 1.0f};
static const float C_ARROW[4]  = {0.95f, 0.78f, 0.28f, 1.0f};
static const float C_TEXT[4]   = {0.94f, 0.96f, 1.00f, 1.0f};
static const float C_MUTED[4]  = {0.62f, 0.68f, 0.78f, 1.0f};
static const float C_BOUND[4]  = {0.58f, 0.62f, 0.70f, 1.0f};
static const float C_ACCENT[4] = {0.98f, 0.55f, 0.80f, 1.0f};

#define W_THIN  LINE_THICKNESS_0_18
#define W_ARROW LINE_THICKNESS_0_25
#define W_SUB   LINE_THICKNESS_0_35
#define W_BOX   LINE_THICKNESS_0_5
#define W_FRAME LINE_THICKNESS_0_7

static isogl_vertex arch_vertex(float x, float y, float distance,
                                uint8_t weight, uint8_t pattern,
                                const float color[4]) {
    isogl_vertex v;
    memset(&v, 0, sizeof(v));
    v.position[0] = x;
    v.position[1] = y;
    v.color[0] = color[0];
    v.color[1] = color[1];
    v.color[2] = color[2];
    v.color[3] = color[3];
    v.distanceFromStart = distance;
    v.pk_thickness_pattern_layer = isogl_pack(weight, pattern, 0);
    return v;
}

static int arch_line(ArchDrawing *d, float x0, float y0, float x1, float y1,
                     uint8_t weight, uint8_t pattern, const float color[4]) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);

    isogl_vertex v0 = arch_vertex(x0, y0, 0.0f, weight, pattern, color);
    isogl_vertex v1 = arch_vertex(x1, y1, length, weight, pattern, color);
    return isogl_segment_list_push_pair(d->segments, v0, v1);
}

static int arch_rect(ArchDrawing *d, float x0, float y0,
                     float x1, float y1, uint8_t weight, uint8_t pattern,
                     const float color[4]) {
    int ok = 1;
    if (arch_line(d, x0, y0, x1, y0, weight, pattern, color) != 0) ok = 0;
    if (arch_line(d, x1, y0, x1, y1, weight, pattern, color) != 0) ok = 0;
    if (arch_line(d, x1, y1, x0, y1, weight, pattern, color) != 0) ok = 0;
    if (arch_line(d, x0, y1, x0, y0, weight, pattern, color) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int arch_box(ArchDrawing *d, float x0, float y0, float x1, float y1,
                    uint8_t weight, const float color[4]) {
    return arch_rect(d, x0, y0, x1, y1, weight,
                     LINE_TYPE_CONTINUOUS, color);
}

static int arch_arrow_head(ArchDrawing *d, float tip_x, float tip_y,
                           float dir_x, float dir_y, float length,
                           uint8_t weight, const float color[4]) {
    float norm = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (norm < 1e-6f) return 0;

    dir_x /= norm;
    dir_y /= norm;

    const float angle = 0.42f;   /* ~24 degrees */
    float ca = cosf(angle);
    float sa = sinf(angle);

    float back_x = -dir_x;
    float back_y = -dir_y;
    float left_x = back_x * ca - back_y * sa;
    float left_y = back_x * sa + back_y * ca;
    float right_x = back_x * ca + back_y * sa;
    float right_y = -back_x * sa + back_y * ca;

    int ok = 1;
    if (arch_line(d, tip_x, tip_y,
                  tip_x + left_x * length, tip_y + left_y * length,
                  weight, LINE_TYPE_CONTINUOUS, color) != 0) ok = 0;
    if (arch_line(d, tip_x, tip_y,
                  tip_x + right_x * length, tip_y + right_y * length,
                  weight, LINE_TYPE_CONTINUOUS, color) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int arch_arrow(ArchDrawing *d, float x0, float y0, float x1, float y1,
                      uint8_t weight, const float color[4]) {
    int ok = 1;
    if (arch_line(d, x0, y0, x1, y1, weight,
                  LINE_TYPE_CONTINUOUS, color) != 0) ok = 0;
    if (arch_arrow_head(d, x1, y1, x1 - x0, y1 - y0, 3.2f,
                        weight, color) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int arch_double_arrow(ArchDrawing *d, float x0, float y0,
                             float x1, float y1, uint8_t weight,
                             const float color[4]) {
    int ok = 1;
    if (arch_line(d, x0, y0, x1, y1, weight,
                  LINE_TYPE_CONTINUOUS, color) != 0) ok = 0;
    if (arch_arrow_head(d, x1, y1, x1 - x0, y1 - y0, 2.8f,
                        weight, color) != 0) ok = 0;
    if (arch_arrow_head(d, x0, y0, x0 - x1, y0 - y1, 2.8f,
                        weight, color) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int arch_path(ArchDrawing *d, const float *points, int count,
                     uint8_t weight, const float color[4]) {
    if (!points || count < 2) return -1;

    int ok = 1;
    for (int i = 0; i + 1 < count; ++i) {
        if (arch_line(d, points[2 * i], points[2 * i + 1],
                      points[2 * i + 2], points[2 * i + 3],
                      weight, LINE_TYPE_CONTINUOUS, color) != 0) {
            ok = 0;
        }
    }
    if (arch_arrow_head(d, points[2 * (count - 1)], points[2 * (count - 1) + 1],
                        points[2 * (count - 1)] - points[2 * (count - 2)],
                        points[2 * (count - 1) + 1] -
                        points[2 * (count - 2) + 1],
                        3.2f, weight, color) != 0) {
        ok = 0;
    }
    return ok ? 0 : -1;
}

static int arch_text(ArchDrawing *d, float x, float y, float height,
                     const char *text, const float color[4]) {
    if (!d->font || !text) return -1;

    isogl_glyph_string *string = isogl_create_glyph_string(text, d->font);
    if (!string) return -1;

    isogl_glyph_string_set_color(string, color);
    if (!isogl_string_list_append(d->strings, string, x, y, height,
                                  d->global_scale, 0.0f, d->font, 0)) {
        isogl_free_glyph_string(string);
        return -1;
    }
    return 0;
}

#define ARCH_PUSH(call) do { if ((call) != 0) ok = 0; } while (0)

int arch_build(ArchDrawing *drawing) {
    if (!drawing || !drawing->segments || !drawing->strings ||
        !drawing->font) {
        return -1;
    }

    ArchDrawing *d = drawing;
    int ok = 1;

    /* ---- sheet frame and title -------------------------------------- */
    ARCH_PUSH(arch_box(d, 10, 10, 410, 287, W_FRAME, C_FRAME));
    ARCH_PUSH(arch_text(d, 20, 281, 7.0f, "isogl", C_TEXT));
    ARCH_PUSH(arch_text(d, 20, 273, 5.0f, "architecture", C_ACCENT));
    ARCH_PUSH(arch_text(d, 20, 267, 2.5f,
                        "toolkit-independent 2D technical drawing renderer",
                        C_MUTED));
    ARCH_PUSH(arch_line(d, 20, 263, 400, 263, W_THIN,
                        LINE_TYPE_CONTINUOUS, C_FRAME));

    /* ---- hosts and applications ------------------------------------- */
    ARCH_PUSH(arch_rect(d, 20, 236, 400, 260, W_THIN,
                        LINE_TYPE_DASHED, C_BOUND));
    ARCH_PUSH(arch_text(d, 23, 257, 2.5f, "HOSTS / APPLICATIONS", C_MUTED));

    ARCH_PUSH(arch_box(d, 25, 240, 105, 254, W_SUB, C_BOX));
    ARCH_PUSH(arch_text(d, 31, 250, 3.5f, "isocad", C_TEXT));
    ARCH_PUSH(arch_text(d, 28, 244, 2.5f,
                        "document / kernel / UI", C_MUTED));

    ARCH_PUSH(arch_box(d, 115, 240, 175, 254, W_SUB, C_BOX));
    ARCH_PUSH(arch_text(d, 121, 250, 3.5f, "SDL2 host", C_TEXT));
    ARCH_PUSH(arch_text(d, 118, 244, 2.5f,
                        "window / GL / swap", C_MUTED));

    ARCH_PUSH(arch_box(d, 185, 240, 245, 254, W_SUB, C_BOX));
    ARCH_PUSH(arch_text(d, 196, 250, 3.5f, "GTK4", C_TEXT));
    ARCH_PUSH(arch_text(d, 190, 244, 2.5f, "GtkGLArea", C_MUTED));

    ARCH_PUSH(arch_box(d, 255, 240, 315, 254, W_SUB, C_BOX));
    ARCH_PUSH(arch_text(d, 267, 250, 3.5f, "Qt6", C_TEXT));
    ARCH_PUSH(arch_text(d, 260, 244, 2.5f, "QOpenGLWidget", C_MUTED));

    ARCH_PUSH(arch_box(d, 325, 240, 395, 254, W_SUB, C_BOX));
    ARCH_PUSH(arch_text(d, 331, 250, 3.5f, "demo / tests", C_TEXT));
    ARCH_PUSH(arch_text(d, 332, 244, 2.5f, "GL harness", C_MUTED));

    ARCH_PUSH(arch_arrow(d, 65, 240, 65, 230, W_ARROW, C_ARROW));
    ARCH_PUSH(arch_text(d, 70, 234, 2.5f, "uses libisogl", C_MUTED));
    ARCH_PUSH(arch_double_arrow(d, 350, 236, 350, 230, W_ARROW, C_ARROW));
    ARCH_PUSH(arch_text(d, 300, 233, 2.5f, "host contract", C_MUTED));

    /* ---- libisogl core boundary ------------------------------------- */
    ARCH_PUSH(arch_rect(d, 20, 98, 400, 230, W_THIN,
                        LINE_TYPE_DASHED, C_BOUND));
    ARCH_PUSH(arch_text(d, 24, 225, 5.0f,
                        "libisogl / SDL-free core", C_TEXT));
    ARCH_PUSH(arch_text(d, 24, 219, 2.5f,
                        "no toolkit types in include/isogl or src", C_MUTED));

    /* ---- renderer ---------------------------------------------------- */
    ARCH_PUSH(arch_box(d, 25, 153, 175, 212, W_BOX, C_BOX));
    ARCH_PUSH(arch_text(d, 32, 207, 5.0f, "isogl", C_TEXT));
    ARCH_PUSH(arch_text(d, 32, 200, 2.5f, "viewport / matrices", C_MUTED));
    ARCH_PUSH(arch_text(d, 32, 195, 2.5f, "sheet / segment VBO", C_MUTED));
    ARCH_PUSH(arch_text(d, 32, 190, 2.5f, "pick FBO / zoom / pan", C_MUTED));
    ARCH_PUSH(arch_text(d, 32, 185, 2.5f, "ISO 128 / ISO 3098", C_MUTED));

    ARCH_PUSH(arch_line(d, 32, 178, 75, 178, W_THIN,
                        LINE_TYPE_CONTINUOUS, C_MUTED));
    ARCH_PUSH(arch_text(d, 80, 179, 2.5f, "0.18", C_MUTED));
    ARCH_PUSH(arch_line(d, 32, 173, 75, 173, W_SUB,
                        LINE_TYPE_DASHED, C_MUTED));
    ARCH_PUSH(arch_text(d, 80, 174, 2.5f, "0.35 dashed", C_MUTED));
    ARCH_PUSH(arch_line(d, 32, 168, 75, 168, W_FRAME,
                        LINE_TYPE_LONG_DASH_DOT, C_MUTED));
    ARCH_PUSH(arch_text(d, 80, 169, 2.5f, "0.70 dash-dot", C_MUTED));

    /* ---- font and style --------------------------------------------- */
    ARCH_PUSH(arch_box(d, 190, 192, 275, 212, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 197, 207, 3.5f, "isogl_font", C_TEXT));
    ARCH_PUSH(arch_text(d, 197, 201, 2.5f, "TOML vector font", C_MUTED));
    ARCH_PUSH(arch_text(d, 197, 196, 2.5f, "glyph strings / VBO", C_MUTED));

    ARCH_PUSH(arch_box(d, 285, 192, 370, 212, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 292, 207, 3.5f, "isogl_style", C_TEXT));
    ARCH_PUSH(arch_text(d, 292, 201, 2.5f, "ISO tables", C_MUTED));
    ARCH_PUSH(arch_text(d, 292, 196, 2.5f, "weights / patterns", C_MUTED));

    ARCH_PUSH(arch_arrow(d, 190, 202, 175, 202, W_ARROW, C_ARROW));

    const float style_path[] = {
        285, 197,
        280, 188,
        175, 188
    };
    ARCH_PUSH(arch_path(d, style_path, 3, W_ARROW, C_ARROW));

    /* ---- symbols ----------------------------------------------------- */
    ARCH_PUSH(arch_box(d, 190, 163, 290, 184, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 196, 179, 3.5f,
                        "isogl_symbol_geometry", C_TEXT));
    ARCH_PUSH(arch_text(d, 196, 171, 2.5f,
                        "pure geometry, no GL", C_MUTED));

    ARCH_PUSH(arch_box(d, 300, 163, 395, 184, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 306, 179, 3.5f,
                        "isogl_symbol registry", C_TEXT));
    ARCH_PUSH(arch_text(d, 306, 171, 2.5f,
                        "VBO / text / hit-pick", C_MUTED));
    ARCH_PUSH(arch_text(d, 306, 166, 2.5f, "explode", C_MUTED));

    ARCH_PUSH(arch_arrow(d, 290, 174, 300, 174, W_ARROW, C_ARROW));

    const float registry_path[] = {
        300, 163,
        300, 156,
        175, 156
    };
    ARCH_PUSH(arch_path(d, registry_path, 3, W_ARROW, C_ARROW));

    /* ---- overlay and panels ----------------------------------------- */
    ARCH_PUSH(arch_box(d, 25, 108, 175, 146, W_BOX, C_BOX));
    ARCH_PUSH(arch_text(d, 32, 141, 5.0f, "isogl_overlay", C_TEXT));
    ARCH_PUSH(arch_text(d, 32, 134, 2.5f,
                        "groups: quads, lines, text", C_MUTED));
    ARCH_PUSH(arch_text(d, 32, 129, 2.5f,
                        "retained, dirty per group", C_MUTED));
    ARCH_PUSH(arch_text(d, 32, 124, 2.5f,
                        "clip per vertex / string", C_MUTED));
    ARCH_PUSH(arch_text(d, 32, 119, 2.5f,
                        "UI state, not document", C_MUTED));

    ARCH_PUSH(arch_box(d, 190, 113, 275, 141, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 197, 136, 3.5f, "isogl_panel", C_TEXT));
    ARCH_PUSH(arch_text(d, 197, 130, 2.5f, "edges / content rect", C_MUTED));
    ARCH_PUSH(arch_text(d, 197, 125, 2.5f, "strip quad emit", C_MUTED));

    ARCH_PUSH(arch_arrow(d, 190, 127, 175, 127, W_ARROW, C_ARROW));

    /* ---- core to GPU ------------------------------------------------- */
    const float renderer_path[] = {
        175, 158,
        180, 158,
        180, 92
    };
    ARCH_PUSH(arch_path(d, renderer_path, 3, W_ARROW, C_ARROW));
    ARCH_PUSH(arch_text(d, 185, 120, 2.5f, "GL calls", C_MUTED));

    const float overlay_path[] = {
        100, 108,
        100, 92
    };
    ARCH_PUSH(arch_path(d, overlay_path, 2, W_ARROW, C_ARROW));
    ARCH_PUSH(arch_text(d, 105, 100, 2.5f, "overlay pass", C_MUTED));

    /* ---- GPU / VRAM -------------------------------------------------- */
    ARCH_PUSH(arch_rect(d, 20, 60, 400, 92, W_THIN,
                        LINE_TYPE_DASHED, C_BOUND));
    ARCH_PUSH(arch_text(d, 24, 88, 3.5f,
                        "GPU / VRAM - OpenGL 3.3 core", C_TEXT));

    ARCH_PUSH(arch_box(d, 30, 64, 105, 82, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 36, 77, 3.5f, "VBO / VAO", C_TEXT));
    ARCH_PUSH(arch_text(d, 34, 71, 2.5f, "segment pages", C_MUTED));
    ARCH_PUSH(arch_text(d, 34, 67, 2.5f, "glyph strings", C_MUTED));

    ARCH_PUSH(arch_box(d, 115, 64, 235, 82, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 122, 77, 3.5f, "GLSL 330", C_TEXT));
    ARCH_PUSH(arch_text(d, 120, 71, 2.5f, "segment / sheet", C_MUTED));
    ARCH_PUSH(arch_text(d, 120, 67, 2.5f,
                        "font / overlay / pick", C_MUTED));

    ARCH_PUSH(arch_box(d, 245, 64, 315, 82, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 252, 77, 3.5f, "pick FBO", C_TEXT));
    ARCH_PUSH(arch_text(d, 250, 71, 2.5f, "GL_R32UI", C_MUTED));
    ARCH_PUSH(arch_text(d, 250, 67, 2.5f, "16 x 16", C_MUTED));

    ARCH_PUSH(arch_box(d, 325, 64, 395, 82, W_SUB, C_SUB));
    ARCH_PUSH(arch_text(d, 335, 77, 3.5f, "raster", C_TEXT));
    ARCH_PUSH(arch_text(d, 330, 71, 2.5f, "blend / clip", C_MUTED));
    ARCH_PUSH(arch_text(d, 330, 67, 2.5f, "dashes / no MSAA", C_MUTED));

    /* ---- legend ------------------------------------------------------ */
    ARCH_PUSH(arch_box(d, 20, 18, 190, 52, W_SUB, C_BOX));
    ARCH_PUSH(arch_text(d, 24, 47, 3.5f, "LEGEND", C_TEXT));
    ARCH_PUSH(arch_line(d, 24, 40, 60, 40, W_BOX,
                        LINE_TYPE_CONTINUOUS, C_MUTED));
    ARCH_PUSH(arch_text(d, 65, 41, 2.5f, "data / dependency", C_MUTED));
    ARCH_PUSH(arch_line(d, 24, 34, 60, 34, W_THIN,
                        LINE_TYPE_DASHED, C_MUTED));
    ARCH_PUSH(arch_text(d, 65, 35, 2.5f,
                        "module / host boundary", C_MUTED));
    ARCH_PUSH(arch_arrow(d, 24, 28, 60, 28, W_ARROW, C_ARROW));
    ARCH_PUSH(arch_text(d, 65, 29, 2.5f, "flow / uses", C_MUTED));
    ARCH_PUSH(arch_text(d, 24, 21, 2.5f,
                        "all geometry and text rendered by isogl", C_MUTED));

    /* ---- title block ------------------------------------------------- */
    ARCH_PUSH(arch_box(d, 260, 18, 400, 52, W_BOX, C_BOX));
    ARCH_PUSH(arch_text(d, 266, 47, 3.5f,
                        "isogl / architecture", C_TEXT));
    ARCH_PUSH(arch_line(d, 262, 43, 398, 43, W_THIN,
                        LINE_TYPE_CONTINUOUS, C_MUTED));
    ARCH_PUSH(arch_text(d, 266, 38, 2.5f, "A3, 1:1, mm", C_MUTED));
    ARCH_PUSH(arch_text(d, 266, 32, 2.5f,
                        "ISO 128 / ISO 3098", C_MUTED));
    ARCH_PUSH(arch_text(d, 266, 26, 2.5f, "demo drawing", C_MUTED));

    return ok ? 0 : -1;
}
