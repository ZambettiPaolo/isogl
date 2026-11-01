#include "isogl_symbol_geometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_ITEMS 32

typedef struct {
    IsoglSymItemKind kind;
    double x0, y0, x1, y1;
    char text[128];
    double x, y, height_mm, angle;
    uint8_t pattern, weight, layer;
} TestItem;

typedef struct {
    TestItem items[MAX_ITEMS];
    size_t count;
    int fail_on_call; /* -1 = never, otherwise fail on this 0-based call */
    int calls;
} Collector;

typedef struct {
    TestItem items[MAX_ITEMS];
    size_t count;
} PickCollector;

static int failures = 0;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int near(double a, double b) {
    return fabs(a - b) <= 1e-9;
}

static int collect_item(void *user, const IsoglSymItem *item) {
    Collector *c = user;
    if (c->fail_on_call >= 0 && c->calls == c->fail_on_call) {
        c->calls++;
        return -1;
    }
    c->calls++;
    if (c->count >= MAX_ITEMS) return -1;

    TestItem *t = &c->items[c->count++];
    memset(t, 0, sizeof(*t));
    t->kind = item->kind;
    t->x0 = item->x0;
    t->y0 = item->y0;
    t->x1 = item->x1;
    t->y1 = item->y1;
    if (item->utf8) {
        snprintf(t->text, sizeof(t->text), "%s", item->utf8);
    }
    t->x = item->x;
    t->y = item->y;
    t->height_mm = item->height_mm;
    t->angle = item->angle;
    t->pattern = item->pattern;
    t->weight = item->weight;
    t->layer = item->layer;
    return 0;
}

static int collect_pick_item(void *user, const IsoglSymPickItem *item) {
    PickCollector *c = user;
    if (c->count >= MAX_ITEMS) return -1;
    /* Reuse the TestItem fields for pick items: segments in x0..y1, OBB in
       the first four corners' coordinates. */
    TestItem *t = &c->items[c->count++];
    memset(t, 0, sizeof(*t));
    t->kind = item->kind == ISOGL_SYM_PICK_SEGMENT
            ? ISOGL_SYM_SEGMENT : ISOGL_SYM_TEXT;
    t->x0 = item->x0;
    t->y0 = item->y0;
    t->x1 = item->x1;
    t->y1 = item->y1;
    if (item->kind == ISOGL_SYM_PICK_OBB) {
        t->x0 = item->corners[0][0];
        t->y0 = item->corners[0][1];
        t->x1 = item->corners[2][0];
        t->y1 = item->corners[2][1];
    }
    return 0;
}

/* Deterministic stub: width = 0.6 * height * byte length. */
static double stub_width(void *user, const char *utf8, double height_mm) {
    (void)user;
    if (!utf8 || height_mm <= 0.0) return 0.0;
    return 0.6 * height_mm * (double)strlen(utf8);
}

static IsoglDimensionDesc make_dimension(void) {
    IsoglDimensionDesc d;
    memset(&d, 0, sizeof(d));
    d.x0 = 0.0;
    d.y0 = 0.0;
    d.x1 = 100.0;
    d.y1 = 0.0;
    d.px = 0.0;
    d.py = 20.0;
    d.text_height_mm = 5.0;
    d.sheet_scale = 1.0;
    snprintf(d.unit, sizeof(d.unit), " mm");
    d.precision = 1;
    d.pick_text = 1;
    d.pattern = 0;
    d.weight = 3;
    d.layer = 0;
    d.color[0] = 1.0f;
    d.color[1] = 1.0f;
    d.color[2] = 1.0f;
    d.color[3] = 1.0f;
    return d;
}

