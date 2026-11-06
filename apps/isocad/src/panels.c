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
static const float TOOLTIP_BG[4]       = {0.070f, 0.085f, 0.105f, 1.0f};
static const float TOOLTIP_EDGE[4]     = {0.24f, 0.29f, 0.35f, 1.0f};
static const float TOOLTIP_TEXT[4]     = {0.90f, 0.94f, 0.98f, 1.0f};

/* Icon art lives in a [-1, 1] square with y pointing down.  The group
 * transform places and scales it in viewport pixels; no thick-line quad is
 * generated, overlay lines are GL_LINES. */
typedef struct {
    isogl_overlay *overlay;
    isogl_overlay_group group;
    float sx, sy;       /* mirror, 1 or -1 */
    int ok;
} IconSink;

static void sink_line(IconSink *s, float x0, float y0, float x1, float y1) {
    if (!s->ok) return;
    isogl_overlay_group_line(s->overlay, s->group,
                             x0 * s->sx, y0 * s->sy,
                             x1 * s->sx, y1 * s->sy, ICON_COLOR);
}

static void ile_seg(IconSink *s, float x0, float y0, float x1, float y1) {
    sink_line(s, x0, y0, x1, y1);
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

/* ---- icon artwork ------------------------------------------------------- */

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

typedef struct {
    IsocadPanelSide side;
    const char *name;
    void (*emit)(IconSink *);
} IconDef;

static const IconDef ICON_DEFS[] = {
    {ISOCAD_PANEL_TOP,   "new",        icon_new},
    {ISOCAD_PANEL_TOP,   "open",       icon_open},
    {ISOCAD_PANEL_TOP,   "save",       icon_save},
    {ISOCAD_PANEL_TOP,   "undo",       icon_undo},
    {ISOCAD_PANEL_TOP,   "redo",       icon_redo},
    {ISOCAD_PANEL_TOP,   "print",      icon_print},

    {ISOCAD_PANEL_LEFT,  "select",     icon_select},
    {ISOCAD_PANEL_LEFT,  "line",       icon_line},
    {ISOCAD_PANEL_LEFT,  "circle",     icon_circle},
    {ISOCAD_PANEL_LEFT,  "rectangle",  icon_rectangle},
    {ISOCAD_PANEL_LEFT,  "polyline",   icon_polyline},
    {ISOCAD_PANEL_LEFT,  "text",       icon_text},
    {ISOCAD_PANEL_LEFT,  "dimension",  icon_dimension},
    {ISOCAD_PANEL_LEFT,  "center",     icon_center},

    {ISOCAD_PANEL_RIGHT, "zoom_in",    icon_zoom_in},
    {ISOCAD_PANEL_RIGHT, "zoom_out",   icon_zoom_out},
    {ISOCAD_PANEL_RIGHT, "pan",        icon_pan},
    {ISOCAD_PANEL_RIGHT, "fit",        icon_fit},
    {ISOCAD_PANEL_RIGHT, "grid",       icon_grid},
    {ISOCAD_PANEL_RIGHT, "snap",       icon_snap},
    {ISOCAD_PANEL_RIGHT, "thickness",  icon_thickness}
};

#define ICON_DEF_COUNT (sizeof(ICON_DEFS) / sizeof(ICON_DEFS[0]))

/* ---- lifecycle ---------------------------------------------------------- */

static void panel_emit_icon(IsocadPanels *p, int index) {
    IsocadPanelIcon *icon = &p->icons[index];
    if (!p->overlay || icon->group == ISOGL_OVERLAY_GROUP_INVALID) return;

    isogl_overlay_group_clear(p->overlay, icon->group);
    IconSink sink = {p->overlay, icon->group, 1.0f, 1.0f, 1};
    ICON_DEFS[index].emit(&sink);
}

void isocad_panels_rebuild_icons(IsocadPanels *p) {
    if (!p || !p->overlay) return;
    for (int i = 0; i < p->icon_count; ++i) {
        panel_emit_icon(p, i);
    }
}

void isocad_panels_init(IsocadPanels *p, isogl_overlay *overlay,
                        isogl_font_data *font) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->overlay = overlay;
    p->font = font;
    p->hover = -1;
    if (!overlay) return;

    p->panel_group = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_set_visible(overlay, p->panel_group, 1);

    for (size_t i = 0; i < ICON_DEF_COUNT; ++i) {
        if (p->icon_count >= ISOCAD_PANEL_MAX_ICONS) break;

        isogl_overlay_group group = isogl_overlay_group_create(overlay, 0);
        if (group == ISOGL_OVERLAY_GROUP_INVALID) break;

        IsocadPanelIcon *icon = &p->icons[p->icon_count];
        memset(icon, 0, sizeof(*icon));
        icon->name = ICON_DEFS[i].name;
        icon->side = ICON_DEFS[i].side;
        icon->group = group;
        isogl_overlay_group_set_transform(overlay, group, 0.0f, 0.0f, 1.0f);
        isogl_overlay_group_set_visible(overlay, group, 0);
        p->icon_count++;

        panel_emit_icon(p, p->icon_count - 1);
    }

    p->hover_group = isogl_overlay_group_create(overlay, 0);
    isogl_overlay_group_set_visible(overlay, p->hover_group, 0);

    p->tooltip_group = isogl_overlay_group_create(overlay, 1);
    isogl_overlay_group_set_visible(overlay, p->tooltip_group, 0);
}

