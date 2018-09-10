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

constexpr sampler s = sampler(coord::normalized, address::clamp_to_zero, mag_filter::nearest, min_filter::nearest, mip_filter::linear);

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

float textureGood(texture2d<float> tex, sampler sam, float2 uv, float2 res)
{
    float2 st = uv*res - 0.5;
    
    float2 iuv = floor( st );
    float2 fuv = fract( st );
    
   // fuv.x = smoothstep(0, 1, fuv.x);
   // fuv.y = smoothstep(0, 1, fuv.y);
    float a = tex.sample( sam, (iuv+float2(0.5,0.5))/res ).x;
    float b = tex.sample( sam, (iuv+float2(1.5,0.5))/res ).x;
    float c = tex.sample( sam, (iuv+float2(0.5,1.5))/res ).x;
    float d = tex.sample( sam, (iuv+float2(1.5,1.5))/res ).x;
    
    return mix( mix( a, b, fuv.x), mix( c, d, fuv.x), fuv.y );
}

fragment float4 resolveSOA_fragment_main(ResolveVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                                 texture2d<float> accumulation [[texture(1)]] )
{
    //float winding = textureGood(accumulation, s, vert.uv, float2(state.drawableSize.w, state.drawableSize.h));
    float alpha = select(windingToAlpha(accumulation.sample(s, vert.uv).x, state.evenOdd), 1.0, vert.uv.x < 0);
    return vert.color * alpha;
}


#pragma mark - Shape

struct ShapeVertex
{
    float4 position [[position]];
    float4 color;
    
    float2 unitPoint;
    float scale;
    
    int shapeType;
};

vertex ShapeVertex shapesSOA_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                          constant short *types [[buffer(10)]],
                                          constant float *as [[buffer(11)]], constant float *bs [[buffer(12)]], constant float *cs [[buffer(13)]], constant float *ds [[buffer(14)]],
                                          constant float *txs [[buffer(15)]], constant float *tys [[buffer(16)]],
                                          constant RGBA8 *colors [[buffer(17)]],
                                          uint vid [[vertex_id]], uint iid [[instance_id]])
{
    Circle circle = { txs[iid] + as[iid] * 0.5, tys[iid] + as[iid] * 0.5, as[iid] * 0.5 };
    RGBA8 color = colors[iid];
    constant AffineTransform& CTM = state.CTM;
    float scale = sqrt(abs(CTM.a * CTM.d - CTM.b * CTM.c));
    float dilation = 0.7071 / scale;
    float2 up = { circle.x + (circle.r + dilation) * select(-1.0, 1.0, vid & 1), circle.y + (circle.r + dilation) * select(-1.0, 1.0, vid & 2) };
    float dx = up.x * CTM.a + up.y * CTM.c + CTM.tx;
    float dy = up.x * CTM.b + up.y * CTM.d + CTM.ty;
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    ShapeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 1.0, 1.0);
    vert.unitPoint = { select(-1.0, 1.0, vid & 1), select(-1.0, 1.0, vid & 2) };
    vert.scale = scale * circle.r;
    vert.color = { sRGB(float(color.r) / 255.0), sRGB(float(color.g) / 255.0), sRGB(float(color.b) / 255.0), sRGB(float(color.a) / 255.0) };
    vert.shapeType = types[iid];
    return vert;
}

fragment float4 shapesSOA_fragment_main(ShapeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    const float kDilation = -0.2071067812, kCornerRadius = 0.5, kFilter = 1;
    switch (vert.shapeType) {
        case kShapeTypeEllipse:
            return vert.color * ellipseDistance(vert.unitPoint, kDilation, kFilter);
        case kShapeTypeRect:
            return vert.color * rectDistance(vert.unitPoint, kDilation, kFilter);
        case kShapeTypeRoundedRect:
            return vert.color * roundedRectDistance(vert.unitPoint, max(2.0 / vert.scale, kCornerRadius), kDilation, kFilter);
        case kShapeTypeLozenge:
            return vert.color * lozengeDistance(vert.unitPoint, kDilation);
        default:
            return vert.color * triangleDistance(vert.unitPoint, kDilation, kFilter);
    }
}


