//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Tess.hpp"
#import <vector>

constexpr int kDivisions = 128;

struct Grid {
    struct Cell {
        Cell() : edge(false), winding(0) {}
        
        bool edge;
        float winding;
    };
    
    struct PathCell {
        PathCell() : count(0) {}
        
        int paths[16];
        int count;
    };
    
    Grid(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy), width(ux - lx), height(uy - ly) {
        float dmax = width > height ? width : height;
        dimension = std::ceil(dmax / float(kDivisions));
        iw = std::ceil(width / dimension) + 1, ih = std::ceil(height / dimension) + 1;
        cells.resize(iw * ih);
        pathCells.resize(iw * ih);
    }
    
    void writeEdgeCells(float x0, float y0, float x1, float y1) {
        x0 -= lx, y0 -= ly, x1 -= lx, y1 -= ly;
        float dxdy, ly, uy, ily, iuy, cly, cuy, cy0, cy1, cx0, cx1, clx, cux, coverage;
        int ilx, iux, iy, ix;
        Cell *cell;
        
        dxdy = (x1 - x0) / (y1 - y0);
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        ly = ly > 0 ? ly : 0;
        uy = uy < height ? uy : height;
        ily = std::floor(ly / dimension) * dimension;
        iuy = std::floor(uy / dimension) * dimension;
        for (cly = ily; cly <= iuy; cly += dimension) {
            cuy = cly + dimension;
            cy0 = y0 > cly ? y0 : cly;
            cy0 = cy0 < cuy ? cy0 : cuy;
            cy1 = y1 > cly ? y1 : cly;
            cy1 = cy1 < cuy ? cy1 : cuy;
            
            cx0 = y0 == y1 ? x0 : (cy0 - y0) * dxdy + x0;
            cx1 = y0 == y1 ? x1 : (cy1 - y0) * dxdy + x0;
            clx = cx0 < cx1 ? cx0 : cx1;
            cux = cx0 > cx1 ? cx0 : cx1;
            ilx = clx / dimension;
            iux = std::floor(cux / dimension);
            
            iy = cly / dimension;
            coverage = cy1 - cy0;
            
            cell = & cells[ilx + iy * iw];
            for (ix = ilx; ix <= iux; ix++, cell++) {
                cell->edge = true;
                if (ix == ilx)
                    cell->winding += coverage;
            }
        }
    }
    void writePathCells(Path& path, int ip) {
        float *points, x0, y0, x1, y1, winding;
        int ilx, ily, iux, iuy, ix, iy;
        Cell *cell;
        PathCell *pathCell;
        
        ilx = std::floor((path._bounds.lx - lx) / dimension);
        ily = std::floor((path._bounds.ly - ly) / dimension);
        iux = std::floor((path._bounds.ux - lx) / dimension);
        iuy = std::floor((path._bounds.uy - ly) / dimension);
        
        for (iy = ily; iy <= iuy; iy++) {
            cell = & cells[ilx + iy * iw];
            for (ix = ilx; ix <= iux; ix++, cell++)
                cell->edge = false, cell->winding = 0;
        }

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
        
        for (iy = ily; iy <= iuy; iy++) {
            winding = 0;
            cell = & cells[ilx + iy * iw];
            pathCell = & pathCells[ilx + iy * iw];
            for (ix = ilx; ix <= iux; ix++, cell++, pathCell++) {
                winding += cell->winding / dimension;
                if (cell->edge || std::abs(winding) > 1e-3)
                    pathCell->paths[pathCell->count++ % 16] = cell->edge ? -ip : ip;
            }
        }
    }
    
    float lx, ly, ux, uy;
    float width, height;
    float dimension;
    int iw, ih;
    vector<Cell> cells;
    vector<PathCell> pathCells;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        int ix, iy, pi;
        float winding, x0, y0, x1, y1;
        PathCell *pathCell;
        
        for (Path& path : scene._paths)
            path.update();
        
        Bounds bounds = scene._paths[0]._bounds;
        for (Path& path : scene._paths)
            bounds.unite(path._bounds);
        
        Grid grid(bounds.lx, bounds.ly, bounds.ux, bounds.uy);
        for (Path& path : scene._paths)
            grid.writePathCells(path, (int)(& path - &scene._paths[0]));
        
//        for (int i = 0; i < 16; i++)
//            mesh._colors.emplace_back(RGBA8(255 * i / 16, 255 * i / 16, 255 * i / 16, 255));
//        mesh._colors.emplace_back(RGBA8(0, 0, 0, 255));
//        mesh._colors.emplace_back(RGBA8(255, 0, 0, 255));
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
        
        for (iy = 0; iy < grid.ih; iy++) {
            y0 = iy * grid.dimension + grid.ly, y1 = (iy + 1) * grid.dimension+ grid.ly;
            winding = 0;
            for (ix = 0; ix < grid.iw; ix++) {
                pathCell = & grid.pathCells[ix + iy * grid.iw];
                if (pathCell->count) {
                    pi = (int)mesh._points.size();
                    x0 = ix * grid.dimension + grid.lx, x1 = (ix + 1) * grid.dimension + grid.lx;
                    
                    mesh._points.emplace_back(x0, y0);
                    mesh._points.emplace_back(x1, y0);
                    mesh._points.emplace_back(x1, y1);
                    mesh._points.emplace_back(x0, y1);
                    
                    mesh._triangles.emplace_back(pi + 0, pi + 1, pi + 2, std::abs(pathCell->paths[pathCell->count - 1]));
                    mesh._triangles.emplace_back(pi + 0, pi + 2, pi + 3, std::abs(pathCell->paths[pathCell->count - 1]));
                }
            }
        }
    }
};