void isocad_panels_free(IsocadPanels *p) {
    if (!p) return;
    if (p->overlay) {
        if (p->panel_group != ISOGL_OVERLAY_GROUP_INVALID) {
            isogl_overlay_group_destroy(p->overlay, p->panel_group);
        }
        if (p->hover_group != ISOGL_OVERLAY_GROUP_INVALID) {
            isogl_overlay_group_destroy(p->overlay, p->hover_group);
        }
        if (p->tooltip_group != ISOGL_OVERLAY_GROUP_INVALID) {
            isogl_overlay_group_destroy(p->overlay, p->tooltip_group);
        }
        for (int i = 0; i < p->icon_count; ++i) {
            if (p->icons[i].group != ISOGL_OVERLAY_GROUP_INVALID) {
                isogl_overlay_group_destroy(p->overlay, p->icons[i].group);
            }
        }
    }
    memset(p, 0, sizeof(*p));
    p->hover = -1;
}

/* ---- layout ------------------------------------------------------------- */

static void panels_hide_hover(IsocadPanels *p) {
    p->hover = -1;
    if (!p->overlay) return;
    isogl_overlay_group_clear(p->overlay, p->hover_group);
    isogl_overlay_group_set_visible(p->overlay, p->hover_group, 0);
    isogl_overlay_group_clear(p->overlay, p->tooltip_group);
    isogl_overlay_group_set_visible(p->overlay, p->tooltip_group, 0);
    p->tooltip_shown = 0;
}

