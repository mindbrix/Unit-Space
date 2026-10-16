//
//  Shared.metal
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <simd/simd.h>

#define PI 3.1415926536
#define TWO_PI 6.2831853072

#pragma clang diagnostic ignored "-Wcomma"

struct RGBA { float r, g, b, a; };

struct RGBA8 {
    static RGBA8 Red() { return RGBA8(255, 0, 0, 255); }
    static RGBA8 Green() { return RGBA8(0, 255, 0, 255); }
    static RGBA8 Blue() { return RGBA8(0, 0, 255, 255); }
    static RGBA8 Black() { return RGBA8(0, 0, 0, 255); }
    
    RGBA8() {}
    RGBA8(RGBA rgba) : r(rgba.r * 255), g(rgba.g * 255), b(rgba.b * 255), a(rgba.a * 255) {}
    RGBA8(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : r(r), g(g), b(b), a(a) {}
    RGBA8(int r, int g, int b, int a) : r(r), g(g), b(b), a(a) {}
    RGBA8(unsigned int r, unsigned int g, unsigned int b, unsigned int a) : r(r), g(g), b(b), a(a) {}
    
    unsigned char r, g, b, a;
};

struct AffineTransform { float a, b, c, d, tx, ty; };

struct Bounds {
    Bounds() {}
    Bounds(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy) {}
    bool contains(float px, float py) {
        return px > lx && py > ly && px < ux && py < uy;
    }
    bool contains(Bounds other) const {
        return lx <= other.lx && ux >= other.ux && ly <= other.ly && uy >= other.uy;
    }
    Bounds intersected(Bounds other) {
        return {
            lx < other.lx ? other.lx : lx > other.ux ? other.ux : lx,
            ly < other.ly ? other.ly : ly > other.uy ? other.uy : ly,
            ux < other.lx ? other.lx : ux > other.ux ? other.ux : ux,
            uy < other.ly ? other.ly : uy > other.uy ? other.uy : uy,
        };
    }
    inline bool intersects(Bounds other) const {
        return ux > other.lx && other.ux > lx && uy > other.ly && other.uy > ly;
    }
	inline AffineTransform makeUnit() {
		return { ux - lx, 0, 0, uy - ly, lx, ly };
	}
    inline float scaleToFit(Bounds other) {
        float ow = other.ux - other.lx, oh = other.uy - other.ly, omin = ow < oh ? ow : oh;
        float w = ux - lx, h = uy - ly, max = w > h ? w : h;
        return omin / max;
    }
    Bounds united(Bounds other) {
        return { lx < other.lx ? lx : other.lx, ly < other.ly ? ly : other.ly, ux > other.ux ? ux : other.ux, uy > other.uy ? uy : other.uy };
    }
    void unite(Bounds other) {
        lx = lx < other.lx ? lx : other.lx, ly = ly < other.ly ? ly : other.ly;
        ux = ux > other.ux ? ux : other.ux, uy = uy > other.uy ? uy : other.uy;
    }
    float lx, ly, ux, uy;
};

#define kShortMax 				32767.0
#define kUnsignedShortMax 		65535.0
#define kTriangleDilationMax	8.0
// Bevelled corners dilate further: their bevel ends coverage at the vertex, so a sharp tip's long miter only costs
// fragments, while a shorter one would clip its edges' anti-aliasing near the tip.
#define kTriangleDilationMaxBevel	64.0
#define kTriangleFilterWidth    0.5

struct Triangle {
    enum Flags : unsigned int {
        kFlagsEdge0 = 1U << 31,
        kFlagsEdge1 = 1U << 30,
        kFlagsEdge2 = 1U << 29,
        kFlagsConvex0 = 1U << 28,
        kFlagsConvex1 = 1U << 27,
        kFlagsConvex2 = 1U << 26,
        kFlagsMask = ~(kFlagsEdge0 | kFlagsEdge1 | kFlagsEdge2 | kFlagsConvex0 | kFlagsConvex1 | kFlagsConvex2),
    };
    // A hull triangle is a quadratic's p0 -> c -> p2, and is the only triangle that evaluates that curve.
    // Concave curves bow into the fill, so their chord is the outer edge and c is interior.
    // kControlN: vertex N is a concave curve's control point. Its miter insets the interior pass and sets t, as every
    // vertex's did before curves, but it has no corner, so the edge pass ignores it.
    enum Curves : unsigned int { kHull = 1, kConcave = 2, kControl0 = 4, kControl1 = 8, kControl2 = 16 };
    
