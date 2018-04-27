//
//  Tess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 02/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
#import "tesselator.h"
#import "Algorithms.hpp"
#import "VGQuadTree.hpp"

constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
constexpr int kPolySize = 3;
constexpr int kPointStride = 2 * sizeof(float);
constexpr float kSplitCubicPrecision = 0.1;

#pragma mark - Algorithms

// Source: http://caffeineowl.com/graphics/2d/vectorial/cubic2quad01.html
//
static int countSplitCubics(const float *src, const int npoints, const float prec) {
    float x0, y0, x1, y1, x2, y2, x3, y3;
    float xD01, yD01, D01, tcubed;
    int count = 0, calculated;
    
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
    
    for (int i = 0; i < npoints - 1; i += 3) {
        const float *p = src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
        xD01 = x3 + 3.f * (x1 - x2) - x0, yD01 = y3 + 3.f * (y1 - y2) - y0;
        D01 = std::sqrt(xD01 * xD01 + yD01 * yD01);
        tcubed = D01 == 0 ? 1 : prec * multiplier / D01;
        calculated = tcubed >= 1 ? 1 : std::ceil(1.0 / cbrt(tcubed));
        count += calculated;
    }
    return count + 1;
}

static void splitCubics(float *dst, float *cpdst, const float *src, const int npoints, const float prec) {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, cpx, cpy;
    float xD01, yD01, D01, tcubed, t;
    float n01, n12, n23;
    float n012, n123;
    float n0123;
    
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
    
    for (int i = 0; i < npoints - 1; i += 3) {
        const float *p = src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
        do {
            *dst++ = x0, *dst++ = y0;
            
            xD01 = x3 + 3.f * (x1 - x2) - x0, yD01 = y3 + 3.f * (y1 - y2) - y0;
            D01 = std::sqrt(xD01 * xD01 + yD01 * yD01);
            tcubed = D01 == 0 ? 1 : prec * multiplier / D01;
            
            if (tcubed < 0.999) {
                t = cbrt(tcubed);
                
                n01 = lerp(t, x0, x1), n12 = lerp(t, x1, x2), n23 = lerp(t, x2, x3);
                n012 = lerp(t, n01, n12), n123 = lerp(t, n12, n23);
                n0123 = lerp(t, n012, n123);
                cpx = (3.0 * (n012 + n01) - x0 - n0123) * 0.25;
                x0 = n0123, x1 = n123, x2 = n23;
                
                n01 = lerp(t, y0, y1), n12 = lerp(t, y1, y2), n23 = lerp(t, y2, y3);
                n012 = lerp(t, n01, n12), n123 = lerp(t, n12, n23);
                n0123 = lerp(t, n012, n123);
                cpy = (3.0 * (n012 + n01) - y0 - n0123) * 0.25;
                y0 = n0123, y1 = n123, y2 = n23;
                
                *cpdst++ = cpx, *cpdst++ = cpy;
            }
        } while (tcubed < 0.999);
        
        *cpdst++ = (3.0 * (x2 + x1) - x0 - x3) * 0.25, *cpdst++ = (3.0 * (y2 + y1) - y0 - y3) * 0.25;
    }
    *dst++ = x3, *dst++ = y3;
}

#pragma mark - Data structures

struct Edge {
    enum Flags {
        kFlippedX = 1 << 0,
        kFlippedY = 1 << 1
    };
    
    Edge(float lx, float ly, float ux, float uy, int ci, int pi) : lx(lx), ly(ly), ux(ux), uy(uy), flags(0), p(0), ci((unsigned short int)ci), pi((unsigned short int)pi) {
        if (lx > ux)
            swap(this->lx, this->ux), flags |= kFlippedX;
        if (ly > uy)
            swap(this->ly, this->uy), flags |= kFlippedY;
    }
    
    float lx, ly, ux, uy;
    unsigned short int flags, p, ci, pi;
    inline bool operator< (const Edge& other) const     { return lx < other.lx || (lx == other.lx && ly < other.ly); }
};