void isocad_panels_layout(IsocadPanels *p, const isogl_panel_layout *layout) {
    if (!p || !layout || !p->overlay) return;

    int top = layout->top.height;
    int left = layout->left.width;
    int right = layout->right.width;
    int bottom = layout->bottom.height;

    if (p->built &&
        p->last_width == layout->width && p->last_height == layout->height &&
        p->last_top == top && p->last_left == left &&
        p->last_right == right && p->last_bottom == bottom) {
        return;
    }

    p->built = 1;
    p->last_width = layout->width;
    p->last_height = layout->height;
    p->last_top = top;
    p->last_left = left;
    p->last_right = right;
    p->last_bottom = bottom;

    isogl_overlay_group_clear(p->overlay, p->panel_group);
    isogl_overlay_group_set_transform(p->overlay, p->panel_group,
                                      0.0f, 0.0f, 1.0f);
    isogl_overlay_group_set_layer(p->overlay, p->panel_group, 0);
    isogl_overlay_group_set_visible(p->overlay, p->panel_group, 1);

    isogl_panel_style style;
    memcpy(style.background, PANEL_BG, sizeof(style.background));
    memcpy(style.edge, PANEL_EDGE, sizeof(style.edge));
    isogl_panel_emit(layout, &style, p->overlay, p->panel_group);

    int top_index = 0;
    int left_index = 0;
    int right_index = 0;

    for (int i = 0; i < p->icon_count; ++i) {
        IsocadPanelIcon *icon = &p->icons[i];
        float cell = 0.0f;
        float cx = 0.0f, cy = 0.0f;
        int place = 0;

        if (icon->side == ISOCAD_PANEL_TOP && top > 0) {
            cell = (float)top;
            cx = cell * 0.5f + (float)top_index * cell;
            cy = cell * 0.5f;
            top_index++;
            place = (cx + cell * 0.5f <= (float)layout->width);
        } else if (icon->side == ISOCAD_PANEL_LEFT && left > 0) {
            cell = (float)left;
            cx = cell * 0.5f;
            cy = (float)top + cell * 0.5f + (float)left_index * cell;
            left_index++;
            place = (cy + cell * 0.5f <=
                     (float)(layout->height - bottom));
        } else if (icon->side == ISOCAD_PANEL_RIGHT && right > 0) {
            cell = (float)right;
            cx = (float)layout->width - cell * 0.5f;
            cy = (float)top + cell * 0.5f + (float)right_index * cell;
            right_index++;
            place = (cy + cell * 0.5f <=
                     (float)(layout->height - bottom));
        }

        if (!place) {
            icon->half = 0.0f;
            isogl_overlay_group_set_visible(p->overlay, icon->group, 0);
            continue;
        }

        icon->cx = cx;
        icon->cy = cy;
        icon->half = cell * 0.5f;
        float art = cell * (14.0f / ISOCAD_PANEL_LOGICAL_THICKNESS) * 0.5f;
        isogl_overlay_group_set_transform(p->overlay, icon->group,
                                          cx, cy, art);
        isogl_overlay_group_set_visible(p->overlay, icon->group, 1);
    }

    panels_hide_hover(p);
}

/* ---- hover, tooltip ----------------------------------------------------- */

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

static float panels_ui_scale(const IsocadPanels *p) {
    if (p->hover < 0) return 1.0f;
    float cell = p->icons[p->hover].half * 2.0f;
    if (cell <= 0.0f) return 1.0f;
    return cell / ISOCAD_PANEL_LOGICAL_THICKNESS;
}

static void panels_tooltip_position(const IsocadPanels *p,
                                    const isogl_panel_layout *layout,
                                    float cursor_x, float cursor_y,
                                    float w, float h,
                                    float *tx, float *ty) {
    float u = panels_ui_scale(p);
    float x = cursor_x + 14.0f * u;
    float y = cursor_y + 18.0f * u;

    if (x + w > (float)layout->width - 1.0f) {
        x = cursor_x - w - 14.0f * u;
        if (x < 0.0f) x = 0.0f;
    }
    if (y + h > (float)layout->height - 1.0f) {
        y = cursor_y - h - 18.0f * u;
        if (y < 0.0f) y = 0.0f;
    }
    if (x + w > (float)layout->width - 1.0f) {
        x = (float)layout->width - w - 1.0f;
    }
    if (y + h > (float)layout->height - 1.0f) {
        y = (float)layout->height - h - 1.0f;
    }
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    *tx = x;
    *ty = y;
}

