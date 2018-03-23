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

float det2(float2x2 m) {
    return m[0][0] * m[1][1] - m[0][1] * m[1][0];
}

float fastSign(float x) {
    return x > 0.0 ? 1.0 : -1.0;
}

// Are we inside the convex hull of the curve? (This will always be false if this is a line.)
bool insideCurve(float3 uv, float e) {
    return uv.z != 0.0 && uv.x > -e && uv.x < (1.0 + e) && uv.y > -e && uv.y < (1.0 + e);
}

float signedDistanceToCurve(float2 uv, float2 dUVDX, float2 dUVDY, bool inCurve) {
    // u^2 - v for curves inside uv square; u - v otherwise.
    float g = uv.x;
    float2 dG = float2(dUVDX.x, dUVDY.x);
    if (inCurve) {
        g *= uv.x;
        dG *= 2.0 * uv.x;
    }
    g -= uv.y;
    dG -= float2(dUVDX.y, dUVDY.y);
    return g / length(dG);
}

// Cubic approximation to the square area coverage, accurate to about 4%.
float estimateArea(float dist) {
    if (dist >= 0.707107)
        return 0.5;
    // Catch NaNs here.
    if (!(dist > -0.707107))
        return -0.5;
    return 1.14191 * dist - 0.83570 * dist * dist * dist;
}

float barycentricWinding(float3 uv, float3 xDist, float e) {
    // Unpack.
    float2 dUVDX = dfdx(uv.xy), dUVDY = dfdy(uv.xy);
    
    // Calculate X distances between endpoints (x02, x10, and x21 respectively).
    float3 vDist = xDist - xDist.zxy;
    
    // Compute winding number and convexity.
    bool inCurve = insideCurve(uv, e);
    float openWinding = fastSign(-vDist.x);
    float convex = uv.z != 0.0 ? uv.z : fastSign(vDist.x * dUVDY.y);
    
    // Compute open rect area.
    float2 areas = clamp(xDist.xz, -0.5, 0.5);
    float openRectArea = abs(areas.y - areas.x);
    
    // Compute closed rect area and winding, if necessary.
    float closedRectArea = 0.0, closedWinding = 0.0;
    if (inCurve && vDist.y * vDist.z < 0.0) {
        closedRectArea = 0.5 - fastSign(vDist.y) * (vDist.x * vDist.y < 0.0 ? areas.y : areas.x);
        closedWinding = -fastSign(vDist.y * dUVDY.y);
    }
    
    // Calculate approximate area of the curve covering this pixel square.
    float curveArea = estimateArea(signedDistanceToCurve(uv.xy, dUVDX, dUVDY, inCurve));
    
    // Calculate winding.
    float2 coverage = float2(openWinding, closedWinding) * 0.5 + convex * curveArea;
    return dot(coverage, float2(openRectArea, closedRectArea));
}

float quadraticWinding1(float2 devicePoint, AffineTransform op) {
    // Compute edge vectors.
    float2 v02 = { op.a + op.c, op.b + op.d };
    float2 v01 = { op.a, op.b };
    
    // Compute area of convex hull (w). Change from curve to line if appropriate.
    float w = det2(float2x2(v01, v02));
    
    // Compute UV using Cramer's rule.
    // https://gamedev.stackexchange.com/a/63203
    float2 v03 = { devicePoint.x - op.tx, devicePoint.y - op.ty };
    float3 uv = { 0.0, det2(float2x2(v01, v03)), sign(w) };
//    uv.x = det2(float2x2(v03, v02));
    uv.x = uv.y + 0.5 * det2(float2x2(v03, v02));
    uv.xy /= det2(float2x2(v01, v02));
    
    float3 xDist = devicePoint.x - float3(op.tx, op.tx + op.a, op.tx + op.a + op.c);
    
    float e = 0.5 * rsqrt(abs(w));
    
    return barycentricWinding(uv, xDist, e);
}

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
    bool line = abs(op.a - op.c) < 1e-3 && abs(op.b - op.d) < 1e-3;
    float det = select(op.a * op.d - op.b * op.c, 1.0, line);
    
    float x2 = op.a + op.c, y2 = op.b + op.d;
    
    float convex = sign(y2 * det);
    
    float ox = devicePoint.x - op.tx, oy = devicePoint.y - op.ty;
    // Shear = { 0.5, 0.0, 0.5, 1.0 }
    // a = op.d, b = -op.b, c = -op.c, d = op.a
    // x = (ox * (a + b) + oy * (c + d)) * 0.5, y = (ox * b + oy * d)
    float x = (ox * (op.d - op.b) + oy * (op.a - op.c)) * 0.5 / det, y = (ox * -op.b + oy * op.a) / det;

    float e = 0.5 * rsqrt(abs(det));
    float lower = -e, upper = 1.0 + e;
    line |= x < lower || x > upper || y < lower || y > upper;
    
    float2 m = select(float2(2.0 * x, x), 1.0, line);
    float fx = dfdx(x) * m[0] - dfdx(y), fy = dfdy(x) * m[0] - dfdy(y);
    float sd = (x * m[1] - y) * rsqrt(fx * fx + fy * fy);
    
    float bxsign = x2 * y2 * det;
    float bx = select(x2 - ox, -ox, bxsign < 0.0);
    bx *= fastSign(y2);
    float by = select(oy - y2, oy, bxsign > 0.0);
    by *= fastSign(x2);
    float box = min(bx, by);
    
    float area = clamp(-min(-sd, box), -0.5, 0.5);
    
    float ymin = min(0.0, y2), ymax = max(0.0, y2);
    float sy0 = saturate(ymin - oy + 0.5), sy2 = saturate(ymax - oy + 0.5);
    float ysub = select(sy0, 1.0 - sy2, x2 * det < 0.0);
    ysub = select(0.0, ysub, convex > 0.0);
    
    float ymul = select(1.0 - sy0, sy2, x2 * det > 0.0); // yclip
    return ymul * sign(y2) * saturate(0.5 + area * convex - ysub);
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

float4 quadraticColor(float2 devicePoint, AffineTransform op) {
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
    
    // Look up area under that line, and scale horizontally to the window size.
    float dX = window.x - window.y;
    
    // Compute position and derivative to form a line approximation.
    float y = mix(mix(left.y, ctrl.y, t), mix(ctrl.y, right.y, t), t);
    float d = abs(mix(v0.y, v1.y, t) / mix(v0.x, v1.x, t));
    
    float a0 = 0.5 + y;
    float h = 0.5 * d;
    float e0 = max(0.0, h - a0), e1 = max(0.0, h - (1.0 - a0));
    float A = saturate(a0 - 0.25 / h * (e0 * e0 - e1 * e1));
    float winding = dX * A;
    
    float4 black = { 0, 0, 0, 1 }, white = 1;
    
    return mix(white, black, abs(winding));
}

float4 quadraticColor0(float2 devicePoint, AffineTransform op) {
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
