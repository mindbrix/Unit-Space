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

#pragma mark - Interior

struct IndexedInteriorVertex
{
    float4 position [[position]];
    float4 color [[flat]];
    float clip [[clip_distance]] [1];
};

struct IndexedInteriorFragment
{
    float4 position [[position]];
    float4 color [[flat]];
};

vertex IndexedInteriorVertex interior_indexed_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                                          const device MeshVertex *verts [[buffer(8)]], const device MeshPath *paths [[buffer(9)]],
                                                          uint vid [[vertex_id]])
{
    const device MeshVertex& v = verts[vid];
    uint p = v.p;
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
    float rscale = rsqrt(max(1e-12, abs(CTM.a * CTM.d - CTM.b * CTM.c)));
    
    const float f = kTriangleFilterWidth;
    float2 iv = { (v.ix * CTM.a + v.iy * CTM.c) * rscale * f, (v.ix * CTM.b + v.iy * CTM.d) * rscale * f };
    
    Bounds bounds = paths[p].bounds;
    float w = (bounds.ux - bounds.lx) / kUnsignedShortMax, h = (bounds.uy - bounds.ly) / kUnsignedShortMax;
    float2 devicePoint = {
        iv.x + v.x * w * CTM.a + v.y * h * CTM.c + bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx,
        iv.y + v.x * w * CTM.b + v.y * h * CTM.d + bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty
    };
    RGBA8 color = paths[p].color;
    
    IndexedInteriorVertex vert;
    // As the interior once did per triangle: one the inset would collapse at this scale is left to the edge pass. The
    // inset is f / scale along the miters, so that is when the scale falls below f times the threshold, not twice it:
    // a margin culls triangles that stay whole, which leaves the interior with gaps the edge pass must fill. Its
    // vertices all carry its threshold, so it is clipped away whole.
    vert.clip[0] = 1.0 / rscale > v.t * f ? 1.0 : -1e9;
    float z = (p * 2 + 2) / float(meshState.colorsSize * 2 + 2);
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), z, 1.0);
    vert.color = float4(float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0, float(color.a) / 255.0);
    return vert;
}

fragment float4 interior_indexed_fragment_main(IndexedInteriorFragment vert [[stage_in]])
{
    return vert.color;
}

// The debug outlines: every triangle's edges, or only the interior polygons'. The vertices carry no barycentrics or
// per-triangle flags, so they come from the rasteriser and a byte per triangle. Barycentric coordinates are not on
// every GPU, so this is a separate pipeline, only built where they are.
fragment float4 interior_indexed_debug_fragment_main(IndexedInteriorFragment vert [[stage_in]], float3 b [[barycentric_coord]],
                                                     uint pid [[primitive_id]],
                                                     constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                                     const device uchar *edges [[buffer(2)]])
{
    uchar e = meshState.showTriangles ? 7 : edges[meshState.baseTriangle + pid];
    float w = max(1.0, state.width);
    float d = simpleBarycentricDistance(b.z, b.x, e & 1, e & 2, e & 4);
    return vert.color * saturate(w - d);
}

#pragma mark - Triangle

// Kept small: every scalar here is written out per vertex, and output size measurably limits this pass.
// d0..d5 are two edge lines per corner. A hull triangle instead carries the line at p0 its curve meets (d0), the
// curve's Loop-Blinn (u, v) (d1, d2) and the line at p2 (d3).
struct TriangleVertex
{
    float4 position [[position]];
    float d0, d1, d2, d3, d4, d5;
    uint convex [[flat]];   // bits 2..0: Triangle::kFlagsConvex0..2; bits 7..3: Triangle::curves; bits 13..8: which of
                            // d0..d5 are a concave curve's chord, which the curve replaces.
    uint color [[flat]];    // RGBA8, r in the low byte, not premultiplied
};

// Loop-Blinn coordinates of p for the quadratic a -> c -> b: (0, 0) at a, (1/2, 0) at c and (1, 1) at b, so u^2 = v on the curve.
static inline float2 loopBlinn(float2 p, float2 a, float2 c, float2 b) {
    float2 e1 = c - a, e2 = b - a, w = p - a;
    float rdet = 1.0 / (e1.x * e2.y - e1.y * e2.x);
    float alpha = (e2.y * w.x - e2.x * w.y) * rdet, beta = (e1.x * w.y - e1.y * w.x) * rdet;
    return float2(0.5 * alpha + beta, beta);
}

