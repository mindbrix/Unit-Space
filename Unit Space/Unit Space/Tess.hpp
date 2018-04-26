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

struct Edge {
    Edge(float lx, float ly, float ux, float uy, int ci, int pi) : lx(lx), ly(ly), ux(ux), uy(uy), ci(ci), pi(pi) {}
    
    float lx, ly, ux, uy;
    int ci, pi;
    inline bool operator< (const Edge& other) const     { return lx < other.lx; }
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
        
    bool contains(SubPath& subPath) const {
        return (_llx <= subPath._llx) && (_lly <= subPath._lly) && (_urx >= subPath._urx) && (_ury >= subPath._ury);
    }
    
    bool intersects(SubPath& subPath) const {
        return (_urx > subPath._llx) && (subPath._urx > _llx) && (_ury > subPath._lly) && (subPath._ury > _lly);
    }
    
    // Source: http://caffeineowl.com/graphics/2d/vectorial/cubic2quad01.html
    //
    int countSplitCubics(const float *src, const int npoints, const float prec) {
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
    
    void splitCubics(float *dst, float *cpdst, const float *src, const int npoints, const float prec) {
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
    
    bool polygonIntersects(SubPath& subPath) const {
        for (int i = 0; i < _npoints - 1; i++) {
            float *pts = _points.get() + i * 2;
            for (int j = 0; j < subPath._npoints - 1; j++) {
                float *spts = subPath._points.get() + j * 2;
                if (doLinesIntersect(pts[0], pts[1], pts[2], pts[3], spts[0], spts[1], spts[2], spts[3]))
                    return true;
            }
        }
        return false;
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
        bool bboxIntersects = _subPaths.size() && path._subPaths.size() && (_urx > path._llx) && (path._urx > _llx) && (_ury > path._lly) && (path._ury > _lly);

//        if (bboxIntersects && _subPaths.size() == 1 && _subPaths[0]._npoints < 6 && path._subPaths.size() == 1 && path._subPaths[0]._npoints < 6)
//            return _subPaths[0].polygonIntersects(path._subPaths[0]);

        return bboxIntersects;
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
    
    void resolveSelfIntersections(TESStesselator *tess) {
        if (_subPaths.size() == 1 && (_subPaths[0]._npoints == 4 || (_subPaths[0]._npoints < 10 && !_subPaths[0].polygonIntersects(_subPaths[0])))) {
            setBounds();
            return;
        }
		
        int count = 0;
        for (SubPath& s : _subPaths)
            count += s._npoints;
        
        vector<Edge> edges;
        float lx, ly, ux, uy, *points;
        for (int ci = 0; ci < _subPaths.size(); ci++) {
            points = _subPaths[ci]._points.get();
            for (int pi = 0; pi < _subPaths[ci]._npoints; pi++) {
                lx = points[pi * 2], ly = points[pi * 2 + 1];
                ux = points[((pi + 1) % _subPaths[ci]._npoints) * 2], uy = points[((pi + 1) % _subPaths[ci]._npoints) * 2 + 1];
                if (lx > ux)
                    swap(lx, ux);
                if (ly < uy)
                    swap(ly, uy);
                edges.emplace_back(lx, ly, ux, uy, ci, pi);
            }
        }
        sort(edges.begin(), edges.end());
        
        Edge *begin = & edges[0], *edge = begin, *end = begin + edges.size(), *last, *dst, *e0, *e1;
        lx = edge->lx;
        
        for (;;) {
            last = edge;
            
            do
                edge++;
            while (edge < end && edge->lx == lx);
            
            for (e0 = last; e0 < edge; e0++)
                for (e1 = begin; e1 < edge; e1++)
                    if (e0 != e1)
                        ;
            
            if (edge == end)
                break;
            
            lx = edge->lx;
            
            dst = last;
            while (--last >= begin)
                if (last->ux > lx)
                    *(--dst) = *last;
            begin = dst;
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
