#include "isogl_symbol_geometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    d.text_height_mm = 2.5;
    d.sheet_scale = 1.0;
    d.distance = 0.0;
    d.text_offset_mm = 0.0;
    d.text_left[0] = '\0';
    snprintf(d.text_right, sizeof(d.text_right), " mm");
    d.arrow_left = ISOGL_DIM_ARROW_OPEN;
    d.arrow_right = ISOGL_DIM_ARROW_OPEN;
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

static IsoglDiameterDesc make_diameter(void) {
    IsoglDiameterDesc d;
    memset(&d, 0, sizeof(d));
    d.cx = 0.0;
    d.cy = 0.0;
    d.px = 10.0;
    d.py = 0.0;
    d.tx = 0.0;
    d.ty = 20.0;
    d.text_height_mm = 3.5;
    d.sheet_scale = 1.0;
    snprintf(d.text_left, sizeof(d.text_left), "\xC3\x98");
    d.text_right[0] = '\0';
    d.precision = 1;
    d.pick_text = 1;
    d.arrow_left = ISOGL_DIM_ARROW_CLOSED;
    d.arrow_right = ISOGL_DIM_ARROW_CLOSED;
    d.pattern = 0;
    d.weight = 3;
    d.layer = 0;
    d.color[0] = 1.0f;
    d.color[1] = 1.0f;
    d.color[2] = 1.0f;
    d.color[3] = 1.0f;
    return d;
}