// Inward distances from p to a corner's two edge lines through vertex v: the unit miter rotated either way by the half
// angle, whose cosine is 1 / |miter|. The first is the edge most perpendicular to t. Interior vertices have no miter
// and no corner, so their lines are always inside.
static inline float2 cornerDistances(float2 iv, float2 v, float2 t, float2 p) {
    float len2 = dot(iv, iv), rl = rsqrt(len2), cosine = min(1.0, rl), sine = sqrt(1 - cosine * cosine);
    float2 u = iv * rl, w = p - v;
    float2 na = float2(u.x * cosine + u.y * sine, u.x * -sine + u.y * cosine);
    float2 nb = float2(u.x * cosine + u.y * -sine, u.x * sine + u.y * cosine);
    bool first = abs(dot(na, t)) <= abs(dot(nb, t));
    return select(float2(dot(select(nb, na, first), w), dot(select(na, nb, first), w)), float2(1e6), len2 == 0);
}

// Whether a corner's lines (first, second) run along t, to within about a degree.
static inline bool2 alongEdge(float2 iv, float2 v, float2 t) {
    return abs(cornerDistances(iv, v, t, v + t)) < 0.02 * length(t);
}

#pragma mark Vertex

vertex TriangleVertex mesh_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]],
                                       const device Triangle *tris [[buffer(8)]],
                                       const device MeshPath *paths [[buffer(9)]],
                                       uint vertexId [[vertex_id]])
{
    // Drawn as one non-instanced stream of 3 vertices per triangle: one instance per triangle is much slower.
    uint iid = vertexId / 3, vid = vertexId - iid * 3;
    const device Triangle& tri = tris[iid];
    uint p = tri.p & Triangle::kFlagsMask, curves = tri.curves;
    AffineTransform CTM = AffineTransformConcat(paths[p].ctm, state.CTM);
    float rscale = rsqrt(abs(CTM.a * CTM.d - CTM.b * CTM.c));
    Bounds bounds = paths[p].bounds;
    float width = (bounds.ux - bounds.lx) / kUnsignedShortMax, height = (bounds.uy - bounds.ly) / kUnsignedShortMax;
    float a = width * CTM.a, b = width * CTM.b, c = height * CTM.c, d = height * CTM.d;
    float tx = bounds.lx * CTM.a + bounds.ly * CTM.c + CTM.tx, ty = bounds.lx * CTM.b + bounds.ly * CTM.d + CTM.ty;
    float2 v0 = { tri.x0 * a + tri.y0 * c + tx, tri.x0 * b + tri.y0 * d + ty };
    float2 v1 = { tri.x1 * a + tri.y1 * c + tx, tri.x1 * b + tri.y1 * d + ty };
    float2 v2 = { tri.x2 * a + tri.y2 * c + tx, tri.x2 * b + tri.y2 * d + ty };
    
    const device float *iv = & tri.ix0;
    float2 iv0 = { (iv[0] * CTM.a + iv[1] * CTM.c) * rscale, (iv[0] * CTM.b + iv[1] * CTM.d) * rscale };
    float2 iv1 = { (iv[2] * CTM.a + iv[3] * CTM.c) * rscale, (iv[2] * CTM.b + iv[3] * CTM.d) * rscale };
    float2 iv2 = { (iv[4] * CTM.a + iv[5] * CTM.c) * rscale, (iv[4] * CTM.b + iv[5] * CTM.d) * rscale };
    // Control points have no corner here: no dilation and no lines.
    iv0 = select(iv0, 0, bool(curves & Triangle::kControl0));
    iv1 = select(iv1, 0, bool(curves & Triangle::kControl1));
    iv2 = select(iv2, 0, bool(curves & Triangle::kControl2));
    
    // Bevels: in a polygon triangle with only convex corners, a corner that owns both its outline edges (an ear's tip, or
    // any corner of a triangular contour) needs only one of its lines, as the other edge is the previous corner's. Its
    // other slot gets a bevel through its vertex, perpendicular to its miter, which ends a sharp tip's coverage there.
    uint convex = Triangle::kFlagsConvex0 | Triangle::kFlagsConvex1 | Triangle::kFlagsConvex2;
    bool owned = !(curves & Triangle::kHull) && (tri.p & convex) == convex;
    bool e01 = tri.p & Triangle::kFlagsEdge0, e12 = tri.p & Triangle::kFlagsEdge1, e20 = tri.p & Triangle::kFlagsEdge2;
    bool bevel0 = owned && e20 && e01, bevel1 = owned && e01 && e12, bevel2 = owned && e12 && e20;
    
    const float f = kTriangleFilterWidth;
    float s0 = min(1.0, (bevel0 ? kTriangleDilationMaxBevel : kTriangleDilationMax) * rsqrt(dot(iv0, iv0)));
    float s1 = min(1.0, (bevel1 ? kTriangleDilationMaxBevel : kTriangleDilationMax) * rsqrt(dot(iv1, iv1)));
    float s2 = min(1.0, (bevel2 ? kTriangleDilationMaxBevel : kTriangleDilationMax) * rsqrt(dot(iv2, iv2)));
    float2 p0 = v0 - iv0 * (s0 * f), p1 = v1 - iv1 * (s1 * f), p2 = v2 - iv2 * (s2 * f);
    
    float visible = (p1.x - p0.x) * (p2.y - p1.y) - (p1.y - p0.y) * (p2.x - p1.x) > 0;
    float2 pos = visible * select(select(p2, p1, vid == 1), p0, vid == 0);
    
    RGBA8 color = paths[p].color;
    float z = (p * 2 + 1) / float(meshState.colorsSize * 2 + 2);
    TriangleVertex vert;
    vert.position = float4((pos / float2(state.drawableSize.w, state.drawableSize.h)) * 2.0 - 1.0, z, 1.0);
    
    uint lines = 0;     // polygon triangles: which corner lines (d0..d5) are a concave curve's chord
    // Branch rather than select: each path's hull triangles are contiguous, so the branch is mostly coherent.
    if (curves & Triangle::kHull) {
        // The curve meets the outer polygon along a hull edge if convex, or along the chord if concave.
        bool concave = curves & Triangle::kConcave;
        float2 uv = loopBlinn(pos, v0, v1, v2);
        // The neighbours' tangent lines at the joins. A convex hull runs p0 -> c -> p2, so its predecessor arrives
        // along v0 -> t0 with the fill on the right; a concave hull is reversed, so the sides swap.
        float2 t0 = float2(tri.tx0 * a + tri.ty0 * c + tx, tri.tx0 * b + tri.ty0 * d + ty) - v0;
        float2 t2 = float2(tri.tx2 * a + tri.ty2 * c + tx, tri.tx2 * b + tri.ty2 * d + ty) - v2;
        float2 e0 = t0 * rsqrt(dot(t0, t0)), e2 = t2 * rsqrt(dot(t2, t2));
        float2 n0 = select(float2(e0.y, -e0.x), float2(-e0.y, e0.x), concave), n2 = select(float2(-e2.y, e2.x), float2(e2.y, -e2.x), concave);
        vert.d0 = select(cornerDistances(iv0, v0, select(v1, v2, concave) - v0, pos).y, dot(n0, pos - v0), dot(t0, t0) > 0);
        vert.d1 = uv.x, vert.d2 = uv.y;
        vert.d3 = select(cornerDistances(iv2, v2, select(v1, v0, concave) - v2, pos).y, dot(n2, pos - v2), dot(t2, t2) > 0);
        // Which side of each join's bisector (the miter) we are on: positive is this curve's side, where the curve
        // alone is the boundary. Neighbours' lines only apply beyond it, where sharp corners let the hull overhang.
        float2 q0 = select(v1, v2, concave) - v0, q2 = select(v1, v0, concave) - v2;
        float2 b0 = float2(-iv0.y, iv0.x), b2 = float2(-iv2.y, iv2.x);
        vert.d4 = dot(select(-b0, b0, dot(b0, q0) >= 0), pos - v0);
        vert.d5 = dot(select(-b2, b2, dot(b2, q2) >= 0), pos - v2);
    } else {
        // A clamped corner's lines move in by what its dilation fell short of the miter, so they meet at the clamped
        // vertex as the dilated triangle does: a sharp tip's coverage ends with its geometry instead of being cut off.
        // A concave curve's control point has no corner, and the corners at its ends have its chord as a line, which the
        // hull shares and is dilated from. Inside the hull the chord is beyond the curve, so the polygon would paint past
        // it. So the triangle carries its first control point's curve, in that vertex's unused distances, and the fragment
        // replaces the chord lines at the curve's ends with the curve itself.
        bool c0 = curves & Triangle::kControl0, c1 = curves & Triangle::kControl1, c2 = curves & Triangle::kControl2;
        bool k0c = c0, k1c = c1 && !c0, k2c = c2 && !c0 && !c1, any = k0c || k1c || k2c;
        float2 vc = select(select(v2, v1, k1c), v0, k0c);
        float2 cp0 = float2(tri.tx0 * a + tri.ty0 * c + tx, tri.tx0 * b + tri.ty0 * d + ty), cp2 = float2(tri.tx2 * a + tri.ty2 * c + tx, tri.tx2 * b + tri.ty2 * d + ty);
        // Which corners are the curve's ends (their positions are quantised identically), and the chord from each.
        bool p00 = tri.x0 == tri.tx0 && tri.y0 == tri.ty0, p01 = tri.x1 == tri.tx0 && tri.y1 == tri.ty0, p02 = tri.x2 == tri.tx0 && tri.y2 == tri.ty0;
        bool p20 = tri.x0 == tri.tx2 && tri.y0 == tri.ty2, p21 = tri.x1 == tri.tx2 && tri.y1 == tri.ty2, p22 = tri.x2 == tri.tx2 && tri.y2 == tri.ty2;
        bool e0 = any && !k0c && (p00 || p20), e1 = any && !k1c && (p01 || p21), e2 = any && !k2c && (p02 || p22);
        // Each corner's first line is the one along t: the chord at the curve's ends.
        float2 t0 = select(v1 - v0, select(cp0, cp2, p00) - v0, e0), t1 = select(v0 - v1, select(cp0, cp2, p01) - v1, e1);
        float2 t2 = select(v1 - v2, select(cp0, cp2, p02) - v2, e2);
        float2 k0 = cornerDistances(iv0, v0, t0, pos) - f * (1 - s0);
        float2 k1 = cornerDistances(iv1, v1, t1, pos) - f * (1 - s1);
        float2 k2 = cornerDistances(iv2, v2, t2, pos) - f * (1 - s2);
        bool2 a0 = select(bool2(false), alongEdge(iv0, v0, t0), bool2(e0));
        bool2 a1 = select(bool2(false), alongEdge(iv1, v1, t1), bool2(e1));
        bool2 a2 = select(bool2(false), alongEdge(iv2, v2, t2), bool2(e2));
        lines = uint(a0.x) | uint(a0.y) << 1 | uint(a1.x) << 2 | uint(a1.y) << 3 | uint(a2.x) << 4 | uint(a2.y) << 5;
        if (any) {
            float2 uv = loopBlinn(pos, cp0, vc, cp2);
            k0 = select(k0, uv, k0c), k1 = select(k1, uv, k1c), k2 = select(k2, uv, k2c);
        }
        // A bevelled corner keeps the line along its edge to the next vertex, so a triangular contour has 3 edges and 3 bevels.
        // Its lines are its two triangle edges: the one toward the next vertex is v0's first, v1's second and v2's second.
        k0 = select(k0, float2(dot(iv0 * rsqrt(dot(iv0, iv0)), pos - v0), k0.x), bevel0);
        k1 = select(k1, float2(dot(iv1 * rsqrt(dot(iv1, iv1)), pos - v1), k1.y), bevel1);
        k2 = select(k2, float2(dot(iv2 * rsqrt(dot(iv2, iv2)), pos - v2), k2.y), bevel2);
        vert.d0 = k0.x, vert.d1 = k0.y, vert.d2 = k1.x, vert.d3 = k1.y, vert.d4 = k2.x, vert.d5 = k2.y;
    }
    vert.convex = ((tri.p >> 26) & 7) | (curves << 3) | (lines << 8);
    
    uint rgba8 = uint(color.r) | uint(color.g) << 8 | uint(color.b) << 16 | uint(color.a) << 24;
    vert.color = select(rgba8, pack_float_to_unorm4x8(float4(state.strokeColor.r, state.strokeColor.g, state.strokeColor.b, state.strokeColor.a)), meshState.useStrokeColor);
    return vert;
}

