//
//  SubPath.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Memory.hpp"

constexpr int kPointStride = 2 * sizeof(float);
constexpr float kSplitCubicPrecision = 0.1, kFlattenQuadraticPrecision = 0.5;

// Source: http://caffeineowl.com/graphics/2d/vectorial/cubic2quad01.html
//
static int splitCubics(vector<float>& dst, const float *src, int npoints, const float prec) {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, cpx, cpy;
    float xD01, yD01, D01, tcubed, t;
    float n01, n12, n23, n012, n123, n0123;
    float dx, dy, ax, ay, bx, by, dot, cosine, cx, cy, rt, tx1, ty1, tx2, ty2, tx3, ty3;
    constexpr float kTinyLimit = 1e-6, kMergeLimit = 1e-3, multiplier = 10.3923048454; // 18/sqrt(3);;
    for (int i = 0; i < npoints - 1; i += 3) {
        float *p = (float *)src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
        bool merged = true, tiny;
        while (merged && i + 3 < npoints - 1) {
            tiny = merged = false;
            p += 6;
            ax = x3 - x2, ay = y3 - y2;
            bx = p[2] - x3, by = p[3] - y3;
            if (bx * bx + by * by < kTinyLimit) {
                dx = p[4] - x3, dy = p[5] - y3;
                if (dx * dx + dy * dy < kTinyLimit) {
                    dx = p[6] - x3, dy = p[7] - y3;
                    if (dx * dx + dy * dy < kTinyLimit) {
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
            tcubed = D01 == 0 ? 1 : prec * multiplier / D01;
            if (tcubed > 0.999999)
                break;
            
            dst.emplace_back(x0), dst.emplace_back(y0);
            t = cbrt(tcubed);
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
            dst.emplace_back(cpx), dst.emplace_back(cpy);
        } while (1);
        
        cpx = (3.0 * (x2 + x1) - x0 - x3) * 0.25, cpy = (3.0 * (y2 + y1) - y0 - y3) * 0.25;
        if (cpx != x0 || cpy != y0)
            dst.emplace_back(x0), dst.emplace_back(y0), dst.emplace_back(cpx), dst.emplace_back(cpy);
    }
    dst.emplace_back(x3), dst.emplace_back(y3);
    return (int)dst.size() / 2;
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
    SubPath(const float *points, const int npoints, Type type, float precision = kFlattenQuadraticPrecision) {
        float dx, dy;
        _area = pointsSignedArea(points, points + npoints * 2, kPointStride);
        boundsLimits(& _bounds.lx, points, points + npoints * 2, kPointStride);
        dx = points[0] - points[npoints * 2 - 2], dy = points[1] - points[npoints * 2 - 1];
        
        if (type == kCubic) {
            vector<float> split;
            _npoints = splitCubics(split, points, npoints, kSplitCubicPrecision);
            if (_npoints > 2) {
                if (!(dx == 0 && dy == 0) && dx * dx + dy * dy < 1e-3)
                    split[_npoints * 2 - 2] = split[0], split[_npoints * 2 - 1] = split[1];
                _quadratics = flattenQuadratics(& split[0], (int)split.size() / 2, precision);
            }
        } else if (type == kQuadratic) {
            if ((_npoints = npoints) > 4)
                _quadratics = flattenQuadratics(points, npoints, precision);
        } else {
            _npoints = npoints;
            _points.alloc(_npoints * 2);
            memcpy(_points.get(), points, npoints * kPointStride);
            if (dx == 0 && dy == 0)
                _npoints--;
        }
    }
    
    int pointWinding(float x, float y) {
        float p[2] = { x, y };
        if (_quadratics.get())
            return (int)pointInPolygon(p, _quadratics.get(), (int)_quadratics.count() / 4 + 1, kPointStride * 2);
        else
            return (int)pointInPolygon(p, _points.get(), _npoints, kPointStride);
    }
    
    Memory<float> flattenQuadratics(const float *points, int npoints, float precision) {
        vector<float> dst;
        float x0, y0, x1, y1, x2, y2, dx01, dy01, area, t, x01, x12, x012, y01, y12, y012;
        int i;
        for (x0 = *points++, y0 = *points++, i = 1; i < npoints; i += 2, x0 = x2, y0 = y2) {
            x1 = *points++, y1 = *points++, x2 = *points++, y2 = *points++;
            do {
                dst.emplace_back(x0), dst.emplace_back(y0);
                dx01 = x1 - x0, dy01 = y1 - y0, area = dx01 * (y2 - y0) - dy01 * (x2 - x0);
                t = precision / std::abs(area) * std::sqrt(dx01 * dx01 + dy01 * dy01);
                if (t >= 1)
                    dst.emplace_back(x1), dst.emplace_back(y1);
                else {
                    t = 1.0 / std::ceil(1.0 / std::sqrt(t));
                    x01 = lerp(t, x0, x1), x12 = lerp(t, x1, x2), x012 = lerp(t, x01, x12);
                    y01 = lerp(t, y0, y1), y12 = lerp(t, y1, y2), y012 = lerp(t, y01, y12);
                    dst.emplace_back(x01), dst.emplace_back(y01);
                    x0 = x012, y0 = y012, x1 = x12, y1 = y12;
                }
            } while (t < 1);
        }
        dst.emplace_back(x2), dst.emplace_back(y2);
        return Memory<float>(& dst[0], dst.size());
    }
    
    Memory<float>               _points;
    Memory<float>               _quadratics;
    int                         _npoints;
    float                       _area;
    Bounds                      _bounds;
};