static IsoglRadiusDesc make_radius(void) {
    IsoglRadiusDesc d;
    memset(&d, 0, sizeof(d));
    d.cx = 0.0;
    d.cy = 0.0;
    d.px = 10.0;
    d.py = 0.0;
    d.tx = 15.0;
    d.ty = 5.0;
    d.text_height_mm = 3.5;
    d.sheet_scale = 1.0;
    snprintf(d.text_left, sizeof(d.text_left), "R");
    d.text_right[0] = '\0';
    d.precision = 1;
    d.pick_text = 1;
    d.arrow = ISOGL_DIM_ARROW_CLOSED;
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
              near(c.items[1].x0, 0.0) && near(c.items[1].y0, 3.0) &&
              near(c.items[1].x1, 0.0) && near(c.items[1].y1, 22.0),
              "first extension line starts 3 mm from the measured point");
        check(c.items[2].kind == ISOGL_SYM_SEGMENT &&
              near(c.items[2].x0, 100.0) && near(c.items[2].y0, 3.0) &&
              near(c.items[2].x1, 100.0) && near(c.items[2].y1, 22.0),
              "second extension line starts 3 mm from the measured point");
        check(c.items[7].kind == ISOGL_SYM_TEXT,
              "dimension text item kind");
        check(strcmp(c.items[7].text, "100.0 mm") == 0,
              "dimension text value");
        check(near(c.items[7].x, 44.0) && near(c.items[7].y, 21.0),
              "dimension text is centred above the line");
        check(near(c.items[7].height_mm, 2.5) &&
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
        check(near(p.items[7].x0, 44.0) && near(p.items[7].y0, 21.0) &&
              near(p.items[7].x1, 56.0) && near(p.items[7].y1, 23.5),
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
    scaled.text_height_mm = 2.5;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&scaled, &metrics, &emitter) == 0,
          "scaled dimension geometry succeeds");
    if (c.count == 8) {
        check(near(c.items[1].y0, 6.0) && near(c.items[1].y1, 24.0),
              "extension gap and overshoot are paper-space at 1:2");
        check(near(c.items[7].height_mm, 2.5) &&
              near(c.items[7].x, 44.0),
              "text remains paper-size and is centred in model units");
    }

    /* ---- Arrow styles ---------------------------------------------------- */
    IsoglDimensionDesc ad = make_dimension();
    ad.arrow_left = ISOGL_DIM_ARROW_NONE;
    ad.arrow_right = ISOGL_DIM_ARROW_NONE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&ad, &metrics, &emitter) == 0,
          "dimension without arrows succeeds");
    check(c.count == 4, "NONE arrows emit 3 lines + text");

    ad.arrow_left = ISOGL_DIM_ARROW_TICK;
    ad.arrow_right = ISOGL_DIM_ARROW_TICK;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&ad, &metrics, &emitter) == 0,
          "tick arrows succeed");
    check(c.count == 6, "TICK arrows emit one segment each");

    ad.arrow_left = ISOGL_DIM_ARROW_CLOSED;
    ad.arrow_right = ISOGL_DIM_ARROW_CLOSED;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&ad, &metrics, &emitter) == 0,
          "closed arrows succeed");
    check(c.count == 10, "CLOSED arrows emit three segments each");

    ad.arrow_left = ISOGL_DIM_ARROW_DOT;
    ad.arrow_right = ISOGL_DIM_ARROW_DOT;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&ad, &metrics, &emitter) == 0,
          "dot arrows succeed");
    check(c.count == 28, "DOT arrows emit a 12-segment circle each");

    /* ---- Arrow proportions and outside heads ----------------------------- */
    IsoglDimensionDesc geom = make_dimension();
    geom.arrow_left = ISOGL_DIM_ARROW_OPEN;
    geom.arrow_right = ISOGL_DIM_ARROW_NONE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&geom, &metrics, &emitter) == 0,
          "arrow proportion probe succeeds");
    if (c.count >= 4) {
        check(near(c.items[3].x1, 2.2) &&
              near(c.items[3].y1, 20.0 + 2.2 * tan(M_PI / 12.0)),
              "open arrow is 2.2 mm long with a 30-degree included angle");
        check(near(c.items[0].x0, 0.0) && near(c.items[0].x1, 100.0),
              "inside arrows leave the dimension line on the span");
    }

    geom.arrow_left = ISOGL_DIM_ARROW_OPEN_OUTSIDE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&geom, &metrics, &emitter) == 0,
          "outside open arrow succeeds");
    if (c.count >= 4) {
        check(near(c.items[3].x1, -2.2),
              "outside arrow points inward from beyond the span");
        check(near(c.items[0].x0, -5.0) && near(c.items[0].x1, 100.0),
              "outside arrow extends the dimension line by 5 mm");
        check(near(c.items[1].x1, 0.0) && near(c.items[2].x1, 100.0),
              "outside arrow leaves the extension lines on the measured points");
        check(near(c.items[c.count - 1].x, 44.0),
              "outside arrow keeps the text centred on the measured span");
    }
    check(c.count == 6, "OPEN_OUTSIDE emits one head + text");

    geom.arrow_left = ISOGL_DIM_ARROW_CLOSED_OUTSIDE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&geom, &metrics, &emitter) == 0,
          "outside closed arrow succeeds");
    check(c.count == 7, "CLOSED_OUTSIDE emits a triangle + text");
    if (c.count >= 1) {
        check(near(c.items[0].x0, -5.0) && near(c.items[0].x1, 100.0),
              "outside closed arrow extends the dimension line");
    }

    geom.arrow_right = ISOGL_DIM_ARROW_CLOSED_OUTSIDE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&geom, &metrics, &emitter) == 0,
          "two outside closed arrows succeed");
    check(c.count == 10, "two CLOSED_OUTSIDE heads emit two triangles + text");
    if (c.count >= 1) {
        check(near(c.items[0].x0, -5.0) && near(c.items[0].x1, 105.0),
              "both outside ends extend the dimension line by 5 mm");
    }

    geom.arrow_left = ISOGL_DIM_ARROW_DOT;
    geom.arrow_right = ISOGL_DIM_ARROW_NONE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&geom, &metrics, &emitter) == 0,
          "dot arrow succeeds");
    check(c.count == 16, "DOT with one head emits 12 segments + text");
    if (c.count >= 4) {
        double r0 = hypot(c.items[3].x0 - 0.0, c.items[3].y0 - 20.0);
        double r1 = hypot(c.items[3].x1 - 0.0, c.items[3].y1 - 20.0);
        check(near(r0, 0.75) && near(r1, 0.75),
              "dot is a 1.5 mm diameter circle");
    }

    /* ---- Reference point dots -------------------------------------------- */
    IsoglDimensionDesc md = make_dimension();
    md.arrow_left = ISOGL_DIM_ARROW_NONE;
    md.arrow_right = ISOGL_DIM_ARROW_NONE;
    md.mark_points = 1;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&md, &metrics, &emitter) == 0,
          "reference point geometry succeeds");
    check(c.count == 6, "reference points add one 0.5 mm segment each");
    if (c.count == 6) {
        check(c.items[3].kind == ISOGL_SYM_TEXT,
              "text stays before the reference segments");
        check(near(c.items[4].x0, 0.0) && near(c.items[4].y0, 0.0) &&
              near(c.items[4].x1, 0.0) && near(c.items[4].y1, 0.5),
              "reference segment starts at p1 toward the dimension line");
        check(near(c.items[5].x0, 100.0) && near(c.items[5].y0, 0.0) &&
              near(c.items[5].x1, 100.0) && near(c.items[5].y1, 0.5),
              "reference segment starts at p2 toward the dimension line");
        check(c.items[4].y1 < c.items[1].y0 && c.items[5].y1 < c.items[2].y0,
              "reference segments leave a gap before the extension lines");
    }

    memset(&p, 0, sizeof(p));
    check(isogl_dimension_pick_geometry(&md, &metrics, &pick_emitter) == 0,
          "reference point pick geometry succeeds");
    check(p.count == 6, "reference points are pickable");

    /* ---- Explicit distance ---------------------------------------------- */
    IsoglDimensionDesc dd = make_dimension();
    dd.arrow_left = ISOGL_DIM_ARROW_NONE;
    dd.arrow_right = ISOGL_DIM_ARROW_NONE;
    dd.px = 0.0;
    dd.py = 100.0;
    dd.distance = 20.0;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&dd, &metrics, &emitter) == 0,
          "explicit distance succeeds");
    if (c.count > 0) {
        check(near(c.items[0].y0, 20.0) && near(c.items[0].y1, 20.0),
              "distance overrides the p3 offset");
    }

    dd.distance = 0.0;
    dd.py = -30.0;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&dd, &metrics, &emitter) == 0,
          "distance derived from p3 succeeds");
    if (c.count > 0) {
        check(near(c.items[0].y0, -30.0) && near(c.items[0].y1, -30.0),
              "p3 chooses the dimension-line side");
    }

    /* ---- Text left/right, no automatic unit ----------------------------- */
    IsoglDimensionDesc td = make_dimension();
    snprintf(td.text_left, sizeof(td.text_left), "L ");
    td.text_right[0] = '\0';
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&td, &metrics, &emitter) == 0,
          "text left/right succeeds");
    if (c.count > 0) {
        check(strcmp(c.items[c.count - 1].text, "L 100.0") == 0,
              "text_left precedes the value and no unit is appended");
    }

    snprintf(td.text_right, sizeof(td.text_right), " u");
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&td, &metrics, &emitter) == 0,
          "text right succeeds");
    if (c.count > 0) {
        check(strcmp(c.items[c.count - 1].text, "L 100.0 u") == 0,
              "text_right follows the value");
    }

    /* ---- Text offset ----------------------------------------------------- */
    td.text_offset_mm = 10.0;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&td, &metrics, &emitter) == 0,
          "text offset succeeds");
    if (c.count > 0) {
        /* "L 100.0 u" = 9 chars, stub width = 0.6*2.5*9 = 13.5. */
        check(near(c.items[c.count - 1].x, 50.0 + 10.0 - 13.5 * 0.5) &&
              near(c.items[c.count - 1].y, 21.0),
              "text offset moves the text along the dimension line");
    }

    /* ---- Horizontal, vertical and inclined dimensions -------------------- */
    IsoglDimensionDesc hd = make_dimension();
    hd.arrow_left = ISOGL_DIM_ARROW_NONE;
    hd.arrow_right = ISOGL_DIM_ARROW_NONE;
    hd.text_left[0] = '\0';
    hd.text_right[0] = '\0';
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&hd, &metrics, &emitter) == 0,
          "horizontal dimension succeeds");
    if (c.count > 0) {
        check(near(c.items[0].x0, 0.0) && near(c.items[0].y0, 20.0) &&
              near(c.items[0].x1, 100.0) && near(c.items[0].y1, 20.0),
              "horizontal dimension line");
        check(near(c.items[c.count - 1].angle, 0.0),
              "horizontal text is upright");
    }

    IsoglDimensionDesc vd = make_dimension();
    vd.x0 = 0.0; vd.y0 = 0.0;
    vd.x1 = 0.0; vd.y1 = 100.0;
    vd.px = 20.0; vd.py = 0.0;
    vd.arrow_left = ISOGL_DIM_ARROW_NONE;
    vd.arrow_right = ISOGL_DIM_ARROW_NONE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&vd, &metrics, &emitter) == 0,
          "vertical dimension succeeds");
    if (c.count > 0) {
        check(near(c.items[0].x0, 20.0) && near(c.items[0].y0, 0.0) &&
              near(c.items[0].x1, 20.0) && near(c.items[0].y1, 100.0),
              "vertical dimension line");
        check(near(c.items[c.count - 1].angle, M_PI / 2.0),
              "vertical text is rotated");
    }

    IsoglDimensionDesc id = make_dimension();
    id.x0 = 0.0; id.y0 = 0.0;
    id.x1 = 100.0; id.y1 = 100.0;
    id.px = -20.0; id.py = 20.0;
    id.arrow_left = ISOGL_DIM_ARROW_NONE;
    id.arrow_right = ISOGL_DIM_ARROW_NONE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_dimension_geometry(&id, &metrics, &emitter) == 0,
          "inclined dimension succeeds");
    if (c.count > 0) {
        check(near(c.items[0].x0, -20.0) && near(c.items[0].y0, 20.0) &&
              near(c.items[0].x1, 80.0) && near(c.items[0].y1, 120.0),
              "inclined dimension line");
        check(near(c.items[c.count - 1].angle, M_PI / 4.0),
              "inclined text follows the dimension line");
    }

    /* ---- Invalid new parameters ------------------------------------------ */
    IsoglDimensionDesc bad_arrow = make_dimension();
    bad_arrow.arrow_left = (IsoglDimensionArrow)99;
    check(isogl_dimension_geometry(&bad_arrow, &metrics, &emitter) == -1,
          "invalid arrow type rejected");

    IsoglDimensionDesc bad_offset = make_dimension();
    bad_offset.text_offset_mm = NAN;
    check(isogl_dimension_geometry(&bad_offset, &metrics, &emitter) == -1,
          "non-finite text offset rejected");

    /* ---- Diameter --------------------------------------------------------- */
    IsoglDiameterDesc dia = make_diameter();
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_diameter_geometry(&dia, &metrics, &emitter) == 0,
          "diameter geometry succeeds");
    check(c.count == 9, "diameter emits line + leader + 2 closed heads + text");
    if (c.count == 9) {
        check(c.items[0].kind == ISOGL_SYM_SEGMENT &&
              near(c.items[0].x0, -10.0) && near(c.items[0].y0, 0.0) &&
              near(c.items[0].x1, 10.0) && near(c.items[0].y1, 0.0),
              "diameter line spans -r*u .. +r*u");
        check(c.items[1].kind == ISOGL_SYM_SEGMENT &&
              near(c.items[1].x0, 0.0) && near(c.items[1].y0, 20.0) &&
              near(c.items[1].x1, 0.0) && near(c.items[1].y1, 0.0),
              "diameter leader runs from the text centre to the line");
        check(near(c.items[2].x0, -10.0) && near(c.items[2].y0, 0.0) &&
              near(c.items[2].x1, -7.8) &&
              near(c.items[2].y1, 2.2 * tan(M_PI / 12.0)),
              "first closed head points toward the centre");
        check(c.items[8].kind == ISOGL_SYM_TEXT &&
              strcmp(c.items[8].text, "\xC3\x98" "20.0") == 0,
              "diameter value is 2r with the default prefix");
        check(near(c.items[8].x, -6.3) && near(c.items[8].y, 18.25),
              "diameter text origin is centred on the anchor");
        check(near(c.items[8].height_mm, 3.5) &&
              near(c.items[8].angle, 0.0),
              "diameter text follows the line and keeps paper height");
        check(c.items[8].weight == 3 && c.items[0].weight == 3,
              "diameter style propagates to every item");
    }

    memset(&p, 0, sizeof(p));
    check(isogl_diameter_pick_geometry(&dia, &metrics, &pick_emitter) == 0,
          "diameter pick geometry succeeds");
    check(p.count == 9, "diameter pick = line + leader + heads + text OBB");
    if (p.count == 9) {
        check(near(p.items[8].x0, -6.3) && near(p.items[8].y0, 18.25) &&
              near(p.items[8].x1, 6.3) && near(p.items[8].y1, 21.75),
              "diameter text OBB matches the centred box");
    }

    dia.pick_text = 0;
    memset(&p, 0, sizeof(p));
    check(isogl_diameter_pick_geometry(&dia, &metrics, &pick_emitter) == 0,
          "diameter pick without text succeeds");
    check(p.count == 8, "diameter pick_text=0 skips the text OBB");
    dia.pick_text = 1;

    dia.arrow_left = ISOGL_DIM_ARROW_NONE;
    dia.arrow_right = ISOGL_DIM_ARROW_NONE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_diameter_geometry(&dia, &metrics, &emitter) == 0,
          "diameter without arrows succeeds");
    check(c.count == 3, "NONE diameter arrows emit line + leader + text");

    dia = make_diameter();
    dia.tx = 5.0;
    dia.ty = 0.0;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_diameter_geometry(&dia, &metrics, &emitter) == 0,
          "diameter with the anchor on the line succeeds");
    check(c.count == 8, "anchor on the diameter line suppresses the leader");

    dia = make_diameter();
    dia.sheet_scale = 2.0;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_diameter_geometry(&dia, &metrics, &emitter) == 0,
          "scaled diameter geometry succeeds");
    if (c.count == 9) {
        check(near(c.items[2].x1, -8.9) &&
              near(c.items[2].y1, 1.1 * tan(M_PI / 12.0)),
              "diameter arrow is a 2.2 mm paper size at 1:2");
        check(near(c.items[8].height_mm, 3.5) &&
              near(c.items[8].x, -6.3) && near(c.items[8].y, 19.125),
              "diameter text stays paper-size and centred at 1:2");
    }

    /* ---- Radius ----------------------------------------------------------- */
    IsoglRadiusDesc rad = make_radius();
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_radius_geometry(&rad, &metrics, &emitter) == 0,
          "radius geometry succeeds");
    check(c.count == 6, "radius emits line + leader + one closed head + text");
    if (c.count == 6) {
        check(c.items[0].kind == ISOGL_SYM_SEGMENT &&
              near(c.items[0].x0, 0.0) && near(c.items[0].y0, 0.0) &&
              near(c.items[0].x1, 10.0) && near(c.items[0].y1, 0.0),
              "radius line runs from the centre to the circle point");
        check(c.items[1].kind == ISOGL_SYM_SEGMENT &&
              near(c.items[1].x0, 10.0) && near(c.items[1].y0, 0.0) &&
              near(c.items[1].x1, 15.0) && near(c.items[1].y1, 5.0),
              "radius leader runs from the circle point to the anchor");
        check(near(c.items[2].x0, 10.0) && near(c.items[2].y0, 0.0) &&
              near(c.items[2].x1, 7.8) &&
              near(c.items[2].y1, 2.2 * tan(M_PI / 12.0)),
              "radius head points toward the centre");
        check(c.items[5].kind == ISOGL_SYM_TEXT &&
              strcmp(c.items[5].text, "R10.0") == 0,
              "radius value is r with the default prefix");
        check(near(c.items[5].x, 9.75) && near(c.items[5].y, 3.25),
              "radius text origin is centred on the anchor");
    }

    memset(&p, 0, sizeof(p));
    check(isogl_radius_pick_geometry(&rad, &metrics, &pick_emitter) == 0,
          "radius pick geometry succeeds");
    check(p.count == 6, "radius pick = line + leader + head + text OBB");
    if (p.count == 6) {
        check(near(p.items[5].x0, 9.75) && near(p.items[5].y0, 3.25) &&
              near(p.items[5].x1, 20.25) && near(p.items[5].y1, 6.75),
              "radius text OBB matches the centred box");
    }

    rad.pick_text = 0;
    memset(&p, 0, sizeof(p));
    check(isogl_radius_pick_geometry(&rad, &metrics, &pick_emitter) == 0,
          "radius pick without text succeeds");
    check(p.count == 5, "radius pick_text=0 skips the text OBB");
    rad.pick_text = 1;

    rad.tx = 5.0;
    rad.ty = 0.0;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_radius_geometry(&rad, &metrics, &emitter) == 0,
          "radius with the anchor on the line succeeds");
    check(c.count == 5, "anchor on the radius segment suppresses the leader");

    rad = make_radius();
    rad.arrow = ISOGL_DIM_ARROW_NONE;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_radius_geometry(&rad, &metrics, &emitter) == 0,
          "radius without an arrow succeeds");
    check(c.count == 3, "NONE radius arrow emits line + leader + text");

    rad = make_radius();
    rad.sheet_scale = 2.0;
    memset(&c, 0, sizeof(c));
    c.fail_on_call = -1;
    check(isogl_radius_geometry(&rad, &metrics, &emitter) == 0,
          "scaled radius geometry succeeds");
    if (c.count == 6) {
        check(near(c.items[2].x1, 8.9) &&
              near(c.items[2].y1, 1.1 * tan(M_PI / 12.0)),
              "radius arrow is a 2.2 mm paper size at 1:2");
    }

    /* ---- Circular invalid inputs ----------------------------------------- */
    IsoglDiameterDesc bad_dia = make_diameter();
    bad_dia.px = bad_dia.cx;
    bad_dia.py = bad_dia.cy;
    check(isogl_diameter_geometry(&bad_dia, &metrics, &emitter) == -1,
          "zero-radius diameter rejected");

    IsoglRadiusDesc bad_rad = make_radius();
    bad_rad.px = bad_rad.cx;
    bad_rad.py = bad_rad.cy;
    check(isogl_radius_geometry(&bad_rad, &metrics, &emitter) == -1,
          "zero-radius radius rejected");

    bad_rad = make_radius();
    bad_rad.cx = NAN;
    check(isogl_radius_geometry(&bad_rad, &metrics, &emitter) == -1,
          "non-finite radius input rejected");

    bad_dia = make_diameter();
    bad_dia.arrow_left = (IsoglDimensionArrow)99;
    check(isogl_diameter_geometry(&bad_dia, &metrics, &emitter) == -1,
          "invalid diameter arrow rejected");

    bad_dia = make_diameter();
    bad_dia.precision = 13;
    check(isogl_diameter_geometry(&bad_dia, &metrics, &emitter) == -1,
          "out-of-range diameter precision rejected");

    memset(&c, 0, sizeof(c));
    c.fail_on_call = 1;
    check(isogl_diameter_geometry(&dia, &metrics, &emitter) == -1,
          "diameter emitter failure propagates");

    if (failures == 0) {
        printf("PASSED: 0 failure(s)\n");
        return 0;
    }
    printf("FAILED: %d failure(s)\n", failures);
    return 1;
}
