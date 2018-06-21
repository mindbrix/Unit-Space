//
//  SubPath.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Memory.hpp"

constexpr int kPointStride = 2 * sizeof(float);
constexpr float kSplitCubicPrecision = 0.1;

struct SubPath {
    enum Type {
        kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose,
        kPoints, kControlPoints
    };
    
    SubPath() {}
    SubPath(const float *points, const int npoints, Type type) {
        _closed = (points[0] == points[npoints * 2 - 2] && points[1] == points[npoints * 2 - 1]);
        
        if (type == kCubic) {
            _npoints = countSplitCubics(points, npoints, kSplitCubicPrecision);
            _points.alloc(_npoints * 2);
            _controlPoints.alloc(_npoints * 2);
            _npoints = splitCubics(_points.get(), _controlPoints.get(), points, npoints, kSplitCubicPrecision);
        } else {
            _npoints = npoints;
            _points.alloc(_npoints * 2);
            memcpy(_points.get(), points, npoints * kPointStride);
            if (_closed)
                _npoints--;
        }
        _area = pointsSignedArea(_points.get(), _points.get() + _npoints * 2, kPointStride);
        boundsLimits(& _bounds.lx, _points.get(), _points.get() + _npoints * 2, kPointStride);
    }
    Memory<float>               _points;
    Memory<float>               _controlPoints;
    bool                        _closed;
    int                         _npoints;
    float                       _area;
    Bounds                      _bounds;
};
