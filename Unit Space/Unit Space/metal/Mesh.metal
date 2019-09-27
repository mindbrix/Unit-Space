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
    vert.write(CTM, bounds.ux, vid, & quadratics[idx], state, meshState.clipBounds, 2, -0.5);
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
    float rscale = min(1.0, rsqrt(max(1e-12, abs(CTM.a * CTM.d - CTM.b * CTM.c))));
    
    const float f = kTriangleFilterWidth;
    device float *ix = & tri.ix0 + vid * 2;
    float2 iv = { (ix[0] * CTM.a + ix[1] * CTM.c) * rscale * f, (ix[0] * CTM.b + ix[1] * CTM.d) * rscale * f };
    
    Bounds bounds = paths[p].bounds;
	float w = (bounds.ux - bounds.lx) / kUnsignedShortMax, h = (bounds.uy - bounds.ly) / kUnsignedShortMax;
    float visible = float(1.0 / rscale > t / (0.5 / f));
    device unsigned short *pt = & tri.x0 + vid * 2;
    float2 devicePoint = {
        visible * (iv.x + pt[0] * w * CTM.a + pt[1] * h * CTM.c + bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx),
        visible * (iv.y + pt[0] * w * CTM.b + pt[1] * h * CTM.d + bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty)
    };
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
        float a = dfdx(vert.u), b = dfdy(vert.u), c = dfdx(vert.v), d = dfdy(vert.v);
        float u, v, ux, a1, a0;
        u = vert.u - 0.5 * (a + b), v = vert.v - 0.5 * (c + d);
        ux = u + 0.5 * v, a1 = u - ux * ux;
        
        u = vert.u + 0.5 * (a + b), v = vert.v + 0.5 * (c + d);
        ux = u + 0.5 * v, a0 = u - ux * ux;
        
        return vert.color * saturate(-a0 / (a1 - a0));
        
        /*
        float ux = vert.u + 0.5 * vert.v, imp = vert.u - ux * ux;
        return vert.color * saturate(imp);
        
        
        float invdet = 1.0 / (a * d - b * c);
        a *= invdet, b *= invdet, c *= invdet, d *= invdet;
        float x2 = b * vert.v - d * vert.u, y2 = vert.u * c - vert.v * a;
        float x0 = x2 + d, y0 = y2 - c;
        float x1 = x2 - b, y1 = y2 + a;
        const float dy1 = -0.5, dy0 = 1.0;
        x0 -= 0.5, y0 += dy1, x1 -= 0.5, y1 += dy1, x2 -= 0.5, y2 += dy1;
        a = x0 * y2 - y0 * x2, b = x1 * y0 - y1 * x0, d = x2 * y1 - y2 * x1;
        float a1 = 4.0 * b * d - a * a;
        x0 += 1.0, y0 += dy0, x1 += 1.0, y1 += dy0, x2 += 1.0, y2 += dy0;
        a = x0 * y2 - y0 * x2, b = x1 * y0 - y1 * x0, d = x2 * y1 - y2 * x1;
        float a0 = 4.0 * b * d - a * a;
        return vert.color * saturate(-a0 / (a1 - a0));
        */
//        float w = max(1.0, state.width);
//        float d = simpleBarycentricDistance(vert.u, vert.v, vert.ef0, vert.ef1, vert.ef2);
//        return vert.color * saturate(w - d);
	} else
		return vert.color;
}

#pragma mark - Triangle

struct TriangleVertex
{
    float4 position [[position]];
    float u, v;
    bool convex0, convex1, convex2;
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
    
    device float *iv = & tri.ix0;
    float2 iv0 = { (iv[0] * CTM.a + iv[1] * CTM.c) * rscale, (iv[0] * CTM.b + iv[1] * CTM.d) * rscale };
    float2 iv1 = { (iv[2] * CTM.a + iv[3] * CTM.c) * rscale, (iv[2] * CTM.b + iv[3] * CTM.d) * rscale };
    float2 iv2 = { (iv[4] * CTM.a + iv[5] * CTM.c) * rscale, (iv[4] * CTM.b + iv[5] * CTM.d) * rscale };
    
