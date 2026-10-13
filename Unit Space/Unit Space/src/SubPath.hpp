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
constexpr float kSplitCubicPrecision = 0.01, kSplitCubicRelative = SPLIT_CUBIC_RELATIVE, kQuadraticTurnLimit = 1.5707963268; // 90 degrees
// Cubics whose relative tolerance is below kCubicTangentSize of kSplitCubicPrecision (control polygons shorter than
// 5 units: glyphs) split into tangent-preserving quadratics, which may stray kCubicTangentSlack times further.
constexpr float kCubicTangentSize = 0.2, kCubicTangentSlack = 1;

// Whether both of a cubic's end tangents lie within half a degree of its chord, so it turns by less than the one
// degree that libTess draws as a line.
static inline bool isStraightCubic(const float *p) {
    constexpr float kStraightSine2 = 7.6152e-5f; // sin^2(0.5 degrees)
    float ax = p[2] - p[0], ay = p[3] - p[1], bx = p[6] - p[4], by = p[7] - p[5], wx = p[6] - p[0], wy = p[7] - p[1];
    if (ax == 0 && ay == 0) ax = p[4] - p[0], ay = p[5] - p[1];
    if (bx == 0 && by == 0) bx = p[6] - p[2], by = p[7] - p[3];
    float aw = ax * wy - ay * wx, bw = bx * wy - by * wx, lw = wx * wx + wy * wy;
    return aw * aw <= kStraightSine2 * (ax * ax + ay * ay) * lw && bw * bw <= kStraightSine2 * (bx * bx + by * by) * lw;
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
        float x01 = (p[0] + p[2]) * 0.5f, y01 = (p[1] + p[3]) * 0.5f, x12 = (p[2] + p[4]) * 0.5f, y12 = (p[3] + p[5]) * 0.5f;
        float x23 = (p[4] + p[6]) * 0.5f, y23 = (p[5] + p[7]) * 0.5f, x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
        float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f, x = (x012 + x123) * 0.5f, y = (y012 + y123) * 0.5f;
        const float a[8] = { p[0], p[1], x01, y01, x012, y012, x, y }, b[8] = { x, y, x123, y123, x23, y23, p[6], p[7] };
        appendCubicPiece(quadratics, a, allowance, depth + 1), appendCubicPiece(quadratics, b, allowance, depth + 1);
        return;
    }
    quadratics.push_back(p[0]), quadratics.push_back(p[1]);
    quadratics.push_back(fits ? cx : mx), quadratics.push_back(fits ? cy : my);
}

// Appends one convex cubic as quadratics, splitting it into equal pieces whose error is within tolerance.
static void appendCubic(Memory<float>& quadratics, const float *c, float tolerance, float allowance) {
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3)
    float x0 = c[0], y0 = c[1], x1 = c[2], y1 = c[3], x2 = c[4], y2 = c[5], x3 = c[6], y3 = c[7], cpx, cpy;
    float xD01, yD01, D01, tcubed, t, n01, n12, n23, n012, n123, n0123;
    // Bounded, as tiny cubics far from the origin have a D01 of rounding noise that never shrinks as they split.
    for (int pieces = 0; pieces < 64; pieces++) {
        xD01 = x3 + 3.f * (x1 - x2) - x0, yD01 = y3 + 3.f * (y1 - y2) - y0;
        D01 = std::sqrt(xD01 * xD01 + yD01 * yD01);
        tcubed = D01 == 0 ? 1 : tolerance * multiplier / D01;
        if (tcubed > 0.999999)
            break;
        
        t = 1.0 / std::ceil(1.0 / cbrt(tcubed));
        n01 = lerp(t, x0, x1), n12 = lerp(t, x1, x2), n23 = lerp(t, x2, x3);
        n012 = lerp(t, n01, n12), n123 = lerp(t, n12, n23);
        n0123 = lerp(t, n012, n123);
        float piece[8] = { x0, 0, n01, 0, n012, 0, n0123, 0 };
        x0 = n0123, x1 = n123, x2 = n23;
        
        n01 = lerp(t, y0, y1), n12 = lerp(t, y1, y2), n23 = lerp(t, y2, y3);
        n012 = lerp(t, n01, n12), n123 = lerp(t, n12, n23);
        n0123 = lerp(t, n012, n123);
        piece[1] = y0, piece[3] = n01, piece[5] = n012, piece[7] = n0123;
        y0 = n0123, y1 = n123, y2 = n23;
        appendCubicPiece(quadratics, piece, allowance, 0);
    }
    
    cpx = (3.0 * (x2 + x1) - x0 - x3) * 0.25, cpy = (3.0 * (y2 + y1) - y0 - y3) * 0.25;
    if (cpx != x0 || cpy != y0) {
        const float piece[8] = { x0, y0, x1, y1, x2, y2, x3, y3 };
        appendCubicPiece(quadratics, piece, allowance, 0);
    }
}

