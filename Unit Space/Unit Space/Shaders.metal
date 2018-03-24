//
//  Shaders.metal
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include "Shared.metal.hpp"
#include <metal_stdlib>
#include "Headers.metal"

using namespace metal;


#pragma mark - Unit

struct UnitVertex
{
    float4 position [[position]];
    
    float2 devicePoint;
    
    float ellipseScale, width;
    float2 unitPoint0, unitPoint1, unitPoint2, unitPoint3;
    int count;
    uint32_t flags;
};

vertex UnitVertex unit_vertex_main(constant State &state [[buffer(0)]], constant Units &units [[buffer(1)]], uint vid [[vertex_id]])
{
    float2 unitPoint = float2(vid & 1, vid & 2);
    float dx = unitPoint.x * state.drawableSize.w, dy = unitPoint.y * state.drawableSize.h;
    
    
    UnitVertex vert;
    vert.position = float4(unitPoint * 2.0 - 1.0, 0.0, 1.0);
    
    AffineTransform q;
    q = AffineTransformInvert(units.ctms[0]);
    vert.unitPoint0 = { dx * q.a + dy * q.c + q.tx, dx * q.b + dy * q.d + q.ty };
    q = AffineTransformInvert(units.ctms[1]);
    vert.unitPoint1 = { dx * q.a + dy * q.c + q.tx, dx * q.b + dy * q.d + q.ty };
    q = AffineTransformInvert(units.ctms[2]);
    vert.unitPoint2 = { dx * q.a + dy * q.c + q.tx, dx * q.b + dy * q.d + q.ty };
    q = AffineTransformInvert(units.ctms[3]);
    vert.unitPoint3 = { dx * q.a + dy * q.c + q.tx, dx * q.b + dy * q.d + q.ty };
    vert.count = units.count;
    
//    constant Unit& unit0 = units.units[0];
    AffineTransform el0 = AffineTransformInvert(AffineTransformConcat(units.ctms[0], state.CTM));
    vert.unitPoint0 = { dx * el0.a + dy * el0.c + el0.tx, dx * el0.b + dy * el0.d + el0.ty };
    /*
    constant Unit& unit1 = units.units[1];
    AffineTransform el1 = AffineTransformInvert(AffineTransformConcat(unit1.unitCTM, state.CTM));
    vert.unitPoint1 = { dx * el1.a + dy * el1.c + el1.tx, dx * el1.b + dy * el1.d + el1.ty };
    */
    float scale = select(sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b), 1.0, state.width == 0.0);
    vert.width = select(state.width, 1.0, state.width == 0.0) * sqrt(el0.a * el0.a + el0.b * el0.b) * scale;
    
    vert.flags = 0;
    
    return vert;
}

fragment float4 unit_fragment_main(UnitVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = ellipseStrokeDistance(vert.unitPoint0, vert.width);
//    float alpha = saturate(unitQuadraticWinding(vert.unitPoint0) + 0.5);
    
    /*
    float winding = 0.0;
    winding += unitQuadraticWinding(vert.unitPoint0);
    winding += unitQuadraticWinding(vert.unitPoint1);
    winding += unitQuadraticWinding(vert.unitPoint2);
    winding += unitQuadraticWinding(vert.unitPoint3);
    
    float alpha = abs(winding);
    */
//    alpha = saturate(alpha + 0.5);
    /*
//
    float alpha0 = ellipseDistance(unitRepeat(vert.unitPoint0, false, true));
    float alpha1 = ellipseDistance(unitRepeat(vert.unitPoint1, true, false));
//    float alpha1 = ellipseDistance(unitRepeat(vert.unitPoint0 * 0.25 + 0.25, true, true));
    float alpha2 = 1.0;// ellipseDistance(unitRepeat(vert.unitPoint0 * 0.025 + 0.0125, true, true));
    float alpha = min(alpha2, min(alpha0, alpha1)) + 0.5;
     */
    float4 color = float4(state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a), black = float4(0.0, 0.0, 0.0, 1.0), white = float4(1.0, 1.0, 1.0, 1.0);
    
    return mix(white, color, saturate(alpha));
}


#pragma mark - Arc

struct StrokeVertex
{
    float4 position [[position]];
    
