#include "cad_geometry.h"

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CAD_TWO_PI (2.0 * M_PI)

static int cad_geometry_finite(double value) {
    return isfinite(value) ? 1 : 0;
}

/*
 * Wrapped counter-clockwise delta from `from` to `to`, in (0, 2*pi).  The
 * degenerate answers 0 and 2*pi both mean the two directions are the same ray:
 * the arc is null or ambiguous and the caller rejects it.
 */
static int cad_ccw_sweep(double from, double to, double *sweep) {
    double wrapped = fmod(to - from, CAD_TWO_PI);
    if (wrapped <= 0.0) wrapped += CAD_TWO_PI;
    if (wrapped < CAD_ARC_MIN_SWEEP ||
        wrapped > CAD_TWO_PI - CAD_ARC_MIN_SWEEP) {
        return -1;
    }
    *sweep = wrapped;
    return 0;
}

double cad_chord_segment_count(double radius, double sweep_rad,
                               double tolerance) {
    if (!cad_geometry_finite(radius) ||
        !cad_geometry_finite(sweep_rad) ||
        !cad_geometry_finite(tolerance)) {
        return 0.0;
    }
    if (radius <= 0.0 || tolerance <= 0.0) return 0.0;

    double sweep = fabs(sweep_rad);
    if (sweep <= 0.0) return 0.0;

    /* Clamp the acos argument: tolerance >= 2*radius cannot be resolved by a
       chord, so the maximum step of pi (one segment for a full turn) is used
       and the minimum below takes over. */
    double cosine = 1.0 - tolerance / radius;
    if (cosine < -1.0) cosine = -1.0;
    if (cosine > 1.0) cosine = 1.0;

    double step = acos(cosine);
    if (!(step > 0.0)) return 2.0;

    double full_circle = M_PI / step;
    double count = ceil(full_circle * (sweep / CAD_TWO_PI));
    if (count < 2.0) count = 2.0;
    if (count > CAD_TESSELLATION_MAX_SEGMENTS) {
        count = CAD_TESSELLATION_MAX_SEGMENTS;
    }
    return count;
}

int cad_tessellate_arc(double cx, double cy, double r, double a0, double sweep,
                       double tolerance, CadPointSink sink, void *user) {
    if (!sink) return -1;
    if (!cad_geometry_finite(cx) || !cad_geometry_finite(cy) ||
        !cad_geometry_finite(r) || !cad_geometry_finite(a0) ||
        !cad_geometry_finite(sweep) || !cad_geometry_finite(tolerance)) {
        return -1;
    }
    if (r <= 0.0 || tolerance <= 0.0 || sweep == 0.0) return -1;

    double count = cad_chord_segment_count(r, sweep, tolerance);
    if (count < 2.0) return -1;
    size_t segments = (size_t)count;

    int full_circle = fabs(sweep) >= CAD_TWO_PI - 1e-12;

    double first_x = cx + r * cos(a0);
    double first_y = cy + r * sin(a0);
    if (sink(user, first_x, first_y, 0.0) != 0) return -1;

    double prev_x = first_x;
    double prev_y = first_y;
    double distance = 0.0;

    for (size_t i = 1; i <= segments; ++i) {
        double x;
        double y;
        if (full_circle && i == segments) {
            /* Exact closure: the last point is the first one. */
            x = first_x;
            y = first_y;
        } else {
            double angle = a0 + sweep * ((double)i / (double)segments);
            x = cx + r * cos(angle);
            y = cy + r * sin(angle);
        }
        distance += hypot(x - prev_x, y - prev_y);
        if (sink(user, x, y, distance) != 0) return -1;
        prev_x = x;
        prev_y = y;
    }
    return 0;
}

/* Rotated ellipse point at parameter t. */
static void cad_ellipse_point(double cx, double cy, double rx, double ry,
                              double rotation, double t,
                              double *x, double *y) {
    double ct = cos(t);
    double st = sin(t);
    double cr = cos(rotation);
    double sr = sin(rotation);
    double ex = rx * ct;
    double ey = ry * st;
    *x = cx + ex * cr - ey * sr;
    *y = cy + ex * sr + ey * cr;
}

