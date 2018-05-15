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

float fragmentWinding(float2 p0, float2 p1) {
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

float triangleWinding(float2 p0, float2 t0, float2 p1, float2 t1, float2 p2, float2 t2) {
    float tx0, tx1, ty0, ty1, xt0, xt1, yt0, yt1, _t0, _t1;
    float x01, y01, x02, y02;
    float x11, y11, x12, y12;
    float x21, y21, x22, y22;
    
    tx0 = t0.x - dfdx(t0.x) * 0.5, tx1 = t0.x + dfdx(t0.x) * 0.5;
    ty0 = t0.y - dfdy(t0.y) * 0.5, ty1 = t0.y + dfdy(t0.y) * 0.5;
    xt0 = min(tx0, tx1), xt1 = max(tx0, tx1);
    yt0 = min(ty0, ty1), yt1 = max(ty0, ty1);
    _t0 = saturate(max(xt0, yt0)), _t1 = saturate(min(xt1, yt1));
    x01 = p0.x + _t0 * (p1.x - p0.x), y01 = p0.y + _t0 * (p1.y - p0.y);
    x02 = p0.x + _t1 * (p1.x - p0.x), y02 = p0.y + _t1 * (p1.y - p0.y);
    
    tx0 = t1.x - dfdx(t1.x) * 0.5, tx1 = t1.x + dfdx(t1.x) * 0.5;
    ty0 = t1.y - dfdy(t1.y) * 0.5, ty1 = t1.y + dfdy(t1.y) * 0.5;
    xt0 = min(tx0, tx1), xt1 = max(tx0, tx1);
    yt0 = min(ty0, ty1), yt1 = max(ty0, ty1);
    _t0 = saturate(max(xt0, yt0)), _t1 = saturate(min(xt1, yt1));
    x11 = p1.x + _t0 * (p2.x - p1.x), y11 = p1.y + _t0 * (p2.y - p1.y);
    x12 = p1.x + _t1 * (p2.x - p1.x), y12 = p1.y + _t1 * (p2.y - p1.y);
    
    tx0 = t2.x - dfdx(t2.x) * 0.5, tx1 = t2.x + dfdx(t2.x) * 0.5;
    ty0 = t2.y - dfdy(t2.y) * 0.5, ty1 = t2.y + dfdy(t2.y) * 0.5;
    xt0 = min(tx0, tx1), xt1 = max(tx0, tx1);
    yt0 = min(ty0, ty1), yt1 = max(ty0, ty1);
    _t0 = saturate(max(xt0, yt0)), _t1 = saturate(min(xt1, yt1));
    x21 = p2.x + _t0 * (p0.x - p2.x), y21 = p2.y + _t0 * (p0.y - p2.y);
    x22 = p2.x + _t1 * (p0.x - p2.x), y22 = p2.y + _t1 * (p0.y - p2.y);
    
    p0 = saturate(p0), p1 = saturate(p1), p2 = saturate(p2);
    // p0, p01, p12, p1, p11, p12, p2, p21, p22
    return p0.x * y01 - p0.y * x01
    + x01 * y02 - y01 * x02
    + x02 * p1.y - y02 * p1.x
    + p1.x * y11 - p1.y * x11
    + x11 * y12 - y11 * x12
    + x12 * p2.y - y12 * p2.x
    + p2.x * y21 - p2.y * x21
    + x21 * y22 - y21 * x22
    + x22 * p0.y - y11 * p0.x;
}
#pragma mark - Edge

struct EdgeVertex
{
    float4 position [[position]];
};

vertex EdgeVertex edge_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], device Vector *edges [[buffer(2)]], uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    Vector v0 = edges[iid], v1 = edges[iid + 1];
    bool end = as_type<uint>(v0.x) & 0x3;
    
    float2 p0 = {
        v0.x * state.CTM.a + v0.y * state.CTM.c + state.CTM.tx,
        v0.x * state.CTM.b + v0.y * state.CTM.d + state.CTM.ty
    };
    float2 p1 = {
        v1.x * state.CTM.a + v1.y * state.CTM.c + state.CTM.tx,
        v1.x * state.CTM.b + v1.y * state.CTM.d + state.CTM.ty
    };
    const float dilation = 0.7071;
    float2 up = normalize(p1 - p0), left = { -up.y, up.x };
    float2 devicePoint = select(p0, p1, vid > 1) + (select(-up, up, vid > 1) + select(left, -left, vid & 1)) * dilation;
    devicePoint = select(devicePoint, 0, end);
    
    EdgeVertex vert;
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), 0.0, 1.0);
    return vert;
}

