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

inline float lerp(float t, float n0, float n1) {
    return (1.f - t) * n0 + t * n1;
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
    // Degenerate corners would divide by zero, and a NaN miter drops its triangles from both passes, leaving holes
    // (stroke outlines double back exactly at inner joins). A coincident neighbour takes the other edge's direction.
    if (len0 == 0 && len1 == 0)
        return { 0, 0 };
    if (len0 == 0)
        vx0 = vx1, vy0 = vy1, len0 = len1;
    if (len1 == 0)
        vx1 = vx0, vy1 = vy0, len1 = len0;
    nx0 = vx0 / len0, ny0 = vy0 / len0;
    nx1 = vx1 / len1, ny1 = vy1 / len1;
    tanx = nx0 + nx1, tany = ny0 + ny1;
    tanlen = std::sqrt(tanx * tanx + tany * tany);
    // An exact reversal has no tangent: its bisector runs along the edge, so the miter is as long as the limit allows.
    if (tanlen < 1e-6f)
        tanx = -ny0, tany = nx0, tanlen = 1;
    tanx /= tanlen, tany /= tanlen;
    mx = tany, my = - tanx;
    normx = ny0, normy = -nx0;
    dot = mx * normx + my * normy;
    dot = dot > limit ? dot : limit;
    dot = left ? -dot : dot;
    return { mx / dot, my / dot };
}