static double cad_point_segment_distance(double px, double py,
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
    double qx = ax + t * dx;
    double qy = ay + t * dy;
    return hypot(px - qx, py - qy);
}

/*
 * Interior parameter fractions probed in every interval.  The midpoint alone
 * is not enough: the true maximum chord deviation is not guaranteed to sit at
 * t = (t0 + t1) / 2, especially for eccentric ellipses.  Seven samples per
 * segment make the refinement conservative without assuming the location of
 * the maximum.
 */
static const double cad_ellipse_probe_fractions[7] = {
    1.0 / 8.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 2.0,
    5.0 / 8.0, 3.0 / 4.0, 7.0 / 8.0
};

/*
 * Maximum sampled deviation between the true ellipse and its uniform-parameter
 * polyline.  Every interval is probed at seven interior fractions and the
 * largest point-to-chord distance wins.  The initial count uses max(rx, ry)
 * because for uniform t the worst error scales with the major semi-axis (the
 * parameter speed is smallest there while the tangent turns fastest).  If a
 * future change of parametrisation invalidates the estimate, the refinement
 * below still enforces the measured tolerance.
 */
static double cad_ellipse_max_sagitta(double cx, double cy, double rx,
                                      double ry, double rotation,
                                      size_t segments) {
    double max_deviation = 0.0;
    for (size_t i = 0; i < segments; ++i) {
        double t0 = CAD_TWO_PI * ((double)i / (double)segments);
        double t1 = CAD_TWO_PI * ((double)(i + 1) / (double)segments);
        double x0, y0, x1, y1;
        cad_ellipse_point(cx, cy, rx, ry, rotation, t0, &x0, &y0);
        cad_ellipse_point(cx, cy, rx, ry, rotation, t1, &x1, &y1);

        for (size_t k = 0;
             k < sizeof(cad_ellipse_probe_fractions) /
                     sizeof(cad_ellipse_probe_fractions[0]);
             ++k) {
            double t = t0 + (t1 - t0) * cad_ellipse_probe_fractions[k];
            double xm, ym;
            cad_ellipse_point(cx, cy, rx, ry, rotation, t, &xm, &ym);
            double deviation =
                cad_point_segment_distance(xm, ym, x0, y0, x1, y1);
            if (deviation > max_deviation) max_deviation = deviation;
        }
    }
    return max_deviation;
}

int cad_tessellate_ellipse(double cx, double cy, double rx, double ry,
                           double rotation, double tolerance,
                           CadPointSink sink, void *user) {
    if (!sink) return -1;
    if (!cad_geometry_finite(cx) || !cad_geometry_finite(cy) ||
        !cad_geometry_finite(rx) || !cad_geometry_finite(ry) ||
        !cad_geometry_finite(rotation) ||
        !cad_geometry_finite(tolerance)) {
        return -1;
    }
    if (rx <= 0.0 || ry <= 0.0 || tolerance <= 0.0) return -1;

    double major = rx > ry ? rx : ry;
    double estimate = cad_chord_segment_count(major, CAD_TWO_PI, tolerance);
    if (estimate < 2.0) return -1;
    size_t segments = (size_t)estimate;

    /* A handful of refinement rounds is enough: the error is O(1/N^2), so the
       sqrt scaling below reaches the tolerance in one or two rounds. */
    for (int round = 0; round < 8; ++round) {
        double deviation = cad_ellipse_max_sagitta(cx, cy, rx, ry, rotation,
                                                   segments);
        if (!(deviation > tolerance)) break;

        double factor = sqrt(deviation / tolerance);
        double next = ceil((double)segments * factor * 1.02);
        if (!(next > (double)segments)) next = (double)segments + 1.0;
        if (next > CAD_TESSELLATION_MAX_SEGMENTS) {
            next = CAD_TESSELLATION_MAX_SEGMENTS;
        }
        segments = (size_t)next;
        if (segments >= (size_t)CAD_TESSELLATION_MAX_SEGMENTS) break;
    }

    double first_x, first_y;
    cad_ellipse_point(cx, cy, rx, ry, rotation, 0.0, &first_x, &first_y);
    if (sink(user, first_x, first_y, 0.0) != 0) return -1;

    double prev_x = first_x;
    double prev_y = first_y;
    double distance = 0.0;

    for (size_t i = 1; i <= segments; ++i) {
        double x;
        double y;
        if (i == segments) {
            /* Exact closure: the last point is the first one. */
            x = first_x;
            y = first_y;
        } else {
            double t = CAD_TWO_PI * ((double)i / (double)segments);
            cad_ellipse_point(cx, cy, rx, ry, rotation, t, &x, &y);
        }
        distance += hypot(x - prev_x, y - prev_y);
        if (sink(user, x, y, distance) != 0) return -1;
        prev_x = x;
        prev_y = y;
    }
    return 0;
}

