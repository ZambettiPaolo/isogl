#include "panels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float PANEL_BG[4]        = {0.030f, 0.039f, 0.047f, 1.0f};
static const float PANEL_EDGE[4]      = {0.20f, 0.24f, 0.30f, 1.0f};
static const float PANEL_HOVER_BG[4]  = {0.085f, 0.105f, 0.135f, 1.0f};
static const float ICON_COLOR[4]      = {0.62f, 0.70f, 0.78f, 1.0f};
static const float ICON_HOVER_COLOR[4]= {0.98f, 0.99f, 1.00f, 1.0f};

/* Icon art lives in a [-1, 1] square with y pointing down, like the layout
 * coordinates.  sink_point() converts it to framebuffer pixels with the GL
 * origin at the bottom-left. */
typedef struct {
    float *v;
    size_t count;       /* floats, two per vertex */
    size_t capacity;    /* floats */
    float cx, cy;       /* icon centre, framebuffer px, origin top-left */
    float s;            /* art half-size in px */
    float sx, sy;       /* mirror, 1 or -1 */
    int fb_h;
    int ok;
} IconSink;

static void sink_point(IconSink *s, float x, float y) {
    if (!s->ok) return;

    if (s->count + 2 > s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 512;
        while (capacity < s->count + 2) {
            if (capacity > (size_t)-1 / 2) {
                s->ok = 0;
                return;
            }
            capacity *= 2;
        }
        float *v = realloc(s->v, capacity * sizeof(float));
        if (!v) {
            s->ok = 0;
            return;
        }
        s->v = v;
        s->capacity = capacity;
    }

    float px = s->cx + x * s->sx * s->s;
    float py = s->cy + y * s->sy * s->s;
    s->v[s->count++] = px;
    s->v[s->count++] = (float)s->fb_h - py;
}

static void ile_seg(IconSink *s, float x0, float y0, float x1, float y1) {
    sink_point(s, x0, y0);
    sink_point(s, x1, y1);
}

static void ile_rect(IconSink *s, float x0, float y0, float x1, float y1) {
    ile_seg(s, x0, y0, x1, y0);
    ile_seg(s, x1, y0, x1, y1);
    ile_seg(s, x1, y1, x0, y1);
    ile_seg(s, x0, y1, x0, y0);
}

static void ile_poly(IconSink *s, const float *xy, int count, int closed) {
    for (int i = 0; i + 1 < count; ++i) {
        ile_seg(s, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3]);
    }
    if (closed && count > 2) {
        ile_seg(s, xy[2 * (count - 1)], xy[2 * (count - 1) + 1], xy[0], xy[1]);
    }
}

static void ile_circle(IconSink *s, float cx, float cy, float r, int segments) {
    float px = cx + r;
    float py = cy;
    for (int i = 1; i <= segments; ++i) {
        float a = 2.0f * (float)M_PI * (float)i / (float)segments;
        float x = cx + r * cosf(a);
        float y = cy + r * sinf(a);
        ile_seg(s, px, py, x, y);
        px = x;
        py = y;
    }
}

static void ile_cross(IconSink *s, float cx, float cy, float r) {
    ile_seg(s, cx - r, cy, cx + r, cy);
    ile_seg(s, cx, cy - r, cx, cy + r);
}

/* ---- top strip: document and history ----------------------------------- */

static void icon_new(IconSink *s) {
    ile_rect(s, -0.60f, -0.80f, 0.55f, 0.80f);
    ile_seg(s, 0.15f, -0.80f, 0.55f, -0.40f);
    ile_seg(s, 0.15f, -0.80f, 0.15f, -0.40f);
    ile_seg(s, 0.15f, -0.40f, 0.55f, -0.40f);
    ile_seg(s, -0.35f, 0.10f, 0.30f, 0.10f);
    ile_seg(s, -0.35f, 0.40f, 0.30f, 0.40f);
}

