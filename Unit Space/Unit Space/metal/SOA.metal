//
//  SOA.metal
//  Nu
//
//  Created by Nigel Barber on 19/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include "Shared.metal.hpp"
#include <metal_stdlib>
#include "Headers.metal"

using namespace metal;

constexpr sampler s = sampler(coord::normalized, address::clamp_to_zero, mag_filter::linear, min_filter::linear, mip_filter::linear);

#pragma mark - Resolve

struct ResolveVertex
{
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex ResolveVertex resolveSOA_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                                  device MeshPath *paths [[buffer(9)]],
                                                  constant short *lxs [[buffer(10)]], constant short *uxs [[buffer(11)]], constant short *lys [[buffer(12)]],
                                                  constant short *txs [[buffer(13)]], constant short *tys [[buffer(14)]],
                                                  constant int *ps [[buffer(15)]],
                                                  uint vid [[vertex_id]], uint iid [[instance_id]], uint biid [[base_instance]])
{
    float dx = select(float(lxs[iid]), float(uxs[iid]), vid & 1), dy = select(float(lys[iid]), float(lys[iid]) + kFatScanlineHeight, vid & 2);
    if (meshState.showAccumulationsAllocations)
        dx += float(txs[iid]), dy += float(tys[iid]);
                
    ResolveVertex vert;
    vert.position = { dx / state.drawableSize.w * 2.0 - 1.0, dy / state.drawableSize.h * 2.0 - 1.0, 0.0, 1.0 };
    vert.uv = -1;// { (dx + float(txs[iid])) / state.drawableSize.w, (dy + float(tys[iid])) / state.drawableSize.h };
    vert.color = {
        float(paths[ps[iid]].color.r) / 255.0,
        float(paths[ps[iid]].color.g) / 255.0,
        float(paths[ps[iid]].color.b) / 255.0,
        float(paths[ps[iid]].color.a) / 255.0
    };
    return vert;
}

vertex ResolveVertex resolveQuadSOA_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                                 uint vid [[vertex_id]], uint iid [[instance_id]])
{
    ResolveVertex vert;
    vert.position = { select(-1.0, 1.0, vid & 1), select(-1.0, 1.0, vid & 2), 1.0, 1.0 };
    vert.uv = { select(0.0, 1.0, vid & 1) / meshState.cellSize, 1.0 - select(0.0, 1.0, vid & 2) / meshState.cellSize };
    vert.color = { state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a };
    return vert;
}

fragment float4 resolveSOA_fragment_main(ResolveVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                                 texture2d<half> accumulation [[texture(1)]] )
{
    float alpha = select(windingToAlpha(accumulation.sample(s, vert.uv).x, state.evenOdd), 1.0, vert.uv.x < 0);
    return vert.color * alpha;
}


#pragma mark - Circle

struct CircleVertex
{
    float4 position [[position]];
    float4 color;
    
    float2 unitPoint;
    float unitWidth;
    
    float alpha;
};
vertex CircleVertex circleSOA_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                       constant float *xs [[buffer(10)]], constant float *ys [[buffer(11)]],
                                       constant float *rs [[buffer(12)]], constant RGBA8 *colors [[buffer(13)]],
                                       uint vid [[vertex_id]], uint iid [[instance_id]])
{
    Circle circle = { xs[iid], ys[iid], rs[iid] };
    RGBA8 color = colors[iid];
    float scale = sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b);
    float dilation = 0.7071 / scale;
    float r = circle.r + dilation;
    
    float2 up = { circle.x + r * select(-1.0, 1.0, vid & 1), circle.y + r * select(-1.0, 1.0, vid & 2) };
    
    AffineTransform unit = { circle.r * 2.0, 0.0, 0.0, circle.r * 2.0, circle.x - circle.r, circle.y - circle.r };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, state.CTM));
    
    float dx = up.x * state.CTM.a + up.y * state.CTM.c + state.CTM.tx;
    float dy = up.x * state.CTM.b + up.y * state.CTM.d + state.CTM.ty;
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    CircleVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 1.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    vert.unitWidth = min(0.5, 0.5 * sqrt(abs(el.a * el.d - el.b * el.c)));
    vert.color = { sRGB(float(color.r) / 255.0), sRGB(float(color.g) / 255.0), sRGB(float(color.b) / 255.0), sRGB(float(color.a) / 255.0) };
    
    return vert;
}

fragment float4 circle_fragment_main(CircleVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    return vert.color * saturate(ellipseDistance(vert.unitPoint, vert.unitWidth));
}


#pragma mark - Line

struct LineVertex
{
    float4 position [[position]];
    float u, v;
	float x0, y0, x1, y1;
    float4 color;
};

