//
//  libTess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 06/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Scene.hpp"
#import <vector>
#import "tesselator.h"
#import "Algorithms.hpp"

constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
constexpr int kPolySize = 3;

struct libTess {
    struct Edge {
        Edge() {}
        Edge(float x, float y, int base, int i, int npoints) : x(x), y(y), base((unsigned int)base), i((unsigned short int )i), npoints((unsigned short int )npoints) {}
        inline int idx()                                { return base + i; }
        inline int prev()                               { return base + (i + npoints - 1) % npoints; }
        inline int next()                               { return base + (i + 1) % npoints; }
        inline bool operator< (const Edge& other) const { return x < other.x || (x == other.x && y < other.y); }
        float x, y;
        unsigned int base;
        unsigned short int i, npoints;
    };
    
    static void addPathContours(TESStesselator *dst, Path& path) {
        for (SubPath& s : path._subPaths)
            if (s._npoints > 2) {
                if (s._quadratics.get())
                    tessAddContour(dst, 2, s._quadratics.get(), kPointStride * 2, (int)s._quadratics.count() / 4 + 1);
                else
                    tessAddContour(dst, 2, s._points.get(), kPointStride, s._npoints);
            }
    }
    
    static void addTessContours(TESStesselator *dst, TESStesselator *tess, Edge *edge) {
        const float *verts = tessGetVertices(tess);
        const int *elems = tessGetElements(tess);
        const int nelems = tessGetElementCount(tess);
        int e, base, npoints, i;
        for (e = 0; e < nelems; e++) {
            base = elems[e * 2], npoints = elems[e * 2 + 1];
            if (npoints > 2) {
                tessAddContour(dst, 2, verts + base * 2, kPointStride, npoints);
                for (i = 0; i < npoints; i++)
                    new (edge++) Edge(verts[(base + i) * 2], verts[(base + i) * 2 + 1], base, i, npoints);
            }
        }
    }
    
    static void writeControlPoints(vector<float>& dst, Path& path) {
        for (SubPath& s : path._subPaths)
            if (s._quadratics.get() == nullptr)
                return;
        
        for (SubPath& s : path._subPaths) {
            int npoints = (int)s._quadratics.count() / 4 + 1;
            float *points = s._quadratics.get();
            for (int i = 0; i < npoints; i++)
                dst.emplace_back(points[i * 4]), dst.emplace_back(points[i * 4 + 1]);
        }
    }
    
   static Vector miter(const float *verts, Edge *e, float x0, float y0, float x1, float y1, float x2, float y2) {
        float limit = 5e-1;
        float px, py, nx, ny, x, y;
        bool left;
        
        px = verts[e->prev() * 2], py = verts[e->prev() * 2 + 1];
        nx = verts[e->next() * 2], ny = verts[e->next() * 2 + 1];
        if (!(x1 == px && y1 == py) && !(x1 == nx && y1 == ny))
            x = x1, y = y1;
        else
            x = x2, y = y2;
        left = (x0 - px) * (y - py) - (y0 - py) * (x - px) > 0 || (nx - x0) * (y - y0) - (ny - y0) * (x - x0) > 0;
        if ((x1 == px && y1 == py) || (x1 == nx && y1 == ny))
            if ((x2 == px && y2 == py) || (x2 == nx && y2 == ny))
                limit = 5e-3;
        return miterVector(x0, y0, px, py, nx, ny, left, limit, 1);
    }
    
