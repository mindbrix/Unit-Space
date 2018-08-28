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
    void write(AffineTransform CTM, float _ux, uint vid, device float *points, constant State &state, constant MeshState& meshState) {
        float dx0, dy0, dx1, dy1, dx2, dy2, dx3, dy3, dx4, dy4, lx, ly, ux, uy, dx, dy;
        
        dx0 = points[0] * CTM.a + points[1] * CTM.c + CTM.tx, dy0 = points[0] * CTM.b + points[1] * CTM.d + CTM.ty;
        dx1 = points[2] * CTM.a + points[3] * CTM.c + CTM.tx, dy1 = points[2] * CTM.b + points[3] * CTM.d + CTM.ty;
        dx2 = points[4] * CTM.a + points[5] * CTM.c + CTM.tx, dy2 = points[4] * CTM.b + points[5] * CTM.d + CTM.ty;
        dx3 = points[6] * CTM.a + points[7] * CTM.c + CTM.tx, dy3 = points[6] * CTM.b + points[7] * CTM.d + CTM.ty;
        dx4 = points[8] * CTM.a + points[9] * CTM.c + CTM.tx, dy4 = points[8] * CTM.b + points[9] * CTM.d + CTM.ty;
        
        lx = min(min(min(dx0, dx1), dx2), min(dx3, dx4));
        ly = min(min(min(dy0, dy1), dy2), min(dy3, dy4));
        ux = max(max(max(dx0, dx1), dx2), max(dx3, dx4));
        uy = max(max(max(dy0, dy1), dy2), max(dy3, dy4));
        ux = max(ux, _ux);
        
        dx = select(floor(lx), ceil(ux), vid & 1), dy = select(floor(ly), ceil(uy), vid & 2);
        
        dx = clamp(dx, meshState.clipBounds.lx, meshState.clipBounds.ux);
        dy = clamp(dy, meshState.clipBounds.ly, meshState.clipBounds.uy);
        
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

vertex AccumulateVertex accumulate_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                             device MeshPath *paths [[buffer(9)]], device Accumulation *accumulations [[buffer(10)]],
                                             uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    device Accumulation& acc = accumulations[iid];
    
    AffineTransform CTM = AffineTransformConcat(paths[acc.p].ctm, state.CTM);
    AffineTransform unit = AffineTransformMakeUnit(paths[acc.p].bounds, CTM);
    Bounds bounds = Bounds::transformUnit(unit);
    
    AccumulateVertex vert;
    vert.write(CTM, bounds.ux, vid, & acc.x0, state, meshState);
    return vert;
}

struct AccumulateFragment {
    float4 winding [[ color(1) ]];
};

fragment AccumulateFragment accumulate_fragment_main(AccumulateVertex vert [[stage_in]],
													 constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    AccumulateFragment frag;
	
	if (meshState.winding != 0)
		frag.winding = meshState.winding;
	else {
        
        frag.winding =
        edgeWinding(vert.x0, vert.y0, vert.x1, vert.y1)
        + edgeWinding(vert.x1, vert.y1, vert.x2, vert.y2)
        + edgeWinding(vert.x2, vert.y2, vert.x3, vert.y3)
        + edgeWinding(vert.x3, vert.y3, vert.x4, vert.y4);
        /*
        frag.winding = quadraticWinding(vert.x0, vert.y0, vert.x1, vert.y1, vert.x2, vert.y2) + quadraticWinding(vert.x2, vert.y2, vert.x3, vert.y3, vert.x4, vert.y4);
        */
	}
    return frag;
}

#pragma mark - Interior

struct InteriorVertex
{
    float4 position [[position]];
    float u, v;
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
    vert.color = float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0);
    return vert;
}

fragment float4 interior_fragment_main(InteriorVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    float w = 1.0 - vert.u - vert.v;
    float eu = 1.0 - vert.u * rsqrt(dfdx(vert.u) * dfdx(vert.u) + dfdy(vert.u) * dfdy(vert.u));
    float ev = 1.0 - vert.v * rsqrt(dfdx(vert.v) * dfdx(vert.v) + dfdy(vert.v) * dfdy(vert.v));
    float ew = 1.0 - w * rsqrt(dfdx(w) * dfdx(w) + dfdy(w) * dfdy(w));
    return select(vert.color * max(eu, max(ev, ew)), vert.color, meshState.showInterior);
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
    float winding = edgeWinding(vert.p0.x, vert.p0.y, vert.p1.x, vert.p1.y)
	+ edgeWinding(vert.p1.x, vert.p1.y, vert.p2.x, vert.p2.y)
	+ edgeWinding(vert.p2.x, vert.p2.y, vert.p0.x, vert.p0.y);
    return vert.color * abs(winding);
}