    float2 unitPoint;
    float unitWidth;
    
    float alpha;
};

vertex StrokeVertex arc_vertex_main(constant State &state [[buffer(0)]], constant Arcs &arcs [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    uint count = ceil(1.0 / state.flatness);
    uint index = iid / count;
    constant Arc& arc = arcs.arcs[index];
    
    float dt = (arc.t1 - arc.t0) * state.flatness;
    float t0 = arc.t0 + dt * float(iid % count), t1 = t0 + dt;
    float t = select(t0, t1, vid & 2);
    
    float scale = sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b);
    
    float dilation = 1.0 / scale;
    float maxedWidth = max(state.width, dilation);
    float r0 = max(0.0, arc.r - maxedWidth - dilation);
    float theta = PI * state.flatness * (arc.t1 - arc.t0);
    float r1 = (arc.r + maxedWidth + dilation) / cos(theta);
    
    float r = select(r0, r1, vid & 1);
    float x = arc.x + r * cos(t * TWO_PI);
    float y = arc.y + r * sin(t * TWO_PI);
    
    float dx = x * state.CTM.a + y * state.CTM.c + state.CTM.tx;
    float dy = x * state.CTM.b + y * state.CTM.d + state.CTM.ty;
    
    AffineTransform unit = { arc.r * 2.0, 0, 0, arc.r * 2.0, arc.x - arc.r, arc.y - arc.r };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, state.CTM));
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    vert.unitWidth = maxedWidth * sqrt(el.a * el.a + el.b * el.b) * scale;
    vert.alpha = min(1.0, state.width / dilation);
    return vert;
}

fragment float4 arc_fragment_main(StrokeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = vert.alpha * saturate(ellipseStrokeDistance(vert.unitPoint, vert.unitWidth));
//    float alpha = vert.alpha * saturate(cirleStrokeDistance(vert.unitPoint, vert.unitWidth) + 0.5);
    return float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a) * alpha;
//    return mix(float4(0, 1, 0, 1), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), alpha);
}


#pragma mark - Circle

vertex StrokeVertex circle_vertex_main(constant State &state [[buffer(0)]], constant Circles &circles [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    constant Circle& circle = circles.circles[iid];
    
    float scale = sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b);
    float dilation = 1.0 / scale;
    float r = circle.r + dilation;
    
    float2 up = { circle.x + r * select(-1.0, 1.0, vid & 1), circle.y + r * select(-1.0, 1.0, vid & 2) };

    AffineTransform unit = { circle.r * 2.0, 0.0, 0.0, circle.r * 2.0, circle.x - circle.r, circle.y - circle.r };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, state.CTM));
    
    float dx = up.x * state.CTM.a + up.y * state.CTM.c + state.CTM.tx;
    float dy = up.x * state.CTM.b + up.y * state.CTM.d + state.CTM.ty;
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    return vert;
}

fragment float4 circle_fragment_main(StrokeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = ellipseDistance(vert.unitPoint);
    return float4(state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a) * alpha;
}


#pragma mark - Fractal

