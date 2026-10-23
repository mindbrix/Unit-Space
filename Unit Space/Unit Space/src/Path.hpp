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
    Path(bool closed, bool evenOdd, vector<SubPath> subpaths) : _closed(closed), _evenOdd(evenOdd) { for (SubPath& s : subpaths) addSubPath(s); }
    Path(bool closed, bool evenOdd) : _closed(closed), _evenOdd(evenOdd) {}
    
    void addSubPath(SubPath& s) {
        if (s._quadratics.count() / 2 > 2) {
            if (_subPaths.size() == 0)
                _bounds = s._bounds;
            _bounds.unite(s._bounds);
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
    
    void controlPointForIndex(float *p, int idx, int stride = 2) {
        int begin = 0, end, i;
        for (SubPath& s : _subPaths) {
            end = begin + (int)s._quadratics.count() / 2;
            if (idx < end) {
                i = (idx - begin) * 2;
                if (idx == end - 1) {
                    p[0] = (s._quadratics[i] + s._quadratics[0]) * 0.5, p[1] = (s._quadratics[i + 1] + s._quadratics[1]) * 0.5;
                } else
                    p[0] = s._quadratics[i + 2], p[1] = s._quadratics[i + 3];
                break;
            }
            begin = (end + stride - 1) / stride * stride;
        }
    }
    bool                        _closed, _evenOdd;
    bool                        _outline = false;   // a stroke expanded into a filled outline, which crosses itself
    float                       _penWidth = 0;      // an outline's stroke width
    Bounds                      _bounds;
    vector<SubPath>             _subPaths;
};
