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

constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
constexpr int kPolySize = 6;
constexpr int kPointStride = 2 * sizeof(float);
constexpr float kCollinearTolerance = 1e-2;


struct SubPath {
    enum Type {
        kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose
    };
    
    SubPath() {}
    SubPath(const float *points, const int npoints, Type type) {
        bool closed = (points[0] == points[npoints * 2 - 2] && points[1] == points[npoints * 2 - 1]);
        
        _npoints = npoints;
        _points = shared_ptr<float>((float *)malloc((npoints + (closed ? 0 : 1)) * kPointStride), free);
        
        if (type == kCubic)
            _npoints = flattenCubics(_points.get(), points, _npoints);
        else
            memcpy(_points.get(), points, npoints * kPointStride);
        
        _area = area();
        bounds(& _llx);
        
        if (!closed) {
            float *p = _points.get(), *dst = p + _npoints * 2;
            *dst++ = *p++, *dst++ = *p++;
            _npoints++;
            
            assert(_area == area());
        }
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
    
    int flattenCubics(float *dst, const float *src, int npoints) {
        float *begin = dst;
        float x0, y0, x1, y1, x2, y2, x3, y3;
        
        *dst++ = src[0], *dst++ = src[1];
        
        for (int i = 0; i < npoints - 1; i += 3) {
            const float *p = src + i * 2;
            
            x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
            
            float ax0 = x0 + x2 - x1 - x1, ay0 = y0 + y2 - y1 - y1;
            float ax1 = x1 + x3 - x2 - x2, ay1 = y1 + y3 - y2 - y2;
            float flatness = ax0 * ax0 + ay0 * ay0 + ax1 * ax1 + ay1 * ay1;
            
            if (flatness > 1e-3) {
                *dst++ = deCasteljauCubic(0.33333, x0, x1, x2, x3);
                *dst++ = deCasteljauCubic(0.33333, y0, y1, y2, y3);
                *dst++ = deCasteljauCubic(0.66666, x0, x1, x2, x3);
                *dst++ = deCasteljauCubic(0.66666, y0, y1, y2, y3);
            }
            *dst++ = x3, *dst++ = y3;
        }
        return int(dst - begin) / 2;
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
    int                         _npoints;
    float                       _area;
    float                       _llx, _lly, _urx, _ury;
};

struct Path {
    Path(bool evenOdd, RGBA8 color) {
        _color = color;
        _evenOdd = evenOdd;
        _outerIndex = INT_MAX;
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
    }
    
    void addContour(float *points, int npoints, SubPath::Type type) {
        if (npoints > 2)
            _subPaths.emplace_back(points, npoints, type);
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
        bool bboxIntersects = _subPaths.size() && path._subPaths.size() && _subPaths[_outerIndex].intersects(path._subPaths[path._outerIndex]);
        
        if (bboxIntersects && _subPaths.size() == 1 && _subPaths[0]._npoints < 7 && path._subPaths.size() == 1 && path._subPaths[0]._npoints < 7)
            return _subPaths[0].polygonIntersects(path._subPaths[0]);
        
        return bboxIntersects;
    }
    
    void maskOut(Path& mask) {
        TESStesselator *tess = tessNewTess(NULL);
        bool isPositive = _subPaths[_outerIndex]._area > 0;
        bool maskIsPositive = mask._subPaths[mask._outerIndex]._area > 0;
        
        for (SubPath& subPath : _subPaths)
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints, !isPositive);
        
        for (SubPath& subPath : mask._subPaths)
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints, maskIsPositive);
        
        if (tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeSubpaths(tess);
        
        tessDeleteTess(tess);
    }
    
    void writeBoundaryContours() {
        for (SubPath& subPath : _subPaths)
            if (subPath._npoints > 3)
                tessAddContour(_tess.get(), 2, subPath._points.get(), kPointStride, subPath._npoints, false);
        
        if (tessTesselate(_tess.get(), _evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeSubpaths(_tess.get());
    }
    
    void writeSubpaths(TESStesselator *tess) {
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
    }
    
    RGBA8                       _color;
    shared_ptr<TESStesselator>  _tess;
    bool                        _evenOdd;
    size_t                      _outerIndex;
    vector<SubPath>             _subPaths;
};

struct Mesh {
    Mesh(float w, float h) {
        _quadtree = shared_ptr<VG::QuadTree<size_t>>(new VG::QuadTree<size_t>(0.f, 0.f, w, h));
        
    }
    void addPath(Path& path) {
        path.writeBoundaryContours();
        if (path._subPaths.size() == 0)
            return;
        
        path._outerIndex = path.findOuterIndex();
        SubPath& outer = path._subPaths[path._outerIndex];
        
        auto matches = _quadtree->find(outer._llx, outer._lly, outer._urx, outer._ury);
        
        for (size_t i : matches)
            if (path.intersects(_paths[i]))
                _paths[i].maskOut(path);
        
//        for (Path& p : _paths)
//            if (path.intersects(p))
//                p.maskOut(path);

        _quadtree->add(size_t(_paths.size()), outer._llx, outer._lly, outer._urx, outer._ury);
        _paths.emplace_back(path);
    }
    
    void triangulate(vector<Triangles>& vtriangles) {
        TESStesselator *tess = tessNewTess(NULL);
        
        int t = 0, k = 0, pi = 0, c = 0;
        Triangles* triangles = & vtriangles[t];
        
        for (Path& p : _paths) {
//            if (p._subPaths.size() > 4)
//                continue;
            for (SubPath& s : p._subPaths)
                if (s._npoints > 3)
                    tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints, false);
            
            if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                float *points = (float *)tessGetVertices(tess);
                int npoints = tessGetVertexCount(tess);
                const int nelems = tessGetElementCount(tess);
                const TESSindex *polygon = tessGetElements(tess);
                
                TESSindex indices[npoints];
                memset(indices, -1, npoints * sizeof(TESSindex));
                
                for (int i = 0; i < nelems; i++, polygon += kPolySize) {
                    if (!writePolygon(triangles, points, pi, k, c, p._color, polygon, indices)) {
                        vtriangles.resize(++t + 1);
                        triangles = & vtriangles[t];
                        k = 0, pi = 0, c = 0;
                        memset(indices, INT_MAX, npoints * sizeof(TESSindex));

                        writePolygon(triangles, points, pi, k, c, p._color, polygon, indices);
                    }
                }
            }
        }
        tessDeleteTess(tess);
    }
    
    bool writePolygon(Triangles *triangles, float *points, int& pi, int& k, int& c, RGBA8 color, const TESSindex *polygon, TESSindex *indices) {
        for (int j = 0; j < kPolySize && polygon[j] != TESS_UNDEF; j++) {
            TESSindex index = polygon[j];
            
            if (indices[index] == -1) {
                if (pi == kTrianglesPointsSize)
                    return false;
                indices[index] = pi;
                
                triangles->points[pi++] = { points[index * 2], points[index * 2 + 1] };
            }
        }
        
        for (int j = 0; j < kPolySize - 2 && polygon[j + 2] != TESS_UNDEF; j++) {
            if (triangles->add(k, indices[polygon[0]], indices[polygon[j + 1]], indices[polygon[j + 2]], c % kTrianglesColorsSize))
                k++;
            else
                return false;
        }
        
        triangles->colors[c++ % kTrianglesColorsSize] = color;
        return true;
    }
    
    shared_ptr<VG::QuadTree<size_t>>    _quadtree;
    vector<Path>                        _paths;
};
