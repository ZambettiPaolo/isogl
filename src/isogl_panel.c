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

    /* If the strips do not fit, shrink both sides proportionally instead of
       dropping one edge.  Dropping the top strip made the side strips jump to
       y = 0 on small windows.  The content rectangle keeps at least 1 px. */
    long long horizontal = (long long)e.left + (long long)e.right;
    if (horizontal >= width) {
        long long available = width > 1 ? (long long)width - 1 : 0;
        e.left = (int)((long long)e.left * available / horizontal);
        e.right = (int)((long long)e.right * available / horizontal);
    }

    long long vertical = (long long)e.top + (long long)e.bottom;
    if (vertical >= height) {
        long long available = height > 1 ? (long long)height - 1 : 0;
        e.top = (int)((long long)e.top * available / vertical);
        e.bottom = (int)((long long)e.bottom * available / vertical);
    }

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
