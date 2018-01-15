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
    
    float2 unitPoint;
};

vertex StrokeVertex stroke_vertex_main(constant Uniforms &uniforms [[buffer(0)]], constant Path &path [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint count = ceil(1.0 / uniforms.flatness);
    uint index = iid / count;
    
    constant Arc& arc = path.arcs[index];
    float dt = (arc.t1 - arc.t0) * uniforms.flatness;
    float t0 = arc.t0 + dt * float(iid % count), t1 = t0 + dt;
    
    float t = select(t0, t1, vid & 2);
    float w = select(-uniforms.width, uniforms.width, vid & 1);
    float x = arc.x + (arc.r + w) * cos(t * TWO_PI);
    float y = arc.y + (arc.r + w) * sin(t * TWO_PI);
    
    float dx = x * uniforms.CTM.a + y * uniforms.CTM.c + uniforms.CTM.tx;
    float dy = x * uniforms.CTM.b + y * uniforms.CTM.d + uniforms.CTM.ty;
    
    float scale = sqrt(uniforms.CTM.a * uniforms.CTM.a + uniforms.CTM.b * uniforms.CTM.b);
    float offset = 0.7071 / scale;
    float ux = arc.x - arc.r - offset, uy = arc.y - arc.r - offset, uw = (arc.r + offset) * 2.0;
    AffineTransform unit = { uw, 0, 0, uw, ux, uy };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, uniforms.CTM));
    
    float2 unitPoint = { dx / uniforms.drawableSize.w, dy / uniforms.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    
    return vert;
}

fragment float4 stroke_fragment_main(StrokeVertex vert [[stage_in]], constant Uniforms &uniforms [[buffer(0)]])
{
    float alpha = ellipseDistance(vert.unitPoint);
    return float4(uniforms.strokeColor.r, uniforms.strokeColor.g, uniforms.strokeColor.b, uniforms.strokeColor.a) * alpha;
}
