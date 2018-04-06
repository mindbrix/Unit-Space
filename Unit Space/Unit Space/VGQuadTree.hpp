//
//  VGQuadTree.hpp
//  VectorGL
//
//  Created by Nigel Barber on 02/09/2016.
//
//

namespace VG {
    template<typename T>
    struct QuadTree {
        inline QuadTree(float llx, float lly, float urx, float ury) : _bounds(llx, lly, urx, ury), _midx((llx + urx) * 0.5f), _midy((lly + ury) * 0.5f) {}
        ~QuadTree() {
            if (_stack)
                free(_stack), delete _matches;
        }
        
        void add(T ref, float llx, float lly, float urx, float ury, int level = 0) {
            int qll = quadrant(llx, lly), qur = quadrant(urx, ury);
            if (level == 3 || qll != qur)
                _pairs.emplace_back(ref, Bounds(llx, lly, urx, ury));
            else {
                if (_quads[qll] == nullptr) {
                    float corner_x = qll & 1 ? _bounds._urx : _bounds._llx, corner_y = qll & 2 ? _bounds._ury : _bounds._lly;
                    _quads[qll] = unique_ptr<QuadTree>(new QuadTree(_midx, _midy, corner_x, corner_y));
                }
                _quads[qll]->add(ref, llx, lly, urx, ury, level + 1);
            }
        }
        
        vector<T>& find(float llx, float lly, float urx, float ury) {
            Bounds search(llx, lly, urx, ury);
            
            if (_stack == nullptr)
                _stack = (QuadTree **)malloc(256 * sizeof(QuadTree *)), _matches = new vector<T>();
            _matches->resize(0);
            auto start = _stack, end = start;
            *end++ = this;
            
            while (start != end--) {
                QuadTree *tree = *end;
                for (int i = 0; i < 4; i++) {
                    auto quad = tree->_quads[i].get();
                    if (quad && search.intersects(quad->_bounds))
                        *end++ = quad;
                }
                for (auto& pair : tree->_pairs)
                    if (search.intersects(pair.second))
                        _matches->emplace_back(pair.first);
            }
            return *_matches;
        }
        
    private:
        struct Bounds {
            inline Bounds(float llx, float lly, float urx, float ury) : _llx(llx), _lly(lly), _urx(urx), _ury(ury) {
                if (_llx > _urx)  std::swap(_llx, _urx);
                if (_lly > _ury)  std::swap(_lly, _ury);
            }
            inline bool intersects(const Bounds& other)  {
                return _urx > other._llx && other._urx > _llx && _ury > other._lly && other._ury > _lly;
            }
            float _llx, _lly, _urx, _ury;
        };
        inline int quadrant(float x, float y)   { return ((y >= _midy) << 1) + (x >= _midx); }
        
        Bounds                      _bounds;
        float                       _midx, _midy;
        unique_ptr<QuadTree>        _quads[4];
        vector<pair<T, Bounds>>   _pairs;
        QuadTree                    **_stack = nullptr;
        vector<T>                 *_matches;
    };
}
