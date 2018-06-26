//
//  Algorithms.hpp
//  Unit Space
//
//  Created by Nigel Barber on 05/02/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Shared.metal.hpp"

#import <algorithm>
#import <cmath>

using namespace std;

#pragma clang diagnostic ignored "-Wcomma"

static void boundsLimits(float *limits, const float *begin, const float *end, size_t stride) {
    float maxX = -FLT_MAX, maxY = -FLT_MAX, minX= FLT_MAX, minY= FLT_MAX;
    
    for (auto it = begin; it < end; it = (float *)((char *)it + stride)) {
        if (it[0] > maxX) maxX = it[0];
        if (it[0] < minX) minX = it[0];
        if (it[1] > maxY) maxY = it[1];
        if (it[1] < minY) minY = it[1];
    }
    limits[0] = minX;  limits[1] = minY;  limits[2] = maxX;  limits[3] = maxY;
}

inline void encodeBits(float *n, uint32_t mask, uint32_t value) {
    *n += copysign(1e-9, *n);
    auto ptr = (uint32_t *)n;
    *ptr = (*ptr & ~mask) | (value & mask);
}

inline float lerp(float t, float n0, float n1) {
    return n0 + t * (n1 - n0);
}

static inline bool linesIntersection(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy, float *px, float *py) {
    double rx = bx - ax, ry = by - ay, sx = dx - cx, sy = dy - cy;
    double r_cross_s = rx * sy - ry * sx;
    
    if (r_cross_s == 0)
        return false;
    
    double pqx = cx - ax, pqy = cy - ay;
    double t = (pqx * sy - pqy * sx) / r_cross_s, u = (pqx * ry - pqy * rx) / r_cross_s;
    if (px)
        *px = ax + rx * t;
    if (py)
        *py = ay + ry * t;
    return t > 0 && t < 1 && u > 0 && u < 1;
}

static float pointsSignedArea(const float *begin, const float *end, size_t stride) {
    double sum = 0.0;
    auto prev = (const float *)((char *)end - stride);
    for (auto it = begin; it < end; it = (float *)((char *)it + stride)) {
        sum += (double)prev[0] * (double)it[1] - (double)prev[1] * (double)it[0];
        prev = it;
    }
    return sum * 0.5;
}

static Vector miterVector(float x, float y, float px, float py, float nx, float ny, bool left) {
    constexpr float limit = 1e-3;
    float vx0, vy0, vx1, vy1, len0, len1;
    float nx0, ny0, nx1, ny1;
    float tanx, tany, tanlen;
    float mx, my, normx, normy, dot;
    vx0 = x - px, vy0 = y - py, vx1 = nx - x, vy1 = ny - y;
    len0 = std::sqrt(vx0 * vx0 + vy0 * vy0), len1 = std::sqrt(vx1 * vx1 + vy1 * vy1);
    nx0 = vx0 / len0, ny0 = vy0 / len0;
    nx1 = vx1 / len1, ny1 = vy1 / len1;
    tanx = nx0 + nx1, tany = ny0 + ny1;
    tanlen = std::sqrt(tanx * tanx + tany * tany);
    tanx /= tanlen, tany /= tanlen;
    mx = tany, my = - tanx;
    normx = ny0, normy = -nx0;
    dot = mx * normx + my * normy;
    dot = dot > limit ? dot : limit;
    dot = left ? -dot : dot;
    return { mx / dot, my / dot };
}

// http://www.codercorner.com/RadixSortRevisited.htm
template<typename T>
static void radixSort(T *out, T *in, int n, int shift) {
    union { unsigned int ix; float x; };
    int counts[256];
    memset(counts, 0, sizeof(counts));
    for (int i = 0; i < n; i++) {
        x = *(float *)(in + i);
        counts[(ix >> shift) & 0xFF]++;
    }
    for (int i = 1; i < 256; i++)
        counts[i] += counts[i - 1];
    
    for (int i = n - 1; i >= 0; i--) {
        x = *(float *)(in + i);
        out[counts[(ix >> shift) & 0xFF] - 1] = in[i];
        counts[(ix >> shift) & 0xFF]--;
    }
}
template<typename T>
static void radixSort(T *begin, T *end) {
    int n = int(end - begin);
    char mem0[n * sizeof(T)], mem1[n * sizeof(T)];
    T *out0 = (T *)mem0, *out1 = (T *)mem1;
    radixSort(out0, begin, n, 0);
    radixSort(out1, out0, n, 8);
    radixSort(out0, out1, n, 16);
    radixSort(begin, out0, n, 24);
}

static inline void solveQuadratic(float A, float B, float C, float *t0, float *t1) {
    *t0 = -C / B;
    if (abs(A) > 1e-3) {
        float discriminant = B * B - 4.0f * A * C, sqrtDiscriminant = sqrt(abs(discriminant));
        float denominator = 1.0f / (2.0f * A);
        
        if (discriminant < 0.0f) {
            float real = -B * denominator, imaginary = sqrtDiscriminant * denominator;
            *t0 = sqrt(real * real + imaginary * imaginary), *t1 = -1;
        } else
            *t0 = (-B + sqrtDiscriminant) * denominator, *t1 = (-B - sqrtDiscriminant) * denominator;
    }
}

static inline void solveTriangle(Vector r, Vector s, Vector u, Vector v, Vector w, Vector x, float *t0, float *t1) {
    float VyXy = v.y - x.y, XySy = x.y - s.y, SyVy = s.y - v.y;
    float UyWy = u.y - w.y, WyRy = w.y - r.y, RyUy = r.y - u.y;
    float A =    s.x * VyXy + v.x * XySy + x.x * SyVy;
    float B =    r.x * VyXy + u.x * XySy + w.x * SyVy + s.x * UyWy + v.x * WyRy + x.x * RyUy;
    float C =    r.x * UyWy + u.x * WyRy + w.x * RyUy;
    solveQuadratic(A, B, C, t0, t1);
}

static AffineTransform *writeQuadraticCTM(AffineTransform *ctm, float x0, float y0, float x1, float y1, float x2, float y2) {
    *ctm++ = { x1 - x0, y1 - y0, x2 - x1, y2 - y1, x0, y0 };
    return ctm;
}


static int WriteQuadratics(AffineTransform *base, AffineTransform &m, Vector *begin, int count) {
    AffineTransform *ctm = base;
    float x0, y0, x1, y1, x2, y2;
    float *q = (float *)begin, *end = (float *)(begin + count);
    
    for (x0 = *q++, y0 = *q++; q < end; x0 = x2, y0 = y2) {
        x1 = *q++, y1 = *q++, x2 = *q++, y2 = *q++;
        
        float _x0 = x0 * m.a + y0 * m.c + m.tx, _y0 = x0 * m.b + y0 * m.d + m.ty;
        float _x1 = x1 * m.a + y1 * m.c + m.tx, _y1 = x1 * m.b + y1 * m.d + m.ty;
        float _x2 = x2 * m.a + y2 * m.c + m.tx, _y2 = x2 * m.b + y2 * m.d + m.ty;
        
        ctm = writeQuadraticCTM(ctm, _x0, _y0, _x1, _y1, _x2, _y2);
    }
    return int(ctm - base);
}

