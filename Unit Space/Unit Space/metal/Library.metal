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

// http://pcwalton.github.io/2018/02/14/determining-triangle-geometry-in-fragment-shaders.html
// https://www.shadertoy.com/view/XsXSz4
float barycentricDistance(float u, float v, bool ef0, bool ef1, bool ef2) {
    float w = 1.0 - u - v, a = dfdx(u), b = dfdy(u), c = dfdx(v), d = dfdy(v);
    float invdet = 1.0 / (a * d - b * c);
    a *= invdet, b *= invdet, c *= invdet, d *= invdet;
    
    float a2 = b * b + a * a, b2 = d * d + c * c, e0x = -b - d, e0y = a + c, c2 = e0x * e0x + e0y * e0y;
    float x2 = b * v - d * u, y2 = u * c - v * a;
    
    float t0 = float(ef2) * saturate(((x2 + d) * e0x + (y2 - c) * e0y) / -c2);
    float t1 = float(ef0) * saturate(((x2 - b) * b + (y2 + a) * -a) / -a2);
    float t2 = float(ef1) * saturate((x2 * d + y2 * -c) / -b2);
    
    float u0 = (1 - t0) - u, v0 = t0 - v, w0 = -w;
    float u1 = -u, v1 = (1 - t1) - v, w1 = t1 - w;
    float u2 = t2 - u, v2 = -v, w2 = (1 - t2) - w;
    float d0 = -a2 * v0 * w0 - b2 * w0 * u0 - c2 * u0 * v0;
    float d1 = -a2 * v1 * w1 - b2 * w1 * u1 - c2 * u1 * v1;
    float d2 = -a2 * v2 * w2 - b2 * w2 * u2 - c2 * u2 * v2;
    return select(1.0, -1.0, u < 0 || v < 0 || w < 0) * sqrt(min(d2, min(d0, d1)));
}

float simpleBarycentricWinding(float u, float v, float f) {
    float w = 1.0 - u - v;
    float eu = sign(u) * u * u / max(1e-13, (dfdx(u) * dfdx(u) + dfdy(u) * dfdy(u)));
    float ev = sign(v) * v * v / max(1e-13, (dfdx(v) * dfdx(v) + dfdy(v) * dfdy(v)));
    float ew = sign(w) * w * w / max(1e-13, (dfdx(w) * dfdx(w) + dfdy(w) * dfdy(w)));
    
    float t0 = v + w, t1 = w + u, t2 = u + v;
    float et0 = sign(t0) * t0 * t0 / max(1e-13, (dfdx(t0) * dfdx(t0) + dfdy(t0) * dfdy(t0)));
    float et1 = sign(t1) * t1 * t1 / max(1e-13, (dfdx(t1) * dfdx(t1) + dfdy(t1) * dfdy(t1)));
    float et2 = sign(t2) * t2 * t2 / max(1e-13, (dfdx(t2) * dfdx(t2) + dfdy(t2) * dfdy(t2)));
    
    float m = min(eu, min(ev, ew));
    float mt = min(et0, min(et1, et2));
    
    return saturate(sign(m) * sqrt(abs(min(m, mt))) + f) * saturate(sign(mt) * sqrt(abs(mt)) + f);
}

float simpleBarycentricDistance(float u, float v, bool ef0, bool ef1, bool ef2) {
    float w = 1.0 - u - v;
	float eu = float(!ef0) * 1e12 + sign(u) * u * u / max(1e-13, (dfdx(u) * dfdx(u) + dfdy(u) * dfdy(u)));
    float ev = float(!ef1) * 1e12 + sign(v) * v * v / max(1e-13, (dfdx(v) * dfdx(v) + dfdy(v) * dfdy(v)));
    float ew = float(!ef2) * 1e12 + sign(w) * w * w / max(1e-13, (dfdx(w) * dfdx(w) + dfdy(w) * dfdy(w)));
    return 1 - ((1 - saturate(0.5 + eu)) + (1 - saturate(0.5 + ev)) + (1 - saturate(0.5 + ew)));
//    float m = min(eu, min(ev, ew));
//    return sign(m) * sqrt(abs(m));
}

float adjustedT(float t) {
    return select(2 * t * (t - 1) + 1, 2 * t * (1 - t), t < 0.5);
}

float verticalWinding(float x, float y0, float y1) {
    return (1.0 - saturate(x)) * (saturate(y1) - saturate(y0));
}

float winding(float y0, float y1, float a0, float a1, float cosine) {
    const float f = 0.5;
    float up, cy0, cy1;
    up = sign(a1 - a0) * cosine;
    cy0 = clamp(y0, -f, f), cy1 = clamp(y1, -f, f);
    a0 = fma((f + cy0), -up, a0), a1 = fma((f - cy1), up, a1);
    return smoothstep(0.0, 1.0, -a0 / (a1 - a0)) * copysign(cy1 - cy0, up);
}

