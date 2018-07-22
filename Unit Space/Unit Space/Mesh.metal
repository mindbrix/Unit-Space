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

#pragma mark - Library

float edgeWinding(float2 p0, float2 p1) {
    float y0 = saturate(p0.y), y1 = saturate(p1.y);
    float coverage = y1 - y0;
    
    if (coverage == 0.0 || saturate(p0.x) + saturate(p1.x) == 0.0)
        return coverage;
    
    float dx = p1.x - p0.x, dy = p1.y - p0.y, dxdy = dx / dy;
    float x0 = (y0 - p0.y) * dxdy + p0.x;
    float x1 = (y1 - p0.y) * dxdy + p0.x;
    
    float minx = min(x0, x1), range = abs(x1 - x0);
    float t0 = saturate(-minx / range), t1 = saturate((1.0 - minx) / range);
    
    float area = (saturate(x0) + saturate(x1)) * 0.5;
    return coverage * (t1 - ((t1 - t0) * area));
}

#pragma mark - Interior

struct InteriorVertex
{
    float4 position [[position]];
    float u, v;
    bool showInterior;
    float4 color;
};

vertex InteriorVertex interior_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], device RGBA8 *colors [[buffer(2)]], device Vector *points [[buffer(3)]], device Triangle *triangles [[buffer(4)]], device float *triangleScales [[buffer(5)]], device Vector *interiorVectors [[buffer(6)]], device AffineTransform *pathCTMs [[buffer(7)]], device Vector *pts [[buffer(10)]], uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    uint idx = select(iid, meshState.reverseCount - 1 - iid + biid, meshState.reverseCount);
    idx *= 7;
    
    int c = pts[idx].x;
    float t = pts[idx].y;
    int i = idx + 1 + vid * 2;
    
    
//    device Triangle &triangle = triangles[idx];
//    float t = triangleScales[idx];
//    int c = triangle.c;
//    int i = select(select(triangle.i2, triangle.i1, vid == 1), triangle.i0, vid == 0);
    
    AffineTransform CTM = pathCTMs[c];
    CTM = AffineTransformConcat(CTM, state.CTM);
    float2 devicePoint = {
        pts[i].x * CTM.a + pts[i].y * CTM.c + CTM.tx,
        pts[i].x * CTM.b + pts[i].y * CTM.d + CTM.ty
    };
//    float2 devicePoint = {
//        points[i].x * CTM.a + points[i].y * CTM.c + CTM.tx,
//        points[i].x * CTM.b + points[i].y * CTM.d + CTM.ty
//    };
    
    float scale = sqrt(abs(CTM.d * CTM.a - CTM.b * CTM.c));
    Vector interiorVector = pts[i + 1];
//    Vector interiorVector = interiorVectors[idx * 3 + vid];
    float2 iv = {
        interiorVector.x * CTM.a + interiorVector.y * CTM.c,
        interiorVector.x * CTM.b + interiorVector.y * CTM.d
    };
    devicePoint += iv / scale;
    devicePoint = select(devicePoint, 0, scale < t);
    InteriorVertex vert;
    float z = (c * 2 + 2) / float(meshState.colorsSize * 2 + 2);
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    vert.u = select(select(1.0, 0.0, vid == 1), 0.0, vid == 0);
    vert.v = select(select(0.0, 0.0, vid == 1), 1.0, vid == 0);
    vert.showInterior = meshState.showInterior;
    RGBA8 color = colors[c];
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
    float u, v;
    float3 distance;
    bool showInterior;
    float4 color;
};

#pragma mark Vertex

