//
//  Mesh.hpp
//  Unit Space
//
//  Created by Nigel Barber on 30/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
using namespace std;

#pragma clang diagnostic ignored "-Wcomma"

struct Mesh {
    vector<RGBA8>                       _colors;
    vector<Vector>                      _pts;
    vector<Vector>                      _points;
    vector<Triangle>                    _curves;
    vector<Triangle>                    _concaveCurves;
    vector<float>                       _interiorScales;
    vector<Vector>                      _interiorVectors;
    vector<Bounds>                      _bounds;
    vector<AffineTransform>             _pathCTMs;
    vector<int>                         _triangleIndices;
    
    void empty() {
        _colors.resize(0), _pts.resize(0), _points.resize(0), _curves.resize(0), _concaveCurves.resize(0), _interiorScales.resize(0), _interiorVectors.resize(0), _bounds.resize(0), _pathCTMs.resize(0), _triangleIndices.resize(0);
    }
};
