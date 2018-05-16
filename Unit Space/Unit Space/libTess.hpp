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
    struct Cell {
        Cell(float lx, float ly, float ux, float uy, int p = INT_MAX) : lx(lx), ly(ly), ux(ux), uy(uy), count(0), p(p) {}
        ~Cell() {
            for (int i = 0; i < count; i++)
                delete children[i];
        }
        inline bool operator< (const Cell& other) const     { return lx < other.lx || (lx == other.lx && ly < other.ly); }
        
        void intersect(float ilx, float ily, float iux, float iuy, int ip) {
            float rlx, rly, rux, ruy;
            if (count == 0) {
                rlx = lx > ilx ? lx : ilx;
                rly = ly > ily ? ly : ily;
                rux = ux < iux ? ux : iux;
                ruy = uy < iuy ? uy : iuy;
                assert(rlx != rux && rly != ruy);
                
                children[count++] = new Cell(rlx, rly, rux, ruy, ip);
                if (rly > ly)
                    children[count++] = new Cell(lx, ly, ux, rly, INT_MAX);
                if (rlx > lx)
                    children[count++] = new Cell(lx, rly, rlx, ruy, INT_MAX);
                if (rux < ux)
                    children[count++] = new Cell(rux, rly, ux, ruy, INT_MAX);
                if (ruy < uy)
                    children[count++] = new Cell(lx, ruy, ux, uy, INT_MAX);
            } else
                for (int i = 0; i < count; i++)
                    if (children[i]->ux > ilx && iux > children[i]->lx && children[i]->uy > ily && iuy > children[i]->ly)
                        children[i]->intersect(ilx, ily, iux, iuy, ip);
        }
        bool merge(Cell &other) {
            if (other.ux >= lx && ux >= other.lx && other.uy >= ly && uy >= other.ly) {
                lx = lx < other.lx ? lx : other.lx;
                ly = ly < other.ly ? ly : other.ly;
                ux = ux > other.ux ? ux : other.ux;
                uy = uy > other.uy ? uy : other.uy;
                return true;
            }
            return false;
        }
        void writeLeaves(vector<Cell *>& leaves, int *paths, int depth = 0) {
            if (p != INT_MAX)
                paths[depth++] = p;
//            depth += p != INT_MAX ? 1 : 0;
            p = depth;
            if (count == 0)
                leaves.emplace_back(this);
            else
                for (int i = 0; i < count; i++)
                    children[i]->writeLeaves(leaves, paths, depth);
        }
        
        float lx, ly, ux, uy;
        Cell *children[5];
        int count;
        int p;
    };
    
    libTess() : _tess(shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess)) {}
    
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
    
    static void resolveSelfIntersections(TESStesselator *tess, Path& path) {
        if (path._subPaths.size() == 1 && path._subPaths[0]._npoints == 3) {
            path.update();
            return;
        }
        for (SubPath& subPath : path._subPaths)
            if (subPath._npoints > 2)
                tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        
        if (tessTesselate(tess, path._evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess, path);
        
    }
    static void maskPath(TESStesselator *tess, Path& path, Path& mask) {
        bool isPositive = path._subPaths[path._outerIndex]._area > 0;
        bool maskIsPositive = mask._subPaths[mask._outerIndex]._area > 0;
        
        for (SubPath& subPath : path._subPaths) {
            tessSetOption(tess, TESS_REVERSE_CONTOURS, !isPositive);
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        }
        
        for (SubPath& subPath : mask._subPaths) {
            tessSetOption(tess, TESS_REVERSE_CONTOURS, maskIsPositive);
            tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        }
        
        if (tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess, path);
    }
    
    
    void maskPaths(TESStesselator *tess, Scene& scene) {
        VG::QuadTree<size_t> quadtree(0.f, 0.f, scene.w, scene.h);
                              
        for (Path& path : scene._paths)
            resolveSelfIntersections(tess, path);
        
        for (size_t i = 0; i < scene._paths.size(); i++) {
            Path& path = scene._paths[i];
            
            auto matches = quadtree.find(path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy);
            for (auto j : matches)
                maskPath(tess, scene._paths[j], path);
            quadtree.add(i, path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy);
        }
    }
    static void writeTessTriangles(Mesh& mesh, TESStesselator *tess, Scene& scene) {
        tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
        
        for (Path& path : scene._paths) {
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
    
    void triangulate(Mesh& mesh, Scene& scene, bool useMasking, bool useDelaunay) {
        mesh.empty();
        
        for (Path& path : scene._paths)
            path.update();
        
        vector<Cell> bboxes, clusters, clustered;
        for (Path& path : scene._paths)
            bboxes.emplace_back(path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy, 0);
        sort(bboxes.begin(), bboxes.end());
        
        float wmax = scene.w * 0.1, hmax = scene.h * 0.1;
        for (Cell& bbox : bboxes) {
//            for (Cell& cluster : clusters)
//                if (cluster.ux < bbox.lx)
//                    clustered.emplace_back(cluster);
            
            if (bbox.ux - bbox.lx < wmax && bbox.uy - bbox.ly < hmax) {
                bool merged = false;
                for (Cell& cluster : clusters)
                    if (cluster.merge(bbox)) {
                        cluster.p++;
                        merged = true;
                        break;
                    }
                
                if (!merged)
                    clusters.emplace_back(bbox);
            }
        }
        Cell cell(0, 0, scene.w, scene.h);
        
        for (Cell& cluster : clusters)
            if (cluster.p > 0)
                cell.intersect(cluster.lx, cluster.ly, cluster.ux, cluster.uy, INT_MAX);
        
        for (Path& path : scene._paths)
            cell.intersect(path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy, int(& path - & scene._paths[0]));
        
        vector<Cell *> leaves;
        int paths[scene._paths.size()];
        cell.writeLeaves(leaves, paths);
        size_t pi = 0;
        int pmax = 0;
        for (Cell *leaf : leaves) {
            if (leaf->p == INT_MAX)
                continue;
            if (leaf->p > pmax)
                pmax = leaf->p;
            
            pi = mesh._points.size();
            mesh._points.emplace_back(leaf->lx, leaf->ly);
            mesh._points.emplace_back(leaf->ux, leaf->ly);
            mesh._points.emplace_back(leaf->ux, leaf->uy);
            mesh._points.emplace_back(leaf->lx, leaf->uy);
            
            mesh._triangles.emplace_back(pi + 0, pi + 1, pi + 2, leaf->p);
            mesh._triangles.emplace_back(pi + 0, pi + 2, pi + 3, leaf->p);
        }
        
        for (int i = 0; i < pmax; i++)
            mesh._colors.emplace_back(RGBA8(255 * i / pmax, 255 * i / pmax, 255 * i / pmax, 255));
        
//        for (Path& path : scene._paths)
//            mesh._colors.emplace_back(path._color);
        
        
        
//        Tess::writeCurves(mesh, scene);
//        TESStesselator *tess = _tess.get();
//
//        if (useMasking)
//            maskPaths(tess, scene);
//        writeTessTriangles(mesh, tess, scene);
    }
    
    shared_ptr<TESStesselator>            _tess;
};
