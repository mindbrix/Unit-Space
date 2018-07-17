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

struct Arc { float x, y, r, t0, t1; };

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
    RGBA8() {}
    RGBA8(RGBA rgba) : r(rgba.r * 255), g(rgba.g * 255), b(rgba.b * 255), a(rgba.a * 255) {}
    RGBA8(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : r(r), g(g), b(b), a(a) {}
    
    RGBA8 multiply(unsigned char alpha) {
        return RGBA8(r, g, b, a * alpha / 255);
    }
    static RGBA8 over(RGBA8 src, RGBA8 dst) {
        return src.a == 255 ? src : RGBA8((src.r * src.a / 255) + (dst.r * (255 - src.a) / 255),
                     (src.g * src.a / 255) + (dst.g * (255 - src.a) / 255),
                     (src.b * src.a / 255) + (dst.b * (255 - src.a) / 255),
                     src.a + (dst.a * (255 - src.a) / 255));
    }
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
    bool intersects(Bounds other) const {
        return ux > other.lx && other.ux > lx && uy > other.ly && other.uy > ly;
    }
    Bounds transformed(AffineTransform t) {
        float w, h, x0, x1, y0, y1, ox, oy;
        w = ux - lx, h = uy - ly;
        x0 = (t.a < 0 ? t.a * w : 0) + (t.c < 0 ? t.c * h : 0);
        x1 = (t.a < 0 ? 0 : t.a * w) + (t.c < 0 ? 0 : t.c * h);
        y0 = (t.b < 0 ? t.b * w : 0) + (t.d < 0 ? t.d * h : 0);
        y1 = (t.b < 0 ? 0 : t.b * w) + (t.d < 0 ? 0 : t.d * h);
        ox = lx * t.a + ly * t.c + t.tx;
        oy = lx * t.b + ly * t.d + t.ty;
        return { ox + x0, oy + y0, ox + x1, oy + y1 };
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
    bool add(float x, float y, float r) {
        if (count == kCirclesSize)
            return false;
        circles[count++] = { x, y, r };
        return true;
    }
    void empty() { count = 0; }
    
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
    bool                showEdges;
    bool                showColors;
    bool                showInterior;
    bool                useStrokeColor;
    int                 colorsSize;
    int                 reverseCount;
};

struct Triangle {
    Triangle() {}
    Triangle(int i0, int i1, int i2, int c) : i0(i0), i1(i1), i2(i2), c(c) {}
    
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
    kCallTypeMeshInterior,
    kCallTypeMeshInteriorBlend,
    kCallTypeMeshTriangles,
    kCallTypeQuadratics,
    kCallTypeUnits,
};

struct Call {
    Call() {}
    CallType    type;
    int         instanceCount, baseInstance;
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