float edgeCoverage(float x0, float y0, float x1, float y1) {
    const float f = 0.5;
    float dx = x1 - x0, dy = y1 - y0, left = float(dx < 0.0) * dy;
   // float t = saturate(min(abs(dx), abs(dy)) * rsqrt(dx * dx + dy * dy) / 0.7071);
    float wy0 = clamp(y0 + left, -f, f) - y0, wy1 = clamp(y1 - left, -f, f) - y0;
    float a0 = fma(dx, wy0, -dy * (f - x0)), a1 = fma(dx, wy1, -dy * (-f - x0));
    return saturate(-a0 / (a1 - a0)) * copysign(0.5 * (wy1 - wy0) / f, dy);
//    return smoothstep(0.0, 1.0, -a0 / (a1 - a0)) * copysign(wy1 - wy0, dy);
}

float edgeWinding(float x0, float y0, float x1, float y1) {
    float sy0 = saturate(y0), sy1 = saturate(y1);
    float coverage = sy1 - sy0;
    
    if (coverage == 0.0 || saturate(x0) + saturate(x1) == 0.0)
        return coverage;
	
	float dxdy = (x1 - x0) / (y1 - y0);
	float sx0 = fma(sy0 - y0, dxdy, x0), sx1 = fma(sy1 - y0, dxdy, x0);
	float minx = min(sx0, sx1), range = abs(sx1 - sx0);
	float t0 = saturate(-minx / range), t1 = saturate((1.0 - minx) / range);
	float area = (saturate(sx0) + saturate(sx1)) * 0.5;
	return coverage * (t1 - ((t1 - t0) * area));
}

float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2) {
    float ax, ay, bx, by, area, w, t, x01, y01, x12, y12;
    ax = x1 - x0, ay = y1 - y0, bx = x2 - x0, by = y2 - y0;
    area = ax * by - ay * bx;
    if (abs(area) < 4)
        return edgeWinding(x0, y0, x2, y2);
    
    w = (ax * (0.5 - y0) - ay * (0.5 - x0)) / area;
    t = sqrt(clamp(w, fwidth(w) * 0.5, 1.0));
    x01 = mix(x0, x1, t), y01 = mix(y0, y1, t), x12 = mix(x1, x2, t), y12 = mix(y1, y2, t);
    return edgeWinding(x0, y0, x01, y01) + edgeWinding(x01, y01, x12, y12) + edgeWinding(x12, y12, x2, y2);
}

float windingToAlpha(float winding, bool evenOdd) {
    float absolute = abs(winding);
    float NZ = min(absolute, 1.0);
    float EO = 1.0 - abs(fmod(absolute, 2.0) - 1.0);
    return select(NZ, EO, evenOdd);
}

float sRGB(float x) {
    return x;
    
    return pow(x, 2.2);
    
    if (x <= 0.00031308)
        return 12.92 * x;
    else
        return 1.055*pow(x,(1.0 / 2.4) ) - 0.055;
}

float ellipseDistance(float x, float y, float dilation, float f) {
    float fx = -2.0 * (x * dfdx(x) + y * dfdx(y));
    float fy = -2.0 * (x * dfdy(x) + y * dfdy(y));
    float sd = rsqrt(max(1e-13, fx * fx + fy * fy));
    return 0.5 / f * (min(f, (1 - (x * x + y * y)) * sd - dilation) + min(f, (1 + (x * x + y * y)) * sd - dilation));
}

float ellipseStrokeDistance(float2 unitPoint, float w) {
    float x = 0.5 - unitPoint.x, y = 0.5 - unitPoint.y;
    return (w - abs(0.5 - sqrt(x * x + y * y))) * rsqrt(abs(dfdx(x) * dfdy(y) - dfdx(y) * dfdy(x)));
}

float rectDistance(float x, float y, float dilation, float f) {
    float sx = rsqrt(max(1e-13, dfdx(x) * dfdx(x) + dfdy(x) * dfdy(x)));
    float sy = rsqrt(max(1e-13, dfdx(y) * dfdx(y) + dfdy(y) * dfdy(y)));
    float ew = 0.5 / f * (min(f, (1 - x) * sx - dilation) + min(f, (1 + x) * sx - dilation));
    float ns = 0.5 / f * (min(f, (1 - y) * sy - dilation) + min(f, (1 + y) * sy - dilation));
    return ew * ns;
}

