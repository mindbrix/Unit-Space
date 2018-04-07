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

struct Arc { float x, y, r, t0, t1; };

struct Circle { float x, y, r; };

struct RGBA { float r, g, b, a; };
struct RGBA8 { unsigned char r, g, b, a; };

struct AffineTransform { float a, b, c, d, tx, ty; };

struct Vector { float x, y; };

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

#define kTrianglesIndexSize 384
#define kTrianglesPointsSize 256
#define kTrianglesColorsSize 125

struct Triangles {
    int                 count;
    bool                showColors;
    bool                showInterior;
    unsigned char       i0[kTrianglesIndexSize];
    unsigned char       i1[kTrianglesIndexSize];
    unsigned char       i2[kTrianglesIndexSize];
    unsigned char       c0[kTrianglesIndexSize];
    Vector              points[kTrianglesPointsSize];
    RGBA8               colors[kTrianglesColorsSize];
    
    bool add(int i, int _i0, int _i1, int _i2, int _c0) {
        if (i >= kTrianglesIndexSize)
            return false;
        count = i + 1;
        i0[i] = (unsigned char)_i0;
        i1[i] = (unsigned char)_i1;
        i2[i] = (unsigned char)_i2;
        c0[i] = (unsigned char)_c0;
        return true;
    }
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
    kCallTypeQuadratics,
    kCallTypeUnits,
    kCallTypeTriangles,
};

struct Call {
    CallType    type;
    State       state;
    union {
        Arcs        arcs;
        Circles     circles;
        Fractals    fractals;
        Lines       lines;
        Quadratics  quadratics;
        Units       units;
        Triangles   triangles;
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