static void icon_open(IconSink *s) {
    static const float folder[] = {
        -0.80f, -0.55f, -0.25f, -0.55f, -0.05f, -0.80f,
         0.80f, -0.80f,  0.80f,  0.65f, -0.80f,  0.65f
    };
    ile_poly(s, folder, 6, 1);
    ile_seg(s, -0.80f, 0.30f, 0.80f, 0.30f);
}

static void icon_save(IconSink *s) {
    ile_rect(s, -0.75f, -0.80f, 0.75f, 0.80f);
    ile_rect(s, -0.35f, -0.80f, 0.35f, -0.20f);
    ile_rect(s, -0.50f, 0.15f, 0.50f, 0.80f);
}

static void icon_undo_common(IconSink *s) {
    ile_seg(s, -0.75f,  0.10f, -0.60f, -0.35f);
    ile_seg(s, -0.60f, -0.35f, -0.25f, -0.65f);
    ile_seg(s, -0.25f, -0.65f,  0.20f, -0.60f);
    ile_seg(s,  0.20f, -0.60f,  0.60f, -0.25f);
    ile_seg(s,  0.60f, -0.25f,  0.70f,  0.25f);
    ile_seg(s, -0.75f,  0.10f, -0.30f,  0.00f);
    ile_seg(s, -0.75f,  0.10f, -0.45f, -0.35f);
}

static void icon_undo(IconSink *s) {
    icon_undo_common(s);
}

static void icon_redo(IconSink *s) {
    float saved = s->sx;
    s->sx = -saved;
    icon_undo_common(s);
    s->sx = saved;
}

static void icon_print(IconSink *s) {
    ile_rect(s, -0.45f, -0.80f, 0.45f, -0.30f);
    ile_rect(s, -0.80f, -0.30f, 0.80f, 0.35f);
    ile_rect(s, -0.45f, 0.35f, 0.45f, 0.80f);
    ile_seg(s, -0.65f, -0.05f, -0.45f, -0.05f);
}

/* ---- left strip: drawing tools ----------------------------------------- */

static void icon_select(IconSink *s) {
    static const float cursor[] = {
        -0.55f, -0.80f, -0.55f, 0.60f, -0.25f, 0.30f,
         0.00f,  0.80f,  0.30f, 0.65f,  0.05f, 0.20f,
         0.50f,  0.15f
    };
    ile_poly(s, cursor, 7, 1);
}

static void icon_line(IconSink *s) {
    ile_seg(s, -0.70f, 0.65f, 0.70f, -0.65f);
    ile_seg(s, -0.85f, 0.50f, -0.55f, 0.80f);
    ile_seg(s,  0.55f, -0.80f, 0.85f, -0.50f);
}

static void icon_circle(IconSink *s) {
    ile_circle(s, 0.0f, 0.0f, 0.70f, 24);
}

static void icon_rectangle(IconSink *s) {
    ile_rect(s, -0.75f, -0.60f, 0.75f, 0.60f);
}

static void icon_polyline(IconSink *s) {
    static const float path[] = {
        -0.80f, 0.60f, -0.35f, -0.50f, 0.10f, 0.25f, 0.80f, -0.60f
    };
    ile_poly(s, path, 4, 0);
    for (int i = 0; i < 4; ++i) {
        ile_cross(s, path[2 * i], path[2 * i + 1], 0.10f);
    }
}

static void icon_text(IconSink *s) {
    ile_seg(s, -0.70f, -0.65f, 0.70f, -0.65f);
    ile_seg(s,  0.00f, -0.65f, 0.00f, 0.75f);
    ile_seg(s, -0.35f,  0.75f, 0.35f, 0.75f);
}

