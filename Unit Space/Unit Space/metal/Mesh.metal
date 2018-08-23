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

float edgeDistance(float x0, float y0, float x1, float y1) {
    float ax = x1 - x0, ay = y1 - y0, bx = 0.5 - x0, by = 0.5 - y0;
    return (ax * by - ay * bx) * rsqrt(ax * ax + ay * ay);
}

float edgeWinding(float2 p0, float2 p1, float d = 0) {
    float y0 = saturate(p0.y), y1 = saturate(p1.y);
    float coverage = y1 - y0;
    
    if (abs(d) > 1)
        return select(coverage, 0.0, coverage * d > 0);
    
    if (coverage == 0.0 || saturate(p0.x) + saturate(p1.x) == 0.0)
        return coverage;
    
    float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
    float x0 = fma(y0 - p0.y, dxdy, p0.x), x1 = fma(y1 - p0.y, dxdy, p0.x);
    float minx = min(x0, x1), range = abs(x1 - x0);
    float t0 = saturate(-minx / range), t1 = saturate((1.0 - minx) / range);
    float area = (saturate(x0) + saturate(x1)) * 0.5;
    return coverage * (t1 - ((t1 - t0) * area));
}

float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2) {
    float dx12, dy12, dx20, dy20, area, u, v, w, f, d, t, x01, y01, x12, y12;
    dx12 = x2 - x1, dy12 = y2 - y1;
    dx20 = x0 - x2, dy20 = y0 - y2;
    area = dx12 * dy20 - dy12 * dx20;
    u = (dx12 * (0.5 - y1) - dy12 * (0.5 - x1)) / area;
    v = (dx20 * (0.5 - y2) - dy20 * (0.5 - x2)) / area;
    w = 1 - u - v;
    f = 4.0 * u * w - v * v;
    d = f * rsqrt(dfdx(f) * dfdx(f) + dfdy(f) * dfdy(f));
    
    /*
    if (abs(area) > 1e-3 && !(v > 0 && abs(d) < 0.7071))
        return step(0.0, d) * (saturate( min(min(y0, y1), y2)  ) - saturate( max(max(y0, y1), y2) )) + max(0.0, (saturate(y2) - saturate(y0)));
        //return copysign(step(0.0, d), -area) + max(0.0, (saturate(y2) - saturate(y0)));
        //return select(0.1, step(0.0, d) * (saturate(y1) - saturate(y0)), y2 < y0);
    */
    t = saturate(w / (w + v * 0.5));
    t = select(t, 1.0 - saturate(u / (u + v * 0.5)), t < 0.5);
    t = select(t, 0.5, f < -0.333);
    
    x01 = mix(x0, x1, t), y01 = mix(y0, y1, t), x12 = mix(x1, x2, t), y12 = mix(y1, y2, t);
    
   return edgeWinding(float2(x0, y0), float2(x01, y01))
    + edgeWinding(float2(x01, y01), float2(x12, y12))
    + edgeWinding(float2(x12, y12), float2(x2, y2));
}


#pragma mark - Accumulate

struct AccumulateVertex
{
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4;
    float d0, d1, d2, d3;
};

