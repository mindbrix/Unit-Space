//
//  SubPath.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Memory.hpp"

constexpr int kPointStride = 2 * sizeof(float);
// Edges are anti-aliased as implicit quadratics, so curves are no longer flattened into chords. Cubic error is far
// less visible than faceting, so cubics split coarsely. Quadratics split only to bound their turn, which bounds the
// size of their hull triangles (drawn and blended by the edge pass) and keeps concave controls near their curves.
constexpr float kSplitCubicPrecision = 0.25, kQuadraticTurnLimit = 1.5707963268; // 90 degrees

// Source: http://caffeineowl.com/graphics/2d/vectorial/cubic2quad01.html
//
static Memory<float> splitCubics(const float *src, int npoints, const float precision, const float tinylimit) {
    Memory<float> quadratics;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, cpx, cpy;
    float xD01, yD01, D01, tcubed, t;
    float n01, n12, n23, n012, n123, n0123;
    float dx, dy, ax, ay, bx, by, dot, cosine, cx, cy, rt, tx1, ty1, tx2, ty2, tx3, ty3;
    constexpr float kMergeLimit = 1e-3, multiplier = 10.3923048454; // 18/sqrt(3);;
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
                    dx = tx3 - p[6], dy = ty3 - p[7];
                    if (dx * dx + dy * dy < kMergeLimit) {
                        x1 = tx1, y1 = ty1, x2 = tx2, y2 = ty2, x3 = p[6], y3 = p[7];
                        i += 3;
                        merged = true;
                    }
                }
            }
        }
        do {
            xD01 = x3 + 3.f * (x1 - x2) - x0, yD01 = y3 + 3.f * (y1 - y2) - y0;
            D01 = std::sqrt(xD01 * xD01 + yD01 * yD01);
            tcubed = D01 == 0 ? 1 : precision * multiplier / D01;
            if (tcubed > 0.999999)
                break;
            
            quadratics.push_back(x0), quadratics.push_back(y0);
            t = 1.0 / std::ceil(1.0 / cbrt(tcubed));
            n01 = lerp(t, x0, x1), n12 = lerp(t, x1, x2), n23 = lerp(t, x2, x3);
            n012 = lerp(t, n01, n12), n123 = lerp(t, n12, n23);
            n0123 = lerp(t, n012, n123);
            cpx = (3.0 * (n012 + n01) - x0 - n0123) * 0.25;
            x0 = n0123, x1 = n123, x2 = n23;
            
            n01 = lerp(t, y0, y1), n12 = lerp(t, y1, y2), n23 = lerp(t, y2, y3);
            n012 = lerp(t, n01, n12), n123 = lerp(t, n12, n23);
            n0123 = lerp(t, n012, n123);
            cpy = (3.0 * (n012 + n01) - y0 - n0123) * 0.25;
            y0 = n0123, y1 = n123, y2 = n23;
            quadratics.push_back(cpx), quadratics.push_back(cpy);
        } while (1);
        
        cpx = (3.0 * (x2 + x1) - x0 - x3) * 0.25, cpy = (3.0 * (y2 + y1) - y0 - y3) * 0.25;
        if (cpx != x0 || cpy != y0)
            quadratics.push_back(x0), quadratics.push_back(y0), quadratics.push_back(cpx), quadratics.push_back(cpy);
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
    SubPath(const float *points, const int npoints, Type type) {
        _area = pointsSignedArea(points, points + npoints * 2, kPointStride);
        boundsLimits(& _bounds.lx, points, points + npoints * 2, kPointStride);
        float dx = points[0] - points[npoints * 2 - 2], dy = points[1] - points[npoints * 2 - 1];
        
        if (type == kCubic) {
            float w = _bounds.ux - _bounds.lx, h = _bounds.uy - _bounds.ly;
            Memory<float> quadratics = splitCubics(points, npoints, kSplitCubicPrecision, 1e-3 * (w < h ? w : h));
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
