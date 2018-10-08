//
//  libTess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 06/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <dispatch/dispatch.h>

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
            if (path._strokeWidth) {
                float w, x0, y0, x1, y1, x2, y2, *q, *qend, dx, dy, l, nx, ny, pnx, pny, det;
                w = path._strokeWidth * 0.5, pnx = pny = 0;
                q = s._quadratics.get(), qend = q + s._quadratics.count();
                if (path._closed) {
                    x0 = *(qend - 6), y0 = *(qend - 5), x2 = *(qend - 2), y2 = *(qend - 1);
                    dx = x2 - x0, dy = y2 - y0, l = std::sqrt(dx * dx + dy * dy), pnx = -dy / l, pny = dx / l;
    
                    x0 = *(qend - 2), y0 = *(qend - 1), x2 = *q++, y2 = *q++, x1 = (x0 + x2) * 0.5, y1 = (y0 + y2) * 0.5;
                } else
                    x0 = *q++, y0 = *q++, x1 = *q++, y1 = *q++, x2 = *q++, y2 = *q++;
                while (q <= qend) {
                    if (x0 != x2 || y0 != y2) {
                        dx = x2 - x0, dy = y2 - y0, l = std::sqrt(dx * dx + dy * dy), nx = -dy / l, ny = dx / l;
                        det = pnx * ny - pny * nx;
                        float polygon[10] = {
                            x0 + nx * w, y0 + ny * w,
                            det == 0 ? x0 : det < 0 ? x0 + pnx * w : x0 - pnx * w, det == 0 ? y0 : det < 0 ? y0 + pny * w : y0 - pny * w,
                            x0 - nx * w, y0 - ny * w,
                            x2 - nx * w, y2 - ny * w,
                            x2 + nx * w, y2 + ny * w,
                        };
                        tessAddContour(dst, 2, polygon, kPointStride, 5);
                        
                        pnx = nx, pny = ny;
                    }
                    x0 = x2, y0 = y2, x1 = *q++, y1 = *q++, x2 = *q++, y2 = *q++;
                }
            } else
                addTessSubPathContour(dst, s);
    }
    
    static void addTessSubPathContour(TESStesselator *dst, SubPath& s) {
        const float kSharpLimit = 0.017452406437284;
        Memory<float> subpath;
        vector<size_t> sharps;
        size_t count = s._quadratics.count();
        float *qbegin = s._quadratics.get(), *q = qbegin, *qend, x0, y0, x1, y1, x2, y2, ax, ay, bx, by, sine;
        if (q[0] == q[count - 2] && q[1] == q[count - 1])
            x0 = q[count - 6], y0 = q[count - 5];
        else
            x0 = q[0], y0 = q[1];
        for (x1 = q[0], y1 = q[1], qend = q + count - 4; q < qend; q += 4, x0 = x1, y0 = y1, x1 = x2, y1 = y2) {
            x2 = q[4], y2 = q[5];
            ax = x1 - x0, ay = y1 - y0, bx = x2 - x1, by = y2 - y1;
            sine = (ax * by - ay * bx) / std::sqrt((ax * ax + ay * ay) * (bx * bx + by * by));
            if (std::abs(sine) < kSharpLimit)
                sharps.emplace_back(q - qbegin);
        }
        tessAddContour(dst, 2, qbegin, kPointStride * 2, (int)s._quadratics.count() / 4 + 1);
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
    
    static Vector miter(const float *verts, Edge *e, float x0, float y0, float x1, float y1, float x2, float y2, bool *concave) {
        float limit = 5e-2;
        float px, py, nx, ny, x, y;
        bool left, edge, tangent;
        
        px = verts[e->prev() * 2], py = verts[e->prev() * 2 + 1];
        nx = verts[e->next() * 2], ny = verts[e->next() * 2 + 1];
        edge = (x0 - px) * (ny - y0) - (y0 - py) * (nx - x0) > 0;
        tangent = (x1 - x0) * (ny - py) - (y1 - y0) * (nx - px);
        *concave = edge == tangent;
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
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        int p, i, base, divisions = 4, size = (int)scene._paths.size();
        
        mesh.empty();
        if (size == 0)
            return;
        
        for (p = 0; p < size; p++)
            mesh._timeline.pathsForTime(0).emplace_back(scene._paths[p]._color, scene._CTMs[p], scene._paths[p]._bounds);
        
        int counts[size], count, total;
        for (total = count = p = 0; p < size; counts[p] = count, total += count, count = 0, p++)
            for (SubPath& s : scene._paths[p]._subPaths)
                count += s._quadratics.count();
        int begins[divisions + 1], target, *b = begins;
        begins[0] = 0, begins[divisions] = size;
        for (count = p = 0, i = 1; i < divisions; i++) {
            target = total * i / divisions;
            while (count < target)
                count += counts[p++];
            begins[i] = p;
        }
        vector<Triangle> tris[divisions], *triss = tris;
        vector<int> idxs[divisions], *idxss = idxs;
        if (divisions == 1)
            triangulateRange(tris[0], idxss[0], scene, 0, size);
        else {
            dispatch_apply(divisions, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                triangulateRange(triss[idx], idxss[idx], scene, b[idx], b[idx + 1]);
            });
        }
        for (base = i = 0; i < divisions; base += (int)tris[i].size(), i++) {
            memcpy(mesh._tris.new_ptr(tris[i].size()), & tris[i][0], tris[i].size() * sizeof(tris[i][0]));
            for (int idx : idxs[i])
                mesh._idxs.emplace_back(base + idx);
        }
        mesh._idxs.emplace_back((int)mesh._tris.count());
    }
    
    static void triangulateRange(vector<Triangle>& _tris, vector<int>& _idxs, Scene& scene, int begin, int end) {
        TESStesselator *tess = tessNewTess(NULL), *dst = tessNewTess(NULL);
        vector<float> scales;
        tessSetOption(dst, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
        Memory<Edge> _edges;
        for (int p = begin; p < end; p++) {
            Path& path = scene._paths[p];
            float lx, ly, rw, rh;
			lx = path._bounds.lx, rw = kUnsignedShortMax / (path._bounds.ux - lx);
			ly = path._bounds.ly, rh = kUnsignedShortMax / (path._bounds.uy - ly);

            _idxs.emplace_back((int)_tris.size());
            
            addPathContours(tess, path);
            if (tessTesselate(tess, path._evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal)) {
                int count = tessGetVertexCount(tess);
                const TESSindex *indices = tessGetVertexIndices(tess);
                const float *verts = tessGetVertices(tess);
                _edges.empty();
                Edge *edges = _edges.new_ptr(count), *eend = edges + count, *e0, *e1, *e2;
                addTessContours(dst, tess, edges);
                sort(edges, eend);
                if (tessTesselate(dst, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                    float *points = (float *)tessGetVertices(dst);
                    const int nelems = tessGetElementCount(dst);
                    const TESSindex *polygon = tessGetElements(dst);
                    float x0, y0, x1, y1, x2, y2, t, t1, a0, a1, ta, tx0, ty0, tx1, ty1, tx2, ty2, tp0[2], tp1[2], tp2[2];
                    bool ef0, ef1, ef2, left0, left1, left2;
                    Vector m0, m1, m2;
                    int i, i0, i1, i2, oi0, oi1, oi2;
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
                        ef0 = e0->next() == e1->idx() && e0->base == e1->base;
                        ef1 = e1->next() == e2->idx() && e1->base == e2->base;
                        ef2 = e2->next() == e0->idx() && e2->base == e0->base;
                        
                        oi0 = indices[e0->idx()], oi1 = indices[e1->idx()], oi2 = indices[e2->idx()];
                        if (oi0 != TESS_UNDEF) {
                            path.controlPointForIndex(tp0, oi0 * 2);
//                            assert(x0 == tp0[0] && y0 == tp0[1]);
                        }
                        if (oi1 != TESS_UNDEF) {
                            path.controlPointForIndex(tp1, oi1 * 2);
//                            assert(x1 == tp1[0] && y1 == tp1[1]);
                        }
                        if (oi2 != TESS_UNDEF) {
                            path.controlPointForIndex(tp2, oi2 * 2);
//                            assert(x2 == tp2[0] && y2 == tp2[1]);
                        }
                        
                       // if (int(ef0) + int(ef1) + int(ef2) != 1)
                       //     continue;
                        
                        
                        m0 = miter(verts, e0, x0, y0, x1, y1, x2, y2, & left0);
                        m1 = miter(verts, e1, x1, y1, x2, y2, x0, y0, & left1);
                        m2 = miter(verts, e2, x2, y2, x0, y0, x1, y1, & left2);
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
						
                        _tris.emplace_back(p, ef0, ef1, ef2,
                                           left0, left1, left2,
										   ta > t ? ta : t,
                                           (x0 - lx) * rw, (y0 - ly) * rh,
                                           (x1 - lx) * rw, (y1 - ly) * rh,
                                           (x2 - lx) * rw, (y2 - ly) * rh,
                                           m0.x, m0.y, m1.x, m1.y, m2.x, m2.y
                        );
                    }
                }
            }
        }
        tessDeleteTess(tess), tessDeleteTess(dst);
    }
};
