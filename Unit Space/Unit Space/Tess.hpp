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
        kFlippedY = 1 << 1,
        kStale    = 1 << 2
    };
    
    Edge(float lx, float ly, float ux, float uy, int p, int ci, int pi) : x0(lx), y0(ly), x1(ux), y1(uy), flags(0), p(p), ci((unsigned short int)ci), pi((unsigned short int)pi) {}

    inline float y(float x) {
        return y0 + (x - x0) * (y1 - y0) / (x1 - x0);
    }
    float lx() const { return x0 > x1 ? x1 : x0; }
    float ly() const { return y0 > y1 ? y1 : y0; }
    float ux() const { return x0 < x1 ? x1 : x0; }
    float uy() const { return y0 < y1 ? y1 : y0; }
    
    inline bool operator< (const Edge& other) const     { return lx() < other.lx() || (lx() == other.lx() && ly() < other.ly()); }
    
    float x0, y0, x1, y1;
    unsigned short int flags, p, ci, pi;
};

struct ActiveEdge {
    ActiveEdge(Edge *e, float lx, float ux) : e(e) {
        if (e->x0 == e->x1) {
            ly = e->y0, uy = e->y1;
            lyt = uyt = 0;
            if (ly > uy)
                std::swap(ly, uy);
        } else {
            uyt = e->y(lx);
            update(ux);
        }
    }
    inline void update(float ux) {
        if (e->x0 == e->x1)
            return;
        
        lyt = uyt, uyt = e->y(ux);
        ly = lyt, uy = uyt;
        if (ly > uy)
            std::swap(ly, uy);
    }
    
    inline bool operator< (const ActiveEdge& other) const     { return ly < other.ly || (ly == other.ly && uy < other.uy); }
    
    float ly, uy;
    float lyt, uyt;
    Edge *e;
};

struct IntersectionEdge {
    IntersectionEdge(Edge *e, float ly, float uy) : e(e), ly(ly), uy(uy) {}
    
    float ly, uy;
    Edge *e;
};

struct Sample {
    Sample(float x, float y, int i) : x(x), y(y), i(i) {}
    
    inline bool operator< (const Sample& other) const     { return x < other.x || (x == other.x && y < other.y); }
    
    float x, y;
    int i;
};

struct Intersection {
    Intersection(Edge e0, Edge e1, float px, float py) : e0(e0), e1(e1), px(px), py(py) {}
    
    inline bool operator< (const Intersection& other) const     { return px < other.px || (px == other.px && py < other.py); }
    