vertex StrokeVertex fractal_vertex_main(constant State &state [[buffer(0)]], constant Fractals &fractals [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    AffineTransform ctm = state.CTM;
    
    uint i0 = iid % uint(fractals.counts[0]);
    uint i1 = iid / uint(fractals.counts[0]);
    uint i2 = iid / uint(fractals.counts[0] * fractals.counts[1]);
    uint i3 = iid / uint(fractals.counts[0] * fractals.counts[1] * fractals.counts[2]);
    
    for (uint i = 0; i < i3; i++)
        ctm = AffineTransformConcat(fractals.ctms[3], ctm);
    
    for (uint i = 0; i < i2; i++)
        ctm = AffineTransformConcat(fractals.ctms[2], ctm);
    
    for (uint i = 0; i < i1; i++)
        ctm = AffineTransformConcat(fractals.ctms[1], ctm);
    
    for (uint i = 0; i < i0; i++)
        ctm = AffineTransformConcat(fractals.ctms[0], ctm);
    
    ctm = AffineTransformConcat(fractals.unitCTM, ctm);
    AffineTransform el = AffineTransformInvert(ctm);
    float scale = sqrt(abs(ctm.a * ctm.d - ctm.b * ctm.c));
    float dilation = 1.0 / scale;
    
    float2 up = { float(vid & 1), float(vid >> 1) };
    up += (up - 0.5) * dilation;
    float dx = up.x * ctm.a + up.y * ctm.c + ctm.tx;
    float dy = up.x * ctm.b + up.y * ctm.d + ctm.ty;
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
    return vert;
}

fragment float4 fractal_fragment_main(StrokeVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant Fractals &fractals [[buffer(1)]])
{
    float2 inverse = vert.unitPoint;
//    float2 inverse = unitComplexMultiply(vert.unitPoint);
//    float2 inverse = unitInverse(vert.unitPoint);
    float alpha = saturate(fractals.ellipse ? ellipseDistance(inverse) : rectDistance(inverse));
    return float4(state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a) * alpha;
}


#pragma mark - Line

vertex StrokeVertex line_vertex_main(constant State &state [[buffer(0)]], constant Lines &lines [[buffer(1)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    constant Line& line = lines.lines[iid];
    
    float2 p0 = { line.x0, line.y0 }, p1 = { line.x1, line.y1 };
    float2 v = p1 - p0, nv = normalize(v);
    float2 lnv = { -nv.y, nv.x };
    float w2 = state.width * 0.5;
    float scale = sqrt(state.CTM.a * state.CTM.a + state.CTM.b * state.CTM.b);
    
    float dilation = 1.0 / scale;
    
    float2 up = select(p0, p1, vid & 2) + (select(-nv, nv, vid & 2) + select(-lnv, lnv, vid & 1)) * (w2 + dilation);
    
    float2 nvw = nv * state.width, ab = { nvw.y, -nvw.x };
    float2 cd = v + nvw, txty = p0 - (nvw + ab) * 0.5;
    
    AffineTransform unit = { ab.x, ab.y, cd.x, cd.y, txty.x, txty.y };
    AffineTransform el = AffineTransformInvert(AffineTransformConcat(unit, state.CTM));
    
    float dx = up.x * state.CTM.a + up.y * state.CTM.c + state.CTM.tx;
    float dy = up.x * state.CTM.b + up.y * state.CTM.d + state.CTM.ty;
    
    float2 unitPoint = { dx / state.drawableSize.w, dy / state.drawableSize.h };
    
    StrokeVertex vert;
    vert.position = float4((unitPoint * 2.0 - 1.0), 0.0, 1.0);
    vert.unitPoint = { dx * el.a + dy * el.c + el.tx, dx * el.b + dy * el.d + el.ty };
//    vert.unitWidth = maxedWidth * sqrt(el.a * el.a + el.b * el.b) * scale;
//    vert.alpha = min(1.0, state.width / dilation);
    return vert;
}

fragment float4 line_fragment_main(StrokeVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = rectDistance(vert.unitPoint);
//    return mix(float4(0, 1, 0, 1), float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a), alpha);
    return float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a) * alpha;
}


#pragma mark - Quadratics

vertex UnitVertex quadratic_vertex_main(constant State &state [[buffer(0)]], uint vid [[vertex_id]])
{
    float2 unitPoint = float2(vid & 1, vid & 2);
    
    UnitVertex vert;
    vert.position = float4(unitPoint * 2.0 - 1.0, 0.0, 1.0);
    vert.devicePoint = { unitPoint.x * state.drawableSize.w, unitPoint.y * state.drawableSize.h };
    vert.flags = 0;
    
    return vert;
}


fragment float4 quadratic_fragment_main(UnitVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant Quadratics &quadratics [[buffer(1)]])
{
//    return quadraticColor(vert.devicePoint, quadratics.ctms[0]);
    
    float winding = 0.0;

    for (int i = 0; i < quadratics.count; i++)
        winding += quadraticWinding(vert.devicePoint, quadratics.ctms[i]);

    float alpha = abs(winding);
    
//    float alpha = abs(quadraticWinding(vert.devicePoint, quadratics.ctms[0]));
    float4 color = float4(state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a), white = float4(1.0, 1.0, 1.0, 1.0);
//    return color * alpha;
    return mix(white, color, saturate(alpha));
}
