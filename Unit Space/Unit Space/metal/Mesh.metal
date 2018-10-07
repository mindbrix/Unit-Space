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
    void write(AffineTransform CTM, float ux, uint vid, device float *points, constant State &state, Bounds clipBounds, uint kStride, float offset) {
        float dx0, dy0, dx1, dy1, dx2, dy2, dx3, dy3, dx4, dy4, lx, ly, uy, dx, dy, vx, vy;
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
        
        dx = select(floor(lx), ceil(ux), vid & 1), dy = select(floor(ly), ceil(uy), vid & 2);
        dx = clamp(dx, clipBounds.lx, clipBounds.ux);
        dy = clamp(dy, clipBounds.ly, clipBounds.uy);
        
        position = float4((float2(dx / state.drawableSize.w, dy / state.drawableSize.h) * 2.0 - 1.0), 0.0, 1.0);
        
        dx += offset, dy += offset;
        x0 = dx0 - dx, y0 = dy0 - dy;
        x1 = dx1 - dx, y1 = dy1 - dy;
        x2 = dx2 - dx, y2 = dy2 - dy;
        x3 = dx3 - dx, y3 = dy3 - dy;
        x4 = dx4 - dx, y4 = dy4 - dy;
        
//        vx = x2 - x0, vy = y2 - y0;
//        cosine0 = abs(vx) * rsqrt(vx * vx + vy * vy);
//        vx = x4 - x2, vy = y4 - y2;
//        cosine1 = abs(vx) * rsqrt(vx * vx + vy * vy);
        
    }
    float4 position [[position]];
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4;
//    float cosine0, cosine1;
//    float a0_0, a1_0, a0_1, a1_1;
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
    vert.write(CTM, bounds.ux, vid, & quadratics[idx], state, meshState.clipBounds, 2, select(-0.5,0.0, meshState.useQuadraticWinding));
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
        frag.winding = edgeCoverage(vert.x0, vert.y0, vert.x2, vert.y2) + edgeCoverage(vert.x2, vert.y2, vert.x4, vert.y4);
        //frag.winding = quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2) + quadraticWinding(vert.x2, vert.y2, vert.x3, vert.y3, vert.x4, vert.y4);
    else
        frag.winding = edgeWinding(vert.x0, vert.y0, vert.x2, vert.y2) + edgeWinding(vert.x2, vert.y2, vert.x4, vert.y4);
    return frag;
}

#pragma mark - Interior

struct InteriorVertex
{
    float4 position [[position]];
    float u, v;
    bool ef0, ef1, ef2;
    float4 color;
};

vertex InteriorVertex interior_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                           device Triangle *tris [[buffer(8)]], device MeshPath *paths [[buffer(9)]],
                                           uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    uint idx = select(iid, meshState.reverseCount - 1 - iid + biid, meshState.reverseCount);
    device Triangle& tri = tris[idx];
    uint p = tri.p & Triangle::kFlagsMask;
    float t = tri.t;
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
    float rscale = rsqrt(abs(CTM.a * CTM.d - CTM.b * CTM.c));
    
    Bounds bounds = paths[p].bounds;
	float w = (bounds.ux - bounds.lx) / kUnsignedShortMax, h = (bounds.uy - bounds.ly) / kUnsignedShortMax;
    device unsigned short *pt = & tri.x0 + vid * 2;
    float2 devicePoint = {
        pt[0] * w * CTM.a + pt[1] * h * CTM.c + bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx,
        pt[0] * w * CTM.b + pt[1] * h * CTM.d + bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty
    };
    float ms = rscale / kShortMax * kInteriorMax;
    device short *ix = & tri.ix0 + vid * 2;
    float2 iv = { ix[0] * CTM.a * ms + ix[1] * CTM.c * ms, ix[0] * CTM.b * ms + ix[1] * CTM.d * ms };
    devicePoint = select(devicePoint + iv * 0.5, 0, 1.0 / rscale < t);
    RGBA8 color = paths[p].color;
    
    InteriorVertex vert;
    float z = (p * 2 + 2) / float(meshState.colorsSize * 2 + 2);
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    vert.u = float(vid == 2);
    vert.v = float(vid == 0);
    vert.ef0 = meshState.showTriangles | bool(tri.p & Triangle::kFlagsEdge0);
    vert.ef1 = meshState.showTriangles | bool(tri.p & Triangle::kFlagsEdge1);
    vert.ef2 = meshState.showTriangles | bool(tri.p & Triangle::kFlagsEdge2);
    vert.color = float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0);
    return vert;
}

