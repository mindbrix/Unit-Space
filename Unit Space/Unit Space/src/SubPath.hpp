//
//  SubPath.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Path.hpp"

// Edges are anti-aliased as implicit quadratics, so curves are no longer flattened into chords. Cubics still split
// finely: where two curves run close, a coarse quadratic strays across the gap between them. Quadratics split only to
// bound their turn, which bounds the size of their hull triangles (drawn and blended by the edge pass) and keeps
// concave controls near their curves.
#ifndef SPLIT_CUBIC_RELATIVE
#define SPLIT_CUBIC_RELATIVE 0.01
#endif
// A cubic's quadratic approximation may stray by kSplitCubicPrecision path units, or by kSplitCubicRelative of the
// cubic's own control polygon length if that is smaller: small glyphs need far tighter tolerances than maps.
constexpr float kSplitCubicPrecision = 0.25, kSplitCubicRelative = SPLIT_CUBIC_RELATIVE;
// Cubics whose relative tolerance is below kCubicTangentSize of kSplitCubicPrecision (control polygons shorter than
// 5 units: glyphs) split into tangent-preserving quadratics, which may stray kCubicTangentSlack times further.
constexpr float kCubicTangentSize = 0.2, kCubicTangentSlack = 1;

// Whether both of a cubic's end tangents lie within half a degree of its chord, so it turns by less than the one
// degree that libTess draws as a line. A cubic that ends where it starts has no chord: it is straight if it runs out
// along its start tangent and back.
static inline bool isStraightCubic(const float *p) {
    constexpr float kStraightSine2 = 7.6152e-5f; // sin^2(0.5 degrees)
    float ax = p[2] - p[0], ay = p[3] - p[1], bx = p[6] - p[4], by = p[7] - p[5], wx = p[6] - p[0], wy = p[7] - p[1];
    if (ax == 0 && ay == 0) ax = p[4] - p[0], ay = p[5] - p[1];
    if (bx == 0 && by == 0) bx = p[6] - p[2], by = p[7] - p[3];
    if (wx == 0 && wy == 0) wx = ax, wy = ay;
    float aw = ax * wy - ay * wx, bw = bx * wy - by * wx, lw = wx * wx + wy * wy;
    return aw * aw <= kStraightSine2 * (ax * ax + ay * ay) * lw && bw * bw <= kStraightSine2 * (bx * bx + by * by) * lw;
}

// Splits cubic c at t (de Casteljau): a covers [0, t] and b [t, 1]. b may be c.
static inline void splitCubic(const float *c, float t, float *a, float *b) {
    for (int k = 0; k < 2; k++) {
        float p01 = lerp(t, c[k], c[2 + k]), p12 = lerp(t, c[2 + k], c[4 + k]), p23 = lerp(t, c[4 + k], c[6 + k]);
        float p012 = lerp(t, p01, p12), p123 = lerp(t, p12, p23), p = lerp(t, p012, p123);
        a[k] = c[k], a[2 + k] = p01, a[4 + k] = p012, a[6 + k] = p;
        b[k] = p, b[2 + k] = p123, b[4 + k] = p23, b[6 + k] = c[6 + k];
    }
}

// The point at t on cubic c.
static inline void cubicPoint(const float *c, float t, float *p) {
    float u = 1 - t, b0 = u * u * u, b1 = 3.f * u * u * t, b2 = 3.f * u * t * t, b3 = t * t * t;
    p[0] = b0 * c[0] + b1 * c[2] + b2 * c[4] + b3 * c[6], p[1] = b0 * c[1] + b1 * c[3] + b2 * c[5] + b3 * c[7];
}

// Splits quadratic q at t = 1/2 until each piece turns by at most a right angle, when its legs' dot product is not
// negative, appending the pieces' controls and ends.
static void subdivideQuadratic(vector<float>& quadratics, const float *q, int depth) {
    float ax = q[2] - q[0], ay = q[3] - q[1], bx = q[4] - q[2], by = q[5] - q[3];
    if (depth < 8 && ax * bx + ay * by < 0) {
        float x01 = (q[0] + q[2]) * 0.5f, y01 = (q[1] + q[3]) * 0.5f, x12 = (q[2] + q[4]) * 0.5f, y12 = (q[3] + q[5]) * 0.5f;
        float x = (x01 + x12) * 0.5f, y = (y01 + y12) * 0.5f;
        const float a[6] = { q[0], q[1], x01, y01, x, y }, b[6] = { x, y, x12, y12, q[4], q[5] };
        subdivideQuadratic(quadratics, a, depth + 1), subdivideQuadratic(quadratics, b, depth + 1);
    } else
        quadratics.push_back(q[2]), quadratics.push_back(q[3]), quadratics.push_back(q[4]), quadratics.push_back(q[5]);
}