fragment float4 edge_fragment_main(EdgeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    return float4(0, 0, 0, 1);
}

#pragma mark - Interior

struct InteriorVertex
{
    float4 position [[position]];
    float4 color;
};

vertex InteriorVertex interior_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], device RGBA8 *colors [[buffer(2)]], device Vector *points [[buffer(3)]], device Triangle *triangles [[buffer(4)]], uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    device Triangle &triangle = triangles[iid - biid];
    
    int i = select(select(triangle.i2, triangle.i1, vid == 1), triangle.i0, vid == 0);
    Vector p = points[i];
    
    float2 devicePoint = {
        p.x * state.CTM.a + p.y * state.CTM.c + state.CTM.tx,
        p.x * state.CTM.b + p.y * state.CTM.d + state.CTM.ty
    };
    
    InteriorVertex vert;
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), 0.0, 1.0);
    RGBA8 color = colors[triangle.c];
    vert.color = float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0);
    return vert;
}

fragment float4 interior_fragment_main(InteriorVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    return vert.color;
}

#pragma mark - Triangle

struct TriangleVertex
{
    float4 position [[position]];
    
    float2 p0, p1, p2;
    float2 t0, t1, t2;
    float u, v;
    float3 distance;
    bool showInterior;
    float4 color;
};

#pragma mark Vertex

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
    
    vert.t0 = { (p0.x - devicePoint.x) / (p1.x - p0.x), (p0.y - devicePoint.y) / (p1.y - p0.y) };
    vert.t1 = { (p1.x - devicePoint.x) / (p2.x - p1.x), (p1.y - devicePoint.y) / (p2.y - p1.y) };
    vert.t2 = { (p2.x - devicePoint.x) / (p0.x - p2.x), (p2.y - devicePoint.y) / (p0.y - p2.y) };
    
    devicePoint -= 0.5;
    vert.p0 = p0 - devicePoint, vert.p1 = p1 - devicePoint, vert.p2 = p2 - devicePoint;
    
    vert.u = select(select(1.0, 0.0, vid == 1), 0.0, vid == 0);
    vert.v = select(select(0.0, 0.0, vid == 1), 1.0, vid == 0);
    vert.distance = { d0, d1, d2 };
    vert.showInterior = meshState.showInterior;
    vert.color = select(float4(0, 0, 0, 0.666), float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0), meshState.showColors);
    
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
    
    float winding = fragmentWinding(vert.p0, p01) + fragmentWinding(p01, p12) + fragmentWinding(p12, vert.p2) + fragmentWinding(vert.p2, vert.p0);
    return vert.color * abs(winding);
}

fragment float4 triangle_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float min_dist = min(vert.distance.x, min(vert.distance.y, vert.distance.z));
    
    if (!vert.showInterior)
        return vert.color * (1.0 - abs(min_dist - 0.7071));
    
    if (min_dist > 0.7071)
        return vert.color;
    
//    float winding = triangleWinding(vert.p0, vert.t0, vert.p1, vert.t1, vert.p2, vert.t2) * 0.5;
    
    float winding = fragmentWinding(vert.p0, vert.p1) + fragmentWinding(vert.p1, vert.p2) + fragmentWinding(vert.p2, vert.p0);
    return vert.color * abs(winding);
}
