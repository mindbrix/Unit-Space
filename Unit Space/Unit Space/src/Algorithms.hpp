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

static Vector miterVector(float x, float y, float px, float py, float nx, float ny, bool left, float limit) {
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
    void *mem0 = malloc(n * sizeof(T)), *mem1 = malloc(n * sizeof(T));
    T *out0 = (T *)mem0, *out1 = (T *)mem1;
    radixSort(out0, begin, n, 0);
    radixSort(out1, out0, n, 8);
    radixSort(out0, out1, n, 16);
    radixSort(begin, out0, n, 24);
    free(mem0);
    free(mem1);
}
