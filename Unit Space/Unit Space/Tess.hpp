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
    SubPath() {}
    SubPath(const float *points, const int npoints) {
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
        _area = pointsSignedArea(points, points + npoints * 2, sizeof(float) * 2);
        tessAddContour(_tess.get(), 2, points, sizeof(float) * 2, npoints, false);
    }
    
    shared_ptr<TESStesselator>  _tess;
    float                       _area;
};

struct Path {
    Path() {
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
    }
    
    void addContour(float *points, int npoints) {
        tessAddContour(_tess.get(), 2, points, sizeof(float) * 2, npoints, false);
    }
    
    void writeSubpaths(bool evenOdd) {
        if (tessTesselate(_tess.get(), evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
            const float *verts = tessGetVertices(_tess.get());
            const int *elems = tessGetElements(_tess.get());
            const int nelems = tessGetElementCount(_tess.get());
            
            for (int i = 0; i < nelems; i++) {
                int base = elems[i * 2], npoints = elems[i * 2 + 1];
                const float *points = verts + base * 2;
                
                _subPaths.emplace_back(points, npoints);
            }
        }
    }
    
    shared_ptr<TESStesselator>  _tess;
    vector<SubPath>             _subPaths;
};

struct Mesh {
    vector<Path>             _paths;
};
