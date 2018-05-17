//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Tess.hpp"
#import <vector>

constexpr int kDivisions = 32;

struct Grid {
    struct Cell {
        Cell() : edge(false), winding(0) {}
        
        bool edge;
        float winding;
    };
    
    Grid(float w, float h) {
        float dmax = w > h ? w : h;
        dimension = std::ceil(dmax / float(kDivisions));
        iw = std::ceil(w / dimension), ih = std::ceil(h / dimension);
        cells.resize(iw * ih);
    }
    
    void writeEdgeCells(float x0, float y0, float x1, float y1) {
        float dxdy, ly, uy, ily, iuy, cly, cuy, cy0, cy1, cx0, cx1, clx, cux, coverage;
        int ilx, iux, iy, ix;
        Cell *cell;
        
        if (y0 == y1)
            return;
        dxdy = (x1 - x0) / (y1 - y0);
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        ily = std::floor(ly / dimension) * dimension;
        iuy = std::ceil(uy / dimension) * dimension;
        for (cly = ily; cly < iuy; cly += dimension) {
            cuy = cly + dimension;
            cy0 = y0 > cly ? y0 : cly;
            cy0 = cy0 < cuy ? cy0 : cuy;
            cy1 = y1 > cly ? y1 : cly;
            cy1 = cy1 < cuy ? cy1 : cuy;
            
            cx0 = (cy0 - y0) * dxdy + x0;
            cx1 = (cy1 - y0) * dxdy + x0;
            clx = cx0 < cx1 ? cx0 : cx1;
            cux = cx0 > cx1 ? cx0 : cx1;
            ilx = clx / dimension;
            iux = std::ceil(cux / dimension);
            
            iy = cly / dimension;
            coverage = cy1 - cy0;
            
            for (ix = ilx; ix < iux; ix++) {
                cell = & cells[ix * iy];
                cell->edge = true;
                if (ix == ilx)
                    cell->winding += coverage;
            }
        }
    }
    void writePathCells(Path& path) {
        int ilx = path._bounds.lx / dimension;
        int ily = path._bounds.ly / dimension;
        
        float *points, x0, y0, x1, y1;
        for (SubPath& s : path._subPaths)
            if (s._npoints > 2) {
                points = s._points.get();
                x0 = points[0], y0 = points[1];
                for (int i = 1; i < s._npoints; i++) {
                    x1 = points[i * 2], y1 = points[i * 2 + 1];
                    writeEdgeCells(x0, y0, x1, y1);
                    x0 = x1, y0 = y1;
                }
                x1 = points[0], y1 = points[1];
                writeEdgeCells(x0, y0, x1, y1);
            }
    }
    
    float dimension;
    int iw, ih;
    vector<Cell> cells;
    static void triangulate(Mesh& mesh, Scene& scene) {
        Grid grid(scene.w, scene.h);
        
        for (Path& path : scene._paths)
            path.update();
        
        for (Path& path : scene._paths)
            grid.writePathCells(path);
        
        mesh.empty();
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
    }
};
