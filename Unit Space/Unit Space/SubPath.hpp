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
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
    
    for (int i = 0; i < npoints - 1; i += 3) {
        const float *p = src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
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

static int countSplitQuadratics(float *points, float *controlPoints, int npoints, float unit) {
    int i, divisions, count;
    float x0, y0, x1, y1, x2, y2, area;
    for (x0 = points[0], y0 = points[1], count = i = 0; i < npoints - 1; i++, x0 = x2, y0 = y2) {
        x1 = controlPoints[i * 2], y1 = controlPoints[i * 2 + 1];
        x2 = points[i * 2 + 2], y2 = points[i * 2 + 3];
        area = std::abs((x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1));
        divisions = std::ceil(std::max(0.f, log2f(area / unit)));
        count += divisions;
        if (divisions) {
            float t = 0, dt = 1.0 / float(divisions + 1);
        }
    }
    return count;
}

struct SubPath {
    enum Type {
        kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose,
        kPoints, kControlPoints
    };
    
    SubPath() {}
    SubPath(const float *points, const int npoints, Type type) {
        _closed = (points[0] == points[npoints * 2 - 2] && points[1] == points[npoints * 2 - 1]);
        _area = pointsSignedArea(points, points + _npoints * 2, kPointStride);
        boundsLimits(& _bounds.lx, points, points + _npoints * 2, kPointStride);

        if (type == kCubic) {
            _npoints = countSplitCubics(points, npoints, kSplitCubicPrecision);
            _points.alloc(_npoints * 2);
            _controlPoints.alloc(_npoints * 2);
            _npoints = splitCubics(_points.get(), _controlPoints.get(), points, npoints, kSplitCubicPrecision);
//            int count = countSplitQuadratics(_points.get(), _controlPoints.get(), _npoints, (_bounds.ux - _bounds.lx) * (_bounds.uy - _bounds.ly) * 1e-3);
        } else {
            _npoints = npoints;
            _points.alloc(_npoints * 2);
            memcpy(_points.get(), points, npoints * kPointStride);
            if (_closed)
                _npoints--;
        }
    }
    Memory<float>               _points;
    Memory<float>               _controlPoints;
    bool                        _closed;
    int                         _npoints;
    float                       _area;
    Bounds                      _bounds;
};