// A chain of quadratics, written to out as splitCubics() makes them, a start point and control at a time: each is
// subdivided (subdivideQuadratic()) once its end, the next one's start, arrives. A chain of none writes nothing.
struct QuadraticChain {
    QuadraticChain(vector<float>& out) : out(out) {}
    void add(float x, float y, float cx, float cy) {
        if (count++ == 0)
            out.push_back(x), out.push_back(y);
        else
            end(x, y);
        p[0] = x, p[1] = y, p[2] = cx, p[3] = cy;
    }
    void end(float x, float y) {
        if (count == 0)
            return;
        const float q[6] = { p[0], p[1], p[2], p[3], x, y };
        subdivideQuadratic(out, q, 0);
    }
    vector<float>& out;
    float p[4];
    int count = 0;
};

// Appends a cubic piece's start point and quadratic control. The control is the intersection of the piece's end
// tangents, so consecutive quadratics join with the cubic's own tangents (G1) rather than kinking at every split.
// When the tangents are parallel, meet behind an end, or would move the curve's midpoint by more than allowance from
// the least-error control (3(p1 + p2) - p0 - p3) / 4, the piece is halved instead, using that control at depth. A zero
// allowance always uses the least-error control.
static void appendCubicPiece(QuadraticChain& quadratics, const float *p, float allowance, int depth) {
    float mx = (3.f * (p[2] + p[4]) - p[0] - p[6]) * 0.25f, my = (3.f * (p[3] + p[5]) - p[1] - p[7]) * 0.25f;
    float ax = p[2] - p[0], ay = p[3] - p[1], bx = p[6] - p[4], by = p[7] - p[5];
    if (ax == 0 && ay == 0) ax = p[4] - p[0], ay = p[5] - p[1];
    if (bx == 0 && by == 0) bx = p[6] - p[2], by = p[7] - p[3];
    float wx = p[6] - p[0], wy = p[7] - p[1], den = ax * by - ay * bx;
    // Nearly straight pieces (lines arrive from nanosvg as cubics) are drawn as lines, so their joins need no tangents.
    if (allowance == 0 || isStraightCubic(p)) {
        quadratics.add(p[0], p[1], mx, my);
        return;
    }
    float s = den == 0 ? -1 : (wx * by - wy * bx) / den, u = den == 0 ? -1 : (ax * wy - ay * wx) / den;
    float cx = p[0] + s * ax, cy = p[1] + s * ay;
    // The quadratic's midpoint moves by half the control's displacement.
    bool fits = s > 0 && u > 0 && (cx - mx) * (cx - mx) + (cy - my) * (cy - my) <= 4.f * allowance * allowance;
    if (!fits && depth < 2) {
        float a[8], b[8];
        splitCubic(p, 0.5f, a, b);
        appendCubicPiece(quadratics, a, allowance, depth + 1), appendCubicPiece(quadratics, b, allowance, depth + 1);
        return;
    }
    quadratics.add(p[0], p[1], fits ? cx : mx, fits ? cy : my);
}

// Appends one convex cubic as quadratics, in the fewest equal pieces whose error is within tolerance: a piece covering
// 1/n of the cubic has 1/n^3 of its third difference, and its error as one quadratic is that over the multiplier.
static void appendCubic(QuadraticChain& quadratics, const float *cubic, float tolerance, float allowance) {
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3)
    float dx = cubic[6] + 3.f * (cubic[2] - cubic[4]) - cubic[0], dy = cubic[7] + 3.f * (cubic[3] - cubic[5]) - cubic[1];
    float r = std::sqrt(dx * dx + dy * dy) / (tolerance * multiplier);
    int n = r > 1 ? (int)std::ceil(std::cbrt(r)) : 1;
    float c[8], piece[8];
    memcpy(c, cubic, sizeof(c));
    for (int k = n; k > 1; k--) {
        splitCubic(c, 1.f / k, piece, c);
        appendCubicPiece(quadratics, piece, allowance, 0);
    }
    // A cubic that is a point adds nothing.
    float cpx = (3.0 * (c[4] + c[2]) - c[0] - c[6]) * 0.25, cpy = (3.0 * (c[5] + c[3]) - c[1] - c[7]) * 0.25;
    if (cpx != c[0] || cpy != c[1])
        appendCubicPiece(quadratics, c, allowance, 0);
}

