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
#define kMeshTriangleSize  7
#define kFatScanlineHeight  4.0f

#pragma clang diagnostic ignored "-Wcomma"

struct Circle { float x, y, r; };

struct RGBA { float r, g, b, a;
    static RGBA hsv2rgb(float H, float S, float V) {
        RGBA RGB;
        float P, Q, T;
        float fract;
        
        H *= 360;
        (H == 360.)?(H = 0.):(H /= 60.);
        fract = H - int(H);
        
        P = V*(1. - S);
        Q = V*(1. - S*fract);
        T = V*(1. - S*(1. - fract));
        
        if (0. <= H && H < 1.)
            RGB = (RGBA){.r = V, .g = T, .b = P};
        else if (1. <= H && H < 2.)
            RGB = (RGBA){.r = Q, .g = V, .b = P};
        else if (2. <= H && H < 3.)
            RGB = (RGBA){.r = P, .g = V, .b = T};
        else if (3. <= H && H < 4.)
            RGB = (RGBA){.r = P, .g = Q, .b = V};
        else if (4. <= H && H < 5.)
            RGB = (RGBA){.r = T, .g = P, .b = V};
        else if (5. <= H && H < 6.)
            RGB = (RGBA){.r = V, .g = P, .b = Q};
        else
            RGB = (RGBA){.r = 0., .g = 0., .b = 0.};
        
        RGB.a = 1;
        return RGB;
    }
};

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
    Bounds integral() {
        return { float(int(lx)), float(int(ly)), float(int(ux + 0.99999999)), float(int(uy + 0.99999999)) };
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
    static inline Bounds transformUnit(const AffineTransform t) {
        float wa = t.a < 0 ? 1 : 0, wb = t.b < 0 ? 1 : 0, wc = t.c < 0 ? 1 : 0, wd = t.d < 0 ? 1 : 0;
        return {
            t.tx + wa * t.a + wc * t.c,
            t.ty + wb * t.b + wd * t.d,
            t.tx + (1 - wa) * t.a + (1 - wc) * t.c,
            t.ty + (1 - wb) * t.b + (1 - wd) * t.d };
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

struct Vector {
    Vector() {}
    Vector(float x, float y) : x(x), y(y) {}
    float x, y;
};

struct _Size { float w, h; };

#define kFlagsSize 1022

struct Flags {
    int                 w, h;
    unsigned int        flags[kFlagsSize];
};

struct MeshPath {
    MeshPath() {}
    MeshPath(RGBA8 color, AffineTransform ctm, Bounds bounds) : color(color), ctm(ctm), bounds(bounds) {}
    
    RGBA8               color;
    AffineTransform     ctm;
    Bounds              bounds;
};

struct MeshState {
    bool                disableInteriorVector;
    bool                showEdges;
    bool                showInterior;
    bool                showColors;
    bool                showTriangles;
    bool                showQuadratics;
    bool                showAccumulations;
    bool                showAccumulationsSOA;
    bool                showAccumulationsAllocations;
    bool                showVisibles;
    bool                useStrokeColor;
    bool                useQuadraticWinding;
	float				winding;
	int                 colorsSize;
    int                 reverseCount;
    unsigned int        quadraticStride;
    unsigned int        subdivisionCount;
	Bounds				clipBounds;
	float               cellSize;
};

struct State {
    RGBA                fillColor;
    RGBA                strokeColor;
    AffineTransform     CTM;
    _Size               drawableSize;
    float               dashLength;
    float               flatness;
    float               width;
    float               cap;
    float               scaleFactor;
    bool                evenOdd;
    bool                roundCap;
};


enum CallType : unsigned int {
    kCallTypeShapesSOA = 0,
    kCallTypeFlags,
    kCallTypeMeshInterior,
    kCallTypeMeshInteriorBlend,
    kCallTypeMeshTriangles,
    kCallTypeResolveSOA,
    kCallTypeResolveQuadSOA,
    kCallTypeQuadraticLines,
    kCallTypeQuadraticAccumulate,
};

enum ShapeType : short {
    kShapeTypeEllipse,
    kShapeTypeLozenge,
    kShapeTypeRect,
    kShapeTypeRoundedRect,
    kShapeTypeQuadratic,
    kShapeTypeTriangle,
};

struct Call {
    Call() {}
    Call(CallType type, int instanceCount, int baseInstance, State state, MeshState meshState) : type(type), instanceCount(instanceCount), baseInstance(baseInstance), state(state), meshState(meshState) {}
    
    CallType    type;
    int         instanceCount, baseInstance;
    State       state;
    MeshState   meshState;
    union {
        Flags       flags;
    };
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

inline AffineTransform AffineTransformMakeUnit(const Bounds bounds, const AffineTransform t) {
    float w = bounds.ux - bounds.lx, h = bounds.uy - bounds.ly;
    return {
        t.a * w,                                    t.b * w,
        t.c * h,                                    t.d * h,
        bounds.lx * t.a + bounds.ly * t.c + t.tx,   bounds.lx * t.b + bounds.ly * t.d + t.ty
    };
}
