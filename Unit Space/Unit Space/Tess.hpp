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
        _npoints = npoints;
        _points = shared_ptr<float>((float *)malloc(npoints * kPointStride), free);
        memcpy(_points.get(), points, npoints * kPointStride);
        _area = area();
        bounds(& _llx);
    }
    
    void maskOut(SubPath &mask) {
        if (intersects(mask))
            ;
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
    Path(bool evenOdd) {
        _evenOdd = evenOdd;
        _outerIndex = INT_MAX;
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
    }
    
    void addContour(float *points, int npoints) {
        if (npoints > 2)
            tessAddContour(_tess.get(), 2, points, kPointStride, npoints, false);
    }
    
    void maskOut(Path &mask) {
        if (_outerIndex == INT_MAX)
            _outerIndex = findOuterIndex();
        
        for (SubPath& maskSubPath : mask._subPaths)
            for (SubPath& subPath : _subPaths)
                subPath.maskOut(maskSubPath);
    }
    
    size_t findOuterIndex() const {
        float maxArea = 0;
        size_t index = 0;
        
        for (size_t i = 0; i < _subPaths.size(); i++)
            if (std::abs(_subPaths[i]._area) > maxArea)
                maxArea = std::abs(_subPaths[i]._area), index = i;
        
        return index;
    }
    
    void writeSubpaths() {
        if (tessTesselate(_tess.get(), _evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
            const float *verts = tessGetVertices(_tess.get());
            const int *elems = tessGetElements(_tess.get());
            const int nelems = tessGetElementCount(_tess.get());
            
            for (int i = 0; i < nelems; i++) {
                int base = elems[i * 2], npoints = elems[i * 2 + 1];
                const float *points = verts + base * 2;
                
                if (npoints > 2)
                    _subPaths.emplace_back(points, npoints);
            }
        }
    }
    
    shared_ptr<TESStesselator>  _tess;
    bool                        _evenOdd;
    size_t                      _outerIndex;
    vector<SubPath>             _subPaths;
};

struct Mesh {
    void addPath(Path& path) {
        path.writeSubpaths();
        
        for (Path& p : _paths)
            p.maskOut(path);
        
        _paths.emplace_back(path);
    }
    void triangulate(Triangles& triangles) {
        TESStesselator *tess = tessNewTess(NULL);
        
        int k = 0, pi = 0;
        triangles.colors[0] = { 0, 0, 0, 0.333 };
        
        for (Path& p : _paths) {
            for (SubPath& s : p._subPaths)
                tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints, false);
            
            if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                float *points = (float *)tessGetVertices(tess);
                int npoints = tessGetVertexCount(tess);
                const int nelems = tessGetElementCount(tess);
                auto polygon = (TESSindex *)tessGetElements(tess);
                
                if (pi + npoints < kTrianglesPointsSize) {
                    memcpy(& triangles.points[pi], points, kPointStride * npoints);
                    
                    for (int i = 0; i < nelems; i++, polygon += kPolySize)
                        for (int j = 0; j < kPolySize - 2 && polygon[j + 2] != TESS_UNDEF; j++)
                            if (triangles.add(k, polygon[0] + pi, polygon[j + 1] + pi, polygon[j + 2] + pi, 0))
                                k++;
                    
                    pi += npoints;
                }
            }
        }
        triangles.count = k;
        tessDeleteTess(tess);
    }
    vector<Path>             _paths;
};