// The error a cubic's quadratics may have, and how far tangent-preserving pieces may stray from least-error ones
// (zero for least-error pieces). Small curves (glyphs) show kinks far more than offsets, so their pieces keep the cubic's
// tangents. Larger curves keep least-error pieces, as tangent-preserving ones stray one-sidedly by twice as far,
// shifting whole outlines.
// The error is no finer than limit, the subpath's vertex merge limit, which merges any finer detail away. That also
// bounds a cubic's pieces at any size or magnitude: its third difference is at most 8 times its subpath's extent, and
// limit is at least 1e-5 of that extent, so appendCubic() makes at most cbrt(8 / (1e-5 * 18 / sqrt(3))), 43, of them.
static void cubicTolerances(const float *c, float precision, float limit, bool tangents, float *tolerance, float *allowance) {
    float length = std::sqrt((c[2] - c[0]) * (c[2] - c[0]) + (c[3] - c[1]) * (c[3] - c[1])) + std::sqrt((c[4] - c[2]) * (c[4] - c[2]) + (c[5] - c[3]) * (c[5] - c[3]))
        + std::sqrt((c[6] - c[4]) * (c[6] - c[4]) + (c[7] - c[5]) * (c[7] - c[5]));
    float relative = kSplitCubicRelative * length;
    *tolerance = std::max(std::min(precision, relative), limit);
    *allowance = relative < kCubicTangentSize * precision ? kCubicTangentSlack * relative : 0;
    // A control on its end point (common in Illustrator art) leaves the cubic's end tangent heading for the other
    // control, which the least-error control ignores: its quadratic hooks off the tangent, then runs straight. Such
    // cubics keep their tangents at any size, within the usual tolerance, as do all cubics when asked (stroke centrelines:
    // least-error pieces kink by degrees where cubics meet, and the stroke turns those into corners).
    float near = 1e-4f * length;
    bool degenerate = (std::abs(c[2] - c[0]) <= near && std::abs(c[3] - c[1]) <= near) || (std::abs(c[6] - c[4]) <= near && std::abs(c[7] - c[5]) <= near);
    if (*allowance == 0 && (degenerate || tangents))
        *allowance = *tolerance;
}

// A cubic's inflections in order, where its curvature changes sign, skipping any closer than tolerance to an end or to
// each other: where a control sits on its end point the curve barely moves near that end, so splitting there in
// parameter space leaves a piece a few ULPs long.
static int cubicInflections(const float *c, float tolerance, float *ts) {
    float ax = c[2] - c[0], ay = c[3] - c[1], bx = c[4] - 2.f * c[2] + c[0], by = c[5] - 2.f * c[3] + c[1];
    float cx = c[6] - 3.f * c[4] + 3.f * c[2] - c[0], cy = c[7] - 3.f * c[5] + 3.f * c[3] - c[1];
    float qa = bx * cy - by * cx, qb = ax * cy - ay * cx, qc = ax * by - ay * bx, roots[2];
    int nroots = 0, n = 0;
    if (std::abs(qa) > 1e-12f) {
        float disc = qb * qb - 4.f * qa * qc;
        if (disc >= 0) {
            float r = std::sqrt(disc), t0 = (-qb - r) / (2.f * qa), t1 = (-qb + r) / (2.f * qa);
            roots[0] = std::min(t0, t1), roots[1] = std::max(t0, t1), nroots = 2;
        }
    } else if (std::abs(qb) > 1e-12f)
        roots[nroots++] = -qc / qb;
    float last[2] = { c[0], c[1] }, p[2];
    for (int k = 0; k < nroots; k++) {
        if (!(roots[k] > 0 && roots[k] < 1))
            continue;
        cubicPoint(c, roots[k], p);
        if (std::hypot(p[0] - last[0], p[1] - last[1]) > tolerance && std::hypot(p[0] - c[6], p[1] - c[7]) > tolerance)
            ts[n++] = roots[k], last[0] = p[0], last[1] = p[1];
    }
    return n;
}