    Triangle(int p, bool ef0, bool ef1, bool ef2,
             bool convex0, bool convex1, bool convex2,
			 float x0, float y0, float x1, float y1, float x2, float y2,
			 float ix0, float iy0, float ix1, float iy1, float ix2, float iy2)
    : p((unsigned int)p | (ef0 ? kFlagsEdge0 : 0) | (ef1 ? kFlagsEdge1 : 0) | (ef2 ? kFlagsEdge2 : 0) | (convex0 ? kFlagsConvex0 : 0) | (convex1 ? kFlagsConvex1 : 0) | (convex2 ? kFlagsConvex2 : 0)),
    x0(x0 < 0 ? 0 : x0 > kUnsignedShortMax ? kUnsignedShortMax : x0), y0(y0 < 0 ? 0 : y0 > kUnsignedShortMax ? kUnsignedShortMax : y0),
    x1(x1 < 0 ? 0 : x1 > kUnsignedShortMax ? kUnsignedShortMax : x1), y1(y1 < 0 ? 0 : y1 > kUnsignedShortMax ? kUnsignedShortMax : y1),
    x2(x2 < 0 ? 0 : x2 > kUnsignedShortMax ? kUnsignedShortMax : x2), y2(y2 < 0 ? 0 : y2 > kUnsignedShortMax ? kUnsignedShortMax : y2),
    ix0(ix0), iy0(iy0), ix1(ix1), iy1(iy1), ix2(ix2), iy2(iy2), curves(0), tx0(0), ty0(0), tx2(0), ty2(0)
    {}
    unsigned int p;
    unsigned short x0, y0, x1, y1, x2, y2;
    float ix0, iy0, ix1, iy1, ix2, iy2;     // miters; zero for interior vertices, which have no corner
    unsigned int curves;
    // Hull triangles: quantised points the neighbouring segments' tangents head toward at v0 and v2. Joins use the
    // neighbours' tangents rather than their chords, which differ for curves and would bump the join.
    // Polygon triangles with a control vertex: its curve's quantised end points, p0 and p2.
    unsigned short tx0, ty0, tx2, ty2;
};

// A shared interior mesh vertex: quantised position within its path's bounds plus its miter vector.
struct MeshVertex {
    MeshVertex() {}
    MeshVertex(unsigned int p, unsigned short x, unsigned short y, float ix, float iy) : p(p), x(x), y(y), ix(ix), iy(iy), t(0) {}
    unsigned int p;
    unsigned short x, y;
    float ix, iy;
    float t;        // its triangle's solveTriangle() threshold, so vertices are only shared within a threshold
};

struct Vector {
    Vector() {}
    Vector(float x, float y) : x(x), y(y) {}
    float x, y;
};

struct _Size { float w, h; };

struct MeshPath {
    MeshPath() {}
    MeshPath(RGBA8 color, AffineTransform ctm, Bounds bounds) : color(color), ctm(ctm), bounds(bounds) {}
    
    RGBA8               color;
    AffineTransform     ctm;
    Bounds              bounds;
};

struct MeshState {
    bool                showEdgeTriangles;
    bool                showInterior;
    bool                showPolygons;       // debug: outline the interior polygons
    bool                showTriangles;      // debug: outline every interior triangle
    bool                useStrokeColor;     // debug: edges in the state's stroke colour
	int                 colorsSize;
    int                 baseTriangle;       // debug: an interior call's first triangle, as primitive_id restarts per draw
};

struct State {
    RGBA                strokeColor;
    AffineTransform     CTM;
    _Size               drawableSize;
    float               width;              // the debug outline width, in pixels
};

enum CallType : unsigned int {
    kCallTypeMeshInteriorIndexed,
    kCallTypeMeshTriangles,
};

struct Call {
    Call() {}
    Call(CallType type, int instanceCount, int baseInstance, State state, MeshState meshState)
	: type(type), instanceCount(instanceCount), baseInstance(baseInstance), state(state), meshState(meshState), mesh(0), firstPath(0) {}
    
    CallType    type;
    int         instanceCount, baseInstance;
    int         mesh;           // which of the layer's meshes it draws
    int         firstPath;      // edge calls: the scene index of their first path, which orders them
	State       state;
    MeshState   meshState;
};

inline float AffineTransformArea(const AffineTransform t) {
    return t.a * t.d - t.b * t.c;
}

inline AffineTransform AffineTransformConcat(const AffineTransform t0, const AffineTransform t1) {
    return {
        t0.a * t1.a + t0.b * t1.c,              t0.a * t1.b + t0.b * t1.d,
        t0.c * t1.a + t0.d * t1.c,              t0.c * t1.b + t0.d * t1.d,
        t0.tx * t1.a + t0.ty * t1.c + t1.tx,    t0.tx * t1.b + t0.ty * t1.d + t1.ty
    };
}

inline AffineTransform AffineTransformInvert(const AffineTransform t) {
    float determinant = t.a * t.d - t.b * t.c;
    if (determinant == 0)
        return t;
    
    float recip = 1.0 / determinant;
    return {
        t.d * recip,                            -t.b * recip,
        -t.c * recip,                           t.a * recip,
        (t.c * t.ty - t.d * t.tx) * recip,      -(t.a * t.ty - t.b * t.tx) * recip
    };
}