static void icon_dimension(IconSink *s) {
    ile_seg(s, -0.85f, 0.80f, -0.85f, 0.00f);
    ile_seg(s,  0.85f, 0.80f,  0.85f, 0.00f);
    ile_seg(s, -0.85f, 0.00f,  0.85f, 0.00f);
    ile_seg(s, -0.85f, 0.00f, -0.50f, -0.22f);
    ile_seg(s, -0.85f, 0.00f, -0.50f,  0.22f);
    ile_seg(s,  0.85f, 0.00f,  0.50f, -0.22f);
    ile_seg(s,  0.85f, 0.00f,  0.50f,  0.22f);
    ile_seg(s, -0.85f, 0.80f, -0.70f, 0.80f);
    ile_seg(s,  0.70f, 0.80f,  0.85f, 0.80f);
}

static void icon_center(IconSink *s) {
    ile_circle(s, 0.0f, 0.0f, 0.55f, 20);
    ile_seg(s, -0.85f, 0.0f, 0.85f, 0.0f);
    ile_seg(s, 0.0f, -0.85f, 0.0f, 0.85f);
}

/* ---- right strip: view and display ------------------------------------- */

static void icon_zoom_common(IconSink *s, int plus) {
    ile_circle(s, -0.15f, -0.15f, 0.55f, 20);
    ile_seg(s, 0.25f, 0.25f, 0.85f, 0.85f);
    ile_seg(s, -0.50f, -0.15f, 0.20f, -0.15f);
    if (plus) ile_seg(s, -0.15f, -0.50f, -0.15f, 0.20f);
}

static void icon_zoom_in(IconSink *s) {
    icon_zoom_common(s, 1);
}

static void icon_zoom_out(IconSink *s) {
    icon_zoom_common(s, 0);
}

static void icon_pan(IconSink *s) {
    ile_seg(s, -0.85f, 0.00f, 0.85f, 0.00f);
    ile_seg(s,  0.00f, -0.85f, 0.00f, 0.85f);
    ile_seg(s, -0.85f, 0.00f, -0.55f, -0.20f);
    ile_seg(s, -0.85f, 0.00f, -0.55f,  0.20f);
    ile_seg(s,  0.85f, 0.00f,  0.55f, -0.20f);
    ile_seg(s,  0.85f, 0.00f,  0.55f,  0.20f);
    ile_seg(s,  0.00f, -0.85f, -0.20f, -0.55f);
    ile_seg(s,  0.00f, -0.85f,  0.20f, -0.55f);
    ile_seg(s,  0.00f, 0.85f, -0.20f,  0.55f);
    ile_seg(s,  0.00f, 0.85f,  0.20f,  0.55f);
}

static void icon_fit(IconSink *s) {
    ile_seg(s, -0.80f, -0.80f, -0.40f, -0.80f);
    ile_seg(s, -0.80f, -0.80f, -0.80f, -0.40f);
    ile_seg(s,  0.80f, -0.80f,  0.40f, -0.80f);
    ile_seg(s,  0.80f, -0.80f,  0.80f, -0.40f);
    ile_seg(s, -0.80f,  0.80f, -0.40f,  0.80f);
    ile_seg(s, -0.80f,  0.80f, -0.80f,  0.40f);
    ile_seg(s,  0.80f,  0.80f,  0.40f,  0.80f);
    ile_seg(s,  0.80f,  0.80f,  0.80f,  0.40f);
    ile_rect(s, -0.45f, -0.35f, 0.45f, 0.35f);
}

static void icon_grid(IconSink *s) {
    ile_rect(s, -0.75f, -0.75f, 0.75f, 0.75f);
    ile_seg(s, 0.00f, -0.75f, 0.00f, 0.75f);
    ile_seg(s, -0.75f, 0.00f, 0.75f, 0.00f);
}

static void icon_snap(IconSink *s) {
    ile_seg(s, -0.65f, -0.70f, -0.65f, 0.10f);
    ile_seg(s, -0.65f,  0.10f, -0.35f, 0.45f);
    ile_seg(s, -0.35f,  0.45f,  0.35f, 0.45f);
    ile_seg(s,  0.35f,  0.45f,  0.65f, 0.10f);
    ile_seg(s,  0.65f,  0.10f,  0.65f, -0.70f);
    ile_seg(s, -0.25f, -0.70f, -0.25f, 0.05f);
    ile_seg(s, -0.25f,  0.05f, -0.10f, 0.20f);
    ile_seg(s, -0.10f,  0.20f,  0.10f, 0.20f);
    ile_seg(s,  0.10f,  0.20f,  0.25f, 0.05f);
    ile_seg(s,  0.25f,  0.05f,  0.25f, -0.70f);
    ile_seg(s, -0.65f, -0.70f, -0.25f, -0.70f);
    ile_seg(s,  0.25f, -0.70f,  0.65f, -0.70f);
}

