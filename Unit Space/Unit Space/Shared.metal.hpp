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
#define MIN(A,B)    ((A) < (B) ? (A) : (B))
#define MAX(A,B)    ((A) > (B) ? (A) : (B))

struct Arc { float x, y, r, t0, t1; };

struct Circle { float x, y, r; };

struct RGBA { float r, g, b, a; };

struct RGBA8 {
    RGBA8() {}
    RGBA8(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : r(r), g(g), b(b), a(a) {}
    
    unsigned char r, g, b, a;
};

struct AffineTransform { float a, b, c, d, tx, ty; };

struct Bounds {
    Bounds() {}
    Bounds(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy) {}
    bool contains(float px, float py) {
        return px > lx && py > ly && px < ux && py < uy;
    }
    bool intersects(Bounds other) const {
        return ux > other.lx && other.ux > lx && uy > other.ly && other.uy > ly;
    }
    void unite(Bounds other) {
        lx = MIN(lx, other.lx), ly = MIN(ly, other.ly);
        ux = MAX(ux, other.ux), uy = MAX(uy, other.uy);
    }
    float lx, ly, ux, uy;
};

struct Vector {
    Vector() {}
    Vector(float x, float y) : x(x), y(y) {}
    
    float x, y;
};

struct _Size { float w, h; };

struct Quadratic { float x0, y0, x1, y1, x2, y2; };

struct Line { float x0, y0, x1, y1; };

struct Unit {
    AffineTransform     unitCTM;
};

struct Arcs {
    int         count;
    Arc         arcs[192];
};

#define kCirclesSize 340

struct Circles {
    int         count;
    Circle      circles[kCirclesSize];
};

struct Fractals {
    int                 count;
    AffineTransform     unitCTM;
    AffineTransform     ctms[8];
    int                 counts[8];
    bool                ellipse;
};

struct Lines {
    int         count;
    Line        lines[255];
};

struct Quadratics {
    int                 count;
    AffineTransform     ctms[170];
};

struct Units {
    int                 count;
    AffineTransform     ctms[170];
};

struct MeshState {
    bool                hideCurves;
    bool                hideTriangles;
    bool                showColors;
    bool                showInterior;
};

struct Triangle {
    int i0, i1, i2, c;
};

struct State {
    RGBA                fillColor;
    RGBA                strokeColor;
    AffineTransform     CTM;
    _Size               drawableSize;
    float               flatness;
    float               width;
    float               cap;
};


enum CallType : unsigned int {
    kCallTypeArcs = 0,
    kCallTypeCircles,
    kCallTypeFractal,
    kCallTypeLines,
    kCallTypeMeshCurves,
    kCallTypeMeshConcaveCurves,
    kCallTypeMeshTriangles,
    kCallTypeQuadratics,
    kCallTypeUnits,
};

struct Call {
    Call() {}
    CallType    type;
    State       state;
    union {
        Arcs        arcs;
        Circles     circles;
        Fractals    fractals;
        Lines       lines;
        MeshState   meshState;
        Quadratics  quadratics;
        Units       units;
    };
};

static AffineTransform AffineTransformConcat(const AffineTransform t0, const AffineTransform t1) {
    return {
        t0.a * t1.a + t0.b * t1.c,              t0.a * t1.b + t0.b * t1.d,
        t0.c * t1.a + t0.d * t1.c,              t0.c * t1.b + t0.d * t1.d,
        t0.tx * t1.a + t0.ty * t1.c + t1.tx,    t0.tx * t1.b + t0.ty * t1.d + t1.ty
    };
}

static AffineTransform AffineTransformInvert(const AffineTransform t) {
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
