//
//  SubPath.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Memory.hpp"

constexpr int kPointStride = 2 * sizeof(float);
constexpr float kSplitCubicPrecision = 0.1;

// Source: http://caffeineowl.com/graphics/2d/vectorial/cubic2quad01.html
//
static int countSplitCubics(const float *src, const int npoints, const float prec) {
    float x0, y0, x1, y1, x2, y2, x3, y3;
    float xD01, yD01, D01, tcubed;
    int count = 0, calculated;
    
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
    
    for (int i = 0; i < npoints - 1; i += 3) {
        const float *p = src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
        xD01 = x3 + 3.f * (x1 - x2) - x0, yD01 = y3 + 3.f * (y1 - y2) - y0;
        D01 = std::sqrt(xD01 * xD01 + yD01 * yD01);
        tcubed = D01 == 0 ? 1 : prec * multiplier / D01;
        calculated = tcubed >= 1 ? 1 : std::ceil(1.0 / cbrt(tcubed));
        count += calculated;
    }
    return count + 1;
}

static int splitCubics(float *dst, float *cpdst, const float *src, int npoints, const float prec) {
    float *dbegin = dst, x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, cpx, cpy;
    float xD01, yD01, D01, tcubed, t;
    float n01, n12, n23;
    float n012, n123;
    float n0123;
    float dx, dy, ax, ay, bx, by, dot, cosine, cx, cy, rt, tx1, ty1, tx2, ty2, tx3, ty3;
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
    constexpr float kTinyLimit = 1e-6, kMergeLimit = 1e-3;
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
            
            *dst++ = x0, *dst++ = y0;
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
            *cpdst++ = cpx, *cpdst++ = cpy;
        } while (1);
        
        cpx = (3.0 * (x2 + x1) - x0 - x3) * 0.25, cpy = (3.0 * (y2 + y1) - y0 - y3) * 0.25;
        if (cpx != x0 || cpy != y0)
            *dst++ = x0, *dst++ = y0, *cpdst++ = cpx, *cpdst++ = cpy;
    }
    *dst++ = x3, *dst++ = y3;
    return (int)(dst - dbegin) / 2;
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

static int quadraticDivisions(float x0, float y0, float x1, float y1, float x2, float y2) {
    constexpr float kHeight = 0.2;
    float bvx, bvy, cpx, cpy, area, unit;
    bool left, in0, in1;
    bvx = x2 - x0, bvy = y2 - y0;
    left = bvx * (y1 - y0) - bvy * (x1 - x0) > 0;
    cpx = (x0 + x2) * 0.5 + kHeight * bvy * (left ? -1 : 1);
    cpy = (y0 + y2) * 0.5 + kHeight * bvx * (left ? 1 : -1);
    in0 = ((cpx - x0) * (y1 - y0) - (cpy - y0) * (x1 - x0)) * (left ? -1 : 1) > 0;
    in1 = ((x2 - cpx) * (y1 - cpy) - (y2 - cpy) * (x1 - cpx)) * (left ? -1 : 1) > 0;
    area = (x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1);
    unit = bvx * (cpy - y0) - bvy * (cpx - x0);
    return in0 && in1 ? 0 : std::ceil(std::max(0.f, log2f(std::abs(area / unit))));
}

struct SubPath {
    enum Type {
        kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose,
        kPoints, kControlPoints
    };
    
    SubPath() {}
    SubPath(const float *points, const int npoints, Type type) {
        float dx, dy;
        _area = pointsSignedArea(points, points + npoints * 2, kPointStride);
        boundsLimits(& _bounds.lx, points, points + npoints * 2, kPointStride);
        dx = points[0] - points[npoints * 2 - 2], dy = points[1] - points[npoints * 2 - 1];
        
        if (type == kCubic) {
            _npoints = countSplitCubics(points, npoints, kSplitCubicPrecision);
            _points.alloc(_npoints * 2);
            _controlPoints.alloc(_npoints * 2);
            _npoints = splitCubics(_points.get(), _controlPoints.get(), points, npoints, kSplitCubicPrecision);
            if (!(dx == 0 && dy == 0) && dx * dx + dy * dy < 1e-3) {
                float *pts = _points.get();
                pts[_npoints * 2 - 2] = pts[0], pts[_npoints * 2 - 1] = pts[1];
            }
            vector<float> dst;
            writeQuadratics(dst);
            _quadratics.alloc((int)dst.size());
            memcpy(_quadratics.get(), & dst[0], _quadratics.size());
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
    
    void writeQuadratics(vector<float> & dst) {
        float *points, *controlPoints, x0, y0, x1, y1, x2, y2, dt;
        float x01, x12, x012, y01, y12, y012;
        int i, divisions, div;
        if (_npoints > 2) {
            points = _points.get(), controlPoints = _controlPoints.get();
            dst.resize(0);
            for (x0 = points[0], y0 = points[1], i = 0; i < _npoints - 1; i++, x0 = x2, y0 = y2) {
                x1 = controlPoints[i * 2], y1 = controlPoints[i * 2 + 1];
                x2 = points[i * 2 + 2], y2 = points[i * 2 + 3];
                divisions = quadraticDivisions(x0, y0, x1, y1, x2, y2);
                for (div = divisions + 1; div > 0; div--) {
                    dst.emplace_back(x0), dst.emplace_back(y0);
                    if (div == 1)
                        dst.emplace_back(x1), dst.emplace_back(y1);
                    else {
                        dt = 1.0 / float(div);
                        x01 = lerp(dt, x0, x1), x12 = lerp(dt, x1, x2), x012 = lerp(dt, x01, x12);
                        y01 = lerp(dt, y0, y1), y12 = lerp(dt, y1, y2), y012 = lerp(dt, y01, y12);
                        dst.emplace_back(x01), dst.emplace_back(y01);
                        x0 = x012, y0 = y012, x1 = x12, y1 = y12;
                    }
                }
            }
            dst.emplace_back(x2), dst.emplace_back(y2);
        }
    }
    Memory<float>               _points;
    Memory<float>               _controlPoints;
    Memory<float>               _quadratics;
    int                         _npoints;
    float                       _area;
    Bounds                      _bounds;
};
