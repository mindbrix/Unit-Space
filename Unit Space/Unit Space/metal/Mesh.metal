//
//  Mesh.metal
//  Unit Space
//
//  Created by Nigel Barber on 27/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#include "Shared.metal.hpp"
#include "Headers.metal"
#include <metal_stdlib>

using namespace metal;

#pragma mark - Interior

struct InteriorVertex
{
    float4 position [[position]];
    float u, v;
    bool showInterior;
    float4 color;
};

vertex InteriorVertex interior_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], device MeshPath *paths [[buffer(9)]], device Vector *pts [[buffer(10)]], uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    uint idx = kMeshTriangleSize * select(iid, meshState.reverseCount - 1 - iid + biid, meshState.reverseCount);
    int c = pts[idx].x;
    float t = pts[idx].y;
    int i = idx + 1 + vid * 2;
    
    RGBA8 color = paths[c].color;
    AffineTransform CTM = AffineTransformConcat(paths[c].ctm, state.CTM);
    float2 devicePoint = { pts[i].x * CTM.a + pts[i].y * CTM.c + CTM.tx, pts[i].x * CTM.b + pts[i].y * CTM.d + CTM.ty };
    
    if (!meshState.disableInteriorVector) {
        float scale = sqrt(abs(CTM.d * CTM.a - CTM.b * CTM.c));
        Vector interiorVector = pts[i + 1];
        float2 iv = { interiorVector.x * CTM.a + interiorVector.y * CTM.c, interiorVector.x * CTM.b + interiorVector.y * CTM.d };
        devicePoint = select(devicePoint + iv / scale, 0, scale < t);
    }
    InteriorVertex vert;
    float z = (c * 2 + 2) / float(meshState.colorsSize * 2 + 2);
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    vert.u = float(vid == 2);
    vert.v = float(vid == 0);
    vert.showInterior = meshState.showInterior;
    vert.color = float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0);
    return vert;
}

fragment float4 interior_fragment_main(InteriorVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float w = 1.0 - vert.u - vert.v;
    float eu = 1.0 - vert.u * rsqrt(dfdx(vert.u) * dfdx(vert.u) + dfdy(vert.u) * dfdy(vert.u));
    float ev = 1.0 - vert.v * rsqrt(dfdx(vert.v) * dfdx(vert.v) + dfdy(vert.v) * dfdy(vert.v));
    float ew = 1.0 - w * rsqrt(dfdx(w) * dfdx(w) + dfdy(w) * dfdy(w));
    return select(vert.color * max(eu, max(ev, ew)), vert.color, vert.showInterior);
}

#pragma mark - Triangle

struct TriangleVertex
{
    float4 position [[position]];
    float2 p0, p1, p2;
    float4 color;
};

#pragma mark Vertex

vertex TriangleVertex mesh_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], device MeshPath *paths [[buffer(9)]], device Vector *pts [[buffer(10)]], uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    uint idx = iid * kMeshTriangleSize;
    int c = pts[idx].x;
    float x0 = pts[idx + 1].x, y0 = pts[idx + 1].y, x1 = pts[idx + 3].x, y1 = pts[idx + 3].y, x2 = pts[idx + 5].x, y2 = pts[idx + 5].y;
        
    RGBA8 color = paths[c].color;
    AffineTransform CTM = AffineTransformConcat(paths[c].ctm, state.CTM);
    
    float2 p0 = { x0 * CTM.a + y0 * CTM.c + CTM.tx, x0 * CTM.b + y0 * CTM.d + CTM.ty };
    float2 p1 = { x1 * CTM.a + y1 * CTM.c + CTM.tx, x1 * CTM.b + y1 * CTM.d + CTM.ty };
    float2 p2 = { x2 * CTM.a + y2 * CTM.c + CTM.tx, x2 * CTM.b + y2 * CTM.d + CTM.ty };
    
    const float dilation = -0.5, hmin = 0.1;
    float2 v01 = p1 - p0, n01 = normalize(float2(v01.y, -v01.x));
    float2 v12 = p2 - p1, n12 = normalize(float2(v12.y, -v12.x));
    float2 v20 = p0 - p2, n20 = normalize(float2(v20.y, -v20.x));
    float h0 = dot(n12, v01), h1 = dot(n20, v12), h2 = dot(n01, v20);
    float dh0 = dilation / max(hmin, h0), dh1 = dilation / max(hmin, h1), dh2 = dilation / max(hmin, h2);
    float2 dp0 = p0 * (1 - dh1 - dh2) + p1 * dh1 + p2 * dh2;
    float2 dp1 = p0 * dh0 + p1 * (1 - dh0 - dh2) + p2 * dh2;
    float2 dp2 = p0 * dh0 + p1 * dh1 + p2 * (1 - dh0 - dh1);
    float2 devicePoint = select(select(dp2, dp1, vid == 1), dp0, vid == 0);
    
    float z = (c * 2 + 1) / float(meshState.colorsSize * 2 + 2);
    TriangleVertex vert;
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    
    devicePoint -= 0.5;
    vert.p0 = p0 - devicePoint, vert.p1 = p1 - devicePoint, vert.p2 = p2 - devicePoint;
    vert.color = select(float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), meshState.useStrokeColor);
    return vert;
}

#pragma mark Fragment

fragment float4 triangle_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float winding = edgeWinding(vert.p0, vert.p1) + edgeWinding(vert.p1, vert.p2) + edgeWinding(vert.p2, vert.p0);
    return vert.color * abs(winding);
}

//fragment float4 curve_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]])
//{
//    float w = 1.0 - vert.u - vert.v, f = 4.0 * vert.u * vert.v - w * w;
//    float dfx = dfdx(f), dfy = dfdy(f);
//    float dist = f * rsqrt(dfx * dfx + dfy * dfy);
//
//    if (!vert.showInterior)
//        return vert.color * (1.0 - abs(min(dist, vert.distance.z) - 0.7071));
//
//    if (dist < -0.7071)
//        return 0;
//
//    if (dist > 1.41 && vert.distance.z > 0.7071)
//        return vert.color;
//
//    float t = saturate(vert.u / (vert.u + w * 0.5));
//    t = select(t, 1.0 - saturate(vert.v / (vert.v + w * 0.5)), t < 0.5);
//    float2 p01 = mix(vert.p0, vert.p1, t), p12 = mix(vert.p1, vert.p2, t);
//
//    float winding = edgeWinding(vert.p0, p01) + edgeWinding(p01, p12) + edgeWinding(p12, vert.p2) + edgeWinding(vert.p2, vert.p0);
//    return vert.color * abs(winding);
//}
