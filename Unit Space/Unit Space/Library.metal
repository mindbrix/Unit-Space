//
//  Library.metal
//  Unit Space
//
//  Created by Nigel Barber on 22/02/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include <metal_stdlib>
#import "Headers.metal"

using namespace metal;

float ellipseDistance(float2 unitPoint) {
    float x = 0.5 - unitPoint.x, y = 0.5 - unitPoint.y;
    
    float fx = -2.0 * (x * dfdx(x) + y * dfdx(y));
    float fy = -2.0 * (x * dfdy(x) + y * dfdy(y));
    return (0.25 - (x * x + y * y)) * rsqrt(max(1e-13, fx * fx + fy * fy));
}

float cirleStrokeDistance(float2 unitPoint, float w) {
    unitPoint = unitPoint * 2.0 - 1.0;
    float2 nu = normalize(unitPoint);
    
    //    float rad = acos(nu.x);
    //    float t = rad / TWO_PI;
    //    t = clamp(t, 0.20, 0.425);
    //    float x = cos(t * TWO_PI);
    //    float y = sin(t * TWO_PI);
    //    float2 v = (unitPoint - float2(x, y)) / w;
    
    float2 v = (unitPoint - (nu)) / w;
    
    float fx = -2.0 * (v.x * dfdx(v.x) + v.y * dfdx(v.y));
    float fy = -2.0 * (v.x * dfdy(v.x) + v.y * dfdy(v.y));
    return (1.0 - (v.x * v.x + v.y * v.y)) * rsqrt(max(1e-13, fx * fx + fy * fy));
}

float ellipseStrokeDistance(float2 unitPoint, float w) {
    float x = 0.5 - unitPoint.x, y = 0.5 - unitPoint.y;
    return (w - abs(0.5 - sqrt(x * x + y * y))) * rsqrt(abs(dfdx(x) * dfdy(y) - dfdx(y) * dfdy(x)));
}

float quadraticWinding(float2 devicePoint, AffineTransform op) {
    float recipScale = 1.0 / (op.a * op.d - op.b * op.c);
    float a = op.d * recipScale, b = -op.b * recipScale, c = -op.c * recipScale, d = op.a * recipScale;
    
    float ox = devicePoint.x - op.tx, oy = devicePoint.y - op.ty;
    // Shear = { 0.5, 0.0, 0.5, 1.0 }
    float x = (ox * (a + b) + oy * (c + d)) * 0.5, y = ox * b + oy * d;
    //    x = saturate(x);
    
    float fx = dfdx(x) - dfdx(y);
    float fy = dfdy(x) - dfdy(y);
    float sd = sign((op.b + op.d) * recipScale) * (x - y) * rsqrt(fx * fx + fy * fy) + 0.5;
    
    //    float fx = 2.0 * x * dfdx(x) - dfdx(y);
    //    float fy = 2.0 * x * dfdy(x) - dfdy(y);
    //    float sd = sign((op.b + op.d) * recipScale) * (x * x - y) * rsqrt(fx * fx + fy * fy) + 0.5;
    
    float x0 = op.tx, x2 = op.tx + op.a + op.c;
    float sx0 = saturate((devicePoint.x + 0.0) - min(x0, x2));
    float sx1 = saturate((devicePoint.x - 0.0) - max(x0, x2));
    //    float sx1 = step(max(x0, x2), devicePoint.x - 0.5);
    
    float py0 = devicePoint.y - 0.5;
    float sy0 = saturate(op.ty - py0), sy2 = saturate(op.ty + op.b + op.d - py0);
    float coverage = sy2 - sy0;
    
    return sx0 * coverage * max(sx1, saturate(sd));
    //    return sx0 * sign(coverage) * abs(coverage) * max(sx1, saturate(sd));
    //    return sx0 * sign(coverage) * min(abs(coverage), max(sx1, saturate(sd)));
}


