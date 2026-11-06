#include "isogl_panel.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static int rect_is(const isogl_panel_rect *r, int x, int y, int w, int h) {
    return r->x == x && r->y == y && r->width == w && r->height == h;
}

int main(void) {
    isogl_panel_layout layout;
    isogl_panel_edges edges;

    /* Standard window: 32-px tool strips, 100-px console. */
    edges.top = 32;
    edges.left = 32;
    edges.right = 32;
    edges.bottom = 100;
    edges.separator = 1;
    CHECK(isogl_panel_compute(&layout, 800, 600, &edges) == 0);
    CHECK(layout.width == 800 && layout.height == 600);
    CHECK(rect_is(&layout.top, 0, 0, 800, 32));
    CHECK(rect_is(&layout.left, 0, 32, 32, 468));
    CHECK(rect_is(&layout.right, 768, 32, 32, 468));
    CHECK(rect_is(&layout.bottom, 0, 500, 800, 100));
    CHECK(rect_is(&layout.content, 32, 32, 736, 468));
    CHECK(layout.edges.separator == 1);

    /* All edges disabled: content is the full viewport. */
    edges.top = 0;
    edges.left = 0;
    edges.right = 0;
    edges.bottom = 0;
    edges.separator = 0;
    CHECK(isogl_panel_compute(&layout, 800, 600, &edges) == 0);
    CHECK(rect_is(&layout.top, 0, 0, 800, 0));
    CHECK(rect_is(&layout.left, 0, 0, 0, 600));
    CHECK(rect_is(&layout.right, 800, 0, 0, 600));
    CHECK(rect_is(&layout.bottom, 0, 600, 800, 0));
    CHECK(rect_is(&layout.content, 0, 0, 800, 600));

    /* NULL edges behaves like all edges disabled. */
    CHECK(isogl_panel_compute(&layout, 800, 600, NULL) == 0);
    CHECK(rect_is(&layout.content, 0, 0, 800, 600));

    /* Tiny window: strips shrink proportionally instead of dropping an edge,
       so the side strips stay below the (very thin) top strip. */
    edges.top = 32;
    edges.left = 32;
    edges.right = 32;
    edges.bottom = 100;
    edges.separator = 1;
    CHECK(isogl_panel_compute(&layout, 40, 30, &edges) == 0);
    CHECK(layout.edges.left == 19 && layout.edges.right == 19);
    CHECK(layout.edges.top == 7 && layout.edges.bottom == 21);
    CHECK(layout.left.y == layout.edges.top);
    CHECK(rect_is(&layout.content, 19, 7, 2, 2));
    CHECK(layout.content.width >= 1 && layout.content.height >= 1);

    /* Negative values disable edges. */
    edges.top = -5;
    edges.left = -5;
    edges.right = -5;
    edges.bottom = -5;
    edges.separator = -1;
    CHECK(isogl_panel_compute(&layout, 320, 200, &edges) == 0);
    CHECK(layout.edges.top == 0 && layout.edges.left == 0);
    CHECK(layout.edges.right == 0 && layout.edges.bottom == 0);
    CHECK(layout.edges.separator == 0);
    CHECK(rect_is(&layout.content, 0, 0, 320, 200));

    /* Edges wider than the viewport are scaled down, never dropped. */
    edges.top = 600;
    edges.left = 600;
    edges.right = 600;
    edges.bottom = 600;
    edges.separator = 1;
    CHECK(isogl_panel_compute(&layout, 800, 600, &edges) == 0);
    CHECK(layout.edges.left == 399 && layout.edges.right == 399);
    CHECK(layout.edges.top == 299 && layout.edges.bottom == 299);
    CHECK(rect_is(&layout.content, 399, 299, 2, 2));

    /* Invalid sizes. */
    CHECK(isogl_panel_compute(&layout, 0, 600, &edges) == -1);
    CHECK(isogl_panel_compute(&layout, 800, 0, &edges) == -1);
    CHECK(isogl_panel_compute(NULL, 800, 600, &edges) == -1);

    if (failures == 0) {
        printf("test-panel-layout: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test-panel-layout: %d failure(s)\n", failures);
    return 1;
}
