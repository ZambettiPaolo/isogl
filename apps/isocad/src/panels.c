#include "panels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float PANEL_BG[4]         = {0.030f, 0.039f, 0.047f, 1.0f};
static const float PANEL_EDGE[4]       = {0.20f, 0.24f, 0.30f, 1.0f};
static const float PANEL_HOVER_BG[4]   = {0.085f, 0.105f, 0.135f, 1.0f};
static const float ICON_COLOR[4]       = {0.62f, 0.70f, 0.78f, 1.0f};
static const float ICON_HOVER_COLOR[4] = {0.98f, 0.99f, 1.00f, 1.0f};

/* In the "colour by thickness" display mode the segment shader ignores the
 * vertex colour and uses the ISO weight colour: weight 7 is grey, weight 6
 * is white.  In print-colour mode the vertex colour below is used.  Setting
 * both keeps the icons legible in either mode and makes hover visible. */
#define ICON_THICKNESS       ((uint8_t)LINE_THICKNESS_2_0)
#define ICON_HOVER_THICKNESS ((uint8_t)LINE_THICKNESS_1_4)

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

/* ---- lifecycle ---------------------------------------------------------- */

int isocad_panels_init(IsocadPanels *p) {
    if (!p) return -1;

    memset(p, 0, sizeof(*p));
    p->hover = -1;
    p->hover_drawn = -1;
    return isogl_vbo_array_init(&p->vbos) == 0 ? 0 : -1;
}

void isocad_panels_free(IsocadPanels *p) {
    if (!p) return;

    free(p->art);
    isogl_vbo_array_free(&p->vbos);
    memset(p, 0, sizeof(*p));
    p->hover = -1;
    p->hover_drawn = -1;
}

/* ---- geometry ----------------------------------------------------------- */

static isogl_vertex panels_make_vertex(const IsocadPanels *p, GLint index,
                                       int hovered) {
    isogl_vertex v;
    memset(&v, 0, sizeof(v));

    v.position[0] = p->art[2 * (size_t)index];
    v.position[1] = p->art[2 * (size_t)index + 1];

    const float *color = hovered ? ICON_HOVER_COLOR : ICON_COLOR;
    memcpy(v.color, color, sizeof(v.color));
    v.distanceFromStart = 0.0f;
    v.pk_thickness_pattern_layer = isogl_pack(
        hovered ? ICON_HOVER_THICKNESS : ICON_THICKNESS,
        LINE_TYPE_CONTINUOUS, 0);
    return v;
}

/* Bake the hover state into the segment VBO.  Called only when the layout or
 * the hovered icon changes, not every frame. */
static void panels_upload(IsocadPanels *p) {
    if (!p || !p->art || p->art_count < 4 || p->icon_count == 0) {
        p->hover_drawn = p->hover;
        return;
    }

    isogl_segment_list list;
    if (isogl_segment_list_init(&list, p->art_count / 2) != 0) return;

    int ok = 1;
    for (int i = 0; i < p->icon_count && ok; ++i) {
        const IsocadPanelIcon *icon = &p->icons[i];
        int hovered = (i == p->hover);
        for (GLint v = icon->first;
             v + 1 < icon->first + icon->count; v += 2) {
            isogl_vertex a = panels_make_vertex(p, v, hovered);
            isogl_vertex b = panels_make_vertex(p, v + 1, hovered);
            if (isogl_segment_list_push_pair(&list, a, b) != 0) {
                ok = 0;
                break;
            }
        }
    }

    if (ok && list.size > 0) {
        if (p->vbo_id != 0) {
            if (isogl_vbo_array_update(&p->vbos, p->vbo_id, &list) != 0) {
                ok = 0;
            }
        } else {
            p->vbo_id = isogl_vbo_array_add(&p->vbos, &list,
                                            ISOGL_VBO_SEGMENT);
            if (p->vbo_id == 0) ok = 0;
        }
    }

    isogl_segment_list_free(&list);
    if (ok) p->hover_drawn = p->hover;
}

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
    int icon_count = 0;

    for (size_t i = 0; i < ICON_DEF_COUNT; ++i) {
        const IconDef *def = &ICON_DEFS[i];
        if (icon_count >= ISOCAD_PANEL_MAX_ICONS) break;

        IsocadPanelIcon *icon = &p->icons[icon_count];
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
        icon_count++;
    }

    free(p->art);
    p->art = NULL;
    p->art_count = 0;
    p->icon_count = 0;

    if (!sink.ok || sink.count == 0) {
        free(sink.v);
    } else {
        p->art = sink.v;
        p->art_count = sink.count;
        p->icon_count = icon_count;
    }

    isogl_vbo_array_free(&p->vbos);
    isogl_vbo_array_init(&p->vbos);
    p->vbo_id = 0;
    p->hover = -1;
    p->hover_drawn = -1;
    panels_upload(p);
    p->built = 1;
}

