//
//  Path.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "SubPath.hpp"

struct Path {
    Path() {}
    Path(bool evenOdd, RGBA8 color, vector<SubPath> subpaths) : _color(color), _evenOdd(evenOdd) { for (SubPath& s : subpaths) addSubPath(s); }
    Path(bool evenOdd, RGBA8 color) : _color(color), _evenOdd(evenOdd) {}
    
    void addSubPath(SubPath& s) {
        if (s._quadratics.count() / 2 > 2) {
            if (_subPaths.size() == 0)
                _bounds = s._bounds, _maxArea = -FLT_MAX;
            _bounds.unite(s._bounds);
            if (std::abs(s._area) > _maxArea)
                _maxArea = std::abs(s._area), _outerIndex = _subPaths.size();
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
    
    RGBA8                       _color;
    bool                        _evenOdd;
    float                       _maxArea;
    size_t                      _outerIndex;
    vector<SubPath>             _subPaths;
    Bounds                      _bounds;
};
