//
//  Tess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 02/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
#import "tesselator.h"
#import "Algorithms.hpp"
#import "VGQuadTree.hpp"
#import "delaunay.h"

constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
constexpr int kPolySize = 3;
constexpr int kPointStride = 2 * sizeof(float);
constexpr float kSplitCubicPrecision = 0.1;

#pragma mark - Data structures

struct Edge {
    enum Flags {
        kFlippedX = 1 << 0,
        kFlippedY = 1 << 1
    };
    
    Edge(float lx, float ly, float ux, float uy, int ci, int pi) : lx(lx), ly(ly), ux(ux), uy(uy), flags(0), p(0), ci((unsigned short int)ci), pi((unsigned short int)pi) {
        if (lx > ux)
            swap(this->lx, this->ux), flags |= kFlippedX;
        if (ly > uy)
            swap(this->ly, this->uy), flags |= kFlippedY;
    }
    
    float x0() { return  flags & kFlippedX ? ux : lx; }
    float y0() { return  flags & kFlippedY ? uy : ly; }
    float x1() { return ~flags & kFlippedX ? ux : lx; }
    float y1() { return ~flags & kFlippedY ? uy : ly; }
    
    float lx, ly, ux, uy;
    unsigned short int flags, p, ci, pi;
    inline bool operator< (const Edge& other) const     { return lx < other.lx || (lx == other.lx && ly < other.ly); }
};

struct Intersection {
    Intersection(Edge e0, Edge e1) : e0(e0), e1(e1) {}
    Edge e0, e1;
};

template<typename T>
struct Memory {
private:
    struct Header { int refCount; void *memory; size_t size; };
    Header *header = nullptr;
public:
    Memory()                            {}
    Memory(size_t size)                 { alloc(size); }
    Memory(const Memory& other)         { assign(other); }
    Memory& operator= (Memory other)    { assign(other); return *this; }
    ~Memory() {
        if (header && (--(header->refCount) == 0))
            free(header->memory), free(header);
    }
    void alloc(size_t size) {
        if (header)
            header->memory = ::realloc(header->memory, size);
        else {
            header = (Header *)malloc(sizeof(Header));
            header->memory = malloc(size);
            header->refCount = 1;
        }
        header->size = size;
    }
    void assign(const Memory& other) {
        if (this != & other) {
            this->~Memory();
            if ((header = other.header))
                header->refCount++;
        }
    }
    Memory clone() {
        if (header) {
            Memory c(header->size);
            memcpy(c.header->memory, header->memory, header->size);
            return c;
        }
        return Memory();
    }
    T *get() const { return (T *) (header ? header->memory : nullptr); }
};

#pragma mark - Algorithms

// Source: http://caffeineowl.com/graphics/2d/vectorial/cubic2quad01.html
//
static int countSplitCubics(const float *src, const int npoints, const float prec) {
    float x0, y0, x1, y1, x2, y2, x3, y3;
    float xD01, yD01, D01, tcubed;
    int count = 0, calculated;
    
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
    
    for (int i = 0; i < npoints - 1; i += 3) {
        const float *p = src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
        xD01 = x3 + 3.f * (x1 - x2) - x0, yD01 = y3 + 3.f * (y1 - y2) - y0;
        D01 = std::sqrt(xD01 * xD01 + yD01 * yD01);
        tcubed = D01 == 0 ? 1 : prec * multiplier / D01;
        calculated = tcubed >= 1 ? 1 : std::ceil(1.0 / cbrt(tcubed));
        count += calculated;
    }
    return count + 1;
}

