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

#pragma mark - Accumulate


struct AccumulateVertex
{
    void write(AffineTransform CTM, float ux, uint vid, device float *points, constant State &state, Bounds clipBounds, uint kStride) {
        float dx0, dy0, dx1, dy1, dx2, dy2, dx3, dy3, dx4, dy4, lx, ly, uy, dx, dy;
        device float *px, *py;
        
        px = points, py = px + 1;
        dx0 = *px * CTM.a + *py * CTM.c + CTM.tx, dy0 = *px * CTM.b + *py * CTM.d + CTM.ty;
        px += kStride, py += kStride;
        if (*px != FLT_MAX) {
            dx1 = *px * CTM.a + *py * CTM.c + CTM.tx, dy1 = *px * CTM.b + *py * CTM.d + CTM.ty;
            px += kStride, py += kStride;
            dx2 = *px * CTM.a + *py * CTM.c + CTM.tx, dy2 = *px * CTM.b + *py * CTM.d + CTM.ty;
            px += kStride, py += kStride;
            if (*px != FLT_MAX) {
                dx3 = *px * CTM.a + *py * CTM.c + CTM.tx, dy3 = *px * CTM.b + *py * CTM.d + CTM.ty;
                px += kStride, py += kStride;
                dx4 = *px * CTM.a + *py * CTM.c + CTM.tx, dy4 = *px * CTM.b + *py * CTM.d + CTM.ty;
            } else
                dx3 = dx4 = dx2, dy3 = dy4 = dy2;
        } else
            dx1 = dx2 = dx3 = dx4 = dx0, dy1 = dy2 = dy3 = dy4 = dy0;
        
        lx = min(min(min(dx0, dx1), dx2), min(dx3, dx4));
        ly = min(min(min(dy0, dy1), dy2), min(dy3, dy4));
        uy = max(max(max(dy0, dy1), dy2), max(dy3, dy4));
        
//        const float kDilation = 0.5 * ((1.0 / 0.8) - 1.0);
//        dx = select(floor(lx - kDilation), ceil(ux + kDilation), vid & 1), dy = select(floor(ly), ceil(uy), vid & 2);
        dx = select(floor(lx), ceil(ux), vid & 1), dy = select(floor(ly), ceil(uy), vid & 2);
        
        dx = clamp(dx, clipBounds.lx, clipBounds.ux);
        dy = clamp(dy, clipBounds.ly, clipBounds.uy);
        
        position = float4((float2(dx / state.drawableSize.w, dy / state.drawableSize.h) * 2.0 - 1.0), 0.0, 1.0);
        
        dx -= 0.5, dy -= 0.5;
        x0 = dx0 - dx, y0 = dy0 - dy;
        x1 = dx1 - dx, y1 = dy1 - dy;
        x2 = dx2 - dx, y2 = dy2 - dy;
        x3 = dx3 - dx, y3 = dy3 - dy;
        x4 = dx4 - dx, y4 = dy4 - dy;
    }
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4;
};

vertex AccumulateVertex quadratic_accumulate_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                             device MeshPath *paths [[buffer(9)]],
                                             device float *quadratics [[buffer(10)]], device int *ps [[buffer(11)]],
                                             uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint idx = iid * meshState.quadraticStride;
    int p = ps[idx / 8];
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
    AffineTransform unit = AffineTransformMakeUnit(paths[p].bounds, CTM);
    Bounds bounds = Bounds::transformUnit(unit);
    
    AccumulateVertex vert;
    vert.write(CTM, bounds.ux, vid, & quadratics[idx], state, meshState.clipBounds, 2);
    return vert;
}

struct AccumulateFragment {
    float4 winding [[ color(1) ]];
};

fragment AccumulateFragment accumulate_fragment_main(AccumulateVertex vert [[stage_in]],
													 constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    AccumulateFragment frag;
    if (meshState.useQuadraticWinding)
        frag.winding = quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2) + quadraticWinding(vert.x2, vert.y2, vert.x3, vert.y3, vert.x4, vert.y4);
    else
        frag.winding = edgeWinding(vert.x0, vert.y0, vert.x2, vert.y2) + edgeWinding(vert.x2, vert.y2, vert.x4, vert.y4);
    return frag;
}

