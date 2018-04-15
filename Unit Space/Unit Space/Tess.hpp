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
constexpr int kPolySize = 6;
constexpr int kPointStride = 2 * sizeof(float);

struct SubPath {
    enum Type {
        kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose
    };
    
    SubPath() {}
    SubPath(const float *points, const int npoints, Type type) {
        bool closed = (points[0] == points[npoints * 2 - 2] && points[1] == points[npoints * 2 - 1]);
        
        if (type == kCubic) {
            int cubicCount = (npoints - 1) / 3;
            int subdivisions[cubicCount];
            _npoints = countCubicSubdivisions(points, npoints, subdivisions) + 1;
            _points = shared_ptr<float>((float *)malloc((_npoints + (closed ? 0 : 1)) * kPointStride), free);
            flattenCubics(_points.get(), points, npoints, subdivisions);
        } else {
            _npoints = npoints;
            _points = shared_ptr<float>((float *)malloc((_npoints + (closed ? 0 : 1)) * kPointStride), free);
            
            memcpy(_points.get(), points, npoints * kPointStride);
        }
        _area = area();
        bounds(& _llx);
        
        if (!closed) {
            float *p = _points.get(), *dst = p + _npoints * 2;
            *dst++ = *p++, *dst++ = *p++;
            _npoints++;
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
    
    int countCubicSubdivisions(const float *src, int npoints, int *subdivisions) {
        int total = 0, c0, c1, count;
        float x0, y0, x1, y1, x2, y2, x3, y3;
        float ax0, ay0, ax1, ay1, area0, area1, flatness;
        float qcpx0, qcpy0, qcpx1, qcpy1;
        
        for (int i = 0; i < npoints - 1; i += 3) {
            const float *p = src + i * 2;
            x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
            
            qcpx0 = x0 + 1.5f * (x1 - x0), qcpy0 = y0 + 1.5f * (y1 - y0);
            qcpx1 = x3 + 1.5f * (x2 - x3), qcpy1 = y3 + 1.5f * (y2 - y3);
        
            if (std::abs(qcpx0 - qcpx1) < 1e-3 && std::abs(qcpy0 - qcpy1) < 1e-3) {
                x0 = x0;
            }
            ax0 = x0 + x2 - x1 - x1, ay0 = y0 + y2 - y1 - y1;
            ax1 = x1 + x3 - x2 - x2, ay1 = y1 + y3 - y2 - y2;
            flatness = ax0 * ax0 + ay0 * ay0 + ax1 * ax1 + ay1 * ay1;
            
            area0 = std::abs((x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1)) / 3;
            area1 = std::abs((x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2)) / 3;
            
            c0 = std::max(2.f, log2f(area0));
            c1 = std::max(2.f, log2f(area1));
            count = flatness > 1e-3 ? c0 + c1 : 1;
            
            *subdivisions++ = count;
            total += count;
        }
        return total;
    }
    
    void flattenCubics(float *dst, const float *src, int npoints, int *subdivisions) {
        float x0, y0, x1, y1, x2, y2, x3, y3, t, dt;
        
        *dst++ = src[0], *dst++ = src[1];
        for (int i = 0; i < npoints - 1; i += 3, subdivisions++) {
            const float *p = src + i * 2;
            x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
            
            dt = 1.0 / float(*subdivisions), t = dt;
            for (int j = 0; j < *subdivisions - 1; j++, t += dt)
                *dst++ = deCasteljauCubic(t, x0, x1, x2, x3), *dst++ = deCasteljauCubic(t, y0, y1, y2, y3);
            *dst++ = x3, *dst++ = y3;
        }
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
        _outerIndex = 0;
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
        bool bboxIntersects = _subPaths.size() && path._subPaths.size() && (_urx > path._llx) && (path._urx > _llx) && (_ury > path._lly) && (path._ury > _lly);

//        if (bboxIntersects && _subPaths.size() == 1 && _subPaths[0]._npoints < 6 && path._subPaths.size() == 1 && path._subPaths[0]._npoints < 6)
//            return _subPaths[0].polygonIntersects(path._subPaths[0]);

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
            writeTessSubpaths(tess);
        
        tessDeleteTess(tess);
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
    
    void writeBoundaryContours() {
        if (_subPaths.size() == 1 && (_subPaths[0]._npoints == 4 || (_subPaths[0]._npoints < 10 && !_subPaths[0].polygonIntersects(_subPaths[0])))) {
            setBounds();
            return;
        }
        
        TESStesselator *tess = tessNewTess(NULL);
        for (SubPath& subPath : _subPaths)
            if (subPath._npoints > 3)
                tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints, false);
        
        if (tessTesselate(tess, _evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess);
        tessDeleteTess(tess);
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
    Mesh(float w, float h) {
        _quadtree = shared_ptr<VG::QuadTree<size_t>>(new VG::QuadTree<size_t>(0.f, 0.f, w, h));
        
    }
    void addPath(Path& path) {
        path.writeBoundaryContours();
        if (path._subPaths.size() == 0)
            return;
        
        _paths.emplace_back(path);
    }
    
    void maskPaths() {
        for (size_t i = 0; i < _paths.size(); i++) {
            Path& path = _paths[i];
            
            auto matches = _quadtree->find(path._llx, path._lly, path._urx, path._ury);
            
            for (auto j : matches)
//                if (_paths[j].intersects(path))
                _paths[j].maskOut(path);
            _quadtree->add(i, path._llx, path._lly, path._urx, path._ury);
        }
    }
    
    int triangulateDelaunayC(vector<Triangles>& vtriangles) {
        int count = 0;
        for (Path& p : _paths)
            for (SubPath& s : p._subPaths)
                count += s._npoints - 1;
        
        size_t pts_size = count + 3;
        auto points = shared_ptr<XYZ>((XYZ *)malloc(pts_size * sizeof(XYZ)), free);
        XYZ *dst = points.get();
        for (Path& p : _paths)
            for (SubPath& s : p._subPaths) {
                float *src = s._points.get(), *end = src + (s._npoints - 1) * 2;
                while (src != end)
                    dst->x = *src++, dst->y = *src++, dst++->i = (int)(&p - &_paths[0]);
            }
        
        std::vector<ITRIANGLE> triangles(pts_size * 4);
        
        int ntri = 0;
        int status = Triangulate(count, points.get(), &triangles[0], (int)triangles.size(), & ntri);
        
        if (status == 0) {
            triangles.resize(ntri);
            
            {
                _npoints = count;
                _points = shared_ptr<Vector>((Vector *)malloc(_npoints * sizeof(Vector)), free);
                
                XYZ *src = points.get(), *end = src + _npoints;
                Vector *dst = _points.get();
                while(src != end)
                    *dst++ = { src->x, src++->y };
            }
            
            {
                _ntriangles = ntri;
                _triangles = shared_ptr<Triangle>((Triangle *)malloc(_ntriangles * sizeof(Triangle)), free);
                
                Triangle *dst = _triangles.get();
                for(auto &t : triangles)
                    *dst++ = { t.p3, t.p2, t.p1, 0 };
            }
            
            {
                _ncolors = (int)_paths.size();
                _colors = shared_ptr<RGBA8>((RGBA8 *)malloc(_ncolors * sizeof(RGBA8)), free);
                
                RGBA8 *dst = _colors.get();
                for (Path& p : _paths)
                    *dst++ = p._color;
            }
            
            
            Triangles* triangles0 = & vtriangles[0];
            RGBA8 red(255, 0, 0, 255), white(255, 255, 255, 255);
            triangles0->colors[0] = red;
            RGBA8 color = red;
            
            int npoints = count;
            TESSindex polygon[kPolySize] = { TESS_UNDEF, TESS_UNDEF, TESS_UNDEF, TESS_UNDEF, TESS_UNDEF, TESS_UNDEF };
            TESSindex indices[pts_size];
            memset(indices, -1, npoints * sizeof(TESSindex));
            
            int ti = 0, k = 0, pi = 0, c = 0;
            XYZ *pts = points.get();
            int ic, i0, i1, i2;
            
            for(auto &t : triangles) {
                polygon[2] = t.p1, polygon[1] = t.p2, polygon[0] = t.p3;
                
//                i0 = pts[t.p1].i, i1 = pts[t.p2].i, i2 = pts[t.p3].i;
//                ic = -1;
//                if (i0 == i1 || i1 == i2)
//                    ic = i1;
//                if (i0 == i2)
//                    ic = i0;
                
//                color = ic < 0 ? white : _paths[ic]._color;
                
                if (!writePolygon(triangles0, (float *)pts, sizeof(XYZ), pi, k, c, color, polygon, indices)) {
                    vtriangles.resize(++ti + 1);
                    triangles0 = & vtriangles[ti];
                    k = 0, pi = 0, c = 0;
                    memset(indices, INT_MAX, npoints * sizeof(TESSindex));
                    
                    writePolygon(triangles0, (float *)pts, sizeof(XYZ), pi, k, c, color, polygon, indices);
                }
            }
        }
        
        return status;
    }
    
    int triangulate(vector<Triangles>& vtriangles) {
//        maskPaths();
        
        vtriangles.resize(1);
        
        return triangulateDelaunayC(vtriangles);
        
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
                    if (!writePolygon(triangles, points, kPointStride, pi, k, c, p._color, polygon, indices)) {
                        vtriangles.resize(++t + 1);
                        triangles = & vtriangles[t];
                        k = 0, pi = 0, c = 0;
                        memset(indices, INT_MAX, npoints * sizeof(TESSindex));

                        writePolygon(triangles, points, kPointStride, pi, k, c, p._color, polygon, indices);
                    }
                }
            }
        }
        tessDeleteTess(tess);
        
        int count = 0;
        for (Triangles& triangles : vtriangles)
            count += triangles.count;
        return count;
    }
    
    bool writePolygon(Triangles *triangles, float *points, int stride, int& pi, int& k, int& c, RGBA8 color, const TESSindex *polygon, TESSindex *indices) {
        if (c == kTrianglesColorsSize)
            return false;
        
        for (int j = 0; j < kPolySize && polygon[j] != TESS_UNDEF; j++) {
            TESSindex index = polygon[j];
            
            if (indices[index] == -1) {
                if (pi == kTrianglesPointsSize)
                    return false;
                indices[index] = pi;
                
                auto pt = (const float *)((char *)points + stride * index);
                triangles->points[pi++] = { pt[0], pt[1] };
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
    
    shared_ptr<RGBA8>                   _colors;
    int                                 _ncolors;
    shared_ptr<Vector>                  _points;
    int                                 _npoints;
    shared_ptr<Triangle>                _triangles;
    int                                 _ntriangles;
};
