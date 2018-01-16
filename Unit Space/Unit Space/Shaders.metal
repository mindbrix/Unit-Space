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

float ellipseStrokeDistance(float2 unitPoint, float w) {
    float x = 0.5 - unitPoint.x, y = 0.5 - unitPoint.y;
    return (w - abs(0.5 - sqrt(x * x + y * y))) * rsqrt(abs(dfdx(x) * dfdy(y) -  dfdx(y) * dfdy(x)));
}

#pragma mark - Unit

struct UnitVertex
{
    float4 position [[position]];
    
    float2 edges;
    
    float3 triangleEdges;
    float ellipseScale, width;
    float2 rectUnitPoint, ellipseUnitPoint, quadraticUnitPoint;
    uint32_t flags;
};

vertex UnitVertex unit_vertex_main(constant Uniforms &uniforms [[buffer(0)]], uint vid [[vertex_id]])
{
    float2 unitPoint = float2(vid & 1, vid & 2);
    float dx = unitPoint.x * uniforms.drawableSize.w, dy = unitPoint.y * uniforms.drawableSize.h;
    
    UnitVertex vert;
    vert.position = float4(unitPoint * 2.0 - 1.0, 0.0, 1.0);
    
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(uniforms.ellipseCTM, uniforms.CTM));
    vert.ellipseUnitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    
    float scale = select(sqrt(uniforms.CTM.a * uniforms.CTM.a + uniforms.CTM.b * uniforms.CTM.b), 1.0, uniforms.width == 0.0);
    vert.width = select(uniforms.width, 1.0, uniforms.width == 0.0) * sqrt(el.a * el.a + el.b * el.b) * scale;
    
    vert.flags = 0;
    
    return vert;
}

fragment float4 unit_fragment_main(UnitVertex vert [[stage_in]], constant Uniforms &uniforms [[buffer(0)]])
{
//    float alpha = ellipseStrokeDistance(vert.ellipseUnitPoint, vert.width);
    float alpha = ellipseDistance(fmod(vert.ellipseUnitPoint, 0.0)) + 0.5;
    float4 color = float4(uniforms.Color.r, uniforms.Color.g, uniforms.Color.b, uniforms.Color.a), black = float4(0.0, 0.0, 0.0, 1.0), white = float4(1.0, 1.0, 1.0, 1.0);
    
    return mix(white, color, saturate(alpha));
}


#pragma mark - Stroke

struct StrokeVertex
{
    float4 position [[position]];
    
    float2 unitPoint;
    float unitWidth;
    
    float alpha;
};

vertex StrokeVertex stroke_vertex_main(constant Uniforms &uniforms [[buffer(0)]], constant Path &path [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint count = ceil(1.0 / uniforms.flatness);
    uint index = iid / count;
    constant Arc& arc = path.arcs[index];
    
    float dt = (arc.t1 - arc.t0) * uniforms.flatness;
    float theta = PI * uniforms.flatness * (arc.t1 - arc.t0);
    float t0 = arc.t0 + dt * float(iid % count), t1 = t0 + dt;
    float t = select(t0, t1, vid & 2);
    
    float rawScale = sqrt(uniforms.CTM.a * uniforms.CTM.a + uniforms.CTM.b * uniforms.CTM.b);
    float scale = select(rawScale, 1.0, uniforms.width == 0.0);
    
    float dilation = 1.0 / rawScale;
    float width = select(uniforms.width, dilation, uniforms.width == 0.0);
    float maxedWidth = max(width * 0.5, dilation);
    float w = select(-maxedWidth - dilation, maxedWidth + dilation, vid & 1);
    float r = max(0.0, arc.r + w);
    r = select(r, r / cos(theta), vid & 1);
    float x = arc.x + r * cos(t * TWO_PI);
    float y = arc.y + r * sin(t * TWO_PI);
    
    float dx = x * uniforms.CTM.a + y * uniforms.CTM.c + uniforms.CTM.tx;
    float dy = x * uniforms.CTM.b + y * uniforms.CTM.d + uniforms.CTM.ty;
    
    AffineTransform unit = { arc.r * 2.0, 0, 0, arc.r * 2.0, arc.x - arc.r, arc.y - arc.r };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, uniforms.CTM));
    
    float2 unitPoint = { dx / uniforms.drawableSize.w, dy / uniforms.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    vert.unitWidth = maxedWidth * sqrt(el.a * el.a + el.b * el.b) * scale;
    vert.alpha = min(1.0, width / dilation);
    return vert;
}

fragment float4 stroke_fragment_main(StrokeVertex vert [[stage_in]], constant Uniforms &uniforms [[buffer(0)]])
{
    float alpha = vert.alpha * saturate(ellipseStrokeDistance(vert.unitPoint, vert.unitWidth));
    return float4(uniforms.strokeColor.r, uniforms.strokeColor.g, uniforms.strokeColor.b, uniforms.strokeColor.a) * alpha;
//    return mix(float4(0, 1, 0, 1), float4(uniforms.strokeColor.r, uniforms.strokeColor.g, uniforms.strokeColor.b, uniforms.strokeColor.a), alpha);
}
