//
//  Mesh.metal
//  Unit Space
//
//  Created by Nigel Barber on 27/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include "Shared.metal.hpp"
#include <metal_stdlib>
#include "Headers.metal"

using namespace metal;


#pragma mark - Triangle

struct TriangleVertex
{
    float4 position [[position]];
    
    float2 p0, p1, p2;
    float u, v;
    float3 distance;
    bool showInterior;
    float4 color;
};


vertex TriangleVertex mesh_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], device RGBA8 *colors [[buffer(2)]], device Vector *points [[buffer(3)]], device Triangle *triangles [[buffer(4)]], uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    device Triangle &triangle = triangles[iid - biid];
    
    RGBA8 color = colors[triangle.c];
    
    Vector _v0 = points[triangle.i0], _v1 = points[triangle.i1], _v2 = points[triangle.i2];
    
    constant AffineTransform& ctm = state.CTM;
    
    float2 p0 = {
        _v0.x * ctm.a + _v0.y * ctm.c + ctm.tx,
        _v0.x * ctm.b + _v0.y * ctm.d + ctm.ty
    };
    
    float2 p1 = {
        _v1.x * ctm.a + _v1.y * ctm.c + ctm.tx,
        _v1.x * ctm.b + _v1.y * ctm.d + ctm.ty
    };
    
    float2 p2 = {
        _v2.x * ctm.a + _v2.y * ctm.c + ctm.tx,
        _v2.x * ctm.b + _v2.y * ctm.d + ctm.ty
    };
    
    float2 v01 = p1 - p0;
    float2 n01 = normalize(float2(v01.y, -v01.x));
    
    float2 v12 = p2 - p1;
    float2 n12 = normalize(float2(v12.y, -v12.x));
    
    float2 v20 = p0 - p2;
    float2 n20 = normalize(float2(v20.y, -v20.x));
    
    float h0 = dot(n12, v01);
    float h1 = dot(n20, v12);
    float h2 = dot(n01, v20);
    
    
    const float r = 10.0;
    const float dilation = 0.7071067812;
    const float hmin = 0.1;
    
    float dh0 = dilation / max(hmin, h0), dh1 = dilation / max(hmin, h1), dh2 = dilation / max(hmin, h2);
    
    
    float2 v0 = p0 - v01 * dh1 + v20 * dh2;
    float2 v1 = p1 - v12 * dh2 + v01 * dh0;
    float2 v2 = p2 - v20 * dh0 + v12 * dh1;
    
    float2 devicePoint = select(select(v2, v1, vid == 1), v0, vid == 0);
    float d0 = dot(p0 - devicePoint, n01);
    float d1 = dot(p1 - devicePoint, n12);
    float d2 = dot(p2 - devicePoint, n20);
    
    TriangleVertex vert;
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), 0.0, 1.0);
    
    devicePoint -= 0.5;
    vert.p0 = p0 - devicePoint, vert.p1 = p1 - devicePoint, vert.p2 = p2 - devicePoint;
    
    vert.u = select(select(1.0, 0.0, vid == 1), 0.0, vid == 0);
    vert.v = select(select(0.0, 0.0, vid == 1), 1.0, vid == 0);
    
    vert.distance = { d0, d1, d2 };
    vert.showInterior = meshState.showInterior;
    vert.color = select(float4(0, 0, 0, 0.666), float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0), meshState.showColors);
    
    return vert;
}

fragment float4 curve_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float w = 1.0 - vert.u - vert.v, f = 4.0 * vert.u * vert.v - w * w;
    float dfx = dfdx(f), dfy = dfdy(f);
    float dist = f * rsqrt(dfx * dfx + dfy * dfy);
    
    if (!vert.showInterior) {
        const float offset = -0.7071;
        return vert.color * (1.0 - abs(min(dist + offset, vert.distance.z + offset)));
    }
    
    if (dist < -0.7071)
        return 0;
    
    if (dist > 1.41 && vert.distance.z > 0.7071)
        return vert.color;
    
    float t = saturate(vert.u / (vert.u + w * 0.5));
    t = select(t, 1.0 - saturate(vert.v / (vert.v + w * 0.5)), t < 0.5);
    float2 p01 = mix(vert.p0, vert.p1, t), p12 = mix(vert.p1, vert.p2, t);
    
    float winding = fragmentWinding(vert.p0, p01) + fragmentWinding(p01, p12) + fragmentWinding(p12, vert.p2) + fragmentWinding(vert.p2, vert.p0);
    return vert.color * abs(winding);
}

fragment float4 triangle_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float min_dist = min(vert.distance.x, min(vert.distance.y, vert.distance.z));
    
    if (!vert.showInterior) {
        const float offset = -0.7071;
        float min_dist = min(vert.distance.x + offset, min(vert.distance.y + offset, vert.distance.z + offset));
        return vert.color * (1.0 - abs(min_dist));
    }
    if (min_dist > 0.7071)
        return vert.color;
    
    float winding = fragmentWinding(vert.p0, vert.p1) + fragmentWinding(vert.p1, vert.p2) + fragmentWinding(vert.p2, vert.p0);
    return vert.color * abs(winding);
}
