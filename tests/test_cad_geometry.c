/*
 * Headless test for the pure CAD geometry module.  No OpenGL, no SDL, no
 * CadDocument: only cad_geometry.c and libm.
 */
#include "cad_geometry.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TWO_PI (2.0 * M_PI)

static int failures = 0;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int near(double a, double b, double epsilon) {
    return fabs(a - b) <= epsilon;
}

typedef struct {
    double *x;
    double *y;
    double *distance;
    size_t count;
    size_t capacity;
    int fail_at; /* -1 = never, otherwise fail on this 0-based push */
} Collector;

static void collector_free(Collector *c) {
    free(c->x);
    free(c->y);
    free(c->distance);
    memset(c, 0, sizeof(*c));
}

static int collector_push(Collector *c, double x, double y, double distance) {
    if (c->fail_at >= 0 && (int)c->count == c->fail_at) return -1;
    if (c->count == c->capacity) {
        size_t capacity = c->capacity ? c->capacity * 2 : 256;
        double *x_grown = realloc(c->x, capacity * sizeof(*x_grown));
        if (!x_grown) return -1;
        c->x = x_grown;
        double *y_grown = realloc(c->y, capacity * sizeof(*y_grown));
        if (!y_grown) return -1;
        c->y = y_grown;
        double *d_grown = realloc(c->distance, capacity * sizeof(*d_grown));
        if (!d_grown) return -1;
        c->distance = d_grown;
        c->capacity = capacity;
    }
    c->x[c->count] = x;
    c->y[c->count] = y;
    c->distance[c->count] = distance;
    c->count++;
    return 0;
}

static int collect_point(void *user, double x, double y, double distance) {
    return collector_push((Collector *)user, x, y, distance);
}

static void ellipse_point(double cx, double cy, double rx, double ry,
                          double rotation, double t, double *x, double *y) {
    double ct = cos(t);
    double st = sin(t);
    double cr = cos(rotation);
    double sr = sin(rotation);
    double ex = rx * ct;
    double ey = ry * st;
    *x = cx + ex * cr - ey * sr;
    *y = cy + ex * sr + ey * cr;
}