struct SubPath {
    enum Type {
        kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose
    };
    
    SubPath() {}
    SubPath(const float *points, const int npoints, Type type) {
        bool closed = (points[0] == points[npoints * 2 - 2] && points[1] == points[npoints * 2 - 1]);
        
        if (type == kCubic) {
            _npoints = countSplitCubics(points, npoints, kSplitCubicPrecision);
            _points = shared_ptr<float>((float *)malloc(_npoints * kPointStride), free);
            _controlPoints = shared_ptr<float>((float *)malloc(_npoints * kPointStride), free);
            splitCubics(_points.get(), _controlPoints.get(), points, npoints, kSplitCubicPrecision);
        } else {
            _npoints = npoints;
            _points = shared_ptr<float>((float *)malloc(_npoints * kPointStride), free);
            memcpy(_points.get(), points, npoints * kPointStride);
            if (closed)
                _npoints--;
        }
        _area = area();
        bounds(& _llx);
    }
    
    float area() const {
        return pointsSignedArea(_points.get(), _points.get() + _npoints * 2, kPointStride);
    }
    
    void bounds(float *limits) const {
        boundsLimits(limits, _points.get(), _points.get() + _npoints * 2, kPointStride);
    }
    
    shared_ptr<float>           _points;
    shared_ptr<float>           _controlPoints;
    int                         _npoints;
    float                       _area;
    float                       _llx, _lly, _urx, _ury;
};

struct Path {
    Path() {}
    Path(bool evenOdd, RGBA8 color) {
        _color = color;
        _evenOdd = evenOdd;
        _outerIndex = 0;
    }
    
    void addContour(float *points, int npoints, SubPath::Type type) {
        if (npoints > 2) {
            _subPaths.emplace_back(points, npoints, type);
            
            if (_subPaths.back()._npoints < 3)
                _subPaths.pop_back();
        }
    }
    
    size_t findOuterIndex() const {
        float maxArea = 0;
        size_t index = 0;
        
        for (size_t i = 0; i < _subPaths.size(); i++)
            if (std::abs(_subPaths[i]._area) > maxArea)
                maxArea = std::abs(_subPaths[i]._area), index = i;
        
        return index;
    }
    
    bool intersects(Path& path) const {
        return _subPaths.size() && path._subPaths.size() && (_urx > path._llx) && (path._urx > _llx) && (_ury > path._lly) && (path._ury > _lly);
    }
    
    void maskOut(TESStesselator *tess, Path& mask) {
        bool isPositive = _subPaths[_outerIndex]._area > 0;
        bool maskIsPositive = mask._subPaths[mask._outerIndex]._area > 0;
        
        for (SubPath& subPath : _subPaths) {
            tessSetOption(tess, TESS_REVERSE_CONTOURS, !isPositive);
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        }
        
        for (SubPath& subPath : mask._subPaths) {
            tessSetOption(tess, TESS_REVERSE_CONTOURS, maskIsPositive);
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        }
        
        if (tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
            writeTessSubpaths(tess);
            _outerIndex = findOuterIndex();
            setBounds();
        }
    }
    
    void setBounds() {
        _llx = FLT_MAX, _lly = FLT_MAX, _urx = -FLT_MAX, _ury = -FLT_MAX;
        for (SubPath& subPath : _subPaths) {
            _llx = std::min(_llx, subPath._llx);
            _lly = std::min(_lly, subPath._lly);
            _urx = std::max(_urx, subPath._urx);
            _ury = std::max(_ury, subPath._ury);
        }
    }
    