// Source: http://caffeineowl.com/graphics/2d/vectorial/cubic2quad01.html
//
static Memory<float> splitCubics(const float *src, int npoints, const float precision, const float tinylimit, bool tangents) {
    Memory<float> quadratics;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;
    float n12, n23, dx, dy, ax, ay, bx, by, dot, cosine, cx, cy, rt, tx1, ty1, tx2, ty2, tx3, ty3;
    constexpr float kMergeLimit = 1e-3;
    for (int i = 0; i < npoints - 1; i += 3) {
        float *p = (float *)src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
        bool merged = true, tiny;
        while (merged && i + 3 < npoints - 1) {
            tiny = merged = false;
            p += 6;
            ax = x3 - x2, ay = y3 - y2;
            bx = p[2] - x3, by = p[3] - y3;
            if (bx * bx + by * by < tinylimit) {
                dx = p[4] - x3, dy = p[5] - y3;
                if (dx * dx + dy * dy < tinylimit) {
                    dx = p[6] - x3, dy = p[7] - y3;
                    if (dx * dx + dy * dy < tinylimit) {
                        x3 = p[6], y3 = p[7];
                        i += 3;
                        tiny = true;
                    }
                }
            }
            dot = ax * bx + ay * by;
            if (!tiny && dot > 0) {
                cosine = std::sqrt(dot * dot / ((ax * ax + ay * ay) * (bx * bx + by * by)));
                if (cosine > 0.99999) {
                    cx = p[2] - x2, cy = p[3] - y2;
                    rt = 1.0 / std::sqrt((ax * ax + ay * ay) / (cx * cx + cy * cy));
                    tx1 = lerp(rt, x0, x1);
                    n12 = lerp(rt, x1, x2), tx2 = lerp(rt, tx1, n12);
                    n23 = lerp(rt, n12, p[2]), tx3 = lerp(rt, tx2, n23);
                    
                    ty1 = lerp(rt, y0, y1);
                    n12 = lerp(rt, y1, y2), ty2 = lerp(rt, ty1, n12);
                    n23 = lerp(rt, n12, p[3]), ty3 = lerp(rt, ty2, n23);
                    // Merge only when the extension ends on the next endpoint with the next end tangent, so joins stay G1.
                    dx = tx3 - p[6], dy = ty3 - p[7];
                    ax = tx3 - tx2, ay = ty3 - ty2, bx = p[6] - p[4], by = p[7] - p[5];
                    dot = ax * bx + ay * by;
                    if (dx * dx + dy * dy < std::min(kMergeLimit, tinylimit * tinylimit) && dot > 0
                        && dot * dot > 0.99999f * (ax * ax + ay * ay) * (bx * bx + by * by)) {
                        x1 = tx1, y1 = ty1, x2 = tx2, y2 = ty2, x3 = p[6], y3 = p[7];
                        i += 3;
                        merged = true;
                    }
                }
            }
        }
        float length = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1))
            + std::sqrt((x3 - x2) * (x3 - x2) + (y3 - y2) * (y3 - y2));
        // Small curves (glyphs) show kinks far more than offsets, so their pieces keep the cubic's tangents. Larger curves
        // keep least-error pieces, as tangent-preserving ones stray one-sidedly by twice as far, shifting whole outlines.
        float relative = kSplitCubicRelative * length, tolerance = std::min(precision, relative);
        float allowance = relative < kCubicTangentSize * precision ? kCubicTangentSlack * relative : 0;
        // A control on its end point (common in Illustrator art) leaves the cubic's end tangent heading for the other
        // control, which the least-error control ignores: its quadratic hooks off the tangent, then runs straight.
        // Such cubics keep their tangents at any size, within the usual tolerance, as do all cubics when asked (stroke
        // centrelines: least-error pieces kink by degrees where cubics meet, and the stroke turns those into corners).
        float near = 1e-4f * length;
        bool degenerate = (std::abs(x1 - x0) <= near && std::abs(y1 - y0) <= near) || (std::abs(x3 - x2) <= near && std::abs(y3 - y2) <= near);
        if (allowance == 0 && (degenerate || tangents))
            allowance = tolerance;
        // Inflections split first, so every tangent-preserving piece is convex and its end tangents meet ahead of both ends.
        float ax0 = x1 - x0, ay0 = y1 - y0, bx0 = x2 - 2.f * x1 + x0, by0 = y2 - 2.f * y1 + y0;
        float cx0 = x3 - 3.f * x2 + 3.f * x1 - x0, cy0 = y3 - 3.f * y2 + 3.f * y1 - y0;
        float qa = bx0 * cy0 - by0 * cx0, qb = ax0 * cy0 - ay0 * cx0, qc = ax0 * by0 - ay0 * bx0, ts[3];
        float c[8] = { x0, y0, x1, y1, x2, y2, x3, y3 }, done = 0;
        int nts = 0;
        if (allowance && !isStraightCubic(c)) {
            if (std::abs(qa) > 1e-12f) {
                float disc = qb * qb - 4.f * qa * qc;
                if (disc >= 0) {
                    float r = std::sqrt(disc), t0 = (-qb - r) / (2.f * qa), t1 = (-qb + r) / (2.f * qa);
                    if (t0 > t1) std::swap(t0, t1);
                    if (t0 > 1e-3f && t0 < 1 - 1e-3f) ts[nts++] = t0;
                    if (t1 > 1e-3f && t1 < 1 - 1e-3f && t1 - t0 > 1e-3f) ts[nts++] = t1;
                }
            } else if (std::abs(qb) > 1e-12f) {
                float t0 = -qc / qb;
                if (t0 > 1e-3f && t0 < 1 - 1e-3f) ts[nts++] = t0;
            }
        }
        ts[nts++] = 1;
        for (int k = 0; k < nts; k++) {
            // Split the remaining curve c (covering [done, 1]) at the next inflection.
            float u = (ts[k] - done) / (1 - done), head[8];
            if (u < 1) {
                float x01 = lerp(u, c[0], c[2]), y01 = lerp(u, c[1], c[3]), x12 = lerp(u, c[2], c[4]), y12 = lerp(u, c[3], c[5]);
                float x23 = lerp(u, c[4], c[6]), y23 = lerp(u, c[5], c[7]), x012 = lerp(u, x01, x12), y012 = lerp(u, y01, y12);
                float x123 = lerp(u, x12, x23), y123 = lerp(u, y12, y23), x = lerp(u, x012, x123), y = lerp(u, y012, y123);
                const float h[8] = { c[0], c[1], x01, y01, x012, y012, x, y }, r[8] = { x, y, x123, y123, x23, y23, c[6], c[7] };
                memcpy(head, h, sizeof(head)), memcpy(c, r, sizeof(c));
            } else
                memcpy(head, c, sizeof(head));
            appendCubic(quadratics, head, tolerance, allowance);
            done = ts[k];
        }
    }
    quadratics.push_back(x3), quadratics.push_back(y3);
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
    // tangents: cubics convert to tangent-preserving quadratics (see splitCubics).
    SubPath(const float *points, const int npoints, Type type, bool tangents = false) {
        _area = pointsSignedArea(points, points + npoints * 2, kPointStride);
        boundsLimits(& _bounds.lx, points, points + npoints * 2, kPointStride);
        float dx = points[0] - points[npoints * 2 - 2], dy = points[1] - points[npoints * 2 - 1];
        
        if (type == kCubic) {
            float w = _bounds.ux - _bounds.lx, h = _bounds.uy - _bounds.ly;
            Memory<float> quadratics = splitCubics(points, npoints, kSplitCubicPrecision, 1e-3 * (w < h ? w : h), tangents);
            const int nfloats = (int)quadratics.count();
            if (nfloats > 5) {
                if (!(dx == 0 && dy == 0) && dx * dx + dy * dy < 1e-3)
                    quadratics[nfloats - 2] = quadratics[0], quadratics[nfloats - 1] = quadratics[1];
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
