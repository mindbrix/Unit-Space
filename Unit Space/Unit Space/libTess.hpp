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
    libTess() : _tess(shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess)), _dst(shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess)) {}
    
    static void writeTessSubpaths(TESStesselator *tess, TESStesselator *dst, Path& path) {
        const float *verts = tessGetVertices(tess);
        const int *elems = tessGetElements(tess);
        const int nelems = tessGetElementCount(tess);
        
        for (int i = 0; i < nelems; i++) {
            int base = elems[i * 2], npoints = elems[i * 2 + 1];
            const float *points = verts + base * 2;
            
            if (npoints > 2)
                tessAddContour(dst, 2, points, kPointStride, npoints);
        }
    }
    
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
    
    static void resolveSelfIntersections(TESStesselator *tess, TESStesselator *dst, Path& path) {
        for (SubPath& subPath : path._subPaths)
            if (subPath._npoints > 2)
                tessAddContour(tess, 2, subPath._points.get(), kPointStride, subPath._npoints);
        
        if (tessTesselate(tess, path._evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
            writeTessSubpaths(tess, dst, path);
        
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
    static void addMaskContours(TESStesselator *tess, Path& path, bool reversed) {
        bool isPositive = path._subPaths[path._outerIndex]._area > 0;
        tessSetOption(tess, TESS_REVERSE_CONTOURS, reversed && isPositive);
        for (SubPath& s : path._subPaths)
            if (s._npoints > 2)
                tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints);
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
        scene.updateNpoints();
    }
    
    static void writeTriangles(Mesh& mesh, TESStesselator *tess, int windingRule, int p, float scale, float tx, float ty) {
        tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 0);
        if (tessTesselate(tess, windingRule, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
            float *points = (float *)tessGetVertices(tess);
            int npoints = tessGetVertexCount(tess);
            const int nelems = tessGetElementCount(tess);
            const TESSindex *polygon = tessGetElements(tess);
            
            int pi = (int)mesh._points.size();
            for (int i = 0; i < nelems; i++, polygon += kPolySize)
                mesh._triangles.emplace_back(pi + polygon[0], pi + polygon[1], pi + polygon[2], p);
            
            for (int i = 0; i < npoints; i++)
                mesh._points.emplace_back((points[i * 2] + tx) * scale, (points[i * 2 + 1] + ty) * scale);
        }
    }
    static void writePathTriangles(Mesh& mesh, TESStesselator *tess, Path& path) {
        mesh._colors.emplace_back(path._color);
        
        for (SubPath& s : path._subPaths)
            tessAddContour(tess, 2, s._points.get(), kPointStride, s._npoints);
        writeTriangles(mesh, tess, TESS_WINDING_NONZERO, (int)mesh._colors.size() - 1, 1, 0, 0);
    }
    
    static void writeTessTriangles(Mesh& mesh, TESStesselator *tess, Scene& scene) {
        for (Path& path : scene._paths)
            writePathTriangles(mesh, tess, path);
    }
    
    void triangulate(Mesh& mesh, Scene& scene, bool useMasking) {
        mesh.empty();
        TESStesselator *tess = _tess.get(), *dst = _dst.get();
        
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
        
        Tess::writeCurves(mesh, scene);
        if (useMasking) {
            maskPaths(tess, scene);
            writeTessTriangles(mesh, tess, scene);
        } else {
            for (Path& path : scene._paths)
                resolveSelfIntersections(tess, dst, path);
            writeTriangles(mesh, dst, TESS_WINDING_NONZERO, (int)mesh._colors.size() - 1, 1, 0, 0);
        }
    }
    
    shared_ptr<TESStesselator> _tess, _dst;
};
