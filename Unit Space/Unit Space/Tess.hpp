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

constexpr int kPointStride = 2 * sizeof(float);
constexpr float kSplitCubicPrecision = 0.1;

#pragma mark - Data structures

struct Edge {
    Edge(float x0, float y0, float x1, float y1, int p, int ci, int pi) : x0(x0), y0(y0), x1(x1), y1(y1), flags(0), p(p), ci((unsigned short int)ci), pi((unsigned short int)pi) {}

    inline float y(float x) {
        return x == x1 ? y1 : y0 + (x - x0) / (x1 - x0) * (y1 - y0);
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
    ActiveEdge(Edge *e, float lx, float ux) : e(e), lyt(e->y(lx)),  uyt(e->y(ux)) {}
    
    inline void update(float ux)    { lyt = uyt, uyt = e->y(ux); }
    float ly() const                { return lyt < uyt ? lyt : uyt; }
    float uy() const                { return lyt > uyt ? lyt : uyt; }
    
    inline bool operator< (const ActiveEdge& other) const     { return ly() < other.ly() || (ly() == other.ly() && uy() < other.uy()); }
    
    float lyt, uyt;
    Edge *e;
};

struct IntersectionEdge {
    IntersectionEdge(Edge *e, float ly, float uy) : e(e), ly(ly), uy(uy) {}
    
    float ly, uy;
    Edge *e;
};

struct Intersection {
    Intersection(Edge *e, float px, float py) : e(e), px(px), py(py) {}
    
    inline bool operator< (const Intersection& other) const     { return e < other.e || (e == other.e && (px < other.px || (px == other.px && py < other.py))); }
    
    float px, py;
    Edge *e;
};

struct Quad {
    Quad(Edge *le, Edge *ue, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3)
    : le(le), ue(ue), x0(x0), y0(y0), x1(x1), y1(y1), x2(x2), y2(y2), x3(x3), y3(y3) {}
    