static void icon_thickness(IconSink *s) {
    ile_seg(s, -0.80f, -0.58f, 0.80f, -0.58f);
    ile_seg(s, -0.80f, -0.44f, 0.80f, -0.44f);
    ile_seg(s, -0.80f, -0.08f, 0.80f, -0.08f);
    ile_seg(s, -0.80f,  0.04f, 0.80f,  0.04f);
    ile_seg(s, -0.80f,  0.50f, 0.80f,  0.50f);
}

/* ---- registry ----------------------------------------------------------- */

typedef enum {
    PANEL_TOP = 0,
    PANEL_LEFT,
    PANEL_RIGHT
} PanelSide;

typedef struct {
    PanelSide side;
    const char *name;
    void (*emit)(IconSink *);
} IconDef;

static const IconDef ICON_DEFS[] = {
    {PANEL_TOP,   "new",        icon_new},
    {PANEL_TOP,   "open",       icon_open},
    {PANEL_TOP,   "save",       icon_save},
    {PANEL_TOP,   "undo",       icon_undo},
    {PANEL_TOP,   "redo",       icon_redo},
    {PANEL_TOP,   "print",      icon_print},

    {PANEL_LEFT,  "select",     icon_select},
    {PANEL_LEFT,  "line",       icon_line},
    {PANEL_LEFT,  "circle",     icon_circle},
    {PANEL_LEFT,  "rectangle",  icon_rectangle},
    {PANEL_LEFT,  "polyline",   icon_polyline},
    {PANEL_LEFT,  "text",       icon_text},
    {PANEL_LEFT,  "dimension",  icon_dimension},
    {PANEL_LEFT,  "center",     icon_center},

    {PANEL_RIGHT, "zoom_in",    icon_zoom_in},
    {PANEL_RIGHT, "zoom_out",   icon_zoom_out},
    {PANEL_RIGHT, "pan",        icon_pan},
    {PANEL_RIGHT, "fit",        icon_fit},
    {PANEL_RIGHT, "grid",       icon_grid},
    {PANEL_RIGHT, "snap",       icon_snap},
    {PANEL_RIGHT, "thickness",  icon_thickness}
};

#define ICON_DEF_COUNT (sizeof(ICON_DEFS) / sizeof(ICON_DEFS[0]))

/* ---- GL resources ------------------------------------------------------- */

