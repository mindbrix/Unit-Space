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
    
    inline int rowCount(float y0, float y1) {
        float ry0, ry1, rly, ruy;
        ry0 = (y0 - ty) * scale, ry1 = (y1 - ty) * scale;
        rly = ry0 < ry1 ? ry0 : ry1;
        ruy = ry0 > ry1 ? ry0 : ry1;
        return std::ceil(ruy) - std::floor(rly);
    }
    
    Revolution(Scene &scene) : bounds(scene._bounds), width(bounds.ux - bounds.lx), height(bounds.uy - bounds.ly), cellIndex(1), cellCount(0) {
        float dmax = width > height ? width : height;
        scale = float(kDivisions) / dmax, dimension = dmax / float(kDivisions), tx = bounds.lx, ty = bounds.ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
        listsMemory = Memory<EdgeList>(ih);
        edgeLists = listsMemory.zero().get();
//        edgesMemory = scene.writeEdges(cellCount, scale, tx, ty);
//        cellsMemory = Memory<Cell>(cellCount + 1);
//        cells = cellsMemory.get();
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
