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
#import "Memory.hpp"
#import "Path.hpp"
#import "Mesh.hpp"
#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"

#pragma mark - Data structures

struct Edge {
    Edge(float x0, float y0, float x1, float y1, int si, int p, int ci, int pi) : x0(x0), y0(y0), x1(x1), y1(y1), si(si), p(p), ci((unsigned short int)ci), pi((unsigned short int)pi) {}

    inline float y(float x) {
        return x == x1 ? y1 : y0 + (x - x0) / (x1 - x0) * (y1 - y0);
    }
    inline float lx() const { return x0 < x1 ? x0 : x1; }
    inline float ly() const { return y0 < y1 ? y0 : y1; }
    inline float ux() const { return x0 > x1 ? x0 : x1; }
    inline float uy() const { return y0 > y1 ? y0 : y1; }
    
    inline bool operator< (const Edge& other) const     { return lx() < other.lx() || (lx() == other.lx() && ly() < other.ly()); }
    
    float x0, y0, x1, y1;
    unsigned short int si, p, ci, pi;
};

struct ActiveEdge {
    ActiveEdge(Edge *e) : e(e), ly(e->ly()), uy(e->uy()), llx(e->lx()), ulx(llx), ended(false), triangulate(false) {}
    inline bool below(const ActiveEdge& other) const {
        float vx, vy, vx0, vy0, vx1, vy1, det0, det1, sum;
        vx = other.e->x1 - other.e->x0, vy = other.e->y1 - other.e->y0;
        vx0 = e->x0 - other.e->x0, vy0 = e->y0 - other.e->y0;
        vx1 = e->x1 - other.e->x0, vy1 = e->y1 - other.e->y0;
        det0 = vx * vy0 - vy * vx0, det1 = vx * vy1 - vy * vx1, sum = det0 + det1;
        
        if (std::abs(det0) > 1e-3 && std::abs(det1) > 1e-3 && det0 * det1 < 0) {
            if (e->x0 >= other.e->lx() && e->x0 <= other.e->ux())
                sum = det0;
            else
                sum = det1;
        }
        return sum ? (other.e->x1 > other.e->x0 ? sum < 0 : sum > 0) : false;
    }
    inline bool operator<(const ActiveEdge& other) const    { return ly > other.uy ? false : uy < other.ly ? true : below(other); };
    float ly, uy;
    float llx, ulx;
    bool ended, triangulate;
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
    Quad(Edge *le, Edge *ue, float lx, float ux, int p)
     : next(nullptr), p(p), x0(lx), y0(le->y(x0)), x1(ux), y1(le->y(x1)), x2(ux), y2(ue->y(x2)), x3(lx), y3(ue->y(x3)) {}
   
//    inline bool follows(const Quad& other) const { return std::abs(x0 - other.x1) < 1e-3 && std::abs(y0 - other.y1) < 1e-3 && std::abs(x3 - other.x2) < 1e-3 && std::abs(y3 - other.y2) < 1e-3; }
    inline bool leFollows(const Quad& other) const { return x0 == other.x1 && y0 == other.y1; }
    inline bool ueFollows(const Quad& other) const { return x3 == other.x2 && y3 == other.y2; }
    Quad *next;
    int p;
    float x0, y0, x1, y1, x2, y2, x3, y3;
};

#pragma mark - Algorithms

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