vertex AccumulateVertex accumulate_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                             device MeshPath *paths [[buffer(9)]], device Accumulation *accumulations [[buffer(10)]],
                                             uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    float dx0, dy0, dx1, dy1, dx2, dy2, dx3, dy3, dx4, dy4, lx, ly, ux, uy, dx, dy;
    
    device Accumulation& acc = accumulations[iid];
    
    AffineTransform CTM = AffineTransformConcat(paths[acc.p].ctm, state.CTM);
    AffineTransform unit = AffineTransformMakeUnit(paths[acc.p].bounds, CTM);
    Bounds bounds = Bounds::transformUnit(unit);
    
    dx0 = acc.x0 * CTM.a + acc.y0 * CTM.c + CTM.tx, dy0 = acc.x0 * CTM.b + acc.y0 * CTM.d + CTM.ty;
    dx1 = acc.x1 * CTM.a + acc.y1 * CTM.c + CTM.tx, dy1 = acc.x1 * CTM.b + acc.y1 * CTM.d + CTM.ty;
    dx2 = acc.x2 * CTM.a + acc.y2 * CTM.c + CTM.tx, dy2 = acc.x2 * CTM.b + acc.y2 * CTM.d + CTM.ty;
    dx3 = acc.x3 * CTM.a + acc.y3 * CTM.c + CTM.tx, dy3 = acc.x3 * CTM.b + acc.y3 * CTM.d + CTM.ty;
    dx4 = acc.x4 * CTM.a + acc.y4 * CTM.c + CTM.tx, dy4 = acc.x4 * CTM.b + acc.y4 * CTM.d + CTM.ty;
    
   // dx1 = dx2, dy1 = dy2;
   // dx3 = dx2, dy3 = dy2;
    
    lx = min(min(min(dx0, dx1), dx2), min(dx3, dx4));
    ly = min(min(min(dy0, dy1), dy2), min(dy3, dy4));
    ux = max(max(max(dx0, dx1), dx2), max(dx3, dx4));
    uy = max(max(max(dy0, dy1), dy2), max(dy3, dy4));
    ux = max(ux, bounds.ux);
    
    dx = select(floor(lx), ceil(ux), vid & 1), dy = select(floor(ly), ceil(uy), vid & 2);
    
    AccumulateVertex vert;
    vert.position = float4((float2(dx / state.drawableSize.w, dy / state.drawableSize.h) * 2.0 - 1.0), 1.0, 1.0);
    
    dx -= 0.5, dy -= 0.5;
    vert.x0 = dx0 - dx, vert.y0 = dy0 - dy;
    vert.x1 = dx1 - dx, vert.y1 = dy1 - dy;
    vert.x2 = dx2 - dx, vert.y2 = dy2 - dy;
    vert.x3 = dx3 - dx, vert.y3 = dy3 - dy;
    vert.x4 = dx4 - dx, vert.y4 = dy4 - dy;
    
    vert.d0 = edgeDistance(vert.x0, vert.y0, vert.x1, vert.y1);
    vert.d1 = edgeDistance(vert.x1, vert.y1, vert.x2, vert.y2);
    vert.d2 = edgeDistance(vert.x2, vert.y2, vert.x3, vert.y3);
    vert.d3 = edgeDistance(vert.x3, vert.y3, vert.x4, vert.y4);
    
    return vert;
}

struct AccumulateFragment {
    float4 color [[ color(0) ]];
    float4 winding [[ color(1) ]];
};

fragment AccumulateFragment accumulate_fragment_main(AccumulateVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    AccumulateFragment frag;
    
   // frag.winding = saturate(1 - abs(vert.d0)) + saturate(1 - abs(vert.d1)) + saturate(1 - abs(vert.d2)) + saturate(1 - abs(vert.d3));
    
    frag.winding =
    edgeWinding(float2(vert.x0, vert.y0), float2(vert.x1, vert.y1), vert.d0)
    + edgeWinding(float2(vert.x1, vert.y1), float2(vert.x2, vert.y2), vert.d1)
    + edgeWinding(float2(vert.x2, vert.y2), float2(vert.x3, vert.y3), vert.d2)
    + edgeWinding(float2(vert.x3, vert.y3), float2(vert.x4, vert.y4), vert.d3) ;
    
    /*
     frag.winding =
     edgeWinding(float2(vert.x0, vert.y0), float2(vert.x1, vert.y1))
     + edgeWinding(float2(vert.x1, vert.y1), float2(vert.x2, vert.y2))
     + edgeWinding(float2(vert.x2, vert.y2), float2(vert.x3, vert.y3))
     + edgeWinding(float2(vert.x3, vert.y3), float2(vert.x4, vert.y4)) ;
     
     frag.winding = 0.1666;
    frag.winding = quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2)
    + quadraticWinding(vert.x2, vert.y2, vert.x3, vert.y3, vert.x4, vert.y4);
    
     frag.winding =
     edgeWinding(float2(vert.x0, vert.y0), float2(vert.x2, vert.y2))
     + edgeWinding(float2(vert.x2, vert.y2), float2(vert.x4, vert.y4));
     */
    return frag;
}

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
