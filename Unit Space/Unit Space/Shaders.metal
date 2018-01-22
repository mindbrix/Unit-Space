//
//  Shaders.metal
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include "Shared.metal.hpp"
#include <metal_stdlib>

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

float2 unitRepeat(float2 unit, bool rx, bool ry) {
    float2 repeated = abs(fmod(abs(unit), 2.0) - 1.0);
    return { select(unit.x, repeated.x, rx), select(unit.y, repeated.y, ry) };
}

#pragma mark - Unit

struct UnitVertex
{
    float4 position [[position]];
    
    float ellipseScale, width;
    float2 unitPoint0, unitPoint1, unitPoint2, unitPoint3;
    uint32_t flags;
};

vertex UnitVertex unit_vertex_main(constant State &state [[buffer(0)]], constant Units &units [[buffer(1)]], uint vid [[vertex_id]])
{
    float2 unitPoint = float2(vid & 1, vid & 2);
    float dx = unitPoint.x * state.drawableSize.w, dy = unitPoint.y * state.drawableSize.h;
    
    constant Unit& unit = units.units[0];
    
    UnitVertex vert;
    vert.position = float4(unitPoint * 2.0 - 1.0, 0.0, 1.0);
    
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit.unitCTM, state.CTM));
    vert.unitPoint0 = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    
    float scale = select(sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b), 1.0, state.width == 0.0);
    vert.width = select(state.width, 1.0, state.width == 0.0) * sqrt(el.a * el.a + el.b * el.b) * scale;
    
    vert.flags = 0;
    
    return vert;
}

fragment float4 unit_fragment_main(UnitVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
//    float alpha = ellipseStrokeDistance(vert.unitPoint0, vert.width);
    float alpha = ellipseDistance(unitRepeat(vert.unitPoint0, false, false)) + 0.5;
//    float alpha = ellipseStrokeDistance(abs(fmod(abs(vert.unitPoint0), 2.0) - 1.0), 0.01);
    float4 color = float4(state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a), black = float4(0.0, 0.0, 0.0, 1.0), white = float4(1.0, 1.0, 1.0, 1.0);
    
    return mix(white, color, saturate(alpha));
}


#pragma mark - Arc

struct StrokeVertex
{
    float4 position [[position]];
    
    float2 unitPoint;
    float unitWidth;
    
    float alpha;
};

vertex StrokeVertex arc_vertex_main(constant State &state [[buffer(0)]], constant Arcs &arcs [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint count = ceil(1.0 / state.flatness);
    uint index = iid / count;
    constant Arc& arc = arcs.arcs[index];
    
    float dt = (arc.t1 - arc.t0) * state.flatness;
    float t0 = arc.t0 + dt * float(iid % count), t1 = t0 + dt;
    float t = select(t0, t1, vid & 2);
    
    float scale = sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b);
    
    float dilation = 1.0 / scale;
    float maxedWidth = max(state.width, dilation);
    float r0 = max(0.0, arc.r - maxedWidth - dilation);
    float theta = PI * state.flatness * (arc.t1 - arc.t0);
    float r1 = (arc.r + maxedWidth + dilation) / cos(theta);
    
    float r = select(r0, r1, vid & 1);
    float x = arc.x + r * cos(t * TWO_PI);
    float y = arc.y + r * sin(t * TWO_PI);
    
    float dx = x * state.CTM.a + y * state.CTM.c + state.CTM.tx;
    float dy = x * state.CTM.b + y * state.CTM.d + state.CTM.ty;
    
    AffineTransform unit = { arc.r * 2.0, 0, 0, arc.r * 2.0, arc.x - arc.r, arc.y - arc.r };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, state.CTM));
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    vert.unitWidth = maxedWidth * sqrt(el.a * el.a + el.b * el.b) * scale;
    vert.alpha = min(1.0, state.width / dilation);
    return vert;
}

fragment float4 arc_fragment_main(StrokeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = vert.alpha * saturate(ellipseStrokeDistance(vert.unitPoint, vert.unitWidth));
//    float alpha = vert.alpha * saturate(cirleStrokeDistance(vert.unitPoint, vert.unitWidth) + 0.5);
    return float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a) * alpha;
//    return mix(float4(0, 1, 0, 1), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), alpha);
}


#pragma mark - Line

vertex StrokeVertex line_vertex_main(constant State &state [[buffer(0)]], constant Lines &lines [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    constant Line& line = lines.lines[iid];
    
    float2 p0 = { line.x0, line.y0 }, p1 = { line.x1, line.y1 };
    float2 v = p1 - p0, nv = normalize(v);
    float2 lnv = { -nv.y, nv.x };
    float w2 = state.width * 0.5;
    float scale = sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b);
    
    float dilation = 1.0 / scale;
    
    float2 up = select(p0, p1, vid & 2) + (select(-nv, nv, vid & 2) + select(-lnv, lnv, vid & 1)) * (w2 + dilation);
    
    float2 nvw = nv * state.width, ab = { nvw.y, -nvw.x };
    float2 cd = v + nvw, txty = p0 - (nvw + ab) * 0.5;
    
    AffineTransform unit = { ab.x, ab.y, cd.x, cd.y, txty.x, txty.y };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, state.CTM));
    
    float dx = up.x * state.CTM.a + up.y * state.CTM.c + state.CTM.tx;
    float dy = up.x * state.CTM.b + up.y * state.CTM.d + state.CTM.ty;
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
//    vert.unitWidth = maxedWidth * sqrt(el.a * el.a + el.b * el.b) * scale;
//    vert.alpha = min(1.0, state.width / dilation);
    return vert;
}

fragment float4 line_fragment_main(StrokeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = rectDistance(vert.unitPoint);
//    return mix(float4(0, 1, 0, 1), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), alpha);
    return float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a) * alpha;
}


