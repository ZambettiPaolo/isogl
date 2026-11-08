#ifndef CAD_GEOMETRY_H
#define CAD_GEOMETRY_H

/*
 * Pure CAD geometry: arc/ellipse parameter solving and chord-tolerance
 * tessellation.  No OpenGL, SDL, CadDocument or any other application state
 * enters this module.  The caller supplies the model-space tolerance and a
 * sink; the module never allocates a persistent vertex array.
 *
 * Paper tolerance.  The chord tolerance is a paper-space quantity: the caller
 * converts it to model units with tolerance_model = CAD_CHORD_TOLERANCE_MM /
 * sheet_scale.  It is deliberately independent of zoom: VBOs live in paper
 * millimetres, so pan/zoom never retessellates.
 */
#define CAD_CHORD_TOLERANCE_MM 0.03
/* Safety cap for pathological radii / aspect ratios.  Reached only when the
   requested tolerance would need an unreasonable VBO. */
#define CAD_TESSELLATION_MAX_SEGMENTS 16384.0

/* Three-point arc radius check: |r_end - r_start| <= ABS + REL * r_start. */
#define CAD_ARC_RADIUS_ABS_TOL 1e-4
#define CAD_ARC_RADIUS_REL_TOL 1e-3
/* Sweeps at or within this epsilon of 0 or 2*pi are ambiguous (same ray). */
#define CAD_ARC_MIN_SWEEP 1e-9

/*
 * Callback receiving consecutive curve points and the accumulated chord
 * length.  The first point has distance 0.  Return 0 to continue, non-zero to
 * abort the tessellation (the generator returns -1).
 */
typedef int (*CadPointSink)(void *user, double x, double y, double distance);

/*
 * N = ceil(pi / acos(1 - tolerance/radius) * |sweep| / (2*pi)) with the acos
 * argument clamped to [-1, 1].  At least 2 for a non-null sweep; 0 for invalid
 * inputs.  The result is capped at CAD_TESSELLATION_MAX_SEGMENTS.
 *
 * For s = 0.05: r = 5 -> 23, r = 50 -> 71, r = 500 -> 223.
 */
double cad_chord_segment_count(double radius, double sweep_rad,
                               double tolerance);

/*
 * Tessellate a circular arc, counter-clockwise for positive sweep.  The first
 * emitted point is exactly (cx + r*cos(a0), cy + r*sin(a0)); the last is the
 * point at a0 + sweep.  A full 2*pi arc closes exactly onto the first point.
 * Returns 0 on success, -1 on invalid parameters or sink failure.
 */
int cad_tessellate_arc(double cx, double cy, double r, double a0, double sweep,
                       double tolerance, CadPointSink sink, void *user);

/*
 * Tessellate a rotated ellipse (rx, ry semi-axes, rotation radians).  The
 * output is closed: the last point is exactly the first.  The segment count
 * starts from a chord-tolerance estimate and is then refined adaptively by
 * checking the maximum mid-chord deviation, so the requested tolerance is met
 * for elongated ellipses too (a count based on the minimum radius of
 * curvature alone under-samples the major-axis vertices, where the parameter
 * step is small but the turning rate is highest).  Each interval is probed at
 * seven interior fractions, not only at its midpoint, so the measured maximum
 * is not tied to one assumed position.
 * Returns 0 on success, -1 on invalid parameters or sink failure.
 */
int cad_tessellate_ellipse(double cx, double cy, double rx, double ry,
                           double rotation, double tolerance,
                           CadPointSink sink, void *user);

/*
 * Solve an ellipse from the major axis p1-p2 and a point p3 off the axis.
 * Outputs centre, semi-axes and rotation = atan2(p2-p1).  ry is the
 * perpendicular distance of p3 from the p1-p2 line.  Rejects degenerate axes
 * and non-finite input.
 * Returns 0 on success, -1 on rejection.
 */
int cad_ellipse_from_axis(double p1x, double p1y, double p2x, double p2y,
                          double p3x, double p3y,
                          double *cx, double *cy, double *rx, double *ry,
                          double *rotation);

/*
 * Solve a CCW arc through start, end and centre.  The radius is
 * |start - centre|; the end must lie on the circle within
 * CAD_ARC_RADIUS_ABS_TOL + CAD_ARC_RADIUS_REL_TOL * radius.  The sweep is the
 * counter-clockwise delta from start to end in (0, 2*pi); a null or ambiguous
 * (nearly full, same-ray) sweep is rejected.
 * Returns 0 on success, -1 on rejection.
 */
int cad_arc_three_point(double sx, double sy, double ex, double ey,
                        double cx, double cy,
                        double *radius, double *a0, double *sweep);

/*
 * Solve a CCW arc from centre, radius and two angles.  sweep is the
 * counter-clockwise delta from angle1 to angle2 in (0, 2*pi); a null or
 * ambiguous sweep is rejected.  a0 is exactly angle1.
 * Returns 0 on success, -1 on rejection.
 */
int cad_arc_center_angles(double cx, double cy, double radius,
                          double angle1, double angle2,
                          double *a0, double *sweep);

#endif /* CAD_GEOMETRY_H */
