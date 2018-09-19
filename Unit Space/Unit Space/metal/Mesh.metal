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
    float x, y;
    float d0, d1, d2, d3, d4, d5, d6;
    float4 color;
};

float2 ivDistances(float2 iv, float width, float sx, float sy) {
    float cos2 = 1.0 / max(1.0, iv.x * iv.x + iv.y * iv.y), sinecosine = sqrt((1 - cos2) * cos2);
    return {
        width - (sx * (iv.x * cos2 - iv.y * sinecosine) + sy * (iv.x * sinecosine + iv.y * cos2)),
        width - (sx * (iv.x * cos2 + iv.y * sinecosine) + sy * (iv.x * -sinecosine + iv.y * cos2))
    };
}

vertex InteriorVertex interior_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                           device Triangle *tris [[buffer(8)]], device MeshPath *paths [[buffer(9)]],
                                           uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    uint idx = select(iid, meshState.reverseCount - 1 - iid + biid, meshState.reverseCount);
    device Triangle& tri = tris[idx];
    int p = tri.p;
    float t = tri.t;
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
    float rscale = rsqrt(abs(CTM.a * CTM.d - CTM.b * CTM.c));
    
    Bounds bounds = paths[p].bounds;
	float w = (bounds.ux - bounds.lx) / kShortMax, h = (bounds.uy - bounds.ly) / kShortMax;
    float a = w * CTM.a, b = w * CTM.b, c = h * CTM.c, d = h * CTM.d, tx = bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx, ty = bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty;
    float2 p0 = { tri.x0 * a + tri.y0 * c + tx, tri.x0 * b + tri.y0 * d + ty };
    float2 p1 = { tri.x1 * a + tri.y1 * c + tx, tri.x1 * b + tri.y1 * d + ty };
    float2 p2 = { tri.x2 * a + tri.y2 * c + tx, tri.x2 * b + tri.y2 * d + ty };
    float2 devicePoint = select(select(p2, p1, vid == 1), p0, vid == 0);
    
    float ms = rscale / kShortMax * kInteriorMax, ia = CTM.a * ms, ib = CTM.b * ms, ic = CTM.c * ms, id = CTM.d * ms;
    float2 iv0 = { tri.ix0 * ia + tri.iy0 * ic, tri.ix0 * ib + tri.iy0 * id };
    float2 iv1 = { tri.ix1 * ia + tri.iy1 * ic, tri.ix1 * ib + tri.iy1 * id };
    float2 iv2 = { tri.ix2 * ia + tri.iy2 * ic, tri.ix2 * ib + tri.iy2 * id };

    float2 dd0 = ivDistances(iv0, 10, devicePoint.x - p0.x, devicePoint.y - p0.y);
    float2 dd1 = ivDistances(iv1, 10, devicePoint.x - p1.x, devicePoint.y - p1.y);
    float2 dd2 = ivDistances(iv2, 10, devicePoint.x - p2.x, devicePoint.y - p2.y);
    
    float2 iv = select(select(iv2, iv1, vid == 1), iv0, vid == 0);
    devicePoint = select(devicePoint + iv, 0, 1.0 / rscale < t);
    RGBA8 color = paths[p].color;
    
    InteriorVertex vert;
    float z = (p * 2 + 2) / float(meshState.colorsSize * 2 + 2);
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    vert.u = float(vid == 2);
    vert.v = float(vid == 0);
    vert.x = float(vid == 1);
    vert.y = float(vid == 2);
    vert.d0 = dd0.x, vert.d1 = dd0.y, vert.d2 = dd1.x, vert.d3 = dd1.y, vert.d4 = dd2.x, vert.d5 = dd2.y;
    vert.color = float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0);
    return vert;
}

fragment float4 interior_fragment_main(InteriorVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    float2 lambda = { vert.u, vert.v };
    float2x2 j = float2x2(dfdx(lambda), dfdy(lambda));
    float detJ = determinant(j);
    float2 detXY = float2(determinant(float2x2(j[1], lambda)),
                      determinant(float2x2(lambda, j[0])));
    float2 v = detJ * 0 + detXY;
    
    float2 v0 = (v + float2(j[1][1], -j[0][1])) / detJ;
    float2 v1 = (v + float2(-j[1][0], j[0][0])) / detJ;
    float2 v2 = v / detJ;
    
    float2 e0 = v1 - v0, e1 = v2 - v1, e2 = v0 - v2;
    float area = e0.x*e1.y - e0.y*e1.x;
    float2 pq0 = v0 - e0*clamp( dot(v0,e0)/dot(e0,e0), 0.0, 1.0 );
    float2 pq1 = v1 - e1*clamp( dot(v1,e1)/dot(e1,e1), 0.0, 1.0 );
    float2 pq2 = v2 - e2*clamp( dot(v2,e2)/dot(e2,e2), 0.0, 1.0 );
    
    float2 dist = min( min( float2( dot( pq0, pq0 ), (v0.x*e0.y-v0.y*e0.x) ),
                           float2( dot( pq1, pq1 ), (v1.x*e1.y-v1.y*e1.x) )),
                      float2( dot( pq2, pq2 ), (v2.x*e2.y-v2.y*e2.x) ));
    
    float pd = -40 - sqrt(dist.x)*sign(dist.y * -area);
    
    return select(vert.color, mix(vert.color, float4(0, 0, 0, 1), saturate(pd)), meshState.showTriangles);
    
//    float u = vert.u, v = vert.v, w = 1.0 - u - v;
//    float eu = sign(u) * u * u / (dfdx(u) * dfdx(u) + dfdy(u) * dfdy(u));
//    float ev = sign(v) * v * v / (dfdx(v) * dfdx(v) + dfdy(v) * dfdy(v));
//    float ew = sign(w) * w * w / (dfdx(w) * dfdx(w) + dfdy(w) * dfdy(w));
//    float m = min(eu, min(ev, ew));
//    return vert.color * saturate(1 - float(meshState.showTriangles) * sign(m) * sqrt(abs(m)));
    
//    float e0 = max(saturate(vert.d0), saturate(vert.d1));
//    float e1 = max(saturate(vert.d2), saturate(vert.d3));
//    float edges = max(saturate(vert.d4), saturate(vert.d5));
//
//    return select(vert.color, mix(vert.color, float4(0, 0, 0, 1), max(max(e0, e1), edges)), meshState.showTriangles);
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
    device Triangle& tri = tris[iid];
    int p = tri.p;
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
	Bounds bounds = paths[p].bounds;
	float w = (bounds.ux - bounds.lx) / kShortMax, h = (bounds.uy - bounds.ly) / kShortMax;
    float a = w * CTM.a, b = w * CTM.b, c = h * CTM.c, d = h * CTM.d, tx = bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx, ty = bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty;
    float2 p0 = { tri.x0 * a + tri.y0 * c + tx, tri.x0 * b + tri.y0 * d + ty };
    float2 p1 = { tri.x1 * a + tri.y1 * c + tx, tri.x1 * b + tri.y1 * d + ty };
    float2 p2 = { tri.x2 * a + tri.y2 * c + tx, tri.x2 * b + tri.y2 * d + ty };
    
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
    
    RGBA8 color = paths[p].color;
    
    float z = (p * 2 + 1) / float(meshState.colorsSize * 2 + 2);
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
