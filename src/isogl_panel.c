#include "isogl_panel.h"

static int panel_edge(int value) {
    return value > 0 ? value : 0;
}

int isogl_panel_compute(isogl_panel_layout *out, int width, int height,
                        const isogl_panel_edges *edges) {
    if (!out || width <= 0 || height <= 0) return -1;

    isogl_panel_edges e = {0};
    if (edges) e = *edges;

    e.top = panel_edge(e.top);
    e.left = panel_edge(e.left);
    e.right = panel_edge(e.right);
    e.bottom = panel_edge(e.bottom);
    e.separator = panel_edge(e.separator);

    if (e.left >= width || e.right >= width - e.left) {
        e.left = 0;
        e.right = 0;
    }
    if (e.top >= height || e.bottom >= height - e.top) {
        e.top = 0;
    }
    if (e.bottom > height) e.bottom = height;

    int mid_height = height - e.top - e.bottom;
    if (mid_height < 0) mid_height = 0;

    out->width = width;
    out->height = height;
    out->edges = e;

    out->top.x = 0;
    out->top.y = 0;
    out->top.width = width;
    out->top.height = e.top;

    out->left.x = 0;
    out->left.y = e.top;
    out->left.width = e.left;
    out->left.height = mid_height;

    out->right.x = width - e.right;
    out->right.y = e.top;
    out->right.width = e.right;
    out->right.height = mid_height;

    out->bottom.x = 0;
    out->bottom.y = height - e.bottom;
    out->bottom.width = width;
    out->bottom.height = e.bottom;

    out->content.x = e.left;
    out->content.y = e.top;
    out->content.width = width - e.left - e.right;
    out->content.height = mid_height;
    return 0;
}