vertex LineVertex lineSOA_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                      device MeshPath *paths [[buffer(9)]],
                                      constant float *xs [[buffer(10)]], constant float *ys [[buffer(11)]],
                                      constant int *ps [[buffer(12)]],
                                      constant float *as [[buffer(13)]], constant float *bs [[buffer(14)]],
                                      uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint idx = iid / meshState.subdivisionCount;
    int p = max(0, ps[idx]);
    float x0 = xs[idx], y0 = ys[idx], x1 = xs[idx + 1], y1 = ys[idx + 1];
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
    float scale = sqrt(abs(CTM.a * CTM.d - CTM.b * CTM.c));
    
    float2 p0 = { x0 * CTM.a + y0 * CTM.c + CTM.tx, x0 * CTM.b + y0 * CTM.d + CTM.ty };
    float2 p1 = { x1 * CTM.a + y1 * CTM.c + CTM.tx, x1 * CTM.b + y1 * CTM.d + CTM.ty };
    
    if (meshState.subdivisionCount > 1) {
        float a = as[idx], b = bs[idx];
        float dx02 = p1.x - p0.x, dy02 = p1.y - p0.y;
        float2 cp = { dx02 * a + dy02 * -b + p0.x, dx02 * b + dy02 * a + p0.y };
        float dt = 1.0 / float(meshState.subdivisionCount), t0 = float(iid % meshState.subdivisionCount) * dt, t1 = t0 + dt;
       // t0 = adjustedT(t0), t1 = adjustedT(t1);
        float s0 = 1.0 - t0, s1 = 1.0 - t1;
        float2 tp0 = s0 * s0 * p0 + 2.0 * s0 * t0 * cp + t0 * t0 * p1;
        float2 tp1 = s1 * s1 * p0 + 2.0 * s1 * t1 * cp + t1 * t1 * p1;
        p0 = tp0, p1 = tp1;
    }
    
    float2 v = p1 - p0, nv = normalize(v), lnv = { -nv.y, nv.x };
    
    float uw, dilation, dw, dh;
    uw = select(state.width, 1.0 / scale, state.width == 0);
    dilation = (abs(nv.x) + abs(nv.y)) * 0.5;
    dh = dw = uw * scale * 0.5;
    dw = select(dw, select(0.0, 1.0, (vid & 1) == (v.y < 0)), meshState.showLineWinding);
    dh = select(dh, select(0.0, 1.0, (vid >> 1) == (v.x > 0)), meshState.showLineWinding);
    float2 devicePoint = select(p0, p1, vid & 2) + (dh + dilation) * select(-nv, nv, vid & 2) + (dw + dilation) * select(-lnv, lnv, vid & 1);
    float2 ndcPoint = { (devicePoint.x / state.drawableSize.w * 2.0) - 1.0, (devicePoint.y / state.drawableSize.h * 2.0) - 1.0 };
    
    LineVertex vert;
    vert.position = float4(ndcPoint, 1.0, 1.0) * float(ps[idx] != -1);
    vert.u = select(-1, 1, vid & 1), vert.v = select(-1, 1, vid & 2);
    vert.color = { float(paths[p].color.r) / 255.0, float(paths[p].color.g) / 255.0, float(paths[p].color.b) / 255.0, float(paths[p].color.a) / 255.0 };
    devicePoint -= 0.5;
	vert.x0 = p0.x - devicePoint.x, vert.y0 = p0.y - devicePoint.y, vert.x1 = p1.x - devicePoint.x, vert.y1 = p1.y - devicePoint.y;
    return vert;
}

fragment float4 line_fragment_main(LineVertex vert [[stage_in]],
								   constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    float w = rsqrt(dfdx(vert.u) * dfdx(vert.u) + dfdy(vert.u) * dfdy(vert.u));
    float h = rsqrt(dfdx(vert.v) * dfdx(vert.v) + dfdy(vert.v) * dfdy(vert.v));
    float dx = (1.0 - abs(vert.u)) * w;
    float dy = (1.0 - abs(vert.v)) * h;
    float x = w - dx, y = max(0.0, w - dy);
    float rounded = saturate(w - sqrt(x * x + y * y));
    return vert.color * select(saturate(dx) * saturate(dy / 1.74), rounded, state.roundCap);
}

struct LineFragment
{
    float4 color [[ color(0) ]];
    float winding [[ color(1) ]];
};

fragment LineFragment line_winding_fragment_main(LineVertex vert [[stage_in]],
                                                 constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    LineFragment f;
    f.winding = edgeWinding(vert.x0, vert.y0, vert.x1, vert.y1) - edgeWinding(vert.x0 + 1, vert.y0, vert.x1 + 1, vert.y1);
    return f;
}