    bool merge(Quad *rq) {
        if (y1 == y2 || rq->y0 == rq->y3)
            return false;
        if (! (y1 == rq->y0 && y2 == rq->y3))
            return false;
        if (le == rq->le && ue == rq->ue) {
            x1 = rq->x1, y1 = rq->y1, x2 = rq->x2, y2 = rq->y2;
            return true;
        }
        if (y0 == y3) {
            if (rq->y1 == rq->y2) {
                if (le == rq->le) {
                    x1 = rq->x1, y1 = rq->y1, x2 = rq->x2, y2 = rq->y2, x3 = rq->x3, y3 = rq->y3;
                    ue = rq->ue;
                    return true;
                } else if (ue == rq->ue) {
                    x0 = x1, y0 = y1, x1 = rq->x1, y1 = rq->y1, x2 = rq->x2, y2 = rq->y2;
                    le = rq->le;
                    return true;
                }
            } else {
                if (le == rq->le && (x2 - x0) * (rq->y2 - y2) - (y2 - y0) * (rq->x2 - x2) < 0) {
                    x1 = rq->x1, y1 = rq->y1, x2 = rq->x2, y2 = rq->y2, x3 = rq->x3, y3 = rq->y3;
                    ue = rq->ue;
                    return true;
                } else if (ue == rq->ue && (x1 - x0) * (rq->y1 - y1) - (y1 - y0) * (rq->x1 - x1) > 0) {
                    x0 = x1, y0 = y1, x1 = rq->x1, y1 = rq->y1, x2 = rq->x2, y2 = rq->y2;
                    le = rq->le;
                    return true;
                }
            }
        } else {
            if (rq->y1 == rq->y2) {
                if (le == rq->le) {
                    x1 = rq->x1, y1 = rq->y1;
                    return true;
                } else if (ue == rq->ue && (x1 - x0) * (rq->y1 - y1) - (y1 - y0) * (rq->x1 - x1) > 0) {
                    x2 = rq->x2, y2 = rq->y2;
                    return true;
                }
            }
        }
        
        return false;
    }
    inline float ly() const { return y0 < y1 ? y0 : y1; }
    inline float uy() const { return y2 < y3 ? y3 : y2; }
    inline bool operator< (const Quad& other) const     { return ly() < other.ly() || (ly() == other.ly() && uy() < other.uy()); }
    
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Edge *le, *ue;
};

template<typename T>
struct Memory {
private:
    struct Header { int refCount; void *memory; size_t size; };
    Header *header = nullptr;
public:
    Memory()                            {}
    Memory(size_t count)                { alloc(count); }
    Memory(const Memory& other)         { assign(other); }
    Memory& operator= (Memory other)    { assign(other); return *this; }
    ~Memory() {
        if (header && (--(header->refCount) == 0))
            free(header->memory), free(header);
    }
    void alloc(size_t count) {
        if (count == 0)
            return;
        if (header)
            header->memory = ::realloc(header->memory, count * sizeof(T));
        else {
            header = (Header *)malloc(sizeof(Header));
            header->memory = malloc(count * sizeof(T));
            header->refCount = 1;
        }
        header->size = count * sizeof(T);
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
    size_t count() { return header ? header->size / sizeof(T) : 0; }
    size_t size() { return header ? header->size : 0; }
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

static int splitCubics(float *dst, float *cpdst, const float *src, int npoints, const float prec) {
    float *dbegin = dst, x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, cpx, cpy;
    float xD01, yD01, D01, tcubed, t;
    float n01, n12, n23;
    float n012, n123;
    float n0123;
    constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
    
    for (int i = 0; i < npoints - 1; i += 3) {
        const float *p = src + i * 2;
        x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
        
        do {
            xD01 = x3 + 3.f * (x1 - x2) - x0, yD01 = y3 + 3.f * (y1 - y2) - y0;
            D01 = std::sqrt(xD01 * xD01 + yD01 * yD01);
            tcubed = D01 == 0 ? 1 : prec * multiplier / D01;
            if (tcubed > 0.999999)
                break;
            
            *dst++ = x0, *dst++ = y0;
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
        } while (1);
        
        cpx = (3.0 * (x2 + x1) - x0 - x3) * 0.25, cpy = (3.0 * (y2 + y1) - y0 - y3) * 0.25;
        if (cpx != x0 || cpy != y0)
            *dst++ = x0, *dst++ = y0, *cpdst++ = cpx, *cpdst++ = cpy;
    }
    *dst++ = x3, *dst++ = y3;
    return (int)(dst - dbegin) / 2;
}
// The Bentley-Ottmann Algorithm: http://geomalgorithms.com/a09-_intersect-3.html
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
                                          e1->e->x0, e1->e->y0, e1->e->x1, e1->e->y1, & px, & py)) {
                        intersections.emplace_back(e0->e, px, py);
                        intersections.emplace_back(e1->e, px, py);
                    }
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
            _points.alloc(_npoints * 2);
            _controlPoints.alloc(_npoints * 2);
            _npoints = splitCubics(_points.get(), _controlPoints.get(), points, npoints, kSplitCubicPrecision);
        } else {
            _npoints = npoints;
            _points.alloc(_npoints * 2);
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
    Edge *writeEdges(Edge *dst, int p) {
        float x0, y0, x1, y1, *points;
        for (int ci = 0; ci < _subPaths.size(); ci++) {
            points = _subPaths[ci]._points.get();
            x0 = points[0], y0 = points[1];
            for (int pi = 0, npoints = _subPaths[ci]._npoints; pi < npoints; x0 = x1, y0 = y1, pi++) {
                x1 = points[((pi + 1) % npoints) * 2], y1 = points[((pi + 1) % npoints) * 2 + 1];
                new (dst++) Edge(x0, y0, x1, y1, p, ci, pi);
            }
        }
        return dst;
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
    AffineTransform                     _CTM;
    vector<RGBA8>                       _colors;
    vector<Vector>                      _edges;
    vector<Vector>                      _points;
    vector<Triangle>                    _triangles;
    vector<Triangle>                    _curves;
    vector<Triangle>                    _concaveCurves;
    
    void empty() {
        _CTM = { 1, 0, 0, 1, 0, 0 };
        _colors.resize(0), _edges.resize(0), _points.resize(0), _triangles.resize(0), _curves.resize(0), _concaveCurves.resize(0);
    }
     void writeQuad(float x0, float y0, float x1, float y1,float x2, float y2, float x3, float y3, int p) {
        int pi = (int)_points.size();
        _points.emplace_back(x0, y0), _points.emplace_back(x1, y1), _points.emplace_back(x2, y2), _points.emplace_back(x3, y3);
        _triangles.emplace_back(pi + 0, pi + 1, pi + 2, p), _triangles.emplace_back(pi + 0, pi + 2, pi + 3, p);
    }
    void writeRect(float x0, float y0, float x1, float y1, int p) {
        writeQuad(x0, y0, x1, y0, x1, y1, x0, y1, p);
    }
    void writeTriangle(float x0, float y0, float x1, float y1,float x2, float y2, int p) {
        int pi = (int)_points.size();
        _points.emplace_back(x0, y0), _points.emplace_back(x1, y1), _points.emplace_back(x2, y2);
        _triangles.emplace_back(pi + 0, pi + 1, pi + 2, p);
    }
};

struct Scene {
    Scene(float w, float h) : w(w), h(h) {}
    
    void addPath(Path& path) {
        if (path._subPaths.size() != 0)
            _paths.emplace_back(path);
    }
    
    float w, h;
    vector<Path> _paths;
};

struct Tess {
    static void writeCurves(Mesh& mesh, Scene& scene) {
        int pi, c;
        float area, flip;
        for (Path& path : scene._paths) {
            size_t outerIndex = path._outerIndex;
            SubPath* outer = & path._subPaths[outerIndex];
            
            for (SubPath& s : path._subPaths)
                if (s._controlPoints.get() != NULL) {
                    const float *points = s._points.get();
                    const float *controlPoints = s._controlPoints.get();
                    pi = (int)mesh._points.size();
                    c = (int)(& path - & scene._paths[0]);
                    
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
    void writeEdges(Mesh& mesh, Scene& scene) {
        for (Path& path : scene._paths)
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
    static void writeQuad(Mesh& mesh, Quad *q) {
        int pi = (int)mesh._points.size();
        if (q->x1 == q->x2 && q->y1 == q->y2)
            mesh._triangles.emplace_back(pi, pi + 1, pi + 3, q->le->p);
        else if (q->x0 == q->x3 && q->y0 == q->y3)
            mesh._triangles.emplace_back(pi, pi + 1, pi + 2, q->le->p);
        else {
            mesh._triangles.emplace_back(pi, pi + 1, pi + 2, q->le->p);
            mesh._triangles.emplace_back(pi, pi + 2, pi + 3, q->le->p);
        }
        mesh._points.emplace_back(q->x0, q->y0);
        mesh._points.emplace_back(q->x1, q->y1);
        mesh._points.emplace_back(q->x2, q->y2);
        mesh._points.emplace_back(q->x3, q->y3);
    }
    static Memory<Edge> createEdgeList(Scene& scene) {
        float x0, y0, x1, y1;
        
        int count = 0;
        for (Path& path : scene._paths)
            count += path.countPoints();
        
        Memory<Edge> edges0(count);
        Edge *edges = edges0.get(), *begin = edges, *end = begin, *edge;
        for (Path& path : scene._paths)
            end = path.writeEdges(end, (int) (& path - & scene._paths[0]));
        sort(begin, end);

        Memory<IntersectionEdge> intersectionEdges(count);
        IntersectionEdge *iedges = intersectionEdges.get(), *ibegin = iedges, *iend = ibegin;
        for (edge = begin; edge < end; edge++)
            new (iend++) IntersectionEdge(edge, edge->ly(), edge->uy());
        vector<Intersection> intersections;
        findIntersections(intersections, ibegin, iend);
        sort(intersections.begin(), intersections.end());
        Memory<Edge> intersectedEdges(intersections.size());
        Edge *intbegin = intersectedEdges.get(), *intend = intbegin;
        Intersection *inbegin = & intersections[0], *inend = inbegin + intersections.size(), *in = inbegin;
        while (in < inend) {
            do {
                in++;
            } while (in < inend && in->e == inbegin->e);
            
            if (in == inbegin)
                continue;
            for (int j = 0; j < in - inbegin; j++)
                intend[j] = *inbegin->e;
            if (inbegin->e->x0 < inbegin->e->x1) {
                x1 = inbegin->px, y1 = inbegin->py;
                inbegin->e->x1 = x1, inbegin->e->y1 = y1;
                x0 = x1, y0 = y1, inbegin++;
                while (inbegin < in) {
                    x1 = inbegin->px, y1 = inbegin++->py;
                    intend->x0 = x0, intend->y0 = y0, intend->x1 = x1, intend++->y1 = y1;
                    x0 = x1, y0 = y1;
                }
                intend->x0 = x0, intend++->y0 = y0;
            } else {
                x0 = inbegin->px, y0 = inbegin->py;
                inbegin->e->x0 = x0, inbegin->e->y0 = y0;
                x1 = x0, y1 = y0, inbegin++;
                while (inbegin < in) {
                    x0 = inbegin->px, y0 = inbegin++->py;
                    intend->x0 = x0, intend->y0 = y0, intend->x1 = x1, intend++->y1 = y1;
                    x1 = x0, y1 = y0;
                }
                intend->x1 = x1, intend++->y1 = y1;
            }
        }
        Memory<Edge> edges1(intend - intbegin + count);
        memcpy(edges1.get(), edges0.get(), edges0.size());
        memcpy(edges1.get() + count, intersectedEdges.get(), edges1.size() - edges0.size());
        begin = edges1.get(), end = begin + (intend - intbegin + count);
        sort(begin, end);
        return edges1;
    }
    static void writeTriangles(Mesh& mesh, Scene& scene) {
        float lx, ux, x0, y0, x1, y1, x2, y2, x3, y3;
        
        Memory<Edge> edges = createEdgeList(scene);
        Edge *begin = edges.get(), *end = begin + edges.count(), *edge, *le, *ue;
        
        Memory<float> xDivisions(edges.count() * 2);
        float *divisions = xDivisions.get(), *dbegin = divisions, *dend = dbegin, *division;
        for (edge = begin; edge < end; edge++)
            *dend++ = edge->x0, *dend++ = edge->x1;
        sort(dbegin, dend);
        
        Memory<ActiveEdge> activeEdges(edges.count());
        ActiveEdge *actives = activeEdges.get(), *abegin = actives, *aend = abegin, *a, *asrc, *adst;
        
        Memory<Quad> activeQuads(edges.count());
        Quad *quads = activeQuads.get(), *qbegin = quads, *qend = qbegin, *q, *qsrc, *qdst;
        
        int winding;
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
            
            while (edge < end && edge->lx() == lx) {
                if (edge->x0 != edge->x1) {
                    ActiveEdge anew(edge, lx, ux);
                    adst = aend++, asrc = adst - 1;
                    while (adst > abegin && anew < *asrc)
                        *adst-- = *asrc--;
                    *adst = anew;
                }
                edge++;
            }
            
            x0 = x3 = lx, x1 = x2 = ux;
            y0 = y1 = y2 = y3 = 0;
            le = ue = nullptr;
            winding = 0;
            qdst = qbegin, qsrc = qend;
            for (a = abegin; a < aend; a++) {
                if (a->e->x0 == a->e->x1)
                    continue;
                
                y3 = a->lyt, y2 = a->uyt, ue = a->e;
                
                if ((winding & 1) != 0) {
                    new (qsrc) Quad(le, ue, x0, y0, x1, y1, x2, y2, x3, y3);
                    while (qdst < qend && qdst->y1 < qsrc->y0)
                        qdst++;
                    
                    if (qdst < qend && qdst->merge(qsrc))
                        qdst++;
                    else
                        qsrc++;
                }
                winding += a->e->x0 > a->e->x1 ? 1 : -1;
                y0 = y3, y1 = y2, le = ue;
            }

            for (q = qbegin; q < qend; q++)
                if (q->x1 < ux) {
                    writeQuad(mesh, q);
                    q->y0 = q->y1 = FLT_MAX;
                }
            sort(qbegin, qsrc);
            while (qsrc > qbegin && (qsrc - 1)->y0 == FLT_MAX)
                qsrc--;
            qend = qsrc;
        }
        
        for (q = qbegin; q < qend; q++)
            writeQuad(mesh, q);
        
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
    }

    void triangulate(Mesh& mesh, Scene& scene, bool useMasking, bool useDelaunay) {
        mesh.empty();
        writeCurves(mesh, scene);
        writeEdges(mesh, scene);
        writeTriangles(mesh, scene);
    }
};
