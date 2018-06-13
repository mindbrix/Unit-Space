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
        int next, p;
        float x0, y0, x1, y1;
    };
    struct EdgeList {
        int begin, last;
    };
    
    Revolution(Scene &scene) : bounds(scene._bounds), width(bounds.ux - bounds.lx), height(bounds.uy - bounds.ly), cellIndex(1), cellCount(0) {
        float dmax = width > height ? width : height;
        scale = float(kDivisions) / dmax, dimension = dmax / float(kDivisions), tx = bounds.lx, ty = bounds.ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
        listsMemory = Memory<EdgeList>(ih);
        edgeLists = listsMemory.zero().get();
        float y0, y1, *points, rly, ruy;
        int count = 0;
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++) {
            for (int ci = 0; ci < path->_subPaths.size(); ci++) {
                points = path->_subPaths[ci]._points.get();
                y0 = (points[1] - ty) * scale;
                for (int pi = 0, npoints = path->_subPaths[ci]._npoints; pi < npoints; y0 = y1, pi++) {
                    y1 = (points[((pi + 1) % npoints) * 2 + 1] - ty) * scale;
                    rly = y0 < y1 ? y0 : y1, ruy = y0 > y1 ? y0 : y1;
                    count += std::ceil(ruy) - std::floor(rly);
                }
            }
        }
        edgesMemory = Memory<Edge>(count);
        edges = edgesMemory.get();
//        rowCountsMemory = Memory<int>(ih);
//        rowCounts = rowCountsMemory.zero().get();
    }
    
    Bounds bounds;
    float width, height, scale, dimension, tx, ty;
    int iw, ih, cellIndex, cellCount;
    Memory<Edge> edgesMemory;
    Edge *edges;
    Memory<EdgeList> listsMemory;
    EdgeList *edgeLists;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        
        Tess::writeCurves(mesh, scene);
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            mesh._colors.emplace_back(path->_color);
        
        Revolution revolution(scene);
    }
};