vertex TriangleVertex mesh_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], device RGBA8 *colors [[buffer(2)]], device Vector *points [[buffer(3)]], device Triangle *triangles [[buffer(4)]], device float *triangleScales [[buffer(5)]], device AffineTransform *pathCTMs [[buffer(7)]], device Vector *pts [[buffer(10)]], uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    device Triangle &triangle = triangles[iid];
    RGBA8 color = colors[triangle.c];
    Vector _v0 = points[triangle.i0], _v1 = points[triangle.i1], _v2 = points[triangle.i2];
    AffineTransform CTM = pathCTMs[triangle.c];
    CTM = AffineTransformConcat(CTM, state.CTM);
    
    float2 p0 = {
        _v0.x * CTM.a + _v0.y * CTM.c + CTM.tx,
        _v0.x * CTM.b + _v0.y * CTM.d + CTM.ty
    };
    
    float2 p1 = {
        _v1.x * CTM.a + _v1.y * CTM.c + CTM.tx,
        _v1.x * CTM.b + _v1.y * CTM.d + CTM.ty
    };
    
    float2 p2 = {
        _v2.x * CTM.a + _v2.y * CTM.c + CTM.tx,
        _v2.x * CTM.b + _v2.y * CTM.d + CTM.ty
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
    const float dilation = -0.7071067812;
    const float hmin = 0.1;
    
    float dh0 = dilation / max(hmin, h0), dh1 = dilation / max(hmin, h1), dh2 = dilation / max(hmin, h2);
//    dh0 = min(0.33333, dh0), dh1 = min(0.33333, dh1), dh2 = min(0.33333, dh2);
    float2 v0 = p0 * (1 - dh1 - dh2) + p1 * dh1 + p2 * dh2;
    float2 v1 = p0 * dh0 + p1 * (1 - dh0 - dh2) + p2 * dh2;
    float2 v2 = p0 * dh0 + p1 * dh1 + p2 * (1 - dh0 - dh1);
    
    float2 devicePoint = select(select(v2, v1, vid == 1), v0, vid == 0);
    float d0 = dot(p0 - devicePoint, n01);
    float d1 = dot(p1 - devicePoint, n12);
    float d2 = dot(p2 - devicePoint, n20);
    
    float z = (triangle.c * 2 + 1) / float(meshState.colorsSize * 2 + 2);
    TriangleVertex vert;
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    
    devicePoint -= 0.5;
    vert.p0 = p0 - devicePoint, vert.p1 = p1 - devicePoint, vert.p2 = p2 - devicePoint;
    
    vert.u = select(select(1.0, 0.0, vid == 1), 0.0, vid == 0);
    vert.v = select(select(0.0, 0.0, vid == 1), 1.0, vid == 0);
    vert.distance = { d0, d1, d2 };
    vert.showInterior = meshState.showInterior;
    vert.color = select(float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), meshState.useStrokeColor);
    return vert;
}

#pragma mark Fragment

fragment float4 curve_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float w = 1.0 - vert.u - vert.v, f = 4.0 * vert.u * vert.v - w * w;
    float dfx = dfdx(f), dfy = dfdy(f);
    float dist = f * rsqrt(dfx * dfx + dfy * dfy);
    
    if (!vert.showInterior)
        return vert.color * (1.0 - abs(min(dist, vert.distance.z) - 0.7071));
    
    if (dist < -0.7071)
        return 0;
    
    if (dist > 1.41 && vert.distance.z > 0.7071)
        return vert.color;
    
    float t = saturate(vert.u / (vert.u + w * 0.5));
    t = select(t, 1.0 - saturate(vert.v / (vert.v + w * 0.5)), t < 0.5);
    float2 p01 = mix(vert.p0, vert.p1, t), p12 = mix(vert.p1, vert.p2, t);
    
    float winding = edgeWinding(vert.p0, p01) + edgeWinding(p01, p12) + edgeWinding(p12, vert.p2) + edgeWinding(vert.p2, vert.p0);
    return vert.color * abs(winding);
}

fragment float4 triangle_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    if (min(vert.distance.x, min(vert.distance.y, vert.distance.z)) > 0.7071)
        return vert.color;
    
    float winding = edgeWinding(vert.p0, vert.p1) + edgeWinding(vert.p1, vert.p2) + edgeWinding(vert.p2, vert.p0);
    return vert.color * abs(winding);
}