#pragma mark - Interior

struct InteriorVertex
{
    float4 position [[position]];
    float u, v;
    float d0, d1, d2, d3, d4, d5, d6;
    float4 color;
};

vertex InteriorVertex interior_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                           device Triangle *tris [[buffer(8)]], device MeshPath *paths [[buffer(9)]],
                                           uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    uint idx = select(iid, meshState.reverseCount - 1 - iid + biid, meshState.reverseCount);
    
    device Triangle& tri = tris[idx];
    int c = tri.p;
    float t = tri.t;
    AffineTransform CTM = AffineTransformConcat(paths[c].ctm, state.CTM), iCTM = CTM;
    float scale = sqrt(abs(CTM.a * CTM.d - CTM.b * CTM.c));
	Bounds bounds = paths[c].bounds;
	float w = (bounds.ux - bounds.lx) / kShortMax, h = (bounds.uy - bounds.ly) / kShortMax;
	CTM = {
		w * CTM.a,              w * CTM.b,
		h * CTM.c,              h * CTM.d,
		bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx,    bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty
	};
    float2 p0 = { tri.x0 * CTM.a + tri.y0 * CTM.c + CTM.tx, tri.x0 * CTM.b + tri.y0 * CTM.d + CTM.ty };
    float2 p1 = { tri.x1 * CTM.a + tri.y1 * CTM.c + CTM.tx, tri.x1 * CTM.b + tri.y1 * CTM.d + CTM.ty };
    float2 p2 = { tri.x2 * CTM.a + tri.y2 * CTM.c + CTM.tx, tri.x2 * CTM.b + tri.y2 * CTM.d + CTM.ty };
    float2 devicePoint = select(select(p2, p1, vid == 1), p0, vid == 0);
    
    float ms = 1.0 / scale / kShortMax * kInteriorMax;
    CTM = iCTM;
    CTM.a *= ms, CTM.b *= ms, CTM.c *= ms, CTM.d *= ms;
    float2 iv0 = { tri.ix0 * CTM.a + tri.iy0 * CTM.c, tri.ix0 * CTM.b + tri.iy0 * CTM.d };
    float2 iv1 = { tri.ix1 * CTM.a + tri.iy1 * CTM.c, tri.ix1 * CTM.b + tri.iy1 * CTM.d };
    float2 iv2 = { tri.ix2 * CTM.a + tri.iy2 * CTM.c, tri.ix2 * CTM.b + tri.iy2 * CTM.d };

    float d0, d1, d2, d3, d4, d5, width, cosine, sine;
    float2 iv, niv, ep, en;
    d0 = d1 = d2 = d3 = d4 = d5 = 0, width = 10;
    
    iv = iv0;
    cosine = min(1.0, rsqrt(iv.x * iv.x + iv.y * iv.y)), sine = sqrt(1 - cosine * cosine);
//    cosine = sine = sqrt(0.5);
    niv = normalize(iv);
    en = { niv.x * cosine - niv.y * sine, niv.x * sine + niv.y * cosine };
    ep = { niv.x * cosine + niv.y * sine, niv.x * -sine + niv.y * cosine };
    d0 = width - dot(devicePoint - p0, en);
    d1 = width - dot(devicePoint - p0, ep);
    
    iv = iv1;
    cosine = min(1.0, rsqrt(iv.x * iv.x + iv.y * iv.y)), sine = sqrt(1 - cosine * cosine);
    niv = normalize(iv);
    en = { niv.x * cosine - niv.y * sine, niv.x * sine + niv.y * cosine };
    ep = { niv.x * cosine + niv.y * sine, niv.x * -sine + niv.y * cosine };
    d2 = width - dot(devicePoint - p1, en);
    d3 = width - dot(devicePoint - p1, ep);

    iv = iv2;
    cosine = min(1.0, rsqrt(iv.x * iv.x + iv.y * iv.y)), sine = sqrt(1 - cosine * cosine);
    niv = normalize(iv);
    en = { niv.x * cosine - niv.y * sine, niv.x * sine + niv.y * cosine };
    ep = { niv.x * cosine + niv.y * sine, niv.x * -sine + niv.y * cosine };
    d4 = width - dot(devicePoint - p2, en);
    d5 = width - dot(devicePoint - p2, ep);
    
    iv = select(select(iv2, iv1, vid == 1), iv0, vid == 0);
    devicePoint = select(devicePoint + iv, 0, scale < t);
    RGBA8 color = paths[c].color;
    
    InteriorVertex vert;
    float z = (c * 2 + 2) / float(meshState.colorsSize * 2 + 2);
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    vert.u = float(vid == 2);
    vert.v = float(vid == 0);
    vert.d0 = d0, vert.d1 = d1, vert.d2 = d2, vert.d3 = d3, vert.d4 = d4, vert.d5 = d5;
    vert.color = float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0);
    return vert;
}

