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

vertex StrokeVertex line_vertex_main(constant State &state [[buffer(0)]], constant Points &points [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint i0 = iid * 2, i1 = i0 + 2;
    float2 p0 = { points.points[i0].x, points.points[i0].y }, p1 = { points.points[i1].x, points.points[i1].y };
    
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
