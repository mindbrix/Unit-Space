//
//  SubPath.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Memory.hpp"

constexpr int kPointStride = 2 * sizeof(float);
// Edges are anti-aliased as implicit quadratics, so curves are no longer flattened into chords. Cubics still split
// finely: where two curves run close, a coarse quadratic strays across the gap between them. Quadratics split only to
// bound their turn, which bounds the size of their hull triangles (drawn and blended by the edge pass) and keeps
// concave controls near their curves.
#ifndef SPLIT_CUBIC_RELATIVE
#define SPLIT_CUBIC_RELATIVE 0.01
#endif
// A cubic's quadratic approximation may stray by kSplitCubicPrecision path units, or by kSplitCubicRelative of the
// cubic's own control polygon length if that is smaller: small glyphs need far tighter tolerances than maps.
constexpr float kSplitCubicPrecision = 0.25, kSplitCubicRelative = SPLIT_CUBIC_RELATIVE, kQuadraticTurnLimit = 1.5707963268; // 90 degrees
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

// Appends a cubic piece's start point and quadratic control. The control is the intersection of the piece's end
// tangents, so consecutive quadratics join with the cubic's own tangents (G1) rather than kinking at every split.
// When the tangents are parallel, meet behind an end, or would move the curve's midpoint by more than allowance from
// the least-error control (3(p1 + p2) - p0 - p3) / 4, the piece is halved instead, using that control at depth. A zero
// allowance always uses the least-error control.
static void appendCubicPiece(Memory<float>& quadratics, const float *p, float allowance, int depth) {
    float mx = (3.f * (p[2] + p[4]) - p[0] - p[6]) * 0.25f, my = (3.f * (p[3] + p[5]) - p[1] - p[7]) * 0.25f;
    float ax = p[2] - p[0], ay = p[3] - p[1], bx = p[6] - p[4], by = p[7] - p[5];
    if (ax == 0 && ay == 0) ax = p[4] - p[0], ay = p[5] - p[1];
    if (bx == 0 && by == 0) bx = p[6] - p[2], by = p[7] - p[3];
    float wx = p[6] - p[0], wy = p[7] - p[1], den = ax * by - ay * bx;
    // Nearly straight pieces (lines arrive from nanosvg as cubics) are drawn as lines, so their joins need no tangents.
    if (allowance == 0 || isStraightCubic(p)) {
        quadratics.push_back(p[0]), quadratics.push_back(p[1]), quadratics.push_back(mx), quadratics.push_back(my);
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
    quadratics.push_back(p[0]), quadratics.push_back(p[1]);
    quadratics.push_back(fits ? cx : mx), quadratics.push_back(fits ? cy : my);
}

// Appends one convex cubic as quadratics, in the fewest equal pieces whose error is within tolerance: a piece covering
// 1/n of the cubic has 1/n^3 of its third difference, and its error as one quadratic is that over the multiplier.
static void appendCubic(Memory<float>& quadratics, const float *cubic, float tolerance, float allowance) {
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

// Loads cubic i of src into c, starting where the last cubic in c ended, extended over the cubics after it that add
// nothing: any whose points all lie within tinylimit of its end (compared with squared distances; glyphs often close with
// several), which are dropped with the end left where it is, or that continue it exactly, the same curve run on to their
// end with their end tangent. Returns the index of the last cubic it took in.
static int mergeCubics(const float *src, int npoints, int i, float tinylimit, float *c) {
    constexpr float kMergeLimit = 1e-3;
    c[0] = c[6], c[1] = c[7];
    memcpy(c + 2, src + i * 2 + 2, 6 * sizeof(float));
    for (bool merged = true; merged && i + 3 < npoints - 1; ) {
        const float *p = src + (i + 3) * 2;
        auto near = [&](float x, float y) { return (x - c[6]) * (x - c[6]) + (y - c[7]) * (y - c[7]) < tinylimit; };
        if (near(p[2], p[3]) && near(p[4], p[5]) && near(p[6], p[7])) {
            i += 3;
            continue;
        }
        merged = false;
        float ax = c[6] - c[4], ay = c[7] - c[5], bx = p[2] - c[6], by = p[3] - c[7], dot = ax * bx + ay * by;
        if (dot <= 0 || std::sqrt(dot * dot / ((ax * ax + ay * ay) * (bx * bx + by * by))) <= 0.99999)
            break;
        // The cubic run on past t = 1 until its last leg reaches the next cubic's first control.
        float cx = p[2] - c[4], cy = p[3] - c[5], rt = 1.0 / std::sqrt((ax * ax + ay * ay) / (cx * cx + cy * cy)), e[8];
        for (int k = 0; k < 2; k++) {
            float n12 = lerp(rt, c[2 + k], c[4 + k]);
            e[2 + k] = lerp(rt, c[k], c[2 + k]), e[4 + k] = lerp(rt, e[2 + k], n12), e[6 + k] = lerp(rt, e[4 + k], lerp(rt, n12, p[2 + k]));
        }
        // Merge only when that ends on the next end point with the next end tangent, so joins stay G1.
        float dx = e[6] - p[6], dy = e[7] - p[7], ex = e[6] - e[4], ey = e[7] - e[5], fx = p[6] - p[4], fy = p[7] - p[5];
        float d = ex * fx + ey * fy;
        if (dx * dx + dy * dy < std::min(kMergeLimit, tinylimit * tinylimit) && d > 0 && d * d > 0.99999f * (ex * ex + ey * ey) * (fx * fx + fy * fy)) {
            c[2] = e[2], c[3] = e[3], c[4] = e[4], c[5] = e[5], c[6] = p[6], c[7] = p[7];
            i += 3, merged = true;
        }
    }
    return i;
}

// The error a cubic's quadratics may have, and how far tangent-preserving pieces may stray from least-error ones
// (zero for least-error pieces). Small curves (glyphs) show kinks far more than offsets, so their pieces keep the cubic's
// tangents. Larger curves keep least-error pieces, as tangent-preserving ones stray one-sidedly by twice as far,
// shifting whole outlines.
static void cubicTolerances(const float *c, float precision, bool tangents, float *tolerance, float *allowance) {
    float length = std::sqrt((c[2] - c[0]) * (c[2] - c[0]) + (c[3] - c[1]) * (c[3] - c[1])) + std::sqrt((c[4] - c[2]) * (c[4] - c[2]) + (c[5] - c[3]) * (c[5] - c[3]))
        + std::sqrt((c[6] - c[4]) * (c[6] - c[4]) + (c[7] - c[5]) * (c[7] - c[5]));
    float relative = kSplitCubicRelative * length;
    *tolerance = std::min(precision, relative);
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
// cubics' tangents (see cubicTolerances), and the spikes of straight cubics, as stroke centrelines need.
static Memory<float> splitCubics(const float *src, int npoints, const float precision, const float tinylimit, bool tangents) {
    Memory<float> quadratics;
    float c[8] = { 0, 0, 0, 0, 0, 0, src[0], src[1] }, head[8], ts[3], tolerance, allowance;
    for (int i = 0; i < npoints - 1; i += 3) {
        i = mergeCubics(src, npoints, i, tinylimit, c);
        // A straight cubic fills as its chord. Any part running on past an end doubles back along itself and encloses
        // nothing, but its pieces would not retrace each other exactly, leaving a sliver whose tip miters run off far
        // beyond the fill. One that ends where it starts is a point, and adds nothing.
        if (!tangents && isStraightCubic(c))
            for (int k = 0; k < 2; k++)
                c[2 + k] = lerp(1.f / 3.f, c[k], c[6 + k]), c[4 + k] = lerp(2.f / 3.f, c[k], c[6 + k]);
        cubicTolerances(c, precision, tangents, & tolerance, & allowance);
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
    quadratics.push_back(c[6]), quadratics.push_back(c[7]);
    return quadratics;
}

static inline float isLeft(const float *P0, const float *P1, const float *P2) {
    return (P1[0] - P0[0]) * (P2[1] - P0[1]) - (P2[0] -  P0[0]) * (P1[1] - P0[1]);
}

// http://geomalgorithms.com/a03-_inclusion.html
//
static size_t pointInPolygon(const float *p, const float *polygon, size_t n, size_t stride) {
    size_t wn = 0; auto mul = stride / sizeof(float);
    for (auto i = 0; i < n; i++) {
        auto p0 = polygon + i * mul; auto p1 = polygon + ((i + 1) % n) * mul;
        if (p0[1] <= p[1]) {
            if (p1[1] > p[1] && isLeft(p0, p1, p) > 0.0f)  ++wn;
        } else {
            if (p1[1] <= p[1] && isLeft(p0, p1, p) < 0.0f)  --wn;
        }
    }
    return wn;
}

struct SubPath {
    enum Type { kLine, kQuadratic, kCubic };
    
    SubPath() {}
    // tangents: cubics convert to tangent-preserving quadratics (see splitCubics). precision: the most their quadratics
    // may stray, in path units.
    SubPath(const float *points, const int npoints, Type type, bool tangents = false, float precision = kSplitCubicPrecision) {
        _area = pointsSignedArea(points, points + npoints * 2, kPointStride);
        boundsLimits(& _bounds.lx, points, points + npoints * 2, kPointStride);
        
        if (type == kCubic) {
            float w = _bounds.ux - _bounds.lx, h = _bounds.uy - _bounds.ly;
            Memory<float> quadratics = splitCubics(points, npoints, precision, 1e-3 * (w < h ? w : h), tangents);
            const int nfloats = (int)quadratics.count();
            if (nfloats > 5) {
                // A gap this small closes on the end, as dropped tiny cubics do, so the last curve keeps its line (an
                // edge skewed by an ULP leaves the sweep in libTess nowhere to put its crossings).
                float dx = quadratics[0] - quadratics[nfloats - 2], dy = quadratics[1] - quadratics[nfloats - 1];
                if (dx * dx + dy * dy < 1e-3)
                    quadratics[0] = quadratics[nfloats - 2], quadratics[1] = quadratics[nfloats - 1];
                _quadratics = subdivideQuadratics(& quadratics[0], nfloats / 2);
            }
        } else if (type == kQuadratic) {
            if (npoints > 2)
                _quadratics = subdivideQuadratics(points, npoints);
        } else {
            float x0, y0, x1, y1;
            x0 = points[0], y0 = points[1];
            _quadratics.push_back(x0), _quadratics.push_back(y0);
            for (int i = 1; i < npoints; i++, x0 = x1, y0 = y1) {
                x1 = points[i * 2], y1 = points[i * 2 + 1];
                _quadratics.push_back((x0 + x1) * 0.5), _quadratics.push_back((y0 + y1) * 0.5);
                _quadratics.push_back(x1), _quadratics.push_back(y1);
            }
        }
    }
    
    int pointWinding(float x, float y) {
        float p[2] = { x, y };
        return (int)pointInPolygon(p, _quadratics.get(), (int)_quadratics.count() / 4 + 1, kPointStride * 2);
    }
    
    // Splits each quadratic at t = 1/2 until it turns by at most kQuadraticTurnLimit.
    static Memory<float> subdivideQuadratics(const float *points, int npoints) {
        Memory<float> quadratics;
        quadratics.push_back(points[0]), quadratics.push_back(points[1]);
        for (int i = 1; i + 1 < npoints; i += 2)
            subdivideQuadratic(quadratics, points + i * 2 - 2, 0);
        return quadratics;
    }
    
    static void subdivideQuadratic(Memory<float>& quadratics, const float *q, int depth) {
        float ax = q[2] - q[0], ay = q[3] - q[1], bx = q[4] - q[2], by = q[5] - q[3];
        float la = ax * ax + ay * ay, lb = bx * bx + by * by;
        if (depth < 8 && la > 0 && lb > 0 && ax * bx + ay * by < std::cos(kQuadraticTurnLimit) * std::sqrt(la * lb)) {
            float x01 = (q[0] + q[2]) * 0.5f, y01 = (q[1] + q[3]) * 0.5f, x12 = (q[2] + q[4]) * 0.5f, y12 = (q[3] + q[5]) * 0.5f;
            float x = (x01 + x12) * 0.5f, y = (y01 + y12) * 0.5f;
            const float a[6] = { q[0], q[1], x01, y01, x, y }, b[6] = { x, y, x12, y12, q[4], q[5] };
            subdivideQuadratic(quadratics, a, depth + 1), subdivideQuadratic(quadratics, b, depth + 1);
        } else
            quadratics.push_back(q[2]), quadratics.push_back(q[3]), quadratics.push_back(q[4]), quadratics.push_back(q[5]);
    }
    
    Memory<float>               _quadratics;
    float                       _area;
    Bounds                      _bounds;
};
