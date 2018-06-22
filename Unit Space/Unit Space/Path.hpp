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
    Path(bool evenOdd, RGBA8 color, vector<SubPath> subpaths) : _color(color), _evenOdd(evenOdd), _outerIndex(0), _subPaths(subpaths) {}
    Path(bool evenOdd, RGBA8 color) : _color(color), _evenOdd(evenOdd), _outerIndex(0) {}
    
    void addSubPath(SubPath& s) {
        if (s._npoints > 2)
            _subPaths.emplace_back(s);
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
