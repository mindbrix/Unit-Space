//
//  Revolution.hpp
//  Unit Space
//
//  Created by Nigel Barber on 13/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
struct Revolution {
    struct Edge {
        Edge(int p, float x0, float y0, float x1, float y1) : next(0), p(p), x0(x0), y0(y0), x1(x1), y1(y1) {}
        inline float ux() const { return x0 < x1 ? x1 : x0; }
        inline float y(float x) { return x == x1 ? y1 : y0 + (x - x0) / (x1 - x0) * (y1 - y0); }
        int next, p;
        float x0, y0, x1, y1;
    };
    struct EdgeList {
        int begin, last;
    };
    struct EdgeIndex {
        EdgeIndex(float x, int i) : x(x), i(i) {}
        float x;
        int i;
    };
    struct EdgeEvent {
        enum Type { kBegin, kEnd, kIntersection };
        EdgeEvent(float x, Type type, int i0, int i1) : x(x), type(type), i0(i0), i1(i1) {}
        inline bool operator< (const EdgeEvent& other) const     { return x < other.x; }
        float x;
        Type type;
        int i0, i1;
    };
    struct ActiveEdge {
        ActiveEdge(Edge *e, float lx, float ux) : e(e), ylx(e->y(lx)), yux(e->y(ux)) {}
        
        inline void update(float ux)    { ylx = yux, yux = e->y(ux); }
        float ly() const                { return ylx < yux ? ylx : yux; }
        float uy() const                { return ylx > yux ? ylx : yux; }
        
        inline bool operator< (const ActiveEdge& other) const     { return ly() < other.ly() || (ly() == other.ly() && uy() < other.uy()); }
        
