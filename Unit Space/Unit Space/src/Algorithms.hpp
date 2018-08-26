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

static float pointsSignedArea(const float *begin, const float *end, size_t stride) {
    double sum = 0.0;
    auto prev = (const float *)((char *)end - stride);
    for (auto it = begin; it < end; it = (float *)((char *)it + stride)) {
        sum += (double)prev[0] * (double)it[1] - (double)prev[1] * (double)it[0];
        prev = it;
    }
    return sum * 0.5;
}

static Vector miterVector(float x, float y, float px, float py, float nx, float ny, bool left, float limit, float scale) {
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
    tanx *= scale, tany *= scale;
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
    void *mem0 = malloc(n * sizeof(T)), *mem1 = malloc(n * sizeof(T));
    T *out0 = (T *)mem0, *out1 = (T *)mem1;
    radixSort(out0, begin, n, 0);
    radixSort(out1, out0, n, 8);
    radixSort(out0, out1, n, 16);
    radixSort(begin, out0, n, 24);
    free(mem0);
    free(mem1);
}

// https://math.stackexchange.com/questions/12186/arc-length-of-bézier-curves
//
static inline float quadraticLength(float x0, float y0, float x1, float y1, float x2, float y2, float t) {
    float dx1, dy1, dx2, dy2, a, b, c, l, l1, l2;
    
    dx1 = x1 - x0, dy1 = y1 - y0, dx2 = x2 - x1, dy2 = y2 - y1;
    l1 = dx1 * dx1 + dy1 * dy1, l2 = dx2 * dx2 + dy2 * dy2;
    if (l1 == 0 && l2 == 0)
        return FLT_MAX;
    
    a = (dx1 * dx2 + dy1 * dy2) / l2 * std::sqrt(l2 / l1);
    if (std::abs(a) > 0.999)
        return FLT_MAX;//t * std::sqrt((x2 - x0) * (x2 - x0) + (y2 - y0) * (y2 - y0));
    
    a = (dx2 - dx1) * (dx2 - dx1) + (dy2 - dy1) * (dy2 - dy1);
    b = dx1 * (dx2 - dx1) + dy1 * (dy2 - dy1);
    c = dx1 * dx1 + dy1 * dy1;
    l = (t + b / a) * std::sqrt(c + 2.0 * b * t + a * t * t) + ((a * c - b * b) / std::pow(a, 1.5f)) * std::asinh((a * t + b) / std::sqrt(a * c - b * b));
    return l;
}

static inline void testQuadraticLength(float x0, float y0, float x1, float y1, float x2, float y2) {
    float d0, d1, d;
    
    d0 = quadraticLength(x0, y0, x1, y1, x2, y2, 0.0);
    d1 = quadraticLength(x0, y0, x1, y1, x2, y2, 1.0);
    d = d1 - d0;
    if (d)
        assert(d < std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1)));
}

static inline void solveQuadratic(float A, float B, float C, float *t0, float *t1) {
    *t0 = -C / B, *t1 = -1;
    if (abs(A) > 1e-3) {
        float discriminant = B * B - 4 * A * C, sqrtDiscriminant = sqrt(abs(discriminant)), denominator = 0.5 / A;
        if (discriminant < 0) {
            float real = -B * denominator, imaginary = sqrtDiscriminant * denominator;
            *t0 = sqrt(real * real + imaginary * imaginary);
        } else {
            *t0 = B < 0 ? (-B + sqrtDiscriminant) * denominator : (-B - sqrtDiscriminant) * denominator;
            *t1 = C / (A * *t0);
        }
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