static void splitCubics(float *dst, float *cpdst, const float *src, const int npoints, const float prec) {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, cpx, cpy;
    float xD01, yD01, D01, tcubed, t;
    float n01, n12, n23;
    float n012, n123;
    float n0123;
    
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
    
    for (int i = 0; i < npoints - 1; i += 3) {
        const float *p = src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
        do {
            *dst++ = x0, *dst++ = y0;
            
            xD01 = x3 + 3.f * (x1 - x2) - x0, yD01 = y3 + 3.f * (y1 - y2) - y0;
            D01 = std::sqrt(xD01 * xD01 + yD01 * yD01);
            tcubed = D01 == 0 ? 1 : prec * multiplier / D01;
            
            if (tcubed < 0.999999) {
                t = cbrt(tcubed);
                
                n01 = lerp(t, x0, x1), n12 = lerp(t, x1, x2), n23 = lerp(t, x2, x3);
                n012 = lerp(t, n01, n12), n123 = lerp(t, n12, n23);
                n0123 = lerp(t, n012, n123);
                cpx = (3.0 * (n012 + n01) - x0 - n0123) * 0.25;
                x0 = n0123, x1 = n123, x2 = n23;
                
                n01 = lerp(t, y0, y1), n12 = lerp(t, y1, y2), n23 = lerp(t, y2, y3);
                n012 = lerp(t, n01, n12), n123 = lerp(t, n12, n23);
                n0123 = lerp(t, n012, n123);
                cpy = (3.0 * (n012 + n01) - y0 - n0123) * 0.25;
                y0 = n0123, y1 = n123, y2 = n23;
                
                *cpdst++ = cpx, *cpdst++ = cpy;
            }
        } while (tcubed < 0.999999);
        
        *cpdst++ = (3.0 * (x2 + x1) - x0 - x3) * 0.25, *cpdst++ = (3.0 * (y2 + y1) - y0 - y3) * 0.25;
    }
    *dst++ = x3, *dst++ = y3;
}

static vector<Intersection> findIntersections(Edge *begin, Edge *end) {
    vector<Intersection> intersections;
    Edge *edge, *last, *src, *dst, *e0, *e1;
    float lx;
    
    sort(begin, end);
    for (edge = begin; edge < end; ) {
        last = edge;
        lx = edge->lx;
        
        do
            edge++;
        while (edge < end && edge->lx == lx);
        
        src = dst = last;
        while (--src >= begin)
            if (src->ux > lx)
                *(--dst) = *src;
        begin = dst;
        
        for (e0 = last; e0 < edge; e0++)
            for (e1 = begin; e1 < edge; e1++)
                if (e0 != e1 && e0->uy > e1->ly && e1->uy > e0->ly && e0->ux > e1->lx && e1->ux > e0->lx)
                    if (linesIntersection(e0->x0(), e0->y0(), e0->x1(), e0->y1(), e1->x0(), e1->y0(), e1->x1(), e1->y1(), nullptr, nullptr)) {
                        bool duplicate = false;
                        for (auto& intersection : intersections)
                            if (intersection.e0.ci == e1->ci && intersection.e0.pi == e1->pi &&
                                intersection.e1.ci == e0->ci && intersection.e1.pi == e0->pi) {
                                duplicate = true;
                                break;
                            }
                        if (!duplicate)
                            intersections.emplace_back(*e0, *e1);
                    }
    }
    return intersections;
}

struct SubPath {
    enum Type {
        kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose,
        kPoints, kControlPoints
    };
    
    SubPath() {}
    SubPath(const float *points, const int npoints, Type type) {
        bool closed = (points[0] == points[npoints * 2 - 2] && points[1] == points[npoints * 2 - 1]);
        
        if (type == kCubic) {
            _npoints = countSplitCubics(points, npoints, kSplitCubicPrecision);
            _points.alloc(_npoints * kPointStride);
            _controlPoints.alloc(_npoints * kPointStride);
            splitCubics(_points.get(), _controlPoints.get(), points, npoints, kSplitCubicPrecision);
        } else {
            _npoints = npoints;
            _points.alloc(_npoints * kPointStride);
            memcpy(_points.get(), points, npoints * kPointStride);
            if (closed)
                _npoints--;
        }
        update();
    }
    
    SubPath clone() {
        SubPath c = *this;
        _points = _points.clone();
        _controlPoints = _controlPoints.clone();
        return c;
    }
    