    static void triangulate(Mesh& mesh, Scene& scene, bool useMasking) {
        mesh.empty();
        TESStesselator *tess = tessNewTess(NULL), *dst = tessNewTess(NULL);
        
        vector<float> controlPoints;
        tessSetOption(dst, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
        for (int p = 0; p < scene._paths.size(); p++) {
            Path& path = scene._paths[p];
            mesh._paths.emplace_back(path._color, (int)(mesh._points.size() / kMeshTriangleSize), (int)mesh._edges.size(), scene._CTMs[p]);
            mesh._bounds.emplace_back(path._bounds);
            
            controlPoints.resize(0);
            writeControlPoints(controlPoints, path);
            addPathContours(tess, path);
            if (tessTesselate(tess, path._evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
                int count = tessGetVertexCount(tess), idx;
                const TESSindex *indices = tessGetVertexIndices(tess);
                const float *verts = tessGetVertices(tess);
                Edge edges[count], *eend = edges + count, *e0, *e1, *e2;
                addTessContours(dst, tess, edges);
                if (controlPoints.size())
                    for (Edge& edge : edges) {
                        if ((idx = indices[edge.base + edge.i]) != -1) {
                            assert(edge.x == controlPoints[idx * 2]);
                            assert(edge.y == controlPoints[idx * 2 + 1]);
                        }
                    }
                sort(edges, eend);
                if (tessTesselate(dst, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                    float *points = (float *)tessGetVertices(dst);
                    const int nelems = tessGetElementCount(dst);
                    const TESSindex *polygon = tessGetElements(dst);
                    float x0, y0, x1, y1, x2, y2, t, t1, a0, a1, ta, tx0, ty0, tx1, ty1, tx2, ty2;
                    Vector m0, m1, m2;
                    int i, i0, i1, i2;
                    unsigned int _edges[count];
                    memset(_edges, 0xFFFFFFFF, sizeof(_edges));
                    for (i = 0; i < nelems; i++, polygon += kPolySize) {
                        i0 = polygon[0], i1 = polygon[1], i2 = polygon[2];
                        
                        x0 = points[i0 * 2], y0 = points[i0 * 2 + 1];
                        x1 = points[i1 * 2], y1 = points[i1 * 2 + 1];
                        x2 = points[i2 * 2], y2 = points[i2 * 2 + 1];
                        
                        e0 = lower_bound(edges, eend, Edge(x0, y0, 0, 0, 0));
                        e1 = lower_bound(edges, eend, Edge(x1, y1, 0, 0, 0));
                        e2 = lower_bound(edges, eend, Edge(x2, y2, 0, 0, 0));
                        if (e0->base != e1->base || e1->base != e2->base || e2->base != e0->base) {
                            if (e0->base == e1->base) {
                                while (e2 < eend - 1 && (e2 + 1)->x == x2 && (e2 + 1)->y == y2)
                                    if ((++e2)->base == e0->base)
                                        break;
                            } else if (e1->base == e2->base) {
                                while (e0 < eend - 1 && (e0 + 1)->x == x0 && (e0 + 1)->y == y0)
                                    if ((++e0)->base == e1->base)
                                        break;
                            } else if (e2->base == e0->base) {
                                while (e1 < eend - 1 && (e1 + 1)->x == x1 && (e1 + 1)->y == y1)
                                    if ((++e1)->base == e2->base)
                                        break;
                            }
                        }
                        if (x1 == verts[e0->next() * 2] && y1 == verts[e0->next() * 2 + 1])
                            _edges[e0->base + e0->i] = (mesh._paths.back().idx + i) * 3;
                        if (x2 == verts[e1->next() * 2] && y2 == verts[e1->next() * 2 + 1])
                            _edges[e1->base + e1->i] = (mesh._paths.back().idx + i) * 3 + 1;
                        if (x0 == verts[e2->next() * 2] && y0 == verts[e2->next() * 2 + 1])
                            _edges[e2->base + e2->i] = (mesh._paths.back().idx + i) * 3 + 2;
                        
                        m0 = miter(verts, e0, x0, y0, x1, y1, x2, y2);
                        m1 = miter(verts, e1, x1, y1, x2, y2, x0, y0);
                        m2 = miter(verts, e2, x2, y2, x0, y0, x1, y1);
                        solveTriangle(Vector(x0, y0), m0, Vector(x1, y1), m1, Vector(x2, y2), m2, & t, & t1);
                        tx0 = x0 + m0.x * t, ty0 = y0 + m0.y * t;
                        tx1 = x1 + m1.x * t, ty1 = y1 + m1.y * t;
                        tx2 = x2 + m2.x * t, ty2 = y2 + m2.y * t;
                        a0 = std::abs((tx1 - tx0) * (ty2 - ty1) - (ty1 - ty0) * (tx2 - tx1)), a1 = 0;
                        if (t1 != -1) {
                            tx0 = x0 + m0.x * t1, ty0 = y0 + m0.y * t1;
                            tx1 = x1 + m1.x * t1, ty1 = y1 + m1.y * t1;
                            tx2 = x2 + m2.x * t1, ty2 = y2 + m2.y * t1;
                            a1 = std::abs((tx1 - tx0) * (ty2 - ty1) - (ty1 - ty0) * (tx2 - tx1));
                        }
                        t = 1.0 / std::abs(t1 > 1e-2 && (a1 < 1 || a1 < a0) ? t1 : t);
                        ta = 1.0 / std::sqrt(0.5 * std::abs((x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1)));
                        
                        mesh._points.emplace_back(p, ta > t ? ta : t);
                        mesh._points.emplace_back(x0, y0), mesh._points.emplace_back(m0);
                        mesh._points.emplace_back(x1, y1), mesh._points.emplace_back(m1);
                        mesh._points.emplace_back(x2, y2), mesh._points.emplace_back(m2);
//                        mesh._points.emplace_back(0, 0);
                    }
                    for (unsigned int e : _edges)
                        mesh._edges.emplace_back(e);
                }
            }
        }
        tessDeleteTess(tess), tessDeleteTess(dst);
    }
};
