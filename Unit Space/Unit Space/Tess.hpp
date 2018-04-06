//
//  Tess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 02/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "tesselator.h"
#import "Algorithms.hpp"


constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
constexpr int kPolySize = 6;
constexpr int kPointStride = 2 * sizeof(float);
constexpr float kCollinearTolerance = 1e-2;


struct SubPath {
    SubPath() {}
    SubPath(const float *points, const int npoints) {
        init(points, npoints);
    }
    
    void init(const float *points, const int npoints) {
        _npoints = npoints;
        _points = shared_ptr<float>((float *)malloc(npoints * kPointStride), free);
        memcpy(_points.get(), points, npoints * kPointStride);
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
    
    void addContour(float *points, int npoints) {
        if (npoints > 2)
            _subPaths.emplace_back(points, npoints);
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
//        if (_subPaths.size() == 1 && _subPaths[0]._npoints == 3 && path._subPaths.size() == 1 && path._subPaths[0]._npoints == 3)
//            return false;
        
        return _subPaths.size() && path._subPaths.size() && _subPaths[_outerIndex].intersects(path._subPaths[path._outerIndex]);
    }
    
    void maskOut(Path& mask) {
        TESStesselator *tess = tessNewTess(NULL);
        bool isPositive = _subPaths[_outerIndex]._area > 0;
        bool maskIsPositive = mask._subPaths[mask._outerIndex]._area > 0;
        
        for (SubPath& subPath : _subPaths)
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints, !isPositive);
        
        for (SubPath& subPath : mask._subPaths)
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints, maskIsPositive);
        
        if (tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
            writeSubpaths(tess);
            
        }
        tessDeleteTess(tess);
    }
    
    void writeSubpaths() {
        for (SubPath& subPath : _subPaths)
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
                _subPaths.emplace_back(points, npoints);
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
    void addPath(Path& path) {
        path.writeSubpaths();
        if (path._subPaths.size() == 0)
            return;
        
        path._outerIndex = path.findOuterIndex();
        
        for (Path& p : _paths)
            if (path.intersects(p))
                p.maskOut(path);

        _paths.emplace_back(path);
    }
    
    void triangulate(vector<Triangles>& vtriangles) {
        TESStesselator *tess = tessNewTess(NULL);
        
        int t = 0, k = 0, pi = 0, c = 0;
        Triangles* triangles = & vtriangles[t];
        
        for (Path& p : _paths) {
            for (SubPath& s : p._subPaths)
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
    
    vector<Path>             _paths;
};