    vector<pair<Edge, Edge>> findIntersections() {
        vector<pair<Edge, Edge>> intersections;
        
        int count = 0;
        for (SubPath& s : _subPaths)
            count += s._npoints;
        
        Edge *edges = (Edge *)alloca(count * sizeof(Edge)), *begin = & edges[0], *end = begin;
        float lx, ly, ux, uy, *points;
        for (int ci = 0; ci < _subPaths.size(); ci++) {
            points = _subPaths[ci]._points.get();
            for (int pi = 0; pi < _subPaths[ci]._npoints; pi++) {
                lx = points[pi * 2], ly = points[pi * 2 + 1];
                ux = points[((pi + 1) % _subPaths[ci]._npoints) * 2], uy = points[((pi + 1) % _subPaths[ci]._npoints) * 2 + 1];
                new (end++) Edge(lx, ly, ux, uy, ci, pi);
            }
        }
        sort(begin, end);
        
        Edge *edge, *last, *src, *dst, *e0, *e1;
        for (edge = begin; edge < end; ) {
            last = edge;
            lx = edge->lx;
            
            do
                edge++;
            while (edge < end && edge->lx == lx);
            
            src = dst = last;
            while (--src >= begin)
                if (src->ux > lx)
                    *(--dst) = *src;
            begin = dst;
            
            for (e0 = last; e0 < edge; e0++)
                for (e1 = begin; e1 < edge; e1++)
                    if (e0 != e1 && e0->uy > e1->ly && e1->uy > e0->ly && e0->ux > e1->lx && e1->ux > e0->lx)
                        if (doLinesIntersect(
                                             e0->flags & Edge::kFlippedX ? e0->ux : e0->lx,     e0->flags & Edge::kFlippedY ? e0->uy : e0->ly,
                                             ~e0->flags & Edge::kFlippedX ? e0->ux : e0->lx,    ~e0->flags & Edge::kFlippedY ? e0->uy : e0->ly,
                                             e1->flags & Edge::kFlippedX ? e1->ux : e1->lx,     e1->flags & Edge::kFlippedY ? e1->uy : e1->ly,
                                             ~e1->flags & Edge::kFlippedX ? e1->ux : e1->lx,    ~e1->flags & Edge::kFlippedY ? e1->uy : e1->ly)) {
                            
                            bool duplicate = false;
                            for (auto& intersection : intersections)
                                if (intersection.first.ci == e1->ci && intersection.first.pi == e1->pi &&
                                    intersection.second.ci == e0->ci && intersection.second.pi == e0->pi) {
                                    duplicate = true;
                                    break;
                                }
                            if (!duplicate)
                                intersections.emplace_back(*e0, *e1);
                        }
        }
        return intersections;
    }
    
    void resolveSelfIntersections(TESStesselator *tess) {
//        if (_subPaths.size() == 1 && _subPaths[0]._npoints == 3) {
//            setBounds();
//            return;
//        }
		
        auto intersections = findIntersections();
        for (auto& intersection : intersections) {
            if (intersection.first.ci == intersection.second.ci) {
                
                break;
            }
        }
      
        for (SubPath& subPath : _subPaths)
            if (subPath._npoints > 2)
                tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        
        if (tessTesselate(tess, _evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
            writeTessSubpaths(tess);
            _outerIndex = findOuterIndex();
            setBounds();
        }
    }
    
    void writeTessSubpaths(TESStesselator *tess) {
        _subPaths.resize(0);
        
        const float *verts = tessGetVertices(tess);
        const int *elems = tessGetElements(tess);
        const int nelems = tessGetElementCount(tess);
        
        for (int i = 0; i < nelems; i++) {
            int base = elems[i * 2], npoints = elems[i * 2 + 1];
            const float *points = verts + base * 2;
            
            if (npoints > 2)
                _subPaths.emplace_back(points, npoints, SubPath::kLine);
        }
    }
    
    RGBA8                       _color;
    bool                        _evenOdd;
    size_t                      _outerIndex;
    vector<SubPath>             _subPaths;
    float                       _llx, _lly, _urx, _ury;
};

struct Mesh {
    vector<RGBA8>                       _colors;
    vector<Vector>                      _points;
    vector<Triangle>                    _triangles;
    vector<Triangle>                    _curves;
    vector<Triangle>                    _concaveCurves;
    