static void panels_build_tooltip(IsocadPanels *p,
                                 const isogl_panel_layout *layout,
                                 const IsocadPanelIcon *icon,
                                 float cursor_x, float cursor_y) {
    float u = panels_ui_scale(p);
    float text_h = 12.0f * u;
    float pad = 6.0f * u;
    float border = 1.0f * u;
    if (border < 1.0f) border = 1.0f;

    float text_w = 0.0f;
    if (p->font && p->font->height > 0.0f) {
        text_w = (float)isogl_font_text_advance(p->font, icon->name) *
                 text_h / p->font->height;
    }

    float w = text_w + 2.0f * pad;
    float h = text_h + 2.0f * pad;
    if (w < 24.0f * u) w = 24.0f * u;
    if (h < 16.0f * u) h = 16.0f * u;

    float tx = 0.0f, ty = 0.0f;
    panels_tooltip_position(p, layout, cursor_x, cursor_y, w, h, &tx, &ty);

    isogl_overlay_group_clear(p->overlay, p->tooltip_group);
    isogl_overlay_group_set_layer(p->overlay, p->tooltip_group, 1);
    isogl_overlay_group_set_visible(p->overlay, p->tooltip_group, 1);
    isogl_overlay_group_set_transform(p->overlay, p->tooltip_group,
                                      tx, ty, 1.0f);
    isogl_overlay_group_set_clip(p->overlay, p->tooltip_group,
                                 1, tx, ty, w, h);

    isogl_overlay_group_quad(p->overlay, p->tooltip_group,
                             0.0f, 0.0f, w, h, TOOLTIP_BG);
    isogl_overlay_group_quad(p->overlay, p->tooltip_group,
                             0.0f, 0.0f, w, border, TOOLTIP_EDGE);
    isogl_overlay_group_quad(p->overlay, p->tooltip_group,
                             0.0f, h - border, w, border, TOOLTIP_EDGE);
    isogl_overlay_group_quad(p->overlay, p->tooltip_group,
                             0.0f, 0.0f, border, h, TOOLTIP_EDGE);
    isogl_overlay_group_quad(p->overlay, p->tooltip_group,
                             w - border, 0.0f, border, h, TOOLTIP_EDGE);
    isogl_overlay_group_text(p->overlay, p->tooltip_group, icon->name,
                             pad, pad + text_h, text_h, 0.0f, TOOLTIP_TEXT);

    p->tooltip_x = tx;
    p->tooltip_y = ty;
    p->tooltip_w = w;
    p->tooltip_h = h;
    p->tooltip_shown = 1;
}

static void panels_move_tooltip(IsocadPanels *p,
                                const isogl_panel_layout *layout,
                                float cursor_x, float cursor_y) {
    if (!p->tooltip_shown) return;

    float tx = 0.0f, ty = 0.0f;
    panels_tooltip_position(p, layout, cursor_x, cursor_y,
                            p->tooltip_w, p->tooltip_h, &tx, &ty);
    if (tx == p->tooltip_x && ty == p->tooltip_y) return;

    p->tooltip_x = tx;
    p->tooltip_y = ty;
    isogl_overlay_group_set_transform(p->overlay, p->tooltip_group,
                                      tx, ty, 1.0f);
    isogl_overlay_group_set_clip(p->overlay, p->tooltip_group,
                                 1, tx, ty, p->tooltip_w, p->tooltip_h);
}

void isocad_panels_emit(IsocadPanels *p, const isogl_panel_layout *layout,
                        float cursor_x, float cursor_y) {
    if (!p || !layout || !p->overlay || !p->built) return;

    int hover = panels_pick(p, cursor_x, cursor_y);
    if (hover != p->hover) {
        if (p->hover >= 0) {
            isogl_overlay_group_set_color(p->overlay,
                                          p->icons[p->hover].group, NULL);
        }

        if (hover < 0) {
            panels_hide_hover(p);
        } else {
            p->hover = hover;
            const IsocadPanelIcon *icon = &p->icons[hover];

            isogl_overlay_group_set_color(p->overlay, icon->group,
                                          ICON_HOVER_COLOR);

            isogl_overlay_group_clear(p->overlay, p->hover_group);
            isogl_overlay_group_set_transform(p->overlay, p->hover_group,
                                              0.0f, 0.0f, 1.0f);
            isogl_overlay_group_set_layer(p->overlay, p->hover_group, 0);
            isogl_overlay_group_set_visible(p->overlay, p->hover_group, 1);
            isogl_overlay_group_quad(p->overlay, p->hover_group,
                                     icon->cx - icon->half,
                                     icon->cy - icon->half,
                                     icon->half * 2.0f,
                                     icon->half * 2.0f,
                                     PANEL_HOVER_BG);

            panels_build_tooltip(p, layout, icon, cursor_x, cursor_y);
        }
    }

    if (p->hover >= 0) {
        panels_move_tooltip(p, layout, cursor_x, cursor_y);
    }
}

int isocad_panels_hover(const IsocadPanels *p) {
    return p ? p->hover : -1;
}
