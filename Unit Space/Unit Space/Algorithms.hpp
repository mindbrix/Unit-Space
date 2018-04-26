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
    return n0 + t * (n1 - n0);
}

static inline bool doLinesIntersect(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy) {
    double vabx = bx - ax, vaby = by - ay, vacx = cx - ax, vacy = cy - ay, vadx = dx - ax, vady = dy - ay;
    double vcdx = dx - cx, vcdy = dy - cy, vcax = ax - cx, vcay = ay - cy, vcbx = bx - cx, vcby = by - cy;
    double cabc = vabx * vacy - vaby * vacx, cabd = vabx * vady - vaby * vadx;
    double ccda = vcdx * vcay - vcdy * vcax, ccdb = vcdx * vcby - vcdy * vcbx;
    return cabc * cabd < 0 && ccda * ccdb < 0;
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
