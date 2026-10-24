//
//  Path.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Shared.metal.hpp"
#import <vector>
#import <algorithm>
#import <cmath>
using namespace std;

constexpr int kPointStride = 2 * sizeof(float);
// Points closer than this in both coordinates are one vertex to libTess, which merges them after its union. Paths merge
// theirs first (mergeVertices()), so no curve loses the vertex it ends on. A fraction of the extent, plus float resolution
// at the bounds' magnitude.
static inline float vertexMergeLimit(const Bounds& b) {
    float extent = std::max(b.ux - b.lx, b.uy - b.ly);
    float magnitude = std::max(std::max(std::abs(b.lx), std::abs(b.ux)), std::max(std::abs(b.ly), std::abs(b.uy)));
    return extent * 1e-5f + magnitude * 1e-6f;
}
// The largest coordinate magnitude geometry may have. Straightness and turn tests compare squared cross products with
// products of squared lengths: fourth powers of differences, each up to twice it, so 64 times its fourth power at most.
// Beyond it they overflow, curves test as straight, and further on the tessellator's own products fail too.
static inline float coordinateLimit() { static const float limit = std::sqrt(std::sqrt(FLT_MAX / 64)); return limit; }
static inline float isLeft(const float *P0, const float *P1, const float *P2) {
    return (P1[0] - P0[0]) * (P2[1] - P0[1]) - (P2[0] -  P0[0]) * (P1[1] - P0[1]);
}

static void boundsLimits(float *limits, const float *begin, const float *end, size_t stride) {
    float maxX = -FLT_MAX, maxY = -FLT_MAX, minX= FLT_MAX, minY= FLT_MAX;
    
    for (auto it = begin; it < end; it = (float *)((char *)it + stride)) {
        if (it[0] > maxX) maxX = it[0];
        if (it[0] < minX) minX = it[0];
        if (it[1] > maxY) maxY = it[1];
        if (it[1] < minY) minY = it[1];
    }
    limits[0] = minX;  limits[1] = minY;  limits[2] = maxX;  limits[3] = maxY;
}

static Bounds BoundsFromFloats(const float *begin, const float *end) {
    Bounds b;
    boundsLimits(& b.lx, begin, end, kPointStride);
    return b;
}

// http://geomalgorithms.com/a03-_inclusion.html
//
static size_t pointInPolygon(const float *p, const float *polygon, size_t n, size_t stride) {
    size_t wn = 0; auto mul = stride / sizeof(float);
    for (auto i = 0; i < n; i++) {
        auto p0 = polygon + i * mul; auto p1 = polygon + ((i + 1) % n) * mul;
        if (p0[1] <= p[1]) {
            if (p1[1] > p[1] && isLeft(p0, p1, p) > 0.0f)  ++wn;
        } else {
            if (p1[1] <= p[1] && isLeft(p0, p1, p) < 0.0f)  --wn;
        }
    }
    return wn;
}

// A path: contours of float points, each lines through them or a chain of quadratics (p0, c, p1, c, p2 ...) or cubics
// (p0, c, c, p1, c, c, p2 ...), open or closed, and how they are drawn: filled by their winding, or stroked by a pen.
struct Path {
    enum Kind { kLines, kQuadratics, kCubics };
    enum Join { kMiter, kRound, kBevel };
    enum Cap { kButt, kRoundCap, kSquare };
    struct Contour {
        size_t first;       // its first float in _points
        int npoints;
        Kind kind;
        bool closed;
    };
    
    Path() {}
    explicit Path(bool evenOdd) : _evenOdd(evenOdd) {}
    
    // A contour is kept if its points are finite and within coordinateLimit(), and bounds the path.
    void add(Kind kind, const float *points, int npoints, bool closed) {
        for (int i = 0; i < npoints * 2; i++)
            if (!std::isfinite(points[i]))
                return;
        
        float limit = coordinateLimit();
        Bounds b = BoundsFromFloats(points, points + npoints * 2);
        
        if (!(b.lx >= -limit && b.ux <= limit && b.ly >= -limit && b.uy <= limit))
            return;
        _bounds = _contours.empty() ? b : _bounds.united(b);
        _contours.push_back({ _points.size(), npoints, kind, closed });
        _points.insert(_points.end(), points, points + npoints * 2);
    }
    // Room for contours of npoints points in all, which spares the growth of adding them one by one.
    void reserve(int npoints, int ncontours) {
        _points.reserve(_points.size() + npoints * 2), _contours.reserve(_contours.size() + ncontours);
    }
    // Strokes it instead of filling it: a pen width wide, with its joins, caps and dashes (on, off, ...), whose pattern
    // each contour starts offset into. A stroke's outline fills by nonzero winding.
    void stroke(float width, Join join, Cap cap, float miterLimit, const float *dashes = nullptr, int ndashes = 0, float offset = 0) {
        _strokeWidth = width, _join = join, _cap = cap, _miterLimit = miterLimit;
        _dashes.assign(dashes, dashes + ndashes), _dashOffset = offset;
    }
    bool stroked() const { return _strokeWidth > 0; }
    
    const float *points(int i) const { return _points.data() + _contours[i].first; }
    float *points(int i) { return _points.data() + _contours[i].first; }
    // After its points move.
    void updateBounds() {
        for (int i = 0; i < (int)_contours.size(); i++) {
            Bounds b = BoundsFromFloats(points(i), points(i) + _contours[i].npoints * 2);
            _bounds = i ? _bounds.united(b) : b;
        }
    }
    // Whether its contours' polygon (lines through their on-curve points) holds a point, by its winding.
    bool contains(float x, float y) {
        if (!_bounds.contains(x, y))
            return false;
        float p[2] = { x, y };
        int wn = 0;
        for (int i = 0; i < (int)_contours.size(); i++) {
            int step = _contours[i].kind == kCubics ? 3 : _contours[i].kind == kQuadratics ? 2 : 1;
            wn += (int)pointInPolygon(p, points(i), (_contours[i].npoints - 1) / step + 1, kPointStride * step);
        }
        return _evenOdd && !stroked() ? wn & 1 : wn;
    }
    
    vector<float>               _points;
    vector<Contour>             _contours;
    Bounds                      _bounds;            // its contours' points'
    bool                        _evenOdd = false;
    float                       _strokeWidth = 0;   // zero for a fill
    Join                        _join = kMiter;
    Cap                         _cap = kButt;
    float                       _miterLimit = 4;
    vector<float>               _dashes;
    float                       _dashOffset = 0;
};