// Source: http://caffeineowl.com/graphics/2d/vectorial/cubic2quad01.html
//
// Converts a chain of cubics (p0, c, c, p1, c, c, p2 ...) to quadratics (p0, c, p1, c, p2 ...). tangents: keep the
// cubics' tangents (see cubicTolerances), and the spikes of straight cubics, as stroke centrelines need. limit: the
// chain's vertex merge limit.
static void splitCubics(const float *src, int npoints, const float precision, float limit, bool tangents, QuadraticChain& quadratics) {
    float c[8] = { 0, 0, 0, 0, 0, 0, src[0], src[1] }, head[8], ts[3], tolerance, allowance;
    for (int i = 0; i < npoints - 1; i += 3) {
        c[0] = c[6], c[1] = c[7];
        memcpy(c + 2, src + i * 2 + 2, 6 * sizeof(float));
        // A straight cubic fills as its chord. Any part running on past an end doubles back along itself and encloses
        // nothing, but its pieces would not retrace each other exactly, leaving a sliver whose tip miters run off far
        // beyond the fill. One that ends where it starts is a point, and adds nothing.
        if (!tangents && isStraightCubic(c))
            for (int k = 0; k < 2; k++)
                c[2 + k] = lerp(1.f / 3.f, c[k], c[6 + k]), c[4 + k] = lerp(2.f / 3.f, c[k], c[6 + k]);
        cubicTolerances(c, precision, limit, tangents, & tolerance, & allowance);
        // Inflections split first, so every tangent-preserving piece is convex and its end tangents meet ahead of both ends.
        int n = allowance && !isStraightCubic(c) ? cubicInflections(c, tolerance, ts) : 0;
        ts[n++] = 1;
        float done = 0;
        for (int k = 0; k < n; done = ts[k++]) {
            // The rest of the cubic, c, covers [done, 1].
            float u = (ts[k] - done) / (1 - done);
            if (u < 1)
                splitCubic(c, u, head, c);
            else
                memcpy(head, c, sizeof(head));
            appendCubic(quadratics, head, tolerance, allowance);
        }
    }
    quadratics.end(c[6], c[7]);
}

// Subdivides each of a chain's quadratics (subdivideQuadratic()) into out.
static void subdivideQuadratics(const float *points, int npoints, vector<float>& out) {
    out.push_back(points[0]), out.push_back(points[1]);
    for (int i = 1; i + 1 < npoints; i += 2)
        subdivideQuadratic(out, points + i * 2 - 2, 0);
}

// Merges each run of on-curve points closer than limit into its first, as libTess would, dropping the quadratics
// between them. The gap fades along the quadratic after (its control moves half as far), so lines stay straight. In a
// closed contour a run reaching the end wraps through the start, so the start moves onto its first point instead (an
// edge skewed by an ULP leaves the sweep in libTess nowhere to put its crossings); in an open one the last
// quadratic has none after it, so the one before takes its end.
// A quadratic whose control lies within limit of its chord is a line, its control moved to the chord's midpoint, so
// no near-straight curve leaves a control vertex beside its end for libTess. Consecutive lines join while every
// vertex between stays within limit of the joined line, so neither moves the outline further than limit. That
// includes a line doubling back on the one before, a spike enclosing nothing, and the closing line libTess adds
// from a closed subpath's end to its start.
// It writes the merged quadratics over the contour's own, as it never writes ahead of what it has yet to read, and
// returns their floats.
static size_t mergeContour(float *q, size_t count, float limit, bool closed) {
    int n = ((int)count - 2) / 4;
    if (n < 2)
        return count;
    auto near = [&](const float *a, const float *b) { return std::abs(a[0] - b[0]) < limit && std::abs(a[1] - b[1]) < limit; };
    // Moves an end point, its quadratic's control moving half as far.
    auto moveOnto = [](float *point, float *control, const float *to) {
        control[0] += (to[0] - point[0]) * 0.5f, control[1] += (to[1] - point[1]) * 0.5f, point[0] = to[0], point[1] = to[1];
    };
    auto straighten = [](float *quad) { quad[2] = (quad[0] + quad[4]) * 0.5f, quad[3] = (quad[1] + quad[5]) * 0.5f; };
    // Whether x lies within limit of the line through a and b.
    auto onLine = [&](const float *a, const float *x, const float *b) {
        float bx = b[0] - a[0], by = b[1] - a[1], cross = bx * (x[1] - a[1]) - by * (x[0] - a[0]);
        return cross * cross < limit * limit * (bx * bx + by * by);
    };
    // Whether x lies within limit of the line through a and b, and projects strictly between them.
    auto between = [&](const float *a, const float *x, const float *b) {
        float bx = b[0] - a[0], by = b[1] - a[1], dot = bx * (x[0] - a[0]) + by * (x[1] - a[1]);
        return onLine(a, x, b) && dot > 0 && dot < bx * bx + by * by;
    };
    size_t size = 2;    // the merged floats so far, from q
    auto last = [&]() { return q + size - 2; };
    int run = -1;  // the first vertex of the current run of joined lines
    // A run's first vertex can be the merged end point that its joins move, so it is kept.
    float first[2];
    for (int k = 0; k < n; k++) {
        const float *e = q + k * 4 + 4;
        float *p = last();
        if (near(p, e) && (k + 1 < n || closed))
            continue;
        if (near(p, e) && size > 2) {
            moveOnto(p, p - 2, e);
            continue;
        }
        float quad[6] = { e[-4], e[-3], e[-2], e[-1], e[0], e[1] };
        moveOnto(quad, quad + 2, p);
        bool line = between(quad, quad + 2, quad + 4);
        if (line)
            straighten(quad);
        if (size > 2 && line && between(p - 4, p - 2, p)) {
            bool on = true;
            for (int j = run < 0 ? k : run; on && j <= k; j++)
                on = onLine(p - 4, j == run ? first : q + j * 4, e);
            if (on) {
                if (run < 0)
                    run = k, first[0] = q[k * 4], first[1] = q[k * 4 + 1];
                // A spike folding back within limit of where it started goes altogether.
                if (near(p - 4, e) && k + 1 < n)
                    size -= 4, run = -1;
                else
                    p[0] = e[0], p[1] = e[1], straighten(p - 4);
                continue;
            }
        }
        run = -1;
        memcpy(q + size, quad + 2, 4 * sizeof(float)), size += 4;
    }
    // Closing on the start can bring the vertex before within limit of it in turn, so repeat until nothing moves.
    for (bool moved = closed; moved && size > 6; ) {
        float *start = q, *end = last();
        bool closes = end[0] == start[0] && end[1] == start[1];
        moved = true;
        if (near(closes ? end - 4 : end, start)) {
            if (closes)
                size -= 4, end = last();
            moveOnto(start, start + 2, end);
        } else if (!closes && between(end - 4, end - 2, end) && onLine(end - 4, end, start))
            end[0] = start[0], end[1] = start[1], straighten(end - 4);
        else
            moved = false;
    }
    return size;
}