#pragma mark - Line

struct LineVertex
{
    void write(float2 p0, float2 p1, float scale, uint vid, constant State &state, RGBA8 rgba) {
        float2 v01 = p1 - p0, nv = normalize(v01), lnv = { -nv.y, nv.x };
        
        float uw, dilation, dw, dh;
        uw = select(state.width, 1.0 / scale, state.width == 0);
        dilation = (abs(nv.x) + abs(nv.y)) * 0.5;
        dh = dw = uw * scale * 0.5;
        float2 devicePoint = select(p0, p1, vid & 2) + (dh + dilation) * select(-nv, nv, vid & 2) + (dw + dilation) * select(-lnv, lnv, vid & 1);
        float2 ndcPoint = { (devicePoint.x / state.drawableSize.w * 2.0) - 1.0, (devicePoint.y / state.drawableSize.h * 2.0) - 1.0 };
        
        position = float4(ndcPoint, 1.0, 1.0);
        u = select(-1, 1, vid & 1), v = select(-1, 1, vid & 2);
        color = { float(rgba.r) / 255.0, float(rgba.g) / 255.0, float(rgba.b) / 255.0, float(rgba.a) / 255.0 };
        devicePoint -= 0.5;
        x0 = p0.x - devicePoint.x, y0 = p0.y - devicePoint.y, x1 = p1.x - devicePoint.x, y1 = p1.y - devicePoint.y;
    }
    float4 position [[position]];
    float u, v, t, padding;
	float x0, y0, x1, y1;
    float4 color;
};

vertex LineVertex quadratic_line_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                      device MeshPath *paths [[buffer(9)]],
                                      device float *quadratics [[buffer(10)]], device int *ps [[buffer(11)]], device float *ts [[buffer(12)]],
                                      uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint idx = iid / meshState.subdivisionCount * meshState.quadraticStride;
    int p = ps[idx / 8];
    float x0 = quadratics[idx], y0 = quadratics[idx + 1], x1 = quadratics[idx + meshState.quadraticStride], y1 = quadratics[idx + meshState.quadraticStride + 1];
    bool visible = quadratics[idx + 2] != FLT_MAX;
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
    float scale = sqrt(abs(CTM.a * CTM.d - CTM.b * CTM.c));
    
    float2 p0 = { x0 * CTM.a + y0 * CTM.c + CTM.tx, x0 * CTM.b + y0 * CTM.d + CTM.ty };
    float2 p1 = { x1 * CTM.a + y1 * CTM.c + CTM.tx, x1 * CTM.b + y1 * CTM.d + CTM.ty };
    
    if (meshState.subdivisionCount > 1) {
        float cpx = quadratics[idx + 2], cpy = quadratics[idx + 3];
        float2 cp = { cpx * CTM.a + cpy * CTM.c + CTM.tx, cpx * CTM.b + cpy * CTM.d + CTM.ty };
        float dt = 1.0 / float(meshState.subdivisionCount), t0 = float(iid % meshState.subdivisionCount) * dt, t1 = t0 + dt;
        float s0 = 1.0 - t0, s1 = 1.0 - t1;
        float2 tp0 = s0 * s0 * p0 + 2.0 * s0 * t0 * cp + t0 * t0 * p1;
        float2 tp1 = s1 * s1 * p0 + 2.0 * s1 * t1 * cp + t1 * t1 * p1;
        p0 = tp0, p1 = tp1;
    }
    
    LineVertex vert;
    vert.write(p0, p1, scale, vid, state, paths[p].color);
    vert.position *= float(visible);
    vert.t = ts[idx / 4 + int(vid >> 1)] / state.dashLength;
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
    return vert.color * select(saturate(dx) * saturate(dy / 1.74), rounded, state.roundCap) * fract(vert.t);
}