float rectDistance(float2 unitPoint) {
    float x = 0.5 - unitPoint.x, y = 0.5 - unitPoint.y;
    
    float a = dfdx(x), b = dfdy(x), c = dfdx(y), d = dfdy(y);
    float sx = max(abs(a), abs(b)) / (a * a + b * b), sy = max(abs(c), abs(d)) / (c * c + d * d);
    
    float x0 = 0.5 - abs(x), y0 = 0.5 - abs(y);
    return saturate(x0 * sx + 0.5) * saturate(y0 * sy + 0.5);
}

float quadraticDistance(float x0, float y0, float x1, float y1, float x2, float y2) {
    float a = x0 * y2 - y0 * x2, b = x1 * y0 - y1 * x0, d = x2 * y1 - y2 * x1;
    float f = 4.0 * b * d - a * a;
    float dfx = dfdx(f), dfy = dfdy(f);
    return f * max(abs(dfx), abs(dfy)) / (dfx * dfx + dfy * dfy);
}

float unitDistance(float2 unitPoint) {
    float x = (unitPoint.x + unitPoint.y) * 0.5, y = unitPoint.y;
    
    float fx = 2.0 * x * dfdx(x) - dfdx(y);
    float fy = 2.0 * x * dfdy(x) - dfdy(y);
    return (y - x * x) * rsqrt(fx * fx + fy * fy);
}


float unitQuadraticWinding(float2 unitPoint) {
    return unitDistance(unitPoint);
    
    float ux = unitPoint.x, uy = unitPoint.y;
    
    float x0 = -ux, x1 = 1.0 - ux, x2 = x1;
    float y0 = -uy, y1 = y0, y2 = 1.0 - uy;
    return quadraticDistance(x0, y0, x1, y1, x2, y2);
    
    
    float a = dfdx(ux), b = dfdy(ux);
    float c = dfdx(uy), d = dfdy(uy);
    
    float determinant = a * d - b * c;
    float recip = 1.0 / determinant;
    AffineTransform it = { d * recip, -b * recip, -c * recip, a * recip, 0, 0 };
    
    
    float dx0 = x0 * it.a + y0 * it.c, dy0 = x0 * it.b + y0 * it.d;
    float dx1 = x1 * it.a + y1 * it.c, dy1 = x1 * it.b + y1 * it.d;
    float dx2 = x2 * it.a + y2 * it.c, dy2 = x2 * it.b + y2 * it.d;
    
    AffineTransform op { dx1 - dx0, dy1 - dy0, dx2 - dx1, dy2 - dy1, dx0, dy0 };
    return quadraticWinding(float2(0, 0), op);
}

float2 unitRepeat(float2 unit, bool rx, bool ry) {
    float2 repeated = abs(fmod(abs(unit), 2.0) - 1.0);
    
    //    float2 repeated = abs(fmod(abs(unit * (fract(unit.x) * fract(unit.y) * sinh(log(abs(unit.x * unit.y))))), 2.0) - 1.0);
    //    float2 repeated = abs(fmod(abs(unit * (fract(unit.x) * fract(unit.y) * sin(abs(unit.x + unit.y)))), 2.0) - 1.0);
    
    //    float2 repeated = abs(fmod(abs(unit * (fract(unit.x) * fract(unit.y) * sin(abs(unit.x * unit.y)))), 2.0) - 1.0);
    //    float2 repeated = abs(fmod(abs(unit * sin(fract(unit.x) * fract(unit.y) * log2(abs(unit.x * unit.y)))), 2.0) - 1.0);
    //    float2 repeated = abs(fmod(abs(unit * sin(log2(abs(unit.x * unit.y)))), 2.0) - 1.0);
    //    float2 repeated = abs(fmod(abs(unit * log2(abs(unit.x * unit.y))), 2.0) - 1.0);
    return { select(unit.x, repeated.x, rx), select(unit.y, repeated.y, ry) };
}