struct Scene {
    void addPath(Path& path) {
        if (path._subPaths.size() != 0) {
            path.update();
            if (_paths.size() == 0)
                _bounds = path._bounds, _npoints = 0;
            _bounds.unite(path._bounds);
            _npoints += path.countPoints();
            _paths.emplace_back(path);
        }
    }
    void addSVG(const void *bytes, size_t size) {
        char *data = (char *)malloc(size + 1);
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = data ? nsvgParse(data, "px", 96) : NULL;
        
        if (image) {
            printf("size: %f x %f\n", image->width, image->height);            
            int limit = 60000;
            for (NSVGshape *shape = image->shapes; shape != NULL && limit; shape = shape->next, limit--) {
                if (shape->fill.type == NSVG_PAINT_COLOR /*&& shape->stroke.type == NSVG_PAINT_NONE*/) {
                    RGBA8 color = RGBA8((unsigned char)((shape->fill.color ) & 0xFF), (unsigned char)((shape->fill.color >> 8) & 0xFF), (unsigned char)((shape->fill.color >> 16) & 0xFF), (unsigned char)(shape->fill.color >> 24));
                    Path p(shape->fillRule == NSVG_FILLRULE_EVENODD, color);
                    
                    for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
                        for (int i = 0; i < path->npts; i++)
                            path->pts[i * 2 + 1] = image->height - path->pts[i * 2 + 1];
                        SubPath s(path->pts, path->npts, SubPath::kCubic);
                        p.addSubPath(s);
                    }
                    addPath(p);
                }
            }
            // Delete
            nsvgDelete(image);
        }
        free(data);
    }
    void empty() { _paths.resize(0); }
    
    void updateNpoints() {
        _npoints = 0;
        for (Path *path = & _paths[0], *pend = path + _paths.size(); path < pend; path++)
            _npoints += path->countPoints();
    }
    Memory<Edge> writeEdges(int& count, float scale, float tx, float ty) {
        Memory<Edge> edgesMemory(_npoints);
        Edge *edges = edgesMemory.get(), *begin = edges, *end = begin;
        for (Path *path = & _paths[0], *pbegin = path, *pend = path + _paths.size(); path < pend; path++)
            end = writeEdges(end, path, count, (int)(path - pbegin), scale, tx, ty);
        _nsubpaths = (end - 1)->si + 1;
        return edgesMemory;
    }
    
    static Edge *writeEdges(Edge *dst, Path *path, int& count, int p, float scale, float tx, float ty) {
        float x0, y0, x1, y1, *points;
        int si = p == 0 ? 0 : (dst - 1)->si + 1;
        for (int ci = 0; ci < path->_subPaths.size(); ci++, si++) {
            points = path->_subPaths[ci]._points.get();
            x0 = (points[0] - tx) * scale, y0 = (points[1] - ty) * scale;
            for (int pi = 0, npoints = path->_subPaths[ci]._npoints; pi < npoints; x0 = x1, y0 = y1, pi++) {
                x1 = (points[((pi + 1) % npoints) * 2] - tx) * scale, y1 = (points[((pi + 1) % npoints) * 2 + 1] - ty) * scale;
                count += std::abs(x0 - x1) + std::abs(y0 - y1) + 2;
                new (dst++) Edge(x0, y0, x1, y1, si, p, ci, pi);
            }
        }
        return dst;
    }
    vector<Path>    _paths;
    Bounds          _bounds;
    int             _npoints, _nsubpaths;
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
    static void writeQuad(Mesh& mesh, Quad *q, int p) {
        int pi = (int)mesh._points.size();
        if (q->x1 == q->x2 && q->y1 == q->y2)
            mesh._triangles.emplace_back(pi, pi + 1, pi + 3, p);
        else if (q->x0 == q->x3 && q->y0 == q->y3)
            mesh._triangles.emplace_back(pi, pi + 1, pi + 2, p);
        else {
            mesh._triangles.emplace_back(pi, pi + 1, pi + 2, p);
            mesh._triangles.emplace_back(pi, pi + 2, pi + 3, p);
        }
        mesh._points.emplace_back(q->x0, q->y0);
        mesh._points.emplace_back(q->x1, q->y1);
        mesh._points.emplace_back(q->x2, q->y2);
        mesh._points.emplace_back(q->x3, q->y3);
    }
    static Memory<Edge> createEdgeList(Scene& scene) {
        float x0, y0, x1, y1;
        
        int count = scene._npoints, dummy;
        
        Memory<Edge> edges0 = scene.writeEdges(dummy, 1, 0, 0);
        Edge *edges = edges0.get(), *begin = edges, *end = begin + scene._npoints, *edge;
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
    static void writeTriangles(Mesh& mesh, Scene& scene, bool merge) {
        Memory<Edge> edges = createEdgeList(scene);
        Edge *begin = edges.get(), *end = begin + edges.count(), *edge, *last, *e;
        
        Memory<float> xDivisions(edges.count() * 2);
        float lx, *divisions = xDivisions.get(), *dbegin = divisions, *dend = dbegin, *division, ledet, uedet;
        for (edge = begin; edge < end; edge++)
            *dend++ = edge->x0, *dend++ = edge->x1;
        sort(dbegin, dend);
        
        Memory<ActiveEdge> activeEdges(edges.count());
        ActiveEdge *actives = activeEdges.get(), *abegin = actives, *aend = abegin, *a, *asrc, *adst, *le, *ue;
        
        Memory<int> pathWindings(scene._paths.size());
        int i, p, pv, *windings = pathWindings.zero().get();
        
        uint64_t flags, nmasks = scene._paths.size() / 64 + 1, visibleFlags[nmasks];
        
        Memory<Quad *>quadBegins(scene._paths.size()), quadLasts(scene._paths.size());
        Quad **qbegins = quadBegins.zero().get(), **qlasts = quadLasts.zero().get();
        
        Memory<Quad> activeQuads(edges.count() * 10);
        Quad *quads = activeQuads.get(), *qbegin = quads, *qend = qbegin, *q, *qnew;
        
        for (edge = begin, division = dbegin; division < dend; ) {
            lx = *division;
            do
                division++;
            while (division < dend && *division == lx);
            
            memset(visibleFlags, 0, sizeof(visibleFlags));
            for (a = abegin; a < aend; a++)
                a->triangulate = false, windings[a->e->p] = 0;
            
            for (a = abegin; a < aend; a++)
                if ((a->ended = a->e->ux() == lx)) {
                    a->triangulate = true;
                    if (a > abegin)
                        (a - 1)->triangulate = true;
                    if (a < aend - 1)
                        (a + 1)->triangulate = true;
                }
            for (last = edge; edge < end && edge->lx() == lx; edge++) {}
            for (a = aend, e = edge - 1; e >= last; e--) {
                if (e->x0 != e->x1) {
                    ActiveEdge anew(e);
                    while (a > abegin && anew < *(a - 1))
                        a--;
                    if (a < aend)
                        a->triangulate = true;
                    if (a > abegin)
                        (a - 1)->triangulate = true;
                }
            }
            edge = last;
            
            for (le = abegin, ue = le + 1; ue < aend; le = ue, ue++) {
                p = le->e->p;
                windings[p] += le->e->x0 > le->e->x1 ? -1 : 1;
                if (scene._paths[p]._evenOdd ? windings[p] & 1 : windings[p])
                    visibleFlags[p >> 6] |= (1UL << (p & 0x3F));
                else
                    visibleFlags[p >> 6] &= ~(1UL << (p & 0x3F));
                
                if (le->triangulate && ue->triangulate) {
                    if (le->ulx >= ue->e->lx()) {
                        for (i = (int)nmasks - 1; i >= 0; i--) {
                            flags = visibleFlags[i];
                            if (flags) {
                                for (pv = i * 64; flags >>= 1; pv++) {}
                                qnew = new (qend++) Quad(le->e, ue->e, le->ulx, lx, pv);
                                if (qbegins[p] == nullptr) {
                                    qbegins[p] = qlasts[p] = qnew;
                                } else {
                                    if (merge) {
                                        q = qbegins[p];
                                        do {
                                            if (qnew->leFollows(*q) && qnew->ueFollows(*q) && qnew->p == q->p) {
                                                ledet = (q->x1 - q->x0) * (qnew->y1 - qnew->y0) - (q->y1 - q->y0) * (qnew->x1 - qnew->x0);
                                                uedet = (q->x2 - q->x3) * (qnew->y2 - qnew->y3) - (q->y2 - q->y3) * (qnew->x2 - qnew->x3);
                                                
                                                if (std::abs(ledet) < 1e-3 && std::abs(uedet) < 1e-3) {
                                                    q->x1 = qnew->x1, q->y1 = qnew->y1;
                                                    q->x2 = qnew->x2, q->y2 = qnew->y2;
                                                    qnew = nullptr, qend--;
                                                    break;
                                                } else if (std::abs(ledet) < 1e-3 && q->y0 == q->y3 && qnew->y1 == qnew->y2) {
                                                    q->x1 = qnew->x1, q->y1 = qnew->y1;
                                                    qnew = nullptr, qend--;
                                                    break;
                                                } else if (std::abs(uedet) < 1e-3 && q->y0 == q->y3 && qnew->y1 == qnew->y2) {
                                                    q->x2 = qnew->x2, q->y2 = qnew->y2;
                                                    qnew = nullptr, qend--;
                                                    break;
                                                }
                                            }
                                        } while ((q = q->next));
                                    }
                                    if (qnew)
                                        qlasts[p]->next = qnew, qlasts[p] = qnew;
                                }
                                break;
                            }
                        }
                    }
                    le->ulx = ue->llx = lx;
                }
            }
            
            for (asrc = adst = abegin; asrc < aend; asrc++)
                if (!asrc->ended)
                    *adst++ = *asrc;
            aend = adst;
            
            while (edge < end && edge->lx() == lx) {
                if (edge->x0 != edge->x1) {
                    ActiveEdge anew(edge);
                    adst = aend++, asrc = adst - 1;
                    while (adst > abegin && anew < *asrc)
                        *adst-- = *asrc--;
                    *adst = anew;
                }
                edge++;
            }
        }
        for (p = 0; p < scene._paths.size(); p++)
            if ((q = qbegins[p]))
                do {
                    writeQuad(mesh, q, q->p);
                } while ((q = q->next));
    }

    void triangulate(Mesh& mesh, Scene& scene, bool useMasking) {
        mesh.empty();
        writeCurves(mesh, scene);
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
        writeTriangles(mesh, scene, useMasking);
    }
};
