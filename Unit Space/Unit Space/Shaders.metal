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
    float sd = (0.25 - (x * x + y * y)) * rsqrt(max(1e-13, fx * fx + fy * fy));
    
    return sd + 0.5;
}

#pragma mark - Unit

struct UnitVertex
{
    float4 position [[position]];
    
    float2 edges;
    
    float3 triangleEdges;
    float ellipseScale;
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
    
    vert.flags = 0;
    
    return vert;
}

fragment float4 unit_fragment_main(UnitVertex vert [[stage_in]], constant Uniforms &uniforms [[buffer(0)]])
{
    float alpha = ellipseDistance(vert.ellipseUnitPoint);
    float4 color = float4(uniforms.Color.r, uniforms.Color.g, uniforms.Color.b, uniforms.Color.a), black = float4(0.0, 0.0, 0.0, 1.0), white = float4(1.0, 1.0, 1.0, 1.0);
    
    return mix(white, color, saturate(alpha));
}


#pragma mark - Stroke

struct StrokeVertex
{
    float4 position [[position]];
    
    float4 color;
    float2 unitPoint;
};

vertex StrokeVertex stroke_vertex_main(constant Uniforms &uniforms [[buffer(0)]], constant Path &path [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    float2 unitPoint = float2(vid & 1, vid & 2);
    
    constant Arc& arc = path.arcs[0];
    float dt = (arc.t1 - arc.t0) * uniforms.flatness;
    float t0 = arc.t0 + dt * float(iid), t1 = t0 + dt;
    
    float x0 = arc.x + arc.r * cos(t0 * TWO_PI);
    float y0 = arc.y + arc.r * sin(t0 * TWO_PI);
    float x1 = arc.x + arc.r * cos(t1 * TWO_PI);
    float y1 = arc.y + arc.r * sin(t1 * TWO_PI);

//    constant Quadratic& q = path.quadratics[0];
//    float t0 = uniforms.flatness * float(iid), t1 = t0 + uniforms.flatness;
//    float dx01 = q.x1 - q.x0, dx12 = q.x2 - q.x1, ax = dx12 - dx01, bx = 2.0 * dx01;
//    float dy01 = q.y1 - q.y0, dy12 = q.y2 - q.y1, ay = dy12 - dy01, by = 2.0 * dy01;
//    
//    float x0 = fma(fma(ax, t0, bx), t0, q.x0);
//    float x1 = fma(fma(ax, t1, bx), t1, q.x0);
//    float y0 = fma(fma(ay, t0, by), t0, q.y0);
//    float y1 = fma(fma(ay, t1, by), t1, q.y0);
    
    float2 p0 = { x0, y0 }, p1 = { x1, y1 };
    float2 unit = normalize(p1 - p0);
    float2 normal = { unit.y, -unit.x };
    
    float2 devicePoint = select(p0, p1, vid & 2) + select(normal, -normal, vid & 1) * uniforms.width + select(-unit, unit, vid & 2) * uniforms.cap;
    float2 scale = { 1.0 / uniforms.drawableSize.w, 1.0 / uniforms.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4(((devicePoint * scale) * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = unitPoint;
    vert.color = { 1, 0, 0, 1 };
    
    return vert;
}

fragment float4 stroke_fragment_main(StrokeVertex vert [[stage_in]], constant Uniforms &uniforms [[buffer(0)]])
{
    return vert.color;
}
