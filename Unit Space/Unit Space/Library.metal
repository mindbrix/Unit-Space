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

float quadraticWinding(float2 devicePoint, AffineTransform op) {
    float x0 = op.tx, y0 = op.ty;
    float x1 = op.tx + op.a, y1 = op.ty + op.b;
    float x2 = x1 + op.c, y2 = y1 + op.d;
    
    x0 -= devicePoint.x, x1 -= devicePoint.x, x2 -= devicePoint.x;
    y0 -= devicePoint.y, y1 -= devicePoint.y, y2 -= devicePoint.y;
    
    float2 from = { x0, y0 }, ctrl = { x1, y1 }, to = { x2, y2 };
    
    // Determine winding, and sort into a consistent order so we only need to find one root below.
    bool positive = from.x < to.x;
    float2 left = positive ? from : to, right = positive ? to : from;
    float2 v0 = ctrl - left, v1 = right - ctrl;
    
    // Shoot a vertical ray toward the curve.
    float2 window = clamp(float2(from.x, to.x), -0.5, 0.5);
    float offset = mix(window.x, window.y, 0.5) - left.x;
    float t = offset / (v0.x + sqrt(v1.x * offset - v0.x * (offset - v0.x)));
    
    // Compute position and derivative to form a line approximation.
    float y = mix(mix(left.y, ctrl.y, t), mix(ctrl.y, right.y, t), t);
    float d = abs(mix(v0.y, v1.y, t) / mix(v0.x, v1.x, t));
    
    float u0 = -y + 0.5, u1 = -y - 0.5;
    float xl = clamp(u0 / d, -0.5, 0.5), xu = clamp(u1 / d, -0.5, 0.5);
    float xmin = min(xl, xu), xmax = max(xl, xu);
    float yl = clamp(y + 0.5 * d, -0.5, 0.5), yu = clamp(y - 0.5 * d, -0.5, 0.5);
    float ymin = min(yl, yu), ymax = max(yl, yu);
    return (window.x - window.y) * saturate(0.5 - xmax + (xmax - xmin) * (ymax - ymin) * 0.5 + 0.5 + ymin);
}

float4 quadraticColor(float2 devicePoint, AffineTransform op) {
    float x0 = op.tx, y0 = op.ty;
    float x1 = op.tx + op.a, y1 = op.ty + op.b;
    float x2 = x1 + op.c, y2 = y1 + op.d;
    
    x0 -= devicePoint.x, x1 -= devicePoint.x, x2 -= devicePoint.x;
    y0 -= devicePoint.y, y1 -= devicePoint.y, y2 -= devicePoint.y;
    
    float a = x0 * y2 - x2 * y0, Ba = y0 - y2, Ca = x2 - x0;
    float b = 2.0 * (x1 * y0 - x0 * y1), Bb = y1 - y0, Cb = x0 - x1;
    float d = 2.0 * (x2 * y1 - x1 * y2), Bd = y2 - y1, Cd = x1 - x2;

    float f = b * d - a * a;
    float dfx = 2.0 * (b * Bd + Bb * d - a * Ba);
    float dfy = 2.0 * (b * Cd + Cb * d - a * Ca);
    
    float vx = -f * dfy / (dfx * dfx + dfy * dfy);
    float vy = -f * -dfx / (dfx * dfx + dfy * dfy);
    float winding = 0.5 + f * rsqrt(dfx * dfx + dfy * dfy);
    
    //    float coverage = clamp(y2, -0.5, 0.5) - clamp(y0, -0.5, 0.5);
//    return mix(1.0, float4(0, 0, 0, 1), saturate((winding)));
    return mix(1.0, float4(vx, vy, 0, 1), saturate((winding)));
//    return mix(1.0, float4(select(0, 1, dfx < 0.0), select(0, 1, dfy < 0.0), 0, 1), saturate((winding)));
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

float2 unitComplexMultiply(float2 unit) {
    unit = (unit - 0.5) * 2.0;
    
    float2 u2 = unit * 10.0;
    unit = { unit.x * u2.x - unit.y * u2.y, unit.x * u2.y + unit.y * u2.x };
    
    unit = unit * 0.5 + 0.5;
    return unit;
}

float2 unitInverse(float2 unit) {
    unit = (unit - 0.5) * 2.0;
    unit = unit / dot(unit, unit);
    unit = unit * 0.5 + 0.5;
    return unit;
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
