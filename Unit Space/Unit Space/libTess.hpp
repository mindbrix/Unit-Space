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

//constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
//constexpr int kPolySize = 3;

struct libTess {
    libTess() : _tess(shared_ptr<TESStesselator>(tessNewTess(NULL), tessDeleteTess)) {}
    
    void maskPaths(TESStesselator *tess, Scene& scene) {
        VG::QuadTree<size_t> quadtree(0.f, 0.f, scene.w, scene.h);
                              
        for (Path& path : scene._paths)
            Path::resolveSelfIntersections(tess, path);
        
        for (size_t i = 0; i < scene._paths.size(); i++) {
            Path& path = scene._paths[i];
            
            auto matches = quadtree.find(path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy);
            for (auto j : matches)
                Path::maskPath(tess, scene._paths[j], path);
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
        TESStesselator *tess = _tess.get();
        
        if (useMasking)
            maskPaths(tess, scene);
        writeTessTriangles(mesh, tess, scene);
    }
    
    shared_ptr<TESStesselator>            _tess;
};