    void empty() {
        _colors.resize(0), _points.resize(0), _triangles.resize(0), _curves.resize(0), _concaveCurves.resize(0);
    }
};

struct Tess {
    Tess() {}
    Tess(float w, float h) {
        reset(w, h);
    }
    
    void reset(float w, float h) {
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
        _quadtree = shared_ptr<VG::QuadTree<size_t>>(new VG::QuadTree<size_t>(0.f, 0.f, w, h));
        _paths.resize(0);
    }
    
    void addPath(Path& path) {
        if (path._subPaths.size() != 0)
            _paths.emplace_back(path);
    }
    
    void triangulate(Mesh& mesh, bool useMasking) {
        mesh.empty();
        TESStesselator *tess = _tess.get();
        
        int pi, c;
        float area, flip;
        for (Path& path : _paths) {
            size_t outerIndex = path.findOuterIndex();
            SubPath* outer = & path._subPaths[outerIndex];
            
            for (SubPath& s : path._subPaths)
                if (s._controlPoints != NULL) {
                    const float *points = s._points.get();
                    const float *controlPoints = s._controlPoints.get();
                    pi = (int)mesh._points.size();
                    c = (int)(& path - & _paths[0]);
                    
                    Vector p0 = { points[0], points[1] }, p1, p2;
                    mesh._points.emplace_back(p0);
                    
                    for (int i = 0; i < s._npoints - 1; i++) {
                        p1 = { controlPoints[i * 2], controlPoints[i * 2 + 1] };
                        p2 = { points[i * 2 + 2], points[i * 2 + 3] };
                        
                        mesh._points.emplace_back(p1);
                        mesh._points.emplace_back(p2);
                        
                        area = (p1.x - p0.x) * (p2.y - p1.y) - (p1.y - p0.y) * (p2.x - p1.x);
                        Triangle tri = { pi + i * 2, pi + i * 2 + 1, pi + i * 2 + 2, c };
                    
                        if (area != 0) {
                            if (area < 0)
                                std::swap(tri.i0, tri.i2);
                            
                            flip = outer == & s ? 1 : -1;
                            
                            if (flip * area * s._area < 0)
                                mesh._concaveCurves.emplace_back(tri);
                            else
                                mesh._curves.emplace_back(tri);
                        }
                        
                        p0 = p2;
                    }
                }
        }
        
        if (useMasking) {
            for (Path& path : _paths)
                path.resolveSelfIntersections(tess);
            
            for (size_t i = 0; i < _paths.size(); i++) {
                Path& path = _paths[i];
                
                auto matches = _quadtree->find(path._llx, path._lly, path._urx, path._ury);
                for (auto j : matches) //                if (_paths[j].intersects(path))
                    _paths[j].maskOut(tess, path);
                _quadtree->add(i, path._llx, path._lly, path._urx, path._ury);
            }
        }

        assert(kPolySize == 3);
        tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
        
        for (Path& path : _paths) {
            mesh._colors.emplace_back(path._color);

            for (SubPath& s : path._subPaths)
                tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints);
            
            if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                float *points = (float *)tessGetVertices(tess);
                int npoints = tessGetVertexCount(tess);
                const int nelems = tessGetElementCount(tess);
                const TESSindex *polygon = tessGetElements(tess);

                int pi = (int)mesh._points.size(), c = (int)mesh._colors.size() - 1;
                for (int i = 0; i < nelems; i++, polygon += kPolySize) {
                    Triangle tri = { pi + polygon[0], pi + polygon[1], pi + polygon[2], c };
                    mesh._triangles.emplace_back(tri);
                }
                for (int i = 0; i < npoints; i++) {
                    Vector p = { points[i * 2], points[i * 2 + 1] };
                    mesh._points.emplace_back(p);
                }
            }
        }
    }

    shared_ptr<TESStesselator>			_tess;
    shared_ptr<VG::QuadTree<size_t>>    _quadtree;
    vector<Path>                        _paths;
};