// Appends a contour as a quadratic chain to out, if it lies within coordinateLimit() and makes a quadratic: cubics split
// (tangents: tangent-preserving, see splitCubics), quadratics subdivided, and lines with their midpoints as controls.
// out's bounds unite with its points'.
static void appendQuadratics(Path& out, const float *points, int npoints, Path::Kind kind, bool tangents) {
    Bounds b;
    float limit = coordinateLimit();
    boundsLimits(& b.lx, points, points + npoints * 2, kPointStride);
    if (!(b.lx >= -limit && b.ux <= limit && b.ly >= -limit && b.uy <= limit))
        return;
    vector<float>& q = out._points;
    size_t first = q.size();
    if (kind == Path::kCubics) {
        QuadraticChain chain(q);
        splitCubics(points, npoints, kSplitCubicPrecision, vertexMergeLimit(b), tangents, chain);
    } else if (kind == Path::kQuadratics) {
        if (npoints > 2)
            subdivideQuadratics(points, npoints, q);
    } else {
        float x0 = points[0], y0 = points[1], x1, y1;
        q.push_back(x0), q.push_back(y0);
        for (int i = 1; i < npoints; i++, x0 = x1, y0 = y1) {
            x1 = points[i * 2], y1 = points[i * 2 + 1];
            q.push_back((x0 + x1) * 0.5), q.push_back((y0 + y1) * 0.5), q.push_back(x1), q.push_back(y1);
        }
    }
    if (q.size() - first < 6) {
        q.resize(first);
        return;
    }
    out._contours.push_back({ first, int(q.size() - first) / 2, Path::kQuadratics, true });
    out._bounds = out._contours.size() == 1 ? b : out._bounds.united(b);
}

// Merges the vertices of each of a quadratic path's contours (mergeContour()), closing the gaps they leave.
static void mergeVertices(Path& path, float limit, bool closed) {
    size_t size = 0;
    for (Path::Contour& c : path._contours) {
        float *q = path._points.data();
        if (size != c.first)
            memmove(q + size, q + c.first, c.npoints * 2 * sizeof(float));
        c.first = size, c.npoints = int(mergeContour(q + size, c.npoints * 2, limit, closed)) / 2, size += c.npoints * 2;
    }
    path._points.resize(size);
}

// Whether a quadratic path is only lines: their controls are their chords' exact midpoints, as lines are written.
static bool isFlat(const Path& path) {
    const vector<float>& q = path._points;
    for (const Path::Contour& c : path._contours)
        for (size_t i = c.first; i + 6 <= c.first + c.npoints * 2; i += 4)
            if (q[i + 2] != (q[i] + q[i + 4]) * 0.5f || q[i + 3] != (q[i + 1] + q[i + 5]) * 0.5f)
                return false;
    return true;
}