int cad_ellipse_from_axis(double p1x, double p1y, double p2x, double p2y,
                          double p3x, double p3y,
                          double *cx, double *cy, double *rx, double *ry,
                          double *rotation) {
    if (!cx || !cy || !rx || !ry || !rotation) return -1;
    if (!cad_geometry_finite(p1x) || !cad_geometry_finite(p1y) ||
        !cad_geometry_finite(p2x) || !cad_geometry_finite(p2y) ||
        !cad_geometry_finite(p3x) || !cad_geometry_finite(p3y)) {
        return -1;
    }

    double dx = p2x - p1x;
    double dy = p2y - p1y;
    double axis_length = hypot(dx, dy);
    if (!(axis_length > 0.0) || !cad_geometry_finite(axis_length)) return -1;

    /* |p3 - line(p1,p2)| = |cross| / |p2 - p1|.  That distance is the
       semi-minor axis; the p1-p2 segment is the major axis. */
    double cross = dx * (p3y - p1y) - dy * (p3x - p1x);
    double minor = fabs(cross) / axis_length;
    double major = 0.5 * axis_length;
    if (!(major > 0.0) || !(minor > 0.0)) return -1;
    if (!cad_geometry_finite(major) || !cad_geometry_finite(minor)) return -1;

    *cx = 0.5 * (p1x + p2x);
    *cy = 0.5 * (p1y + p2y);
    *rx = major;
    *ry = minor;
    *rotation = atan2(dy, dx);
    return 0;
}

int cad_arc_three_point(double sx, double sy, double ex, double ey,
                        double cx, double cy,
                        double *radius, double *a0, double *sweep) {
    if (!radius || !a0 || !sweep) return -1;
    if (!cad_geometry_finite(sx) || !cad_geometry_finite(sy) ||
        !cad_geometry_finite(ex) || !cad_geometry_finite(ey) ||
        !cad_geometry_finite(cx) || !cad_geometry_finite(cy)) {
        return -1;
    }

    double r_start = hypot(sx - cx, sy - cy);
    if (!(r_start > 0.0) || !cad_geometry_finite(r_start)) return -1;

    double r_end = hypot(ex - cx, ey - cy);
    double tolerance = CAD_ARC_RADIUS_ABS_TOL +
                       CAD_ARC_RADIUS_REL_TOL * r_start;
    if (!(fabs(r_end - r_start) <= tolerance)) return -1;

    double start_angle = atan2(sy - cy, sx - cx);
    double end_angle = atan2(ey - cy, ex - cx);
    double delta = 0.0;
    if (cad_ccw_sweep(start_angle, end_angle, &delta) != 0) return -1;

    *radius = r_start;
    *a0 = start_angle;
    *sweep = delta;
    return 0;
}

int cad_arc_center_angles(double cx, double cy, double radius,
                          double angle1, double angle2,
                          double *a0, double *sweep) {
    if (!a0 || !sweep) return -1;
    if (!cad_geometry_finite(cx) || !cad_geometry_finite(cy) ||
        !cad_geometry_finite(radius) ||
        !cad_geometry_finite(angle1) || !cad_geometry_finite(angle2)) {
        return -1;
    }
    if (!(radius > 0.0)) return -1;

    double delta = 0.0;
    if (cad_ccw_sweep(angle1, angle2, &delta) != 0) return -1;

    *a0 = angle1;
    *sweep = delta;
    return 0;
}
