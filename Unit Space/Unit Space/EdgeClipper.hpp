//
//  EdgeClipper.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

// Sutherland-Hodgman polygon clipping
// http://www.cc.gatech.edu/grads/h/Hao-wei.Hsieh/Haowei.Hsieh/code2.html
//
struct EdgeClipper {
    typedef std::vector<Vector> Element;
    enum Type { LLX, LLY, URX, URY };
    
    float   _limit;
    Type    _type;
    Element _in, _out;
    
    EdgeClipper() {}
    
    Element* make(Vector *p, size_t count) {
        _in.resize(count);
        memcpy(& _in[0], p, count * sizeof(*p));
        return & _in;
    }
    
    Element* clip(Element* in, Type type, float limit) {
        _out.resize(0);
        _type = type;
        _limit = limit;
        if (in->size() > 2)
            for (int i = 0, j = (int)in->size() - 1; i < in->size(); j = i, i++)
                clip((*in)[j], (*in)[i]);
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
        return _type == LLX ? p.x >= _limit : _type == LLY ? p.y >= _limit : _type == URX ? p.x <= _limit : p.y <= _limit;
    }
    
    inline Vector intersect(const Vector p0, const Vector p1) {
        if (_type == LLX || _type == URX)
            return { _limit, (_limit - p0.x) / (p1.x - p0.x) * (p1.y - p0.y) + p0.y };
        else
            return { (_limit - p0.y) / (p1.y - p0.y) * (p1.x - p0.x) + p0.x, _limit };
    }
};