float triangleDistance(float x, float y, float dilation, float f) {
    float sx = rsqrt(max(1e-13, dfdx(x) * dfdx(x) + dfdy(x) * dfdy(x)));
    float sy = rsqrt(max(1e-13, dfdx(y) * dfdx(y) + dfdy(y) * dfdy(y)));
    float w = 0.5 / f * (f + max(-f, min(f, (1 - x) * sx - dilation)));
    float n = 0.5 / f * (f + max(-f, min(f, (1 + y) * sy - dilation)));
    float z = x - y, sz = rsqrt(max(1e-13, dfdx(z) * dfdx(z) + dfdy(z) * dfdy(z)));
    float d = 0.5 / f * (f + max(-f, min(f, z * sz)));
    return w * n * d;
}

float lozengeDistance(float2 unitPoint, float dilation) {
    float u = unitPoint.x, v = unitPoint.y;
    float w = rsqrt(dfdx(u) * dfdx(u) + dfdy(u) * dfdy(u));
    float h = rsqrt(dfdx(v) * dfdx(v) + dfdy(v) * dfdy(v));
    float dx = (1.0 - abs(u)) * w;
    float dy = (1.0 - abs(v)) * h;
    float x = w - dx, y = max(0.0, w - dy);
    return saturate(w - sqrt(x * x + y * y) + dilation);
}

float quadraticDistance(float2 unitPoint, float dilation) {
    float x = abs(unitPoint.x), y = 1 - abs(unitPoint.y);
    float fx = 2 * x * dfdx(x) - dfdx(y);
    float fy = 2 * x * dfdy(x) - dfdy(y);
    return saturate((y - x * x) * rsqrt(fx * fx + fy * fy) + dilation);
}

float roundedRectDistance(float x, float y, float radius, float dilation, float f) {
    float u = max(0.0, abs(x) - (1.0 - radius)) / radius, v = max(0.0, abs(y) - (1.0 - radius)) / radius;
    return ellipseDistance(u, v, dilation, f);
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
    x0 -= px, y0 -= py, x1 -= px, y1 -= py, x2 -= px, y2 -= py;
    float a = x0 * y2 - y0 * x2, b = x1 * y0 - y1 * x0, d = x2 * y1 - y2 * x1;
    float f = 4.0 * b * d - a * a;
    
//        float cx = 0.5, cy = 0.5, x = cx - px, y = cy - py, r = 0.5, f = -(x * x + y * y - r * r);
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

float unitQuadraticDistanceChainRule(float2 unitPoint, bool line) {
//    return implicitDistance(max(0.0, unitPoint.x), min(1.0, unitPoint.y), 0.0, 0.0, 1.0, 0.0, 1.0, 1.0);
    
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
    //return quadraticDistance(x0, y0, x1, y1, x2, y2, 0);
    
    
    float a = dfdx(ux), b = dfdy(ux);
    float c = dfdx(uy), d = dfdy(uy);
    
    float determinant = a * d - b * c;
    float recip = 1.0 / determinant;
    AffineTransform it = { d * recip, -b * recip, -c * recip, a * recip, 0, 0 };
    
    
    float dx0 = x0 * it.a + y0 * it.c, dy0 = x0 * it.b + y0 * it.d;
    float dx1 = x1 * it.a + y1 * it.c, dy1 = x1 * it.b + y1 * it.d;
    float dx2 = x2 * it.a + y2 * it.c, dy2 = x2 * it.b + y2 * it.d;
    
    AffineTransform op { dx1 - dx0, dy1 - dy0, dx2 - dx1, dy2 - dy1, dx0, dy0 };
    //return quadraticWinding(float2(0, 0), op);
}

float2 unitComplexMultiply(float2 unit) {
    unit = (unit - 0.5) * 2.0;
    
    float2 u2 = unit * 10.0;
    unit = { unit.x * u2.x - unit.y * u2.y, unit.x * u2.y + unit.y * u2.x };
    
    unit = unit * 0.5 + 0.5;
    return unit;
}

float2 unitInverse(float2 unit) {
    return unit / dot(unit, unit);
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
    float vx = ctm.a + ctm.c, vy = ctm.b + ctm.d;
    return abs(ctm.a * ctm.d - ctm.b * ctm.c) * rsqrt(vx * vx + vy * vy) < 1e-1;
}

AffineTransform lineCTM(AffineTransform ctm) {
    float vx = ctm.a + ctm.c, vy = ctm.b + ctm.d;
    ctm.a = (vx + vy) * 0.5, ctm.b = (vy - vx) * 0.5;
    ctm.c = -ctm.b, ctm.d = ctm.a;
    return ctm;
}

float distanceBoolean(float d0, float d1, int op) {
    float anded = min(d0, d1);
    float ored = max(d0, d1);
    float xored = min(ored, -anded);
    return select(select(xored, ored, op == 1), anded, op == 0);
}
