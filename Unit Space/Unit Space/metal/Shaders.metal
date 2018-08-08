//
//  Shaders.metal
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include "Shared.metal.hpp"
#include <metal_stdlib>
#include "Headers.metal"

using namespace metal;


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


#pragma mark - Circle

vertex StrokeVertex circle_vertex_main(constant State &state [[buffer(0)]], constant Circles &circles [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    constant Circle& circle = circles.circles[iid];
    
    float scale = sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b);
    float dilation = 1.0 / scale;
    float r = circle.r + dilation;
    
    float2 up = { circle.x + r * select(-1.0, 1.0, vid & 1), circle.y + r * select(-1.0, 1.0, vid & 2) };

    AffineTransform unit = { circle.r * 2.0, 0.0, 0.0, circle.r * 2.0, circle.x - circle.r, circle.y - circle.r };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, state.CTM));
    
    float dx = up.x * state.CTM.a + up.y * state.CTM.c + state.CTM.tx;
    float dy = up.x * state.CTM.b + up.y * state.CTM.d + state.CTM.ty;
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 1.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    return vert;
}

fragment float4 circle_fragment_main(StrokeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = saturate(ellipseDistance(vert.unitPoint));
    return float4(state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a) * alpha;
}


#pragma mark - Flags

vertex StrokeVertex flags_vertex_main(constant State &state [[buffer(0)]], constant Flags &flags [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    const float kDimension = 4;
    
    unsigned int flag = flags.flags[iid >> 5];
    bool visible = flag & (1 << (iid % 32));
    unsigned int column = iid % flags.w, row = iid / flags.w;
    float lx = kDimension * column, ly = kDimension * row, ux = lx + kDimension, uy = ly + kDimension;
    float x = select(lx, ux, vid & 1) * state.scaleFactor, y = select(ly, uy, vid > 1) * state.scaleFactor;
    float2 devicePoint = { x, y };
    devicePoint = select(0, devicePoint, visible);
    
    StrokeVertex vert;
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), 1.0, 1.0);
    return vert;
}

fragment float4 flags_fragment_main(StrokeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = 1;
    return float4(state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a) * alpha;
}


#pragma mark - Line

struct LineVertex
{
    float4 position [[position]];
    float u, v;
    float4 color;
};

vertex LineVertex lineAOS_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], constant float *xs [[buffer(10)]], constant float *ys [[buffer(11)]], constant char *bridges [[buffer(12)]], constant MeshPath *paths [[buffer(14)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint idx = iid / meshState.subdivisionCount;
    float x0 = xs[idx], y0 = ys[idx], x1 = xs[idx + 1], y1 = ys[idx + 1];
    AffineTransform CTM = AffineTransformConcat(paths[0].ctm, state.CTM);
    float2 p0 = { x0 * CTM.a + y0 * CTM.c + CTM.tx, x0 * CTM.b + y0 * CTM.d + CTM.ty };
    float2 p1 = { x1 * CTM.a + y1 * CTM.c + CTM.tx, x1 * CTM.b + y1 * CTM.d + CTM.ty };
    
    if (meshState.subdivisionCount > 1) {
        float s = -0.1;// 20.0 * rsqrt(length_squared(p1 - p0));
        float2 cp = { (p0.x + p1.x) * 0.5 + -s * (p1.y - p0.y), (p0.y + p1.y) * 0.5 + s * (p1.x - p0.x)};
        float dt = 1.0 / float(meshState.subdivisionCount), t0 = float(iid % meshState.subdivisionCount) * dt, t1 = t0 + dt;
        float s0 = 1.0 - t0, s1 = 1.0 - t1;
        float2 tp0 = s0 * s0 * p0 + 2.0 * s0 * t0 * cp + t0 * t0 * p1;
        float2 tp1 = s1 * s1 * p0 + 2.0 * s1 * t1 * cp + t1 * t1 * p1;
        p0 = tp0, p1 = tp1;
    }
    float2 v = p1 - p0, nv = normalize(v), lnv = { -nv.y, nv.x };
    float deviceWidth = select(state.width + 1.0, 0.0, bridges[idx] == 1);
    float2 devicePoint = select(p0, p1, vid & 2) + deviceWidth * 0.5 * (select(-nv, nv, vid & 2) + select(-lnv, lnv, vid & 1));
    float2 ndcPoint = { (devicePoint.x / state.drawableSize.w * 2.0) - 1.0, (devicePoint.y / state.drawableSize.h * 2.0) - 1.0 };
    
    LineVertex vert;
    vert.position = float4(ndcPoint, 1.0, 1.0);
    vert.u = select(-1, 1, vid & 1), vert.v = select(-1, 1, vid & 2);
    vert.color = { float(paths[0].color.r) / 255.0, float(paths[0].color.g) / 255.0, float(paths[0].color.b) / 255.0, float(paths[0].color.a) / 255.0 };
    return vert;
}

fragment float4 line_fragment_main(LineVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    return vert.color
    * saturate((1.0 - abs(vert.u)) * rsqrt(dfdx(vert.u) * dfdx(vert.u) + dfdy(vert.u) * dfdy(vert.u)))
    * saturate((1.0 - abs(vert.v)) * rsqrt(dfdx(vert.v) * dfdx(vert.v) + dfdy(vert.v) * dfdy(vert.v)) / 1.74);
}