    Edge e0, e1;
    float px, py;
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
        if (size == 0)
            return;
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
static int *writeColors(int *colors, Edge *begin, Edge *end, Sample *sbegin, Sample *send) {
    Edge *edge, *last, *src, *dst, *e;
    Sample *s = sbegin, *slast, *si;
    float lx, nx, area;
    int positive, winding;
    
    for (edge = begin; edge < end; ) {
        last = edge;
        lx = edge->lx();
        
        do
            edge++;
        while (edge < end && edge->lx() == lx);
        
        src = dst = last;
        while (--src >= begin)
            if (src->ux() > lx)
                *(--dst) = *src;
        begin = dst;
        
        nx = edge < end ? edge->lx() : FLT_MAX;
        slast = s;
        while (s < send && s->x < nx)
            s++;
        
        for (si = slast; si < s; si++) {
            winding = 0;
            for (e = begin; e < edge; e++) {
                area = (e->x1 - e->x0) * (si->y - e->y0) - (e->y1 - e->y0) * (si->x - e->x0);
                positive = e->x1 > e->x0 ? 0 : 1;
                if (e->x0 != e->x1 && ((positive && area > 0) || (!positive && area < 0)))
                    winding += positive ? 1 : -1;
            }
            colors[si->i] = winding & 1 ? 1 : 0;
        }
    }
    return colors;
}
static void findIntersections(vector<Intersection>& intersections, IntersectionEdge *begin, IntersectionEdge *end) {
    IntersectionEdge *edge, *last, *src, *dst, *e0, *e1;
    float lx, px, py;
    
    for (edge = begin; edge < end; ) {
        lx = edge->e->lx();
        
        last = edge;
        do
            edge++;
        while (edge < end && edge->e->lx() == lx);
        
        src = dst = last;
        while (--src >= begin)
            if (src->e->ux() > lx)
                *(--dst) = *src;
        begin = dst;
        
        for (e0 = edge - 1; e0 >= last; e0--)
            for (e1 = e0 - 1; e1 >= begin; e1--)
                if (e0->uy > e1->ly && e1->uy > e0->ly)
                    if (linesIntersection(e0->e->x0, e0->e->y0, e0->e->x1, e0->e->y1,
                                          e1->e->x0, e1->e->y0, e1->e->x1, e1->e->y1, & px, & py))
                        intersections.emplace_back(*(e0->e), *(e1->e), px, py);
    }
}

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
            _points.alloc(_npoints * kPointStride);
            _controlPoints.alloc(_npoints * kPointStride);
            splitCubics(_points.get(), _controlPoints.get(), points, npoints, kSplitCubicPrecision);
        } else {
            _npoints = npoints;
            _points.alloc(_npoints * kPointStride);
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
    Edge *writeEdges(Edge *dst, int p) {
        float lx, ly, ux, uy, *points;
        for (int ci = 0; ci < _subPaths.size(); ci++) {
            points = _subPaths[ci]._points.get();
            for (int pi = 0; pi < _subPaths[ci]._npoints; pi++) {
                lx = points[pi * 2], ly = points[pi * 2 + 1];
                ux = points[((pi + 1) % _subPaths[ci]._npoints) * 2], uy = points[((pi + 1) % _subPaths[ci]._npoints) * 2 + 1];
                new (dst++) Edge(lx, ly, ux, uy, p, ci, pi);
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
    vector<Vector>                      _edges;
    vector<Vector>                      _points;
    vector<Triangle>                    _triangles;
    vector<Triangle>                    _curves;
    vector<Triangle>                    _concaveCurves;
    
    void empty() {
        _colors.resize(0), _edges.resize(0), _points.resize(0), _triangles.resize(0), _curves.resize(0), _concaveCurves.resize(0);
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
    void writeEdges(Mesh& mesh) {
        for (Path& path : _paths)
            for (SubPath& s : path._subPaths) {
                float *points = s._points.get();
                size_t base = mesh._edges.size();
                for (int i = 0; i < s._npoints; i++)
                    mesh._edges.emplace_back(points[i * 2], points[i * 2 + 1]);
                if (!s._closed)
                    mesh._edges.emplace_back(points[0], points[1]);
                for (; base < mesh._edges.size() - 1; base++)
                    encodeBits(& mesh._edges[base].x, 0x3, 0x0);
                encodeBits(& mesh._edges[base].x, 0x3, 0x1);
            }
    }
    void writeQuad(Mesh& mesh, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
        int pi = (int)mesh._points.size();
        if (y1 == y2)
            mesh._triangles.emplace_back(pi, pi + 1, pi + 3, 0);
        else if (y0 == y3)
            mesh._triangles.emplace_back(pi, pi + 1, pi + 2, 0);
        else {
            mesh._triangles.emplace_back(pi, pi + 1, pi + 2, 0);
            mesh._triangles.emplace_back(pi, pi + 2, pi + 3, 0);
        }
        mesh._points.emplace_back(x0, y0);
        mesh._points.emplace_back(x1, y1);
        mesh._points.emplace_back(x2, y2);
        mesh._points.emplace_back(x3, y3);
    }
    void writeTriangles(Mesh& mesh) {
        int count = 0;
        for (Path& path : _paths)
            count += path.countPoints();
        
        Memory<Edge> edges0(count * sizeof(Edge));
        Edge *edges = edges0.get(), *begin = edges, *end = begin, *edge;
        for (Path& path : _paths)
            end = path.writeEdges(end, (int) (& path - & _paths[0]));
        sort(begin, end);

        Memory<IntersectionEdge> intersectionEdges(count * sizeof(IntersectionEdge));
        IntersectionEdge *iedges = intersectionEdges.get(), *ibegin = iedges, *iend = ibegin;
        for (edge = begin; edge < end; edge++)
            new (iend++) IntersectionEdge(edge, edge->ly(), edge->uy());
        vector<Intersection> intersections;
        findIntersections(intersections, ibegin, iend);
        
        Memory<float> xDivisions(((count * 2) + (int)intersections.size()) * sizeof(float));
        float *divisions = xDivisions.get(), *dbegin = divisions, *dend = dbegin, *division;
        for (edge = begin; edge < end; edge++)
            *dend++ = edge->x0, *dend++ = edge->x1;
        for (auto& i : intersections)
            *dend++ = i.px;
        sort(dbegin, dend);
        
        Memory<ActiveEdge> activeEdges(count * sizeof(ActiveEdge));
        ActiveEdge *actives = activeEdges.get(), *abegin = actives, *aend = abegin, *a, *asrc, *adst;
        int winding;
        float lx, ux, x0, y0, x1, y1, x2, y2, x3, y3;
        for (edge = begin, division = dbegin; division < dend; ) {
            lx = *division;
            do
                division++;
            while (division < dend && *division == lx);
            ux = division < dend ? *division : lx;

            asrc = adst = abegin;
            for (; asrc < aend; asrc++)
                if (asrc->e->ux() > lx)
                    *adst++ = *asrc;
            aend = adst;
            for (a = abegin; a < aend; a++)
                a->update(ux);
            
            while (edge < end && edge->lx() == lx)
                new (aend++) ActiveEdge(edge++, lx, ux);

            sort(abegin, aend);
            
            for (a = abegin; a < aend - 1; a++)
                if (std::abs(a->ly - (a + 1)->ly) < 1e-3)
                    (a + 1)->ly = a->ly;
            sort(abegin, aend);
                
            x0 = x3 = lx, x1 = x2 = ux;
            y0 = y1 = y2 = y3 = 0;
            winding = 0;
            for (a = abegin; a < aend; a++) {
                if (a->e->x0 == a->e->x1)
                    continue;
                
                y3 = a->lyt, y2 = a->uyt;
                
                if ((winding & 1) != 0)
                    writeQuad(mesh, x0, y0, x1, y1, x2, y2, x3, y3);
                
                winding += a->e->x0 > a->e->x1 ? 1 : -1;
                y0 = y3, y1 = y2;
            }
        }
        RGBA8 red(255, 0, 0, 255);
        RGBA8 green(0, 255, 0, 255);
        RGBA8 blue(0, 0, 255, 255);
        RGBA8 black(0, 0, 0, 255);
        RGBA8 alpha(255, 0, 0, 128);
        
        mesh._colors.emplace_back(red);
        mesh._colors.emplace_back(green);
    }
    
    void writeDelaunayTriangles(Mesh& mesh, bool useMasking) {
        int count = 0;
        for (Path& path : _paths)
            count += path.countPoints();
        
        Memory<Edge> edges0(count * sizeof(Edge));
        Edge *edges = edges0.get(), *begin = edges, *end = begin;
        for (Path& path : _paths)
            end = path.writeEdges(end, (int) (& path - & _paths[0]));
        sort(begin, end);
        Memory<Edge> edges1 = edges0.clone();
        
//        vector<Intersection> intersections;
//        if (!useMasking) {
//            findIntersections(intersections, begin, end);
//            count += (int)intersections.size();
//        }

        Memory<XYZ> xyz((count + 3) * sizeof(XYZ));
        XYZ *dst = xyz.get();
        
//        float px, py;
//        for (auto& i : intersections) {
//            linesIntersection(i.e0.x0, i.e0.y0, i.e0.x1, i.e0.y1, i.e1.x0, i.e1.y0, i.e1.x1, i.e1.y1, & px, & py);
//            dst->x = i.px, dst->y = i.py, dst->i = 0, dst++;
//        }
        
        for (Path& path : _paths)
            for (SubPath& s : path._subPaths) {
                float *src = s._points.get();
                for (int i = 0; i < s._npoints; i++)
                    dst->x = *src++, dst->y = *src++, dst->i = (int) (& path - & _paths[0]), dst++;
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
            Triangle t = { triangles[i].p1 + pi, triangles[i].p2 + pi, triangles[i].p3 + pi, c };
            mesh._triangles.emplace_back(t);
        }
        Memory<Sample> samples(mesh._triangles.size() * sizeof(Sample));
        Sample *sbegin = samples.get(), *send = sbegin;
        Vector *points = & mesh._points[0];
        float x0, y0, x1, y1, x2, y2, uvw[3], d = -1e-6;
        int imin;
        for (int ti = 0; ti < mesh._triangles.size(); ti++) {
            Triangle& t = mesh._triangles[ti];
            x0 = points[t.i0].x, y0 = points[t.i0].y;
            x1 = points[t.i1].x, y1 = points[t.i1].y;
            x2 = points[t.i2].x, y2 = points[t.i2].y;
            new (send++) Sample((x0 + x1 + x2) * 0.333f, (y0 + y1 + y2) * 0.333f, ti);
        }
        sort(sbegin, send);
        
        size_t size = end - begin;
        edges = edges1.get(), begin = edges, end = begin + size;
        Memory<int> colors(mesh._triangles.size() * sizeof(int));
        int *cdst = colors.get();
        writeColors(cdst, begin, end, sbegin, send);
        for (int ti = 0; ti < mesh._triangles.size(); ti++)
            mesh._triangles[ti].c = cdst[ti];
        
        RGBA8 red(255, 0, 0, 255);
        RGBA8 green(0, 255, 0, 255);
        RGBA8 blue(0, 0, 255, 255);
        RGBA8 black(0, 0, 0, 255);
        RGBA8 alpha(255, 0, 0, 128);
        
        mesh._colors.emplace_back(red);
        mesh._colors.emplace_back(green);
        
//        for (Path& path : _paths)
//            mesh._colors.emplace_back(path._color);
    }
    void triangulate(Mesh& mesh, bool useMasking, bool useDelaunay) {
        mesh.empty();
        TESStesselator *tess = _tess.get();
        
        writeCurves(mesh);
        if (useMasking)
            maskPaths(tess);

        writeEdges(mesh);
        
        if (useDelaunay)
            writeTriangles(mesh);
//            writeDelaunayTriangles(mesh, useMasking);
        else
            writeTessTriangles(mesh, tess);
    }

    shared_ptr<TESStesselator>			_tess;
    shared_ptr<VG::QuadTree<size_t>>    _quadtree;
    vector<Path>                        _paths;
};