void isocad_panels_layout(IsocadPanels *p, int fb_w, int fb_h,
                          int thickness_px, int bottom_px) {
    if (!p || fb_w <= 0 || fb_h <= 0) return;

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

    panels_build(p);
}

/* ---- drawing ------------------------------------------------------------ */

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

/* Layout rectangles are top-left based; isogl_draw_filled_rect works in the
 * current model space, which is GL pixel space during the overlay pass. */
static void panel_quad(const IsocadPanels *p, int x, int y_top,
                       int w, int h, const float color[4]) {
    if (w <= 0 || h <= 0) return;
    float y0 = (float)(p->fb_h - (y_top + h));
    float y1 = (float)(p->fb_h - y_top);
    isogl_draw_filled_rect((float)x, y0, (float)(x + w), y1, color);
}

void isocad_panels_draw(IsocadPanels *p, float cursor_x, float cursor_y) {
    if (!p || !p->built || p->fb_w <= 0 || p->fb_h <= 0) return;

    p->hover = panels_pick(p, cursor_x, cursor_y);
    if (p->hover != p->hover_drawn) panels_upload(p);

    float saved_model[16];
    float saved_view[16];
    memcpy(saved_model, isogl_model_matrix, sizeof(saved_model));
    memcpy(saved_view, isogl_view_matrix, sizeof(saved_view));

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    float ortho[16] = {0};
    ortho[0] = 2.0f / (float)p->fb_w;
    ortho[5] = 2.0f / (float)p->fb_h;
    ortho[10] = 1.0f;
    ortho[12] = -1.0f;
    ortho[13] = -1.0f;
    ortho[15] = 1.0f;

    memcpy(isogl_model_matrix, identity, sizeof(identity));
    memcpy(isogl_view_matrix, ortho, sizeof(ortho));

    int top_h = p->insets.top;
    int left_w = p->insets.left;
    int right_w = p->insets.right;
    int bottom_h = p->insets.bottom;
    int mid_h = p->fb_h - top_h - bottom_h;
    if (mid_h < 0) mid_h = 0;

    if (top_h > 0) {
        panel_quad(p, 0, 0, p->fb_w, top_h, PANEL_BG);
        panel_quad(p, 0, top_h - 1, p->fb_w, 1, PANEL_EDGE);
    }
    if (left_w > 0 && mid_h > 0) {
        panel_quad(p, 0, top_h, left_w, mid_h, PANEL_BG);
        panel_quad(p, left_w - 1, top_h, 1, mid_h, PANEL_EDGE);
    }
    if (right_w > 0 && mid_h > 0) {
        panel_quad(p, p->fb_w - right_w, top_h, right_w, mid_h, PANEL_BG);
        panel_quad(p, p->fb_w - right_w, top_h, 1, mid_h, PANEL_EDGE);
    }
    if (bottom_h > 0) {
        /* The console strip belongs to the same overlay pass; the console
           module only draws its text afterwards. */
        panel_quad(p, 0, p->fb_h - bottom_h, p->fb_w, bottom_h, PANEL_BG);
        panel_quad(p, 0, p->fb_h - bottom_h, p->fb_w, 1, PANEL_EDGE);
    }

    if (p->hover >= 0) {
        const IsocadPanelIcon *icon = &p->icons[p->hover];
        int x0 = (int)floorf(icon->cx - icon->half);
        int x1 = (int)ceilf(icon->cx + icon->half);
        int y0 = (int)floorf(icon->cy - icon->half);
        int y1 = (int)ceilf(icon->cy + icon->half);
        panel_quad(p, x0, y0, x1 - x0, y1 - y0, PANEL_HOVER_BG);
    }

    if (p->vbo_id != 0 && p->vbos.size > 0) {
        isogl_draw_vbo_object(&p->vbos, 0);
    }

    memcpy(isogl_model_matrix, saved_model, sizeof(saved_model));
    memcpy(isogl_view_matrix, saved_view, sizeof(saved_view));
}