#pragma mark Fragment

// A corner joins its two edges: inside both if convex, either if not.
static inline float corner(float convex, float a, float b) {
    return convex * min(convex * a, convex * b);
}

// Signed pixel distance to u^2 = v, positive inside: a convex curve's inside is the parabola's, a concave curve's is outside it.
static inline float curveDistance(float u, float v, float2 du, float2 dv, bool concave) {
    float2 g = 2.0 * u * du - dv;
    float d = (u * u - v) * rsqrt(max(dot(g, g), 1e-24));
    return select(-d, d, concave);
}

fragment float4 triangle_fragment_main(TriangleVertex vert [[stage_in]], constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]])
{
    const float f = kTriangleFilterWidth;
    float d0 = vert.d0, d1 = vert.d1, d2 = vert.d2, d3 = vert.d3, d4 = vert.d4, d5 = vert.d5;
    uint curves = (vert.convex >> 3) & 31, lines = vert.convex >> 8;
    bool hull = curves & Triangle::kHull, c0 = curves & Triangle::kControl0, c1 = curves & Triangle::kControl1 && !c0;
    // The curve's Loop-Blinn (u, v): a hull's d1, d2, or a polygon triangle's in its first control vertex's distances.
    float su = select(select(select(d4, d2, c1), d0, c0), d1, hull);
    float sv = select(select(select(d5, d3, c1), d1, c0), d2, hull);
    // Derivatives are only defined in uniform control flow, so take them first. (u, v) are affine, so they are exact.
    float2 du = float2(dfdx(su), dfdy(su)), dv = float2(dfdx(sv), dfdy(sv));
    float convex0 = (vert.convex & 4) ? 1 : -1, convex1 = (vert.convex & 2) ? 1 : -1, convex2 = (vert.convex & 1) ? 1 : -1;
    bool concave = hull ? bool(curves & Triangle::kConcave) : true;
    float dq = curveDistance(su, sv, du, dv, concave);
    // A concave curve's inside, outside its parabola, spills across its chord (u = v) beyond its ends, so the chord bounds it.
    float2 dc = du - dv;
    dq = select(dq, min(dq, (su - sv) * rsqrt(max(dot(dc, dc), 1e-24))), concave);
    float m;
    if (hull) {
        // The curve, joined at p0 and p2 to the neighbouring outer edges. A convex curve's control point corner is the
        // curve itself; a concave curve's is interior.
        float m0 = select(corner(convex0, d0, dq), dq, d4 >= 0), m2 = select(corner(convex2, dq, d3), dq, d5 >= 0);
        m = min(min(m0, m2), select(dq, 1e6, concave));
    } else {
        // The curve replaces its chord's lines, and control points have no corner.
        bool k0 = curves & Triangle::kControl0, k1 = curves & Triangle::kControl1, k2 = curves & Triangle::kControl2;
        float l0 = select(select(d0, dq, bool(lines & 1)), 1e6, k0), l1 = select(select(d1, dq, bool(lines & 2)), 1e6, k0);
        float l2 = select(select(d2, dq, bool(lines & 4)), 1e6, k1), l3 = select(select(d3, dq, bool(lines & 8)), 1e6, k1);
        float l4 = select(select(d4, dq, bool(lines & 16)), 1e6, k2), l5 = select(select(d5, dq, bool(lines & 32)), 1e6, k2);
        m = min(corner(convex0, l0, l1), min(corner(convex1, l2, l3), corner(convex2, l4, l5)));
    }
    float winding = (clamp(m, -f, f) + f) * 0.5 / f;
    // Debug: the polygon view draws the edges as usual, continued inside as the interior outlines straight edges (which
    // it covers), so curves get the same line. The band the edge pass covers along curves' chords is further in.
    if (meshState.showPolygons && m >= f)
        winding = saturate(max(1.0, state.width) + f - m);
    float4 color = unpack_unorm4x8_to_float(vert.color);
    // The edge pass blends premultiplied colour (source factor one), so translucent paths must be premultiplied.
    color.rgb *= color.a;
    return color * winding;
}