static GLuint panels_make_program(void) {
    static const char *vs_source =
        "#version 330 core\n"
        "layout(location = 0) in vec2 position;\n"
        "uniform mat4 uView;\n"
        "void main() {\n"
        "    gl_Position = uView * vec4(position, 0.0, 1.0);\n"
        "}\n";
    static const char *fs_source =
        "#version 330 core\n"
        "uniform vec4 uColor;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = uColor;\n"
        "}\n";

    GLuint vs = isogl_compile_shader(GL_VERTEX_SHADER, (char *)vs_source);
    GLuint fs = isogl_compile_shader(GL_FRAGMENT_SHADER, (char *)fs_source);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!linked) {
        char log[512] = {0};
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "isocad panels: link error: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

int isocad_panels_init(IsocadPanels *p) {
    if (!p) return -1;

    memset(p, 0, sizeof(*p));
    p->hover = -1;
    p->program = panels_make_program();
    if (p->program == 0) return -1;

    p->view_loc = glGetUniformLocation(p->program, "uView");
    p->color_loc = glGetUniformLocation(p->program, "uColor");

    glGenVertexArrays(1, &p->vao);
    glGenBuffers(1, &p->vbo);
    glBindVertexArray(p->vao);
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                          (void *)0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return 0;
}

void isocad_panels_free(IsocadPanels *p) {
    if (!p) return;

    if (p->vbo) glDeleteBuffers(1, &p->vbo);
    if (p->vao) glDeleteVertexArrays(1, &p->vao);
    if (p->program) glDeleteProgram(p->program);

    memset(p, 0, sizeof(*p));
    p->hover = -1;
}

/* ---- layout and geometry ----------------------------------------------- */

static void panels_build(IsocadPanels *p) {
    IconSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.fb_h = p->fb_h;
    sink.sx = 1.0f;
    sink.sy = 1.0f;
    sink.ok = 1;

    const float cell = (float)p->thickness_px;
    const float half = cell * 0.5f;
    const float art = cell * (14.0f / ISOCAD_PANEL_LOGICAL_THICKNESS) * 0.5f;

    int slot[3] = {0, 0, 0};
    p->icon_count = 0;

    for (size_t i = 0; i < ICON_DEF_COUNT; ++i) {
        const IconDef *def = &ICON_DEFS[i];
        if (p->icon_count >= ISOCAD_PANEL_MAX_ICONS) break;

        IsocadPanelIcon *icon = &p->icons[p->icon_count];
        int index = slot[def->side];

        if (def->side == PANEL_TOP) {
            if (p->insets.top <= 0) continue;
            icon->cx = half + (float)index * cell;
            icon->cy = half;
            if (icon->cx + half > (float)p->fb_w) continue;
        } else if (def->side == PANEL_LEFT) {
            if (p->insets.left <= 0) continue;
            icon->cx = half;
            icon->cy = (float)p->insets.top + half + (float)index * cell;
            if (icon->cy + half > (float)(p->fb_h - p->insets.bottom)) continue;
        } else {
            if (p->insets.right <= 0) continue;
            icon->cx = (float)p->fb_w - half;
            icon->cy = (float)p->insets.top + half + (float)index * cell;
            if (icon->cy + half > (float)(p->fb_h - p->insets.bottom)) continue;
        }

        slot[def->side] = index + 1;

        icon->name = def->name;
        icon->half = half;
        icon->first = (GLint)(sink.count / 2);

        sink.cx = icon->cx;
        sink.cy = icon->cy;
        sink.s = art;
        def->emit(&sink);
        if (!sink.ok) break;

        icon->count = (GLsizei)(sink.count / 2) - icon->first;
        p->icon_count++;
    }

    p->vertex_count = (GLsizei)(sink.count / 2);
    p->hover = -1;

    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    if (sink.ok && sink.count > 0) {
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(sink.count * sizeof(float)),
                     sink.v, GL_DYNAMIC_DRAW);
    } else {
        glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
        p->vertex_count = 0;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    free(sink.v);
    p->built = 1;
}

void isocad_panels_layout(IsocadPanels *p, int fb_w, int fb_h,
                          int thickness_px, int bottom_px) {
    if (!p || p->program == 0 || fb_w <= 0 || fb_h <= 0) return;

    if (thickness_px < 1) thickness_px = 1;
    if (bottom_px < 0) bottom_px = 0;
    if (bottom_px > fb_h - 1) bottom_px = fb_h - 1;

    int max_thickness = (fb_w < fb_h ? fb_w : fb_h) / 3;
    if (max_thickness < 1) max_thickness = 1;
    if (thickness_px > max_thickness) thickness_px = max_thickness;

    if (p->built &&
        p->fb_w == fb_w && p->fb_h == fb_h &&
        p->thickness_px == thickness_px && p->bottom_px == bottom_px) {
        return;
    }

    p->fb_w = fb_w;
    p->fb_h = fb_h;
    p->thickness_px = thickness_px;
    p->bottom_px = bottom_px;

    p->insets.top = thickness_px;
    p->insets.left = thickness_px;
    p->insets.right = thickness_px;
    p->insets.bottom = bottom_px;
    if (p->insets.left + p->insets.right >= fb_w) {
        p->insets.left = 0;
        p->insets.right = 0;
    }
    if (p->insets.top + p->insets.bottom >= fb_h) {
        p->insets.top = 0;
    }

    memset(p->view, 0, sizeof(p->view));
    p->view[0] = 2.0f / (float)fb_w;
    p->view[5] = 2.0f / (float)fb_h;
    p->view[10] = 1.0f;
    p->view[12] = -1.0f;
    p->view[13] = -1.0f;
    p->view[15] = 1.0f;

    panels_build(p);
}

/* ---- drawing ------------------------------------------------------------ */

static void panel_fill(int x, int y, int w, int h, const float color[4]) {
    if (w <= 0 || h <= 0) return;
    glScissor(x, y, w, h);
    glClearColor(color[0], color[1], color[2], color[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}

static int panels_pick(const IsocadPanels *p, float x, float y) {
    for (int i = 0; i < p->icon_count; ++i) {
        const IsocadPanelIcon *icon = &p->icons[i];
        if (x >= icon->cx - icon->half && x < icon->cx + icon->half &&
            y >= icon->cy - icon->half && y < icon->cy + icon->half) {
            return i;
        }
    }
    return -1;
}

void isocad_panels_draw(IsocadPanels *p, float cursor_x, float cursor_y) {
    if (!p || !p->built || p->program == 0) return;

    p->hover = panels_pick(p, cursor_x, cursor_y);

    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLint box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, box);

    glEnable(GL_SCISSOR_TEST);

    int top_h = p->insets.top;
    int left_w = p->insets.left;
    int right_w = p->insets.right;
    int bottom_h = p->insets.bottom;
    int mid_y = bottom_h;
    int mid_h = p->fb_h - top_h - bottom_h;
    if (mid_h < 0) mid_h = 0;

    if (top_h > 0) {
        panel_fill(0, p->fb_h - top_h, p->fb_w, top_h, PANEL_BG);
        panel_fill(0, p->fb_h - top_h, p->fb_w, 1, PANEL_EDGE);
    }
    if (left_w > 0 && mid_h > 0) {
        panel_fill(0, mid_y, left_w, mid_h, PANEL_BG);
        panel_fill(left_w - 1, mid_y, 1, mid_h, PANEL_EDGE);
    }
    if (right_w > 0 && mid_h > 0) {
        panel_fill(p->fb_w - right_w, mid_y, right_w, mid_h, PANEL_BG);
        panel_fill(p->fb_w - right_w, mid_y, 1, mid_h, PANEL_EDGE);
    }

    if (p->hover >= 0) {
        const IsocadPanelIcon *icon = &p->icons[p->hover];
        int x0 = (int)floorf(icon->cx - icon->half);
        int x1 = (int)ceilf(icon->cx + icon->half);
        int y0 = (int)floorf(icon->cy - icon->half);
        int y1 = (int)ceilf(icon->cy + icon->half);
        panel_fill(x0, p->fb_h - y1, x1 - x0, y1 - y0, PANEL_HOVER_BG);
    }

    glDisable(GL_SCISSOR_TEST);

    if (p->vertex_count > 0) {
        glUseProgram(p->program);
        glUniformMatrix4fv(p->view_loc, 1, GL_FALSE, p->view);
        glBindVertexArray(p->vao);
        glUniform4fv(p->color_loc, 1, ICON_COLOR);
        glDrawArrays(GL_LINES, 0, p->vertex_count);
        if (p->hover >= 0) {
            const IsocadPanelIcon *icon = &p->icons[p->hover];
            glUniform4fv(p->color_loc, 1, ICON_HOVER_COLOR);
            glDrawArrays(GL_LINES, icon->first, icon->count);
        }
        glBindVertexArray(0);
        glUseProgram(0);
    }

    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(box[0], box[1], box[2], box[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}
