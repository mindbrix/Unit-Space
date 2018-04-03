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
    
    void empty() {
        _points = nullptr;
        _npoints = _area = 0;
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
    Path(bool evenOdd, RGBA color) {
        _color = color;
        _evenOdd = evenOdd;
        _outerIndex = INT_MAX;
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
    }
    
    void addContour(float *points, int npoints) {
        if (npoints > 2)
            tessAddContour(_tess.get(), 2, points, kPointStride, npoints, false);
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
        return _subPaths[_outerIndex].intersects(path._subPaths[path._outerIndex]);
    }
    
    void writeSubpaths() {
        if (tessTesselate(_tess.get(), _evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeSubpaths(_tess.get());
    }
    
    void writeSubpaths(TESStesselator *tess) {
        const float *verts = tessGetVertices(tess);
        const int *elems = tessGetElements(tess);
        const int nelems = tessGetElementCount(tess);
        
        for (int i = 0; i < nelems; i++) {
            int base = elems[i * 2], npoints = elems[i * 2 + 1];
            const float *points = verts + base * 2;
            
            if (npoints > 2)
                _subPaths.emplace_back(points, npoints);
        }
    }
    
    RGBA                        _color;
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
        
        float maskOuterArea = path._subPaths[path._outerIndex]._area;
        
        for (Path& p : _paths)
            if (path.intersects(p)) {
                float pOuterArea = p._subPaths[p._outerIndex]._area;
                for (SubPath& maskSubPath : path._subPaths) {
                    bool maskIsHole = maskSubPath._area * maskOuterArea < 0;
                    for (SubPath& subPath : p._subPaths) {
                        bool pIsHole = subPath._area * pOuterArea < 0;
                        
                        if (pIsHole)
                            continue;
                        if (subPath._points == nullptr)
                            continue;
                        if (maskIsHole && maskSubPath.contains(subPath))
                            continue;
                        else if (maskSubPath.intersects(subPath)) {
                            TESStesselator *tess = tessNewTess(NULL);
                            
                            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints, subPath._area < 0);
                            tessAddContour(tess, 2, maskSubPath._points.get(), kPointStride, maskSubPath._npoints, maskSubPath._area > 0);
                            
                            if (tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
                                subPath.empty();
                                p.writeSubpaths(tess);
                                p._outerIndex = p.findOuterIndex();
                            }
                            
                            tessDeleteTess(tess);
                        }
                    }
                }
            }
        
        _paths.emplace_back(path);
    }
    
    void triangulate(Triangles& triangles) {
        TESStesselator *tess = tessNewTess(NULL);
        
        int k = 0, pi = 0, c = 0;
        
        for (Path& p : _paths) {
            for (SubPath& s : p._subPaths)
                if (s._points)
                    tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints, false);
            
            if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                float *points = (float *)tessGetVertices(tess);
                int npoints = tessGetVertexCount(tess);
                const int nelems = tessGetElementCount(tess);
                const TESSindex *polygon = tessGetElements(tess);
                
                if (pi + npoints < kTrianglesPointsSize) {
                    memcpy(& triangles.points[pi], points, kPointStride * npoints);
                    
                    for (int i = 0; i < nelems; i++, polygon += kPolySize)
                        for (int j = 0; j < kPolySize - 2 && polygon[j + 2] != TESS_UNDEF; j++)
                            if (triangles.add(k, polygon[0] + pi, polygon[j + 1] + pi, polygon[j + 2] + pi, c % kTrianglesColorsSize))
                                k++;
                    
                    pi += npoints;
                    
                    triangles.colors[c++ % kTrianglesColorsSize] = p._color;
                }
            }
        }
        triangles.count = k;
        tessDeleteTess(tess);
    }
    
    vector<Path>             _paths;
};
