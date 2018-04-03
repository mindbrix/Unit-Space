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
constexpr float kCollinearTolerance = 1e-2;


struct SubPath {
    SubPath(const float *points, const int npoints) {
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
        _area = pointsSignedArea(points, points + npoints * 2, sizeof(float) * 2);
        boundsLimits(& _llx, points, points + npoints * 2, sizeof(float) * 2);
        tessAddContour(_tess.get(), 2, points, sizeof(float) * 2, npoints, false);
    }
    
    void maskOut(SubPath &mask) {
        if ((_urx > mask._llx) && (mask._urx > _llx) && (_ury > mask._lly) && (mask._ury > _lly))
            ;
    }
    shared_ptr<TESStesselator>  _tess;
    float                       _area;
    float                       _llx, _lly, _urx, _ury;
};

struct Path {
    Path() {
        _outerIndex = INT_MAX;
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
    }
    
    void addContour(float *points, int npoints) {
        if (npoints > 2)
            tessAddContour(_tess.get(), 2, points, sizeof(float) * 2, npoints, false);
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
    
    void writeSubpaths(bool evenOdd) {
        if (tessTesselate(_tess.get(), evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
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
    size_t                      _outerIndex;
    vector<SubPath>             _subPaths;
};

struct Mesh {
    void addPath(Path& path) {
        for (Path& p : _paths)
            p.maskOut(path);
        
        _paths.emplace_back(path);
    }
    void triangulate(Triangles& triangles) {
        
    }
    vector<Path>             _paths;
};
