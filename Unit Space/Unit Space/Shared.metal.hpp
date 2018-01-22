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

struct RGBA { float r, g, b, a; };

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

struct Lines {
    int         count;
    Line        lines[255];
};

struct Units {
    int         count;
    Unit        units[16];
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
    kCallTypeLines,
    kCallTypeUnits
};

struct Call {
    CallType    type;
    State       state;
    union        {
        Arcs    arcs;
        Lines   lines;
        Units   units;
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