    int index(Type type, float px, float py) {
        float *points, *src, *end;
        if ((points = (type == kPoints ? _points : _controlPoints).get()))
            for (src = points, end = & points[_npoints * 2]; src < end; src += 2)
                if (src[0] == px && src[1] == py)
                    return (int)(src - points) / 2;
        return INT_MAX;
    }
    void insert(Type type, int pi, float px, float py) {
        if (type == kPoints)
            _npoints++;
        Memory<float>& memory = type == kPoints ? _points : _controlPoints;
        memory.alloc(_npoints * kPointStride);
        float *points = memory.get(), *dst, *src, *end;
        dst = & points[(_npoints - 1) * 2], src = dst - 2, end = & points[pi * 2];
        while (src > end)
            *dst-- = *src--, *dst-- = *src--;
        points[pi * 2] = px, points[pi * 2 + 1] = py;
    }
    void replace(Type type, int pi, float px, float py) {
        float *points = (type == kPoints ? _points : _controlPoints).get();
        points[pi * 2] = px, points[pi * 2 + 1] = py;
    }
    void splice(Type type, int di, int si, float px, float py) {
        float *points, *dst, *src, *end;
        if ((points = (type == kPoints ? _points : _controlPoints).get()) == nullptr)
            return;
        
        di = di % _npoints, si = si % _npoints;
        points[si * 2] = px, points[si * 2 + 1] = py;
        
        if (di > si)
            dst = points, src = & points[si * 2], end = & points[di * 2];
        else
            dst = & points[di * 2], src = & points[si * 2], end = & points[_npoints * 2];
        
        if (type == kPoints)
            _npoints = int(dst + (end - src) - points) / 2;

        while (src < end)
            *dst++ = *src++, *dst++ = *src++;
    }
    void update() {
        _area = pointsSignedArea(_points.get(), _points.get() + _npoints * 2, kPointStride);
        boundsLimits(& _bounds.lx, _points.get(), _points.get() + _npoints * 2, kPointStride);
    }
    Memory<float>               _points;
    Memory<float>               _controlPoints;
    int                         _npoints;
    float                       _area;
    Bounds                      _bounds;
};

struct Path {
    Path() {}
    Path(bool evenOdd, RGBA8 color) : _color(color), _evenOdd(evenOdd), _outerIndex(0) {}
    
    void addContour(float *points, int npoints, SubPath::Type type) {
        if (npoints > 2) {
            _subPaths.emplace_back(points, npoints, type);
            
            if (_subPaths.back()._npoints < 3)
                _subPaths.pop_back();
        }
    }
    
    bool intersects(Path& path) const {
        return _subPaths.size() && path._subPaths.size() && _bounds.intersects(path._bounds);
    }
    
    void maskOut(TESStesselator *tess, Path& mask) {
        bool isPositive = _subPaths[_outerIndex]._area > 0;
        bool maskIsPositive = mask._subPaths[mask._outerIndex]._area > 0;
        
        for (SubPath& subPath : _subPaths) {
            tessSetOption(tess, TESS_REVERSE_CONTOURS, !isPositive);
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        }
        
        for (SubPath& subPath : mask._subPaths) {
            tessSetOption(tess, TESS_REVERSE_CONTOURS, maskIsPositive);
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        }
        
        if (tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess);
    }
    
