#include "isogl_panel.h"
#include "isogl_overlay.h"

static void panel_emit_rect(isogl_overlay *overlay, uint32_t group,
                            int x, int y, int width, int height,
                            const float color[4]) {
    if (width <= 0 || height <= 0) return;
    isogl_overlay_group_quad(overlay, group, (float)x, (float)y,
                             (float)width, (float)height, color);
}

static int panel_separator(int separator, int thickness) {
    if (separator <= 0 || thickness <= 0) return 0;
    return separator < thickness ? separator : thickness;
}

int isogl_panel_emit(const isogl_panel_layout *layout,
                     const isogl_panel_style *style,
                     isogl_overlay *overlay,
                     uint32_t group) {
    if (!layout || !style || !overlay) return -1;

    panel_emit_rect(overlay, group, layout->top.x, layout->top.y,
                    layout->top.width, layout->top.height,
                    style->background);
    panel_emit_rect(overlay, group, layout->left.x, layout->left.y,
                    layout->left.width, layout->left.height,
                    style->background);
    panel_emit_rect(overlay, group, layout->right.x, layout->right.y,
                    layout->right.width, layout->right.height,
                    style->background);
    panel_emit_rect(overlay, group, layout->bottom.x, layout->bottom.y,
                    layout->bottom.width, layout->bottom.height,
                    style->background);

    int sep = layout->edges.separator;
    int s;

    s = panel_separator(sep, layout->edges.top);
    if (s > 0) {
        panel_emit_rect(overlay, group, 0, layout->edges.top - s,
                        layout->width, s, style->edge);
    }

    s = panel_separator(sep, layout->edges.left);
    if (s > 0) {
        panel_emit_rect(overlay, group,
                        layout->edges.left - s, layout->edges.top,
                        s, layout->left.height, style->edge);
    }

    s = panel_separator(sep, layout->edges.right);
    if (s > 0) {
        panel_emit_rect(overlay, group,
                        layout->width - layout->edges.right, layout->edges.top,
                        s, layout->right.height, style->edge);
    }

    s = panel_separator(sep, layout->edges.bottom);
    if (s > 0) {
        panel_emit_rect(overlay, group, 0,
                        layout->height - layout->edges.bottom,
                        layout->width, s, style->edge);
    }

    return 0;
}
