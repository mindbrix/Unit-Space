//
//  Tess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 02/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "tesselator.h"
#import "Algorithms.hpp"
#import "VGQuadTree.hpp"
#import "delaunay.hpp"

constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
constexpr int kPolySize = 3;
constexpr int kPointStride = 2 * sizeof(float);
constexpr float kSplitCubicPrecision = 0.01;

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
        }
        _area = area();
        bounds(& _llx);
        
        if (closed)
            _npoints--;
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
        float xD01, yD01, D01, tcubed, tsplit, t;
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
                   t = tsplit = cbrt(tcubed);
                   
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
        }
        *dst++ = x3, *dst++ = y3;
        *cpdst++ = (3.0 * (x2 + x1) - x0 - x3) * 0.25, *cpdst++ = (3.0 * (y2 + y1) - y0 - y3) * 0.25;
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
        
        if (tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess);
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
    
    void writeBoundaryContours(TESStesselator *tess) {
        if (_subPaths.size() == 1 && (_subPaths[0]._npoints == 4 || (_subPaths[0]._npoints < 10 && !_subPaths[0].polygonIntersects(_subPaths[0])))) {
            setBounds();
            return;
        }
		
        for (SubPath& subPath : _subPaths)
            if (subPath._npoints > 2)
                tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        
        if (tessTesselate(tess, _evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess);
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
        _outerIndex = findOuterIndex();
        setBounds();
    }
    
    RGBA8                       _color;
    bool                        _evenOdd;
    size_t                      _outerIndex;
    vector<SubPath>             _subPaths;
    float                       _llx, _lly, _urx, _ury;
};

struct Mesh {
    Mesh() {}
    Mesh(float w, float h) {
        reset(w, h);
    }
    
    void reset(float w, float h) {
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
        _paths.resize(0);
        
        _quadtree = shared_ptr<VG::QuadTree<size_t>>(new VG::QuadTree<size_t>(0.f, 0.f, w, h));
        
        _vcolors.resize(0);
        _vcurves.resize(0);
        _vpoints.resize(0);
        _vtriangles.resize(0);
    }
    
    void addPath(Path& path) {
        if (path._subPaths.size() == 0)
            return;
        
        path.writeBoundaryContours(_tess.get());
        assert (path._subPaths.size() > 0);
        
        _paths.emplace_back(path);
    }
    
    void maskPaths() {
        TESStesselator *tess = tessNewTess(NULL);

        for (size_t i = 0; i < _paths.size(); i++) {
            Path& path = _paths[i];
            
            auto matches = _quadtree->find(path._llx, path._lly, path._urx, path._ury);
            
            for (auto j : matches)
//                if (_paths[j].intersects(path))
                _paths[j].maskOut(tess, path);
            _quadtree->add(i, path._llx, path._lly, path._urx, path._ury);
        }
        tessDeleteTess(tess);
    }
    
    int triangulateDelaunayC() {
        int count = 0;
        for (Path& p : _paths)
            for (SubPath& s : p._subPaths)
                count += s._npoints;
        
        size_t pts_size = count + 3;
        auto points = shared_ptr<XYZ>((XYZ *)malloc(pts_size * sizeof(XYZ)), free);
        XYZ *dst = points.get();
        for (Path& p : _paths)
            for (SubPath& s : p._subPaths) {
                float *src = s._points.get(), *end = src + (s._npoints) * 2;
                while (src != end)
                    dst->x = *src++, dst->y = *src++, dst++->i = (int)(&p - &_paths[0]);
            }
        
        std::vector<ITRIANGLE> triangles(pts_size * 4);
        
        int ntri = 0;
        int status = Triangulate(count, points.get(), &triangles[0], (int)triangles.size(), & ntri);
        
        if (status == 0) {
            triangles.resize(ntri);
            
            {
                _vpoints.resize(count);
                XYZ *src = points.get(), *end = src + count;
                Vector *dst = & _vpoints[0];
                while(src != end)
                    *dst++ = { src->x, src++->y };
            }
            
            {
                _vtriangles.resize(ntri);
                Triangle *dst = & _vtriangles[0];
                for(auto &t : triangles)
                    *dst++ = { t.p1, t.p2, t.p3, 0 };
            }
            
            {
                _vcolors.resize(_paths.size());
                RGBA8 *dst = & _vcolors[0];
                for (Path& p : _paths)
                    *dst++ = p._color;
            }
        }
        
        return status;
    }
    
    int triangulate(bool useMasking) {
        assert(kPolySize == 3);
        if (useMasking)
            maskPaths();
        
        TESStesselator *tess = tessNewTess(NULL);
        tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
        vector<int> subpathIndices;
        
        for (Path& path : _paths) {
            _vcolors.emplace_back(path._color);

            for (SubPath& s : path._subPaths)
                assert(s._npoints > 2);
            
            for (SubPath& s : path._subPaths)
                tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints);
            
            subpathIndices.resize(0);
            for (int i = 0; i < path._subPaths.size(); i++)
                for (int n = 0; n < path._subPaths[i]._npoints; n++)
                    subpathIndices.emplace_back(i);
            
            if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                float *points = (float *)tessGetVertices(tess);
                const TESSindex *vertexIndices = tessGetVertexIndices(tess);
                int npoints = tessGetVertexCount(tess);
                const int nelems = tessGetElementCount(tess);
                const TESSindex *polygon = tessGetElements(tess);

                int pi = (int)_vpoints.size(), c = (int)_vcolors.size() - 1;
                for (int i = 0; i < nelems; i++, polygon += kPolySize) {
                    Triangle tri = { pi + polygon[0], pi + polygon[1], pi + polygon[2], c };
                    _vtriangles.emplace_back(tri);
                }
                for (int i = 0; i < npoints; i++) {
                    Vector p = { points[i * 2], points[i * 2 + 1] };
                    _vpoints.emplace_back(p);
                }
                int cpi = (int)_vpoints.size();
                
//                float cpoints = 
//                for (int i = 0; i < npoints; i++) {
//                    int idx = vertexIndices[i];
//                    
//                    Vector p = { points[i * 2], points[i * 2 + 1] };
//                    _vpoints.emplace_back(p);
//                }
                
            }
        }
        tessDeleteTess(tess);
        
        return (int)_vtriangles.size();
    }

    shared_ptr<TESStesselator>			_tess;
	
    shared_ptr<VG::QuadTree<size_t>>    _quadtree;
    vector<Path>                        _paths;
    
    vector<RGBA8>                       _vcolors;
    vector<Vector>                      _vpoints;
    vector<Triangle>                    _vtriangles;
    vector<Triangle>                    _vcurves;
};