        float ylx, yux;
        Edge *e;
    };
    Revolution(Scene &scene) : bounds(scene._bounds), width(bounds.ux - bounds.lx), height(bounds.uy - bounds.ly), edgeIndex(1), edgeCount(0) {
        float dmax = width > height ? width : height;
        scale = float(kDivisions) / dmax, dimension = dmax / float(kDivisions), tx = bounds.lx, ty = bounds.ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
        listsMemory = Memory<EdgeList>(ih);
        edgeLists = listsMemory.zero().get();
        float x0, y0, x1, y1, *points, ly, uy, gly, guy, gh, gy0, gy1, sx0, sy0, sx1, sy1;
        int count = 0;
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++) {
            for (int ci = 0; ci < path->_subPaths.size(); ci++) {
                points = path->_subPaths[ci]._points.get();
                y0 = (points[1] - ty) * scale;
                for (int pi = 0, npoints = path->_subPaths[ci]._npoints; pi < npoints; y0 = y1, pi++) {
                    y1 = (points[((pi + 1) % npoints) * 2 + 1] - ty) * scale;
                    ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
                    count += std::ceil(uy) - std::floor(ly);
                }
            }
        }
        edgesMemory = Memory<Edge>(count);
        edges = edgesMemory.get();
        EdgeList *list;
        for (Path *path = & scene._paths[0], *pbegin = path, *pend = path + scene._paths.size(); path < pend; path++) {
            for (int ci = 0; ci < path->_subPaths.size(); ci++) {
                points = path->_subPaths[ci]._points.get();
                x0 = (points[0] - tx) * scale, y0 = (points[1] - ty) * scale;
                for (int pi = 0, i = 2, npoints = path->_subPaths[ci]._npoints; pi < npoints; x0 = x1, y0 = y1, pi++, i = ((pi + 1) % npoints) * 2) {
                    x1 = (points[i] - tx) * scale, y1 = (points[i + 1] - ty) * scale;
                    ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
                    gly = std::floor(ly), guy = std::ceil(uy), gh = guy - gly;
                    list = & edgeLists[int(gly)];
                    for (gy0 = gly, gy1 = gy0 + 1; gh--; gy0 = gy1, gy1 += 1, list++) {
                        sy0 = y0 < gy0 ? gy0 : y0 > gy1 ? gy1 : y0;
                        sy1 = y1 < gy0 ? gy0 : y1 > gy1 ? gy1 : y1;
                        sx0 = y0 == y1 || sy0 == y0 ? x0 : x0 + (sy0 - y0) / (y1 - y0) * (x1 - x0);
                        sx1 = y0 == y1 || sy1 == y1 ? x1 : x0 + (sy1 - y0) / (y1 - y0) * (x1 - x0);
                        new (& edges[edgeIndex]) Edge(int(path - pbegin), sx0, sy0, sx1, sy1);
                        
                        *(list->begin == 0 ? & list->begin : & edges[list->last].next) = edgeIndex;
                        list->last = edgeIndex++;
                    }
                }
            }
        }
        indicesMemory = Memory<EdgeIndex>(edgeIndex * 2);
        activeEdgesMemory = Memory<ActiveEdge>(edgeIndex);
        eventsMemory = Memory<EdgeEvent>(edgeIndex);
    }
    
    Bounds bounds;
    float width, height, scale, dimension, tx, ty;
    int iw, ih, edgeIndex, edgeCount;
    Memory<Edge> edgesMemory;
    Edge *edges;
    Memory<EdgeList> listsMemory;
    EdgeList *edgeLists;
    Memory<EdgeIndex> indicesMemory;
    Memory<ActiveEdge> activeEdgesMemory;
    Memory<EdgeEvent> eventsMemory;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        int gy, ei, winding;
        float lx, ux, x0, y0, x1, y1, x2, y2, x3, y3;
        Edge *edge, *le, *ue;
        EdgeIndex *ibegin, *iend, *index, *ilast;
        ActiveEdge *abegin, *aend, *a, *asrc, *adst;
        
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        
//        Tess::writeCurves(mesh, scene);
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            mesh._colors.emplace_back(path->_color);
        
        Revolution revolution(scene);
        mesh._CTM = { revolution.dimension, 0, 0, revolution.dimension, revolution.tx, revolution.ty };
        
        for (gy = 0; gy < revolution.ih; gy++) {
            if ((ei = revolution.edgeLists[gy].begin)) {
                ibegin = revolution.indicesMemory.get(), iend = ibegin;
                do {
                    edge = revolution.edges + ei;
                    new (iend++) EdgeIndex(edge->x0 < edge->x1 ? edge->x0 : edge->x1, ei);
                    new (iend++) EdgeIndex(edge->x0 > edge->x1 ? edge->x0 : edge->x1, 0);
                } while ((ei = edge->next));
                
                radixSort(ibegin, iend);
                abegin = revolution.activeEdgesMemory.get(), aend = abegin;
                
                for (index = ibegin; index < iend; ) {
                    ilast = index, lx = ilast->x;
                    do
                        ilast++;
                    while (ilast < iend && lx == ilast->x);
                    ux = ilast < iend ? ilast->x : lx;
                    
                    asrc = adst = abegin;
                    for (; asrc < aend; asrc++) 
                        if (asrc->e->ux() > lx)
                            *adst++ = *asrc;
                    
                    aend = adst;
                    for (a = abegin; a < aend; a++)
                        a->update(ux);
                    
                    for (; index < ilast; index++) {
                        if (index->i) {
                            edge = revolution.edges + index->i;
                            if (edge->x0 != edge->x1) {
                                ActiveEdge anew(edge, lx, ux);
                                adst = aend++, asrc = adst - 1;
                                while (adst > abegin && anew < *asrc)
                                    *adst-- = *asrc--;
                                *adst = anew;
                            }
                        }
                    }
                    x0 = x3 = lx, x1 = x2 = ux;
                    y0 = y1 = y2 = y3 = 0;
                    le = ue = nullptr;
                    winding = 0;
                    for (a = abegin; a < aend; a++) {
                        y3 = a->ylx, y2 = a->yux, ue = a->e;
                        
                        if(winding & 1) {
                            Quad q(nullptr, nullptr, x0, y0, x1, y1, x2, y2, x3, y3);
                            Tess::writeQuad(mesh, &q, a->e->p);
                        }
                        winding += a->e->x0 > a->e->x1 ? -1 : 1;
                        y0 = y3, y1 = y2, le = ue;
                    }
                }
            }
        }
    }
};
