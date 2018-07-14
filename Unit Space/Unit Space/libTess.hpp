//
//  libTess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 06/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Tess.hpp"
#import <vector>
#import "tesselator.h"
#import "Algorithms.hpp"
#import "VGQuadTree.hpp"

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
    
    libTess() : _tess(shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess)), _dst(shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess)) {}
    
    static void writeTessSubpaths(TESStesselator *tess, Path& path) {
        path._subPaths.resize(0);
        
        const float *verts = tessGetVertices(tess);
        const int *elems = tessGetElements(tess);
        const int nelems = tessGetElementCount(tess);
        
        for (int i = 0; i < nelems; i++) {
            int base = elems[i * 2], npoints = elems[i * 2 + 1];
            const float *points = verts + base * 2;
            
            if (npoints > 2)
                path._subPaths.emplace_back(points, npoints, SubPath::kLine);
        }
        path.update();
    }
    
    static void addContours(TESStesselator *dst, TESStesselator *src, Edge *edge) {
        const float *verts = tessGetVertices(src);
        const int *elems = tessGetElements(src);
        const int nelems = tessGetElementCount(src);
        int base, npoints, i, j;
        
        for (i = 0; i < nelems; i++) {
            base = elems[i * 2], npoints = elems[i * 2 + 1];
            const float *points = verts + base * 2;
            
            if (npoints > 2) {
                tessAddContour(dst, 2, points, kPointStride, npoints);
                if (edge)
                    for (j = 0; j < npoints; j++)
                        new (edge++) Edge(points[j * 2], points[j * 2 + 1], base, j, npoints);
            }
        }
    }
    
    static void addContours(TESStesselator *tess, Path& path, bool reverse) {
        tessSetOption(tess, TESS_REVERSE_CONTOURS, reverse ? 1 : 0);
        for (SubPath& s : path._subPaths)
            if (s._npoints > 2) {
                if (s._quadratics.get())
                    tessAddContour(tess, 2, s._quadratics.get(), kPointStride * 2, (int)s._quadratics.count() / 4);
                else
                    tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints);
            }
    }
    
    static void resolveSelfIntersections(TESStesselator *tess, Path& path) {
        if (path._subPaths.size() == 1 && path._subPaths[0]._npoints == 3) {
            path.update();
            return;
        }
        addContours(tess, path, false);
        if (tessTesselate(tess, path._evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess, path);
    }
    static void maskPath(TESStesselator *tess, Path& path, Path& mask) {
        bool isPositive = path._subPaths[path._outerIndex]._area > 0;
        bool maskIsPositive = mask._subPaths[mask._outerIndex]._area > 0;
        addContours(tess, path, !isPositive);
        addContours(tess, mask, maskIsPositive);
        if (tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess, path);
    }
    
    void maskPaths(TESStesselator *tess, Scene& scene) {
        for (Path& path : scene._paths)
            resolveSelfIntersections(tess, path);
        
        Bounds bounds = scene._paths[0]._bounds;
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            bounds.unite(path->_bounds);
        
        VG::QuadTree<size_t> quadtree(0.f, 0.f, bounds.ux, bounds.uy);
        
        for (size_t i = 0; i < scene._paths.size(); i++) {
            Path& path = scene._paths[i];
            
            auto matches = quadtree.find(path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy);
            for (auto j : matches)
                maskPath(tess, scene._paths[j], path);
            quadtree.add(i, path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy);
        }
    }
    
    static void writeTriangles(Mesh& mesh, TESStesselator *tess, int CDT, int windingRule, int p) {
        tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, CDT);
        if (tessTesselate(tess, windingRule, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
            float *points = (float *)tessGetVertices(tess);
            int npoints = tessGetVertexCount(tess);
            const int nelems = tessGetElementCount(tess);
            const TESSindex *polygon = tessGetElements(tess);
            
            int pi = (int)mesh._points.size();
            for (int i = 0; i < nelems; i++, polygon += kPolySize)
                mesh._triangles.emplace_back(pi + polygon[0], pi + polygon[1], pi + polygon[2], p);
            
            for (int i = 0; i < npoints; i++)
                mesh._points.emplace_back(points[i * 2], points[i * 2 + 1]);
        }
    }
    static void writePathTriangles(Mesh& mesh, TESStesselator *tess, Path& path) {
        mesh._colors.emplace_back(path._color);
        addContours(tess, path, false);
        writeTriangles(mesh, tess, 1, TESS_WINDING_NONZERO, (int)mesh._colors.size() - 1);
    }
    
    static void writeTessTriangles(Mesh& mesh, TESStesselator *tess, Scene& scene) {
        for (Path& path : scene._paths) {
            mesh._triangleIndices.emplace_back((int)mesh._triangles.size());
            writePathTriangles(mesh, tess, path);
        }
    }
    
    Vector miter(const float *edgePoints, Edge *e, float x0, float y0, float x1, float y1, float x2, float y2) {
        float limit = 5e-1;
        float px, py, nx, ny, x, y;
        bool left;
        
        px = edgePoints[e->prev() * 2], py = edgePoints[e->prev() * 2 + 1];
        nx = edgePoints[e->next() * 2], ny = edgePoints[e->next() * 2 + 1];
        if (!(x1 == px && y1 == py) && !(x1 == nx && y1 == ny))
            x = x1, y = y1;
        else
            x = x2, y = y2;
        left = (x0 - px) * (y - py) - (y0 - py) * (x - px) > 0 || (nx - x0) * (y - y0) - (ny - y0) * (x - x0) > 0;
        if ((x1 == px && y1 == py) || (x1 == nx && y1 == ny))
            if ((x2 == px && y2 == py) || (x2 == nx && y2 == ny))
                limit = 5e-3;
        return miterVector(x0, y0, px, py, nx, ny, left, limit);
    }
    
    void triangulate(Mesh& mesh, Scene& scene, bool useMasking) {
        mesh.empty();
        TESStesselator *tess = _tess.get(), *dst = _dst.get();
        
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
        
        for (int i = 0; i < scene._paths.size(); i++)
            mesh._pathCTMs.emplace_back(scene._CTMs[i]);
        
        Tess::writeCurves(mesh, scene);
        if (useMasking) {
            maskPaths(tess, scene);
            writeTessTriangles(mesh, tess, scene);
        } else {
            tessSetOption(dst, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
            for (int p = 0; p < scene._paths.size(); p++) {
                Path& path = scene._paths[p];
                if (p == 180) {
                    p = p;
                }
//            for (Path& path : scene._paths) {
                mesh._triangleIndices.emplace_back((int)mesh._triangles.size());
                addContours(tess, path, false);
                if (tessTesselate(tess, path._evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
                    int count = tessGetVertexCount(tess);
                    const float *edgePoints = tessGetVertices(tess);
                    Edge edges[count], *eend = edges + count, *e0, *e1, *e2;
                    addContours(dst, tess, edges);
                    sort(edges, eend);
                    if (tessTesselate(dst, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                        float *points = (float *)tessGetVertices(dst);
                        int npoints = tessGetVertexCount(dst);
                        const int nelems = tessGetElementCount(dst);
                        const TESSindex *polygon = tessGetElements(dst);
                        float x0, y0, x1, y1, x2, y2, t, t1, a0, a1;
                        float tx0, ty0, tx1, ty1, tx2, ty2;
                        Vector m0, m1, m2;
                        
                        int pi = (int)mesh._points.size(), i0, i1, i2;
                        for (int i = 0; i < nelems; i++, polygon += kPolySize) {
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
                            m0 = miter(edgePoints, e0, x0, y0, x1, y1, x2, y2);
                            m1 = miter(edgePoints, e1, x1, y1, x2, y2, x0, y0);
                            m2 = miter(edgePoints, e2, x2, y2, x0, y0, x1, y1);
                            
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
                            mesh._triangles.emplace_back(pi + polygon[0], pi + polygon[1], pi + polygon[2], (int) (&path - & scene._paths[0]));
                            mesh._interiorScales.emplace_back(t1 > 1e-2 && (a1 < 1 || a1 < a0) ? t1 : t);
                            mesh._interiorVectors.emplace_back(m0), mesh._interiorVectors.emplace_back(m1), mesh._interiorVectors.emplace_back(m2);
                        }
                        for (int i = 0; i < npoints; i++)
                            mesh._points.emplace_back(points[i * 2], points[i * 2 + 1]);
                    }
                }
            }
        }
    }
    
    shared_ptr<TESStesselator> _tess, _dst;
};