fragment float4 interior_fragment_main(InteriorVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    float w = 1.0 - vert.u - vert.v;
    float eu = 1.0 - vert.u * rsqrt(dfdx(vert.u) * dfdx(vert.u) + dfdy(vert.u) * dfdy(vert.u));
    float ev = 1.0 - vert.v * rsqrt(dfdx(vert.v) * dfdx(vert.v) + dfdy(vert.v) * dfdy(vert.v));
    float ew = 1.0 - w * rsqrt(dfdx(w) * dfdx(w) + dfdy(w) * dfdy(w));
    
    float e0 = max(saturate(vert.d0), max(saturate(vert.d1), saturate(vert.d2)));
    float e1 = max(saturate(vert.d3), max(saturate(vert.d4), saturate(vert.d5)));
    float edges = 0;//max(saturate(eu), max(saturate(ev), saturate(ew)));
    return select(vert.color, mix(vert.color, float4(0, 0, 0, 1), max(max(e0, e1), edges)), meshState.showTriangles);
    
    //return select(vert.color, vert.color * max(max(e0, e1), edges), meshState.showTriangles);
    
    return select(vert.color, vert.color * max(eu, max(ev, ew)), meshState.showTriangles);
}

#pragma mark - Triangle

struct TriangleVertex
{
    float4 position [[position]];
    float2 p0, p1, p2;
    float4 color;
};

#pragma mark Vertex

vertex TriangleVertex mesh_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                       device Triangle *tris [[buffer(8)]],
                                       device MeshPath *paths [[buffer(9)]],
                                       uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    uint idx = iid;
    
    device Triangle& tri = tris[idx];
    int c = tri.p;
    AffineTransform CTM = AffineTransformConcat(paths[c].ctm, state.CTM);
	Bounds bounds = paths[c].bounds;
	float w = (bounds.ux - bounds.lx) / kShortMax, h = (bounds.uy - bounds.ly) / kShortMax;
	CTM = {
		w * CTM.a,              w * CTM.b,
		h * CTM.c,              h * CTM.d,
		bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx,    bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty
	};
    float2 p0 = { tri.x0 * CTM.a + tri.y0 * CTM.c + CTM.tx, tri.x0 * CTM.b + tri.y0 * CTM.d + CTM.ty };
    float2 p1 = { tri.x1 * CTM.a + tri.y1 * CTM.c + CTM.tx, tri.x1 * CTM.b + tri.y1 * CTM.d + CTM.ty };
    float2 p2 = { tri.x2 * CTM.a + tri.y2 * CTM.c + CTM.tx, tri.x2 * CTM.b + tri.y2 * CTM.d + CTM.ty };
        
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
    
    RGBA8 color = paths[c].color;
    
    float z = (c * 2 + 1) / float(meshState.colorsSize * 2 + 2);
    TriangleVertex vert;
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    
    devicePoint -= 0.5;
    vert.p0 = p0 - devicePoint, vert.p1 = p1 - devicePoint, vert.p2 = p2 - devicePoint;
    vert.color = select(float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), meshState.useStrokeColor);
    return vert;
}

#pragma mark Fragment

fragment float4 triangle_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    float winding = edgeWinding(vert.p0.x, vert.p0.y, vert.p1.x, vert.p1.y)
	+ edgeWinding(vert.p1.x, vert.p1.y, vert.p2.x, vert.p2.y)
	+ edgeWinding(vert.p2.x, vert.p2.y, vert.p0.x, vert.p0.y);
    return select(vert.color, float4(0, 0, 0, 1), meshState.showTriangles) * abs(winding);
}
