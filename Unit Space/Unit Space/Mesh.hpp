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
    vector<Vector>                      _points;
    vector<Triangle>                    _triangles;
    vector<Triangle>                    _curves;
    vector<Triangle>                    _concaveCurves;
    vector<float>                       _interiorScales;
    vector<Vector>                      _interiorVectors;
    vector<AffineTransform>             _pathCTMs;
    vector<int>                         _triangleIndices;
    
    void empty() {
        _colors.resize(0), _points.resize(0), _triangles.resize(0), _curves.resize(0), _concaveCurves.resize(0), _interiorScales.resize(0), _interiorVectors.resize(0), _pathCTMs.resize(0), _triangleIndices.resize(0);
    }
    void writeQuad(float x0, float y0, float x1, float y1,float x2, float y2, float x3, float y3, int p) {
        int pi = (int)_points.size();
        _points.emplace_back(x0, y0), _points.emplace_back(x1, y1), _points.emplace_back(x2, y2), _points.emplace_back(x3, y3);
        _triangles.emplace_back(pi + 0, pi + 1, pi + 2, p), _triangles.emplace_back(pi + 0, pi + 2, pi + 3, p);
    }
    void writeRect(float x0, float y0, float x1, float y1, int p) {
        writeQuad(x0, y0, x1, y0, x1, y1, x0, y1, p);
    }
    void writeTriangle(float x0, float y0, float x1, float y1,float x2, float y2, int p) {
        int pi = (int)_points.size();
        _points.emplace_back(x0, y0), _points.emplace_back(x1, y1), _points.emplace_back(x2, y2);
        _triangles.emplace_back(pi + 0, pi + 1, pi + 2, p);
    }
};