fragment float4 interior_fragment_main(InteriorVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
	if (meshState.showTriangles || meshState.showPolygons) {
		float w = max(1.0, state.width);
		float d = simpleBarycentricDistance(vert.u, vert.v, vert.ef0, vert.ef1, vert.ef2);
		return vert.color * saturate(w - d);
	} else
		return vert.color;
}

#pragma mark - Triangle

struct TriangleVertex
{
    float4 position [[position]];
    float u, v;
    float d0, d1, d2, d3, d4, d5;
    float4 color;
};

#pragma mark Vertex

vertex TriangleVertex mesh_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                       device Triangle *tris [[buffer(8)]],
                                       device MeshPath *paths [[buffer(9)]],
                                       uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    device Triangle& tri = tris[iid];
    uint p = tri.p & Triangle::kFlagsMask;
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
    float rscale = rsqrt(abs(CTM.a * CTM.d - CTM.b * CTM.c));
	Bounds bounds = paths[p].bounds;
	float width = (bounds.ux - bounds.lx) / kUnsignedShortMax, height = (bounds.uy - bounds.ly) / kUnsignedShortMax;
    float a = width * CTM.a, b = width * CTM.b, c = height * CTM.c, d = height * CTM.d;
    float tx = bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx, ty = bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty;
    float x0 = tri.x0 * a + tri.y0 * c + tx, y0 = tri.x0 * b + tri.y0 * d + ty;
    float x1 = tri.x1 * a + tri.y1 * c + tx, y1 = tri.x1 * b + tri.y1 * d + ty;
    float x2 = tri.x2 * a + tri.y2 * c + tx, y2 = tri.x2 * b + tri.y2 * d + ty;
    
    
    bool df0 =  !bool(tri.p & Triangle::kFlagsEdge0);
    bool df1 =  !bool(tri.p & Triangle::kFlagsEdge1);
    bool df2 = !bool(tri.p & Triangle::kFlagsEdge2);
    
    const float dilation = 0.5, overlap = 0.425, hmin = 0.1;
    float d0 = float(df0) * -0.495, d1 = float(df1) * -0.495, d2 = float(df2) * -0.495;
    //float d0 = float(df0) * overlap, d1 = float(df1) * overlap, d2 = float(df2) * overlap;
    
    float ax = x1 - x0, ay = y1 - y0, bx = x2 - x1, by = y2 - y1, cx = x0 - x2, cy = y0 - y2;
    float area = ax * by - ay * bx;
    float u0 = -(dilation + d1) / max(hmin, area * rsqrt(bx * bx + by * by));
    float v0 = -(dilation + d2) / max(hmin, area * rsqrt(cx * cx + cy * cy));
    float w0 = -(dilation + d0) / max(hmin, area * rsqrt(ax * ax + ay * ay));
    float u = select(u0, 1 - v0 - w0, vid == 0);
    float v = select(v0, 1 - u0 - w0, vid == 1);
    float dx = float(area > 1e-2) * (x0 * u + x1 * v + x2 * (1 - u - v));
    float dy = float(area > 1e-2) * (y0 * u + y1 * v + y2 * (1 - u - v));
    
    RGBA8 color = paths[p].color;
    float z = (p * 2 + 1) / float(meshState.colorsSize * 2 + 2);
    TriangleVertex vert;
    vert.position = float4((float2(dx / state.drawableSize.w, dy / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    
    u0 *= 1 - d1 / (dilation + d1);
    v0 *= 1 - d2 / (dilation + d2);
    w0 *= 1 - d0 / (dilation + d0);
    vert.u = select(u0, 1 - v0 - w0, vid == 0);
    vert.v = select(v0, 1 - u0 - w0, vid == 1);
    
    float ms = rscale / kShortMax * kInteriorMax;
    device short *iv = & tri.ix0;
    float2 iv0 = { iv[0] * CTM.a * ms + iv[1] * CTM.c * ms, iv[0] * CTM.b * ms + iv[1] * CTM.d * ms };
    float2 iv1 = { iv[2] * CTM.a * ms + iv[3] * CTM.c * ms, iv[2] * CTM.b * ms + iv[3] * CTM.d * ms };
    float2 iv2 = { iv[4] * CTM.a * ms + iv[5] * CTM.c * ms, iv[4] * CTM.b * ms + iv[5] * CTM.d * ms };
    
    float rl, cosine, sine, ix, iy, vx, vy, nx, ny;
    ix = iv0.x, iy = iv0.y, rl = rsqrt(ix * ix + iy * iy), cosine = min(1.0, rl), sine = sqrt(1 - cosine * cosine);
    vx = dx - x0, vy = dy - y0;
    nx = ix * cosine + iy * sine, ny = ix * -sine + iy * cosine;
    vert.d0 = rl * (nx * vx + ny * vy);
    nx = ix * cosine + iy * -sine, ny = ix * sine + iy * cosine;
    vert.d1 = rl * (nx * vx + ny * vy);
    
    ix = iv1.x, iy = iv1.y, rl = rsqrt(ix * ix + iy * iy), cosine = min(1.0, rl), sine = sqrt(1 - cosine * cosine);
    vx = dx - x1, vy = dy - y1;
    nx = ix * cosine + iy * sine, ny = ix * -sine + iy * cosine;
    vert.d2 = rl * (nx * vx + ny * vy);
    nx = ix * cosine + iy * -sine, ny = ix * sine + iy * cosine;
    vert.d3 = rl * (nx * vx + ny * vy);
    
    ix = iv2.x, iy = iv2.y, rl = rsqrt(ix * ix + iy * iy), cosine = min(1.0, rl), sine = sqrt(1 - cosine * cosine);
    vx = dx - x2, vy = dy - y2;
    nx = ix * cosine + iy * sine, ny = ix * -sine + iy * cosine;
    vert.d4 = rl * (nx * vx + ny * vy);
    nx = ix * cosine + iy * -sine, ny = ix * sine + iy * cosine;
    vert.d5 = rl * (nx * vx + ny * vy);
    
    vert.color = select(float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), meshState.useStrokeColor);
    return vert;
}

#pragma mark Fragment

fragment float4 triangle_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    float d01 = min(vert.d0, vert.d1), d23 = min(vert.d2, vert.d3), d45 = min(vert.d4, vert.d5);
    float winding = saturate(min(d01, min(d23, d45)) + 0.5);
    
//    float a = dfdx(vert.u), b = dfdy(vert.u), c = dfdx(vert.v), d = dfdy(vert.v), invdet = 1.0 / (a * d - b * c);
//    a *= invdet, b *= invdet, c *= invdet, d *= invdet;
//    float x2 = 0.5 + b * vert.v - d * vert.u, y2 = 0.5 + vert.u * c - vert.v * a;
//    float x0 = x2 + d, y0 = y2 - c;
//    float x1 = x2 - b, y1 = y2 + a;
//    float winding = edgeWinding(x0, y0, x1, y1) + edgeWinding(x1, y1, x2, y2) + edgeWinding(x2, y2, x0, y0);

    return select(vert.color, float4(0, 0, 0, 1), meshState.showTriangles) * winding;
}