int main(void) {
    IsoglSymMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.text_width = stub_width;

    IsoglDimensionDesc dim = make_dimension();

    /* ---- Dimension render items ----------------------------------------- */
    Collector c;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    IsoglSymEmitter emitter = {&c, collect_item};
    check(isogl_dimension_geometry(&dim, &metrics, &emitter) == 0,
          "dimension geometry succeeds");
    check(c.count == 8, "dimension emits 7 segments + text");

    if (c.count == 8) {
        check(c.items[0].kind == ISOGL_SYM_SEGMENT &&
              near(c.items[0].x0, 0.0) && near(c.items[0].y0, 20.0) &&
              near(c.items[0].x1, 100.0) && near(c.items[0].y1, 20.0),
              "dimension line endpoints");
        check(c.items[1].kind == ISOGL_SYM_SEGMENT &&
              near(c.items[1].x0, 0.0) && near(c.items[1].y0, 0.0) &&
              near(c.items[1].x1, 0.0) && near(c.items[1].y1, 22.0),
              "first extension line");
        check(c.items[2].kind == ISOGL_SYM_SEGMENT &&
              near(c.items[2].x0, 100.0) && near(c.items[2].y0, 0.0) &&
              near(c.items[2].x1, 100.0) && near(c.items[2].y1, 22.0),
              "second extension line");
        check(c.items[7].kind == ISOGL_SYM_TEXT,
              "dimension text item kind");
        check(strcmp(c.items[7].text, "100.0 mm") == 0,
              "dimension text value");
        check(near(c.items[7].x, 38.0) && near(c.items[7].y, 21.0),
              "dimension text is centred above the line");
        check(near(c.items[7].height_mm, 5.0) &&
              near(c.items[7].angle, 0.0),
              "dimension text height and angle");
        check(c.items[7].weight == 3 && c.items[0].weight == 3,
              "style propagated to items");
    }

    /* ---- Dimension pick items ------------------------------------------- */
    PickCollector p;
    memset(&p, 0, sizeof(p));
    IsoglSymPickEmitter pick_emitter = {&p, collect_pick_item};
    check(isogl_dimension_pick_geometry(&dim, &metrics, &pick_emitter) == 0,
          "dimension pick geometry succeeds");
    check(p.count == 8, "dimension pick = 7 segments + text OBB");
    if (p.count == 8) {
        check(near(p.items[7].x0, 38.0) && near(p.items[7].y0, 21.0) &&
              near(p.items[7].x1, 62.0) && near(p.items[7].y1, 26.0),
              "dimension text OBB corners");
    }

    dim.pick_text = 0;
    memset(&p, 0, sizeof(p));
    check(isogl_dimension_pick_geometry(&dim, &metrics, &pick_emitter) == 0,
          "dimension pick without text succeeds");
    check(p.count == 7, "pick_text=0 skips the text OBB");
    dim.pick_text = 1;

    /* ---- Center mark ----------------------------------------------------- */
    IsoglCenterMarkDesc mark;
    memset(&mark, 0, sizeof(mark));
    mark.cx = 10.0;
    mark.cy = 20.0;
    mark.size_mm = 4.0;
    mark.sheet_scale = 1.0;
    mark.weight = 1;
    mark.color[0] = 1.0f;
    mark.color[1] = 1.0f;
    mark.color[2] = 1.0f;
    mark.color[3] = 1.0f;

    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_center_mark_geometry(&mark, &emitter) == 0,
          "center mark geometry succeeds");
    check(c.count == 2, "center mark emits a two-arm cross");
    if (c.count == 2) {
        check(near(c.items[0].x0, 6.0) && near(c.items[0].y0, 20.0) &&
              near(c.items[0].x1, 14.0) && near(c.items[0].y1, 20.0),
              "center mark horizontal arm");
        check(near(c.items[1].x0, 10.0) && near(c.items[1].y0, 16.0) &&
              near(c.items[1].x1, 10.0) && near(c.items[1].y1, 24.0),
              "center mark vertical arm");
    }

    memset(&p, 0, sizeof(p));
    check(isogl_center_mark_pick_geometry(&mark, &pick_emitter) == 0,
          "center mark pick geometry succeeds");
    check(p.count == 2, "center mark pick emits two segments");

    /* ---- Invalid inputs and emitter failure ------------------------------ */
    IsoglDimensionDesc bad = make_dimension();
    bad.x1 = bad.x0;
    bad.y1 = bad.y0;
    check(isogl_dimension_geometry(&bad, &metrics, &emitter) == -1,
          "degenerate dimension rejected");
    check(isogl_dimension_geometry(NULL, &metrics, &emitter) == -1,
          "NULL dimension rejected");
    check(isogl_dimension_geometry(&dim, &metrics, NULL) == -1,
          "NULL emitter rejected");

    IsoglCenterMarkDesc bad_mark = mark;
    bad_mark.size_mm = 0.0;
    check(isogl_center_mark_geometry(&bad_mark, &emitter) == -1,
          "zero-size center mark rejected");

    memset(&c, 0, sizeof(c));
    c.fail_on_call = 2;
    check(isogl_dimension_geometry(&dim, &metrics, &emitter) == -1,
          "emitter failure propagates");

    /* ---- Non-unit scale -------------------------------------------------- */
    IsoglDimensionDesc scaled = make_dimension();
    scaled.sheet_scale = 0.5;
    scaled.text_height_mm = 5.0;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&scaled, &metrics, &emitter) == 0,
          "scaled dimension geometry succeeds");
    if (c.count == 8) {
        check(near(c.items[1].y1, 24.0),
              "extension overshoot is paper-space at 1:2");
        check(near(c.items[7].height_mm, 5.0) &&
              near(c.items[7].x, 38.0),
              "text remains paper-size and is centred in model units");
    }

    if (failures == 0) {
        printf("PASSED: 0 failure(s)\n");
        return 0;
    }
    printf("FAILED: %d failure(s)\n", failures);
    return 1;
}