static double point_segment_distance(double px, double py,
                                     double ax, double ay,
                                     double bx, double by) {
    double dx = bx - ax;
    double dy = by - ay;
    double length2 = dx * dx + dy * dy;
    double t = 0.0;
    if (length2 > 0.0) {
        t = ((px - ax) * dx + (py - ay) * dy) / length2;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}

/*
 * Independent probe: measure the true-ellipse-to-polyline distance at a
 * fraction of every parameter interval.  The fractions used by the test
 * (0.37 and 0.61) are deliberately absent from the implementation's probe set
 * ({1/8, 1/4, 3/8, 1/2, 5/8, 3/4, 7/8}), so the test does not merely mirror
 * the implementation.
 */
static double sampled_sagitta(const Collector *c, double cx, double cy,
                              double rx, double ry, double rotation,
                              double fraction) {
    size_t segments = c->count - 1;
    double max_deviation = 0.0;
    for (size_t i = 0; i < segments; ++i) {
        double t0 = TWO_PI * ((double)i / (double)segments);
        double t1 = TWO_PI * ((double)(i + 1) / (double)segments);
        double t = t0 + (t1 - t0) * fraction;
        double x, y;
        ellipse_point(cx, cy, rx, ry, rotation, t, &x, &y);
        double deviation = point_segment_distance(x, y,
                                                  c->x[i], c->y[i],
                                                  c->x[i + 1], c->y[i + 1]);
        if (deviation > max_deviation) max_deviation = deviation;
    }
    return max_deviation;
}

static void test_segment_count(void) {
    double n5 = cad_chord_segment_count(5.0, TWO_PI, 0.05);
    double n50 = cad_chord_segment_count(50.0, TWO_PI, 0.05);
    double n500 = cad_chord_segment_count(500.0, TWO_PI, 0.05);

    check(fabs(n5 - 23.0) <= 1.0, "chord count r=5 with s=0.05 is ~23");
    check(fabs(n50 - 71.0) <= 1.0, "chord count r=50 with s=0.05 is ~71");
    check(fabs(n500 - 223.0) <= 1.0, "chord count r=500 with s=0.05 is ~223");

    check(cad_chord_segment_count(0.0, TWO_PI, 0.05) == 0.0,
          "zero radius rejected");
    check(cad_chord_segment_count(5.0, 0.0, 0.05) == 0.0,
          "zero sweep rejected");
    check(cad_chord_segment_count(5.0, TWO_PI, 0.0) == 0.0,
          "zero tolerance rejected");
    check(cad_chord_segment_count(NAN, TWO_PI, 0.05) == 0.0,
          "non-finite radius rejected");

    /* Proportional to the sweep, and never below two segments. */
    double full = cad_chord_segment_count(50.0, TWO_PI, 0.05);
    double half = cad_chord_segment_count(50.0, M_PI, 0.05);
    double tiny = cad_chord_segment_count(50.0, 1e-6, 0.05);
    check(fabs(half - full * 0.5) <= 1.0,
          "segment count is proportional to the sweep");
    check(tiny >= 2.0, "non-null arc keeps at least two segments");
}

static void test_arc_tessellation(void) {
    double cx = 1.0, cy = -2.0, r = 7.0, a0 = 0.3, sweep = 1.2;
    Collector c = {0};
    c.fail_at = -1;

    check(cad_tessellate_arc(cx, cy, r, a0, sweep, 0.05,
                             collect_point, &c) == 0,
          "arc tessellation succeeds");
    check(c.count >= 3, "arc emits at least two segments");

    double expected_count = cad_chord_segment_count(r, sweep, 0.05);
    check(near((double)(c.count - 1), expected_count, 1e-9),
          "arc emits the requested number of segments");

    double first_x = cx + r * cos(a0);
    double first_y = cy + r * sin(a0);
    double last_x = cx + r * cos(a0 + sweep);
    double last_y = cy + r * sin(a0 + sweep);
    check(near(c.x[0], first_x, 1e-12) && near(c.y[0], first_y, 1e-12),
          "arc starts exactly at angle a0");
    check(near(c.x[c.count - 1], last_x, 1e-12) &&
          near(c.y[c.count - 1], last_y, 1e-12),
          "arc ends exactly at angle a0 + sweep");
    check(c.distance[0] == 0.0, "first point has zero accumulated distance");
    if (c.count > 2) {
        check(c.distance[c.count - 1] > c.distance[1],
              "accumulated distance grows along the arc");
    }

    /* A full turn closes exactly on the first point. */
    Collector full = {0};
    full.fail_at = -1;
    check(cad_tessellate_arc(0.0, 0.0, 10.0, 0.0, TWO_PI, 0.05,
                             collect_point, &full) == 0,
          "full circle tessellation succeeds");
    check(full.count >= 3 &&
          full.x[0] == full.x[full.count - 1] &&
          full.y[0] == full.y[full.count - 1],
          "full circle closes exactly onto the first point");

    /* A negative (clockwise) sweep is supported symmetrically. */
    Collector cw = {0};
    cw.fail_at = -1;
    check(cad_tessellate_arc(0.0, 0.0, 4.0, 0.0, -M_PI / 2.0, 0.05,
                             collect_point, &cw) == 0,
          "clockwise arc tessellation succeeds");
    check(cw.count > 1 &&
          near(cw.x[cw.count - 1], 0.0, 1e-12) &&
          near(cw.y[cw.count - 1], -4.0, 1e-12),
          "clockwise arc ends at the negative-angle endpoint");

    /* Invalid inputs and sink failure. */
    Collector bad = {0};
    bad.fail_at = -1;
    check(cad_tessellate_arc(0.0, 0.0, 0.0, 0.0, 1.0, 0.05,
                             collect_point, &bad) == -1,
          "zero-radius arc rejected");
    check(cad_tessellate_arc(0.0, 0.0, 1.0, 0.0, 1.0, 0.0,
                             collect_point, &bad) == -1,
          "zero-tolerance arc rejected");
    check(cad_tessellate_arc(0.0, 0.0, 1.0, 0.0, NAN, 0.05,
                             collect_point, &bad) == -1,
          "non-finite sweep rejected");
    check(cad_tessellate_arc(0.0, 0.0, 1.0, 0.0, 1.0, 0.05, NULL,
                             &bad) == -1,
          "NULL sink rejected");

    Collector aborted = {0};
    aborted.fail_at = 2;
    check(cad_tessellate_arc(0.0, 0.0, 10.0, 0.0, TWO_PI, 0.05,
                             collect_point, &aborted) == -1,
          "sink failure aborts arc tessellation");

    collector_free(&c);
    collector_free(&full);
    collector_free(&cw);
    collector_free(&bad);
    collector_free(&aborted);
}

static void test_arc_solving(void) {
    double radius = 0.0;
    double a0 = 0.0;
    double sweep = 0.0;

    /* start (10,0), end (0,10), centre origin: CCW quarter turn. */
    check(cad_arc_three_point(10.0, 0.0, 0.0, 10.0, 0.0, 0.0,
                              &radius, &a0, &sweep) == 0,
          "three-point quarter arc solves");
    check(near(radius, 10.0, 1e-12), "three-point radius");
    check(near(a0, 0.0, 1e-12), "three-point start angle");
    check(near(sweep, M_PI / 2.0, 1e-12),
          "three-point arc turns counter-clockwise");

    /* start (10,0), end (0,-10): the CCW arc is 270 degrees, not -90. */
    check(cad_arc_three_point(10.0, 0.0, 0.0, -10.0, 0.0, 0.0,
                              &radius, &a0, &sweep) == 0,
          "clockwise-looking three-point arc solves as a CCW sweep");
    check(near(sweep, 1.5 * M_PI, 1e-12),
          "right-hand rule wraps the sweep to the CCW side");

    check(cad_arc_three_point(10.0, 0.0, 0.0, 11.0, 0.0, 0.0,
                              &radius, &a0, &sweep) == -1,
          "three-point end off the circle is rejected");
    check(cad_arc_three_point(10.0, 0.0, 10.0, 0.0, 0.0, 0.0,
                              &radius, &a0, &sweep) == -1,
          "three-point null sweep is rejected");
    check(cad_arc_three_point(0.0, 0.0, 10.0, 0.0, 0.0, 0.0,
                              &radius, &a0, &sweep) == -1,
          "three-point start at the centre is rejected");
    check(cad_arc_three_point(NAN, 0.0, 0.0, 10.0, 0.0, 0.0,
                              &radius, &a0, &sweep) == -1,
          "three-point non-finite start is rejected");

    /* Wrap 350 deg -> 10 deg equals a 20 deg CCW arc. */
    double angle350 = 350.0 * M_PI / 180.0;
    double angle10 = 10.0 * M_PI / 180.0;
    check(cad_arc_center_angles(0.0, 0.0, 10.0, angle350, angle10,
                                &a0, &sweep) == 0,
          "center/angle arc across the 0 wrap solves");
    check(near(a0, angle350, 1e-12), "center/angle keeps angle1 as a0");
    check(near(sweep, 20.0 * M_PI / 180.0, 1e-12),
          "wrap 350 -> 10 is a 20 degree sweep");

    check(cad_arc_center_angles(0.0, 0.0, 10.0, 1.0, 1.0,
                                &a0, &sweep) == -1,
          "center/angle null sweep is rejected");
    check(cad_arc_center_angles(0.0, 0.0, 0.0, 0.0, 1.0,
                                &a0, &sweep) == -1,
          "center/angle zero radius is rejected");
    check(cad_arc_center_angles(0.0, 0.0, 10.0, 0.0, NAN,
                                &a0, &sweep) == -1,
          "center/angle non-finite angle is rejected");
    check(cad_arc_center_angles(NAN, 0.0, 10.0, 0.0, 1.0,
                                &a0, &sweep) == -1,
          "center/angle non-finite centre is rejected");
}

static void test_ellipse_solving(void) {
    double cx, cy, rx, ry, rotation;

    check(cad_ellipse_from_axis(0.0, 0.0, 20.0, 0.0, 10.0, 6.0,
                                &cx, &cy, &rx, &ry, &rotation) == 0,
          "ellipse from a horizontal major axis solves");
    check(near(cx, 10.0, 1e-12) && near(cy, 0.0, 1e-12),
          "ellipse centre is the axis midpoint");
    check(near(rx, 10.0, 1e-12), "ellipse semi-major axis");
    check(near(ry, 6.0, 1e-12), "ellipse semi-minor axis is p3's distance");
    check(near(rotation, 0.0, 1e-12), "ellipse rotation follows p1-p2");

    check(cad_ellipse_from_axis(0.0, 0.0, 0.0, 20.0, 6.0, 10.0,
                                &cx, &cy, &rx, &ry, &rotation) == 0,
          "ellipse from a vertical major axis solves");
    check(near(cx, 0.0, 1e-12) && near(cy, 10.0, 1e-12),
          "rotated ellipse centre");
    check(near(rx, 10.0, 1e-12) && near(ry, 6.0, 1e-12),
          "rotated ellipse axes");
    check(near(rotation, M_PI / 2.0, 1e-12),
          "ellipse rotation is atan2 of the major axis");

    check(cad_ellipse_from_axis(1.0, 2.0, 1.0, 2.0, 4.0, 5.0,
                                &cx, &cy, &rx, &ry, &rotation) == -1,
          "zero-length major axis rejected");
    check(cad_ellipse_from_axis(0.0, 0.0, 20.0, 0.0, 10.0, 0.0,
                                &cx, &cy, &rx, &ry, &rotation) == -1,
          "collinear p3 rejects a zero minor axis");
    check(cad_ellipse_from_axis(0.0, 0.0, 20.0, 0.0, 10.0, NAN,
                                &cx, &cy, &rx, &ry, &rotation) == -1,
          "non-finite p3 rejected");
}

static void test_ellipse_tessellation(void) {
    double cx = 3.0, cy = -4.0, rx = 12.0, ry = 5.0, rotation = 0.7;
    double tolerance = 0.05;

    Collector c = {0};
    c.fail_at = -1;
    check(cad_tessellate_ellipse(cx, cy, rx, ry, rotation, tolerance,
                                 collect_point, &c) == 0,
          "ellipse tessellation succeeds");
    check(c.count > 8, "ellipse emits a polyline");

    double expected_first_x, expected_first_y;
    ellipse_point(cx, cy, rx, ry, rotation, 0.0,
                  &expected_first_x, &expected_first_y);
    check(near(c.x[0], expected_first_x, 1e-12) &&
          near(c.y[0], expected_first_y, 1e-12),
          "ellipse starts on the rotated major axis");
    check(c.x[0] == c.x[c.count - 1] && c.y[0] == c.y[c.count - 1],
          "ellipse closes exactly onto the first point");
    check(c.distance[0] == 0.0 && c.distance[c.count - 1] > 0.0,
          "ellipse accumulates chord distance");

    /* The parameter at pi/2 lands on the minor axis, rotated. */
    double minor_x, minor_y;
    ellipse_point(cx, cy, rx, ry, rotation, M_PI / 2.0, &minor_x, &minor_y);
    double min_distance = 1e9;
    for (size_t i = 0; i + 1 < c.count; ++i) {
        double d = point_segment_distance(minor_x, minor_y,
                                          c.x[i], c.y[i],
                                          c.x[i + 1], c.y[i + 1]);
        if (d < min_distance) min_distance = d;
    }
    check(min_distance <= tolerance + 1e-9,
          "minor-axis apex is within the sampled polyline tolerance");

    /* Independent numerical check against the true ellipse, at fractions the
       implementation does not probe. */
    double sagitta_037 = sampled_sagitta(&c, cx, cy, rx, ry, rotation, 0.37);
    double sagitta_061 = sampled_sagitta(&c, cx, cy, rx, ry, rotation, 0.61);
    check(sagitta_037 <= tolerance + 1e-9,
          "ellipse sagitta at 0.37 stays within the chord tolerance");
    check(sagitta_061 <= tolerance + 1e-9,
          "ellipse sagitta at 0.61 stays within the chord tolerance");

    collector_free(&c);

    /* A long, thin ellipse is the worst case for a curvature-only estimate:
       the adaptive refinement must still meet the tolerance. */
    Collector thin = {0};
    thin.fail_at = -1;
    check(cad_tessellate_ellipse(0.0, 0.0, 10.0, 1.0, 0.3, tolerance,
                                 collect_point, &thin) == 0,
          "thin ellipse tessellation succeeds");
    size_t thin_segments = thin.count - 1;
    check(thin_segments > 20, "thin ellipse is refined beyond a coarse count");
    double thin_sagitta_037 =
        sampled_sagitta(&thin, 0.0, 0.0, 10.0, 1.0, 0.3, 0.37);
    double thin_sagitta_061 =
        sampled_sagitta(&thin, 0.0, 0.0, 10.0, 1.0, 0.3, 0.61);
    check(thin_sagitta_037 <= tolerance + 1e-9,
          "thin ellipse sagitta at 0.37 meets the chord tolerance");
    check(thin_sagitta_061 <= tolerance + 1e-9,
          "thin ellipse sagitta at 0.61 meets the chord tolerance");
    check(thin.x[0] == thin.x[thin.count - 1] &&
          thin.y[0] == thin.y[thin.count - 1],
          "thin ellipse closes exactly");

    collector_free(&thin);

    Collector bad = {0};
    bad.fail_at = -1;
    check(cad_tessellate_ellipse(0.0, 0.0, 0.0, 1.0, 0.0, 0.05,
                                 collect_point, &bad) == -1,
          "zero rx rejected");
    check(cad_tessellate_ellipse(0.0, 0.0, 1.0, 0.0, 0.0, 0.05,
                                 collect_point, &bad) == -1,
          "zero ry rejected");
    check(cad_tessellate_ellipse(0.0, 0.0, 1.0, 1.0, NAN, 0.05,
                                 collect_point, &bad) == -1,
          "non-finite rotation rejected");
    check(cad_tessellate_ellipse(0.0, 0.0, 1.0, 1.0, 0.0, 0.0,
                                 collect_point, &bad) == -1,
          "zero tolerance rejected");

    Collector aborted = {0};
    aborted.fail_at = 3;
    check(cad_tessellate_ellipse(0.0, 0.0, 10.0, 5.0, 0.0, 0.05,
                                 collect_point, &aborted) == -1,
          "sink failure aborts ellipse tessellation");

    collector_free(&bad);
    collector_free(&aborted);
}

int main(void) {
    test_segment_count();
    test_arc_tessellation();
    test_arc_solving();
    test_ellipse_solving();
    test_ellipse_tessellation();

    if (failures == 0) {
        printf("PASSED: CAD geometry\n");
        return 0;
    }
    printf("FAILED: %d CAD geometry check(s)\n", failures);
    return 1;
}