    const float f = kTriangleFilterWidth;
    float s = 1;//min(1.0, (0.5 / rscale) / (tri.t / (0.5 / f)));
    float s0 = min(1.0, kTriangleDilationMax * rsqrt(iv0.x * iv0.x + iv0.y * iv0.y));
    float s1 = min(1.0, kTriangleDilationMax * rsqrt(iv1.x * iv1.x + iv1.y * iv1.y));
    float s2 = min(1.0, kTriangleDilationMax * rsqrt(iv2.x * iv2.x + iv2.y * iv2.y));
    iv0 *= s0 * s, iv1 *= s1 * s, iv2 *= s2 * s;
    
    x0 -= iv0.x * f, y0 -= iv0.y * f;
    x1 -= iv1.x * f, y1 -= iv1.y * f;
    x2 -= iv2.x * f, y2 -= iv2.y * f;
    
    float visible = (x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1) > 0;
    float dx = visible * select(select(x2, x1, vid == 1), x0, vid == 0);
    float dy = visible * select(select(y2, y1, vid == 1), y0, vid == 0);
    
    RGBA8 color = paths[p].color;
    float z = (p * 2 + 1) / float(meshState.colorsSize * 2 + 2);
    TriangleVertex vert;
    vert.position = float4((float2(dx / state.drawableSize.w, dy / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    
    float rl, cosine, sine, ix, iy, vx, vy, nx, ny;
	
	vx = dx - x0, vy = dy - y0;
	ix = iv0.x, iy = iv0.y, rl = rsqrt(ix * ix + iy * iy), cosine = min(1.0, rl * s0 * s), sine = sqrt(1 - cosine * cosine);
	ix *= rl, iy *= rl;
	nx = ix * cosine + iy * sine, ny = ix * -sine + iy * cosine;
	vert.d0 = nx * vx + ny * vy - f;
    nx = ix * cosine + iy * -sine, ny = ix * sine + iy * cosine;
    vert.d1 = nx * vx + ny * vy - f;
	
	vx = dx - x1, vy = dy - y1;
	ix = iv1.x, iy = iv1.y, rl = rsqrt(ix * ix + iy * iy), cosine = min(1.0, rl * s1 * s), sine = sqrt(1 - cosine * cosine);
	ix *= rl, iy *= rl;
	nx = ix * cosine + iy * sine, ny = ix * -sine + iy * cosine;
    vert.d2 = nx * vx + ny * vy - f;
    nx = ix * cosine + iy * -sine, ny = ix * sine + iy * cosine;
    vert.d3 = nx * vx + ny * vy - f;
	
	vx = dx - x2, vy = dy - y2;
	ix = iv2.x, iy = iv2.y, rl = rsqrt(ix * ix + iy * iy), cosine = min(1.0, rl * s2 * s), sine = sqrt(1 - cosine * cosine);
	ix *= rl, iy *= rl;
	nx = ix * cosine + iy * sine, ny = ix * -sine + iy * cosine;
    vert.d4 = nx * vx + ny * vy - f;
    nx = ix * cosine + iy * -sine, ny = ix * sine + iy * cosine;
    vert.d5 = nx * vx + ny * vy - f;
	
    vert.convex0 = tri.p & Triangle::kFlagsConvex0;
    vert.convex1 = tri.p & Triangle::kFlagsConvex1;
    vert.convex2 = tri.p & Triangle::kFlagsConvex2;
    
    vert.color = select(float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), meshState.useStrokeColor);
    return vert;
}

#pragma mark Fragment

fragment float4 triangle_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    const float f = kTriangleFilterWidth;
    float convex0 = vert.convex0 ? 1 : -1, convex1 = vert.convex1 ? 1 : -1, convex2 = vert.convex2 ? 1 : -1;
    float d01 = convex0 * min(convex0 * vert.d0, convex0 * vert.d1);
    float d23 = convex1 * min(convex1 * vert.d2, convex1 * vert.d3);
    float d45 = convex2 * min(convex2 * vert.d4, convex2 * vert.d5);
    float winding = (clamp(min(d01, min(d23, d45)), -f, f) + f) * 0.5 / f;
    return vert.color * winding;
}
