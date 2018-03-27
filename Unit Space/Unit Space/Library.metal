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

float unitRoundedRectDistance(float2 unitPoint, float radius) {
    float scale = sqrt(abs(dfdx(unitPoint.x) * dfdy(unitPoint.y) - dfdy(unitPoint.x) * dfdx(unitPoint.y)));
    float rmin = min(0.5, scale), r = max(radius, rmin), b = 0.5 - r;
    float x = max(0.0, abs(0.5 - unitPoint.x) - b), y = max(0.0, abs(0.5 - unitPoint.y) - b);
    
    float fx = x * dfdx(x) + y * dfdx(y);
    float fy = x * dfdy(x) + y * dfdy(y);
    return (r * r - (x * x + y * y)) * rsqrt(2.0 * max(1e-13, fx * fx + fy * fy));
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

// http://hhoppe.com/ravg.pdf
//
float implicitDistance(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2) {
//    x0 -= px, y0 -= py, x1 -= px, y1 -= py, x2 -= px, y2 -= py;
//    float a = x0 * y2 - y0 * x2, b = x1 * y0 - y1 * x0, d = x2 * y1 - y2 * x1;
//    float f = 4.0 * b * d - a * a;
    
        float cx = 0.5, cy = 0.5, x = cx - px, y = cy - py, r = 0.5, f = -(x * x + y * y - r * r);
    //    float cx = 512.0, cy = 512.0, x = cx - px, y = cy - py, a = 512, b = 512.0, f = -(x * x / (a * a) + y * y / (b * b) - 1.0);
    //    float cx = 512.0, cy = 512.0, x = cx - px, y = cy - py, c = 256.0, f = -(abs(x) + abs(y) - c);
    //    float cx = 512.0, cy = 512.0, x = cx - px, y = cy - py, c = 256.0, f = -(max(abs(x), abs(y)) - c);
    // http://benice-equation.blogspot.co.uk/2016/10/equation-of-rounded-rectangle.html
//        float cx = 512.0, cy = 512.0, x = cx - px, y = cy - py, a = 0.0, b = 256.0, c = 64.0, mx = max(abs(x) - a, 0.0), my = max(abs(y) - b, 0.0),  f = -(mx * mx + my * my - c * c);
    
    //    float A = 4, B = -12, C = 9, D = 3, E = -2, F = -6;
    //    float cx = 512.0, cy = 512.0, x = cx - px, y = cy - py;
    //    float f = -(A * x * x + B * x * y + C * y * y + D * x + E * y + F);
    
    float dfx = dfdx(f), dfy = dfdy(f);
    return f * max(abs(dfx), abs(dfy)) / (dfx * dfx + dfy * dfy);
}

float det(float2 a, float2 b) {
    return a.x * b.y - a.y * b.x;
}

float quadraticClosestT(float x0, float y0, float x1, float y1, float x2, float y2) {
    float2 b0 = { x0, y0 }, b1 = { x1, y1 }, b2 = { x2, y2 };
    
    float a = det(b0, b2), b = det(b1, b0), d = det(b2, b1);
    float f = 4.0 * b * d - a * a;
    
    float2 gf = { dfdx(f), dfdy(f) };
    float2 pp = -f * gf / dot(gf, gf);
    
    float2 d0p = b0 - pp;
    float2 d10 = b1 - b0, d20 = b2 - b0;
    float ap = det(d0p, d20), bp = det(d10, d0p);
    // (note that ap + bp + dp = a + b + d = 2 * area(b0, b1, b2))
    return (0.5 * ap + bp) / (a + b + d);
}


float unitQuadraticDistanceChainRule(float2 unitPoint, bool line) {
    float y = select(min(1.0, unitPoint.y), unitPoint.y, line);
    float x = (select(max(0.0, unitPoint.x), unitPoint.x, line) + y) * 0.5;
    
    float mx = select(2.0 * x, 1.0, line);
    float xx = select(x * x, x, line);
    float fx = mx * dfdx(x) - dfdx(y);
    float fy = mx * dfdy(x) - dfdy(y);
    return (y - xx) * rsqrt(fx * fx + fy * fy);
}

float unitQuadraticWinding(float2 unitPoint) {
    return unitQuadraticDistanceChainRule(unitPoint, false);
    
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

bool isLine(AffineTransform ctm) {
    return abs(ctm.c - ctm.a) < 1e-1 && abs(ctm.d - ctm.b) < 1e-1;
}

AffineTransform lineCTM(AffineTransform ctm) {
    float vx = ctm.a + ctm.c, vy = ctm.b + ctm.d;
    ctm.a = (vx + vy) * 0.5, ctm.b = (vy - vx) * 0.5;
    ctm.c = -ctm.b, ctm.d = ctm.a;
    return ctm;
}
