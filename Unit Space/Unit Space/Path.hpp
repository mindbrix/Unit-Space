//
//  Path.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "SubPath.hpp"
#import "murmur3.h"

struct Path {
    Path() {}
    Path(bool evenOdd, RGBA8 color, vector<SubPath> subpaths) : _color(color), _evenOdd(evenOdd), _outerIndex(0), _subPaths(subpaths) {}
    Path(bool evenOdd, RGBA8 color) : _color(color), _evenOdd(evenOdd), _outerIndex(0) {}
    
    void addSubPath(SubPath& s) {
        if (s._npoints > 2) {
            if (_subPaths.size() == 0)
                _maxArea = 0;
            if (std::abs(s._area) > _maxArea)
                _maxArea = std::abs(s._area), _outerIndex = _subPaths.size();
            _bounds = _subPaths.size() ? _bounds.united(s._bounds) : s._bounds;
            _subPaths.emplace_back(s);
        }
    }
    
    bool contains(float x, float y) {
        if (!_bounds.contains(x, y))
            return false;
        int wn = 0;
        for (SubPath& subPath : _subPaths)
            wn += subPath.pointWinding(x, y);
        return _evenOdd ? wn & 1 : wn;
    }
    
    uint64_t hash() {
        int count = 0, i;
        for (SubPath& subPath : _subPaths)
            count += subPath._npoints;
        
        Memory<float> hashPoints(count * 2);
        float *hp = hashPoints.get();
        for (SubPath& s : _subPaths) {
            float *points = s._quadratics.get() ?: s._points.get();
            int npoints = s._quadratics.get() ? (int)s._quadratics.count() / 2 :  s._npoints;
            int stride = s._quadratics.get() ? 2 : 1;
            for (i = 0; i < npoints; i += stride)
                *hp++ = points[i * 2] - _bounds.lx, *hp++ = points[i * 2 + 1] - _bounds.ly;
        }
        uint32_t *ix, *iend;
        for (ix = (uint32_t *)hashPoints.get(), iend = ix + count * 2; ix < iend; ix++)
            *ix &= ~0xFFFF;
        
        return MurmurHash3_x64_64(hashPoints.get(), count * kPointStride, count);
    }
    RGBA8                       _color;
    bool                        _evenOdd;
    float                       _maxArea;
    size_t                      _outerIndex;
    vector<SubPath>             _subPaths;
    Bounds                      _bounds;
};
