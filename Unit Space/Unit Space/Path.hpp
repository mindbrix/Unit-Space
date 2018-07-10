//
//  Path.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "SubPath.hpp"
#import "murmur3.h"

static int quadraticDivisions(float x0, float y0, float x1, float y1, float x2, float y2) {
    constexpr float kHeight = 0.2;
    float bvx, bvy, cpx, cpy, area, unit;
    bool left, in0, in1;
    bvx = x2 - x0, bvy = y2 - y0;
    left = bvx * (y1 - y0) - bvy * (x1 - x0) > 0;
    cpx = (x0 + x2) * 0.5 + kHeight * bvy * (left ? -1 : 1);
    cpy = (y0 + y2) * 0.5 + kHeight * bvx * (left ? 1 : -1);
    in0 = ((cpx - x0) * (y1 - y0) - (cpy - y0) * (x1 - x0)) * (left ? -1 : 1) > 0;
    in1 = ((x2 - cpx) * (y1 - cpy) - (y2 - cpy) * (x1 - cpx)) * (left ? -1 : 1) > 0;
    area = (x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1);
    unit = bvx * (cpy - y0) - bvy * (cpx - x0);
    return in0 && in1 ? 0 : std::ceil(std::max(0.f, log2f(std::abs(area / unit))));
}

struct Path {
    Path() {}
    Path(bool evenOdd, RGBA8 color, vector<SubPath> subpaths) : _color(color), _evenOdd(evenOdd), _outerIndex(0), _subPaths(subpaths) {}
    Path(bool evenOdd, RGBA8 color) : _color(color), _evenOdd(evenOdd), _outerIndex(0) {}
    
    void addSubPath(SubPath& s) {
        if (s._npoints > 2)
            _subPaths.emplace_back(s);
    }
    
    bool contains(float x, float y) {
        if (!_bounds.contains(x, y))
            return false;
        int wn = 0;
        for (SubPath& subPath : _subPaths)
            wn += subPath.pointWinding(x, y);
        return _evenOdd ? wn & 1 : wn;
    }
    
    bool intersects(Path& path) const {
        return _subPaths.size() && path._subPaths.size() && _bounds.intersects(path._bounds);
    }
    
    int countPoints() {
        int count = 0;
        for (SubPath& s : _subPaths)
            count += s._npoints;
        return count;
    }
    
    uint64_t hash() {
        int count = 0, i;
        for (SubPath& subPath : _subPaths)
            count += subPath._npoints;
        
        Memory<float> hashPoints(count * 2);
        float *hp = hashPoints.get(), *points;
        for (SubPath& subPath : _subPaths)
            for (points = subPath._points.get(), i = 0; i < subPath._npoints; i++)
                *hp++ = *points++ - _bounds.lx, *hp++ = *points++ - _bounds.ly;
        
        uint32_t *ix, *iend;
        for (ix = (uint32_t *)hashPoints.get(), iend = ix + count * 2; ix < iend; ix++)
            *ix &= ~0xFFFF;
        
        return MurmurHash3_x64_64(hashPoints.get(), count * kPointStride, count);
    }
    void update() {
        float area, maxArea = 0;
        _outerIndex = 0;
        for (size_t i = 0; i < _subPaths.size(); i++)
            if ((area = std::abs(_subPaths[i]._area)) > maxArea)
                maxArea = area, _outerIndex = i;
        
        _bounds = _subPaths[0]._bounds;
        for (SubPath& subPath : _subPaths)
            _bounds.unite(subPath._bounds);
    }
    RGBA8                       _color;
    bool                        _evenOdd;
    size_t                      _outerIndex;
    vector<SubPath>             _subPaths;
    Bounds                      _bounds;
};
