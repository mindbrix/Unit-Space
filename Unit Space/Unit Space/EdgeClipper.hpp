//
//  EdgeClipper.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

using namespace std;

// Sutherland-Hodgman polygon clipping
// http://www.cc.gatech.edu/grads/h/Hao-wei.Hsieh/Haowei.Hsieh/code2.html
//
struct EdgeClipper {
    typedef vector<Vector> Element;
    enum Type { LLX, LLY, URX, URY };
    
    Vector  _first, _p0;
    float   _limit;
    Type    _type;
    Element _in, _out;
    
    EdgeClipper() {}
    
    void init(Type type, float limit) {
        _p0 = { FLT_MAX, FLT_MAX };
        _type = type;
        _limit = limit;
    }
    
    Element* clip(Vector *p, size_t count) {
        _in.resize(0);
        if (p == nullptr)
            _in.emplace_back(FLT_MAX, FLT_MAX);
        else {
            _in.resize(count);
            memcpy(& _in[0], p, count * sizeof(*p));
        }
        return clip(& _in);
    }
    
    Element* clip(Element* in) {
        _out.resize(0);
        for (auto& p1: *in) {
            if (_p0.x == FLT_MAX)
                _first = p1;
            else if (p1.x == FLT_MAX) {
                clip(_p0, _first);
                _out.emplace_back(p1);
            } else
                clip(_p0, p1);
            
            _p0 = p1;
        }
        return &_out;
    }
    
    inline void clip(Vector p0, Vector p1) {
        if (inside(p1)) {
            if (inside(p0))
                _out.emplace_back(p1);
            else {
                _out.emplace_back(intersect(p0, p1));
                _out.emplace_back(p1);
            }
        } else {
            if (inside(p0))
                _out.emplace_back(intersect(p0, p1));
        }
    }
    
    inline bool inside(const Vector p) {
        switch(_type) {
            case LLX:
                return p.x >= _limit;
            case LLY:
                return p.y >= _limit;
            case URX:
                return p.x <= _limit;
            case URY:
                return p.y <= _limit;
        }
    }
    
    inline Vector intersect(const Vector p0, const Vector p1) {
        float dx = p1.x - p0.x, dy = p1.y - p0.y;
        switch(_type) {
            case LLX:
            case URX: {
                return { _limit, (_limit - p0.x) * dy / dx + p0.y };
            }
            case LLY:
            case URY: {
                return { (_limit - p0.y) * dx / dy + p0.x, _limit };
            }
        }
    }
};