    int countPoints() {
        int count = 0;
        for (SubPath& s : _subPaths)
            count += s._npoints;
        return count;
    }
    Edge *writeEdges(Edge *dst) {
        float lx, ly, ux, uy, *points;
        for (int ci = 0; ci < _subPaths.size(); ci++) {
            points = _subPaths[ci]._points.get();
            for (int pi = 0; pi < _subPaths[ci]._npoints; pi++) {
                lx = points[pi * 2], ly = points[pi * 2 + 1];
                ux = points[((pi + 1) % _subPaths[ci]._npoints) * 2], uy = points[((pi + 1) % _subPaths[ci]._npoints) * 2 + 1];
                new (dst++) Edge(lx, ly, ux, uy, ci, pi);
            }
        }
        return dst;
    }
    
    
    void resolveSelfIntersections(TESStesselator *tess) {
        if (_subPaths.size() == 1 && _subPaths[0]._npoints == 3) {
            update();
            return;
        }
        for (SubPath& subPath : _subPaths)
            if (subPath._npoints > 2)
                tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        
        if (tessTesselate(tess, _evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess);
            
    }
    void resolveSelfIntersections() {
        int count = countPoints();
        Edge *edges = (Edge *)alloca(count * sizeof(Edge)), *begin = & edges[0], *end = begin;
        end = writeEdges(end);
        
        auto intersections = findIntersections(begin, end);
        float *points, *controlPoints, px, py, x1, y1;
        float x01, x12, x123, y01, y12, y123;
        int ce0, ce1;
        for (int j = 0; j < intersections.size(); j++) {
            Edge& e0 = intersections[j].e0, & e1 = intersections[j].e1;
            if (e0.ci == e1.ci) {
                ce0 = 0, ce1 = 0;
                for (int k = j + 1; k < intersections.size(); k++) {
                    Intersection& in = intersections[k];
                    if (e0.ci == in.e0.ci && in.e0.ci == in.e1.ci) {
                        if (e0.pi == in.e0.pi || e0.pi == in.e1.pi)
                            ce0++;
                        if (e1.pi == in.e0.pi || e1.pi == in.e1.pi)
                            ce1++;
                    }
                }
                
                points = _subPaths[e0.ci]._points.get();
                controlPoints = _subPaths[e0.ci]._controlPoints.get();
                linesIntersection(e0.x0(), e0.y0(), e0.x1(), e0.y1(), e1.x0(), e1.y0(), e1.x1(), e1.y1(), & px, & py);
                
                if ((ce0 || ce1) && controlPoints != nullptr) {
                    Edge& e = ce0 > ce1 ? e0 : e1;
                    
                    x1 = controlPoints[e.pi * 2], y1 = controlPoints[e.pi * 2 + 1];
                    x01 = (e.x0() + x1) * 0.5, x12 = (x1 + e.x1()) * 0.5, x123 = (x01 + x12) * 0.5;
                    y01 = (e.y0() + y1) * 0.5, y12 = (y1 + e.y1()) * 0.5, y123 = (y01 + y12) * 0.5;
                    _subPaths[e.ci].insert(SubPath::kPoints, e.pi + 1, x123, y123);
                    _subPaths[e.ci].replace(SubPath::kControlPoints, e.pi, x01, y01);
                    _subPaths[e.ci].insert(SubPath::kControlPoints, e.pi + 1, x12, y12);
                } else {
                
                    _subPaths.emplace_back(_subPaths[e0.ci].clone());
                    _subPaths[e0.ci].splice(SubPath::kControlPoints, e1.pi + 1, e0.pi, px, py);
                    _subPaths[e0.ci].splice(SubPath::kPoints, e1.pi + 1, e0.pi, px, py);
                
                    _subPaths.back().splice(SubPath::kControlPoints, e0.pi + 1, e1.pi, px, py);
                    _subPaths.back().splice(SubPath::kPoints, e0.pi + 1, e1.pi, px, py);
                    
                    int ci, nci = (int)_subPaths.size() - 1, pi;
                    for (int k = j + 1; k < intersections.size(); k++) {
                        Edge& ie0 = intersections[k].e0, & ie1 = intersections[k].e1;
                        if (e0.ci == ie0.ci && ie0.ci == ie1.ci) {
                            ci = e0.ci;
                            if ((pi = _subPaths[ci].index(SubPath::kPoints, ie0.x0(), ie0.y0())) == INT_MAX)
                                ci = nci, pi = _subPaths[ci].index(SubPath::kPoints, ie0.x0(), ie0.y0());
                            
                            assert(pi != INT_MAX);
                            if (pi != INT_MAX)
                                ie0.ci = ci, ie0.pi = pi;
                            
                            ci = e0.ci;
                            if ((pi = _subPaths[ci].index(SubPath::kPoints, ie1.x0(), ie1.y0())) == INT_MAX)
                                ci = nci, pi = _subPaths[ci].index(SubPath::kPoints, ie1.x0(), ie1.y0());
                            
                            assert(pi != INT_MAX);
                            if (pi != INT_MAX)
                                ie1.ci = ci, ie1.pi = pi;
                        }
                    }
                }
            }
        }
        update();
    }
    
    void writeTessSubpaths(TESStesselator *tess) {
        _subPaths.resize(0);
        
        const float *verts = tessGetVertices(tess);
        const int *elems = tessGetElements(tess);
        const int nelems = tessGetElementCount(tess);
        
        for (int i = 0; i < nelems; i++) {
            int base = elems[i * 2], npoints = elems[i * 2 + 1];
            const float *points = verts + base * 2;
            
            if (npoints > 2)
                _subPaths.emplace_back(points, npoints, SubPath::kLine);
        }
        update();
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

struct Mesh {
    vector<RGBA8>                       _colors;
    vector<Vector>                      _points;
    vector<Triangle>                    _triangles;
    vector<Triangle>                    _curves;
    vector<Triangle>                    _concaveCurves;
    
    void empty() {
        _colors.resize(0), _points.resize(0), _triangles.resize(0), _curves.resize(0), _concaveCurves.resize(0);
    }
};

struct Tess {
    Tess() {}
    Tess(float w, float h) {
        reset(w, h);
    }
    
    void reset(float w, float h) {
        _tess = shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess);
        _quadtree = shared_ptr<VG::QuadTree<size_t>>(new VG::QuadTree<size_t>(0.f, 0.f, w, h));
        _paths.resize(0);
    }
    
    void addPath(Path& path) {
        if (path._subPaths.size() != 0)
            _paths.emplace_back(path);
    }
    
    void writeCurves(Mesh& mesh) {
        int pi, c;
        float area, flip;
        for (Path& path : _paths) {
            size_t outerIndex = path._outerIndex;
            SubPath* outer = & path._subPaths[outerIndex];
            
            for (SubPath& s : path._subPaths)
                if (s._controlPoints.get() != NULL) {
                    const float *points = s._points.get();
                    const float *controlPoints = s._controlPoints.get();
                    pi = (int)mesh._points.size();
                    c = (int)(& path - & _paths[0]);
                    
                    Vector p0 = { points[0], points[1] }, p1, p2;
                    mesh._points.emplace_back(p0);
                    
                    for (int i = 0; i < s._npoints - 1; i++) {
                        p1 = { controlPoints[i * 2], controlPoints[i * 2 + 1] };
                        p2 = { points[i * 2 + 2], points[i * 2 + 3] };
                        
                        mesh._points.emplace_back(p1);
                        mesh._points.emplace_back(p2);
                        
                        area = (p1.x - p0.x) * (p2.y - p1.y) - (p1.y - p0.y) * (p2.x - p1.x);
                        Triangle tri = { pi + i * 2, pi + i * 2 + 1, pi + i * 2 + 2, c };
                        
                        if (area != 0) {
                            if (area < 0)
                                std::swap(tri.i0, tri.i2);
                            
                            flip = outer == & s ? 1 : -1;
                            
                            if (flip * area * s._area < 0)
                                mesh._concaveCurves.emplace_back(tri);
                            else
                                mesh._curves.emplace_back(tri);
                        }
                        
                        p0 = p2;
                    }
                }
        }
    }
    void maskPaths(TESStesselator *tess) {
        for (Path& path : _paths)
            path.resolveSelfIntersections(tess);
        
        for (size_t i = 0; i < _paths.size(); i++) {
            Path& path = _paths[i];
            
            auto matches = _quadtree->find(path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy);
            for (auto j : matches)
                _paths[j].maskOut(tess, path);
            _quadtree->add(i, path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy);
        }
    }
    void writeTessTriangles(Mesh& mesh, TESStesselator *tess) {
        assert(kPolySize == 3);
        tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
        
        for (Path& path : _paths) {
            mesh._colors.emplace_back(path._color);
            
            for (SubPath& s : path._subPaths)
                tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints);
            
            if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                float *points = (float *)tessGetVertices(tess);
                int npoints = tessGetVertexCount(tess);
                const int nelems = tessGetElementCount(tess);
                const TESSindex *polygon = tessGetElements(tess);
                
                int pi = (int)mesh._points.size(), c = (int)mesh._colors.size() - 1;
                for (int i = 0; i < nelems; i++, polygon += kPolySize) {
                    Triangle tri = { pi + polygon[0], pi + polygon[1], pi + polygon[2], c };
                    mesh._triangles.emplace_back(tri);
                }
                for (int i = 0; i < npoints; i++) {
                    Vector p = { points[i * 2], points[i * 2 + 1] };
                    mesh._points.emplace_back(p);
                }
            }
        }
    }
    void writeDelaunayTriangles(Mesh& mesh) {
        int count = 0;
        for (Path& path : _paths)
            count += path.countPoints();
        
        Edge *edges = (Edge *)alloca(count * sizeof(Edge)), *begin = & edges[0], *end = begin;
        for (Path& path : _paths)
            end = path.writeEdges(end);
        auto intersections = findIntersections(begin, end);
        count += (int)intersections.size();
        
        Memory<XYZ> xyz((count + 3) * sizeof(XYZ));
        XYZ *dst = xyz.get();
        for (Path& path : _paths)
            for (SubPath& s : path._subPaths) {
                float *src = s._points.get();
                for (int i = 0; i < s._npoints; i++)
                    dst->x = *src++, dst->y = *src++, dst->i = (int) (& path - & _paths[0]), dst++;
            }
        float px, py;
        for (auto& i : intersections) {
            linesIntersection(i.e0.x0(), i.e0.y0(), i.e0.x1(), i.e0.y1(), i.e1.x0(), i.e1.y0(), i.e1.x1(), i.e1.y1(), & px, & py);
            dst->x = px, dst->y = py, dst->i = 0, dst++;
        }
        
        Memory<ITRIANGLE> itriangle((count + 3) * 4 * sizeof(ITRIANGLE));
        int ntri, status = Triangulate(count, xyz.get(), itriangle.get(), (count + 3) * 4, &ntri);
        if (status)
            return;
        
        int pi = (int)mesh._points.size();
        XYZ *src = xyz.get();
        for (int i = 0; i < count; i++) {
            Vector p = { src[i].x, src[i].y };
            mesh._points.emplace_back(p);
        }
        
        for (Path& path : _paths)
            path.update();
        
        XYZ *pts = xyz.get();
        int i0, i1, i2, d01, d12, d20, c0, c1, c2, c;;
        ITRIANGLE *triangles = itriangle.get();
        for (int i = 0; i < ntri; i++) {
            i0 = triangles[i].p1, i1 = triangles[i].p2, i2 = triangles[i].p3;
            d01 = i1 - i0, d12 = i2 - i1, d20 = i0 - i2;
            c0 = pts[i0].i, c1 = pts[i1].i, c2 = pts[i2].i;
            c = std::abs(d01) > 1 ? c0 : std::abs(d12) > 1 ? c1 : std::abs(d20) > 1 ? c2 : c0;
//            c = c != -1 ? c : c0 == c1 ? c0 : c1 == c2 ? c1 : c2 == c0 ? c2 : c0;
            Triangle t = { triangles[i].p1 + pi, triangles[i].p2 + pi, triangles[i].p3 + pi, c };
            mesh._triangles.emplace_back(t);
        }
        
        for (Path& path : _paths)
            mesh._colors.emplace_back(path._color);
    }
    void triangulate(Mesh& mesh, bool useMasking, bool useDelaunay) {
        mesh.empty();
        TESStesselator *tess = _tess.get();
        
        writeCurves(mesh);
        if (useMasking)
            maskPaths(tess);

        if (useDelaunay)
            writeDelaunayTriangles(mesh);
        else
            writeTessTriangles(mesh, tess);
    }

    shared_ptr<TESStesselator>			_tess;
    shared_ptr<VG::QuadTree<size_t>>    _quadtree;
    vector<Path>                        _paths;
};
