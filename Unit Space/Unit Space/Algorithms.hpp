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

AffineTransform *writeQuadraticCTM(AffineTransform *ctm, float x0, float y0, float x1, float y1, float x2, float y2) {
    *ctm++ = { x1 - x0, y1 - y0, x2 - x1, y2 - y1, x0, y0 };
    return ctm;
}

AffineTransform *writeQuadraticCTMs(AffineTransform *ctm, float x0, float y0, float x1, float y1, float x2, float y2) {
    float dx01 = x1 - x0, dx12 = x2 - x1, dy01 = y1 - y0, dy12 = y2 - y1;
    bool xMonotone = dx01 * dx12 >= 0.f, yMonotone = dy01 * dy12 >= 0.f;
    
    if (xMonotone && yMonotone)
        ctm = writeQuadraticCTM(ctm, x0, y0, x1, y1, x2, y2);
    else {
        float tx = xMonotone ? 1.f : -dx01 / (dx12 - dx01);
        float ty = yMonotone ? 1.f : -dy01 / (dy12 - dy01);
        float t = std::min(tx, ty);
        float x01 = x0 + dx01 * t, y01 = y0 + dy01 * t;
        float x12 = x1 + dx12 * t, y12 = y1 + dy12 * t;
        float x012 = x01 + (x12 - x01) * t, y012 = y01 + (y12 - y01) * t;
        
        ctm = writeQuadraticCTM(ctm, x0, y0, x01, y01, x012, y012);
        ctm = writeQuadraticCTM(ctm, x012, y012, x12, y12, x2, y2);
    }
    return ctm;
}

int WriteQuadratics(AffineTransform *base, AffineTransform &m, Vector *begin, int count) {
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
