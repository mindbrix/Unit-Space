//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Tess.hpp"
#import "libTess.hpp"
#import "EdgeClipper.hpp"
#import <vector>

constexpr int kDivisions = 512;

struct Grid {
    struct Cell {
        Cell() {}
        Cell(int ei, float coverage) : ei(ei), coverage(coverage) {}
        int ei;
        float coverage;
    };
    struct PathCell {
        PathCell() {}
        PathCell(int p) : p(p), winding(0), ecount(0) {}
        void addCell(Cell& cell) {
            winding += cell.coverage;
            ei0 = ecount == 0 ? cell.ei : ei0 < cell.ei ? ei0 : cell.ei;
            ei1 = ecount == 0 ? cell.ei : ei1 > cell.ei ? ei1 : cell.ei;
            ecount++;
        }
        int p;
        float winding;
        int ecount, ei0, ei1;
    };
    Grid(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy), width(ux - lx), height(uy - ly) {
        float dmax = width > height ? width : height;
        scale = float(kDivisions) / dmax, tx = lx, ty = ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
        edgeCounts.resize(iw * ih + 1);
        for (auto& count : edgeCounts)
            count = 0;
    }
    
    void writeEdgeCells(Edge *edge, int ei, bool count) {
        float x0, y0, x1, y1;
        float ly, uy, cly, cuy, sx0, sy0, sx1, sy1, coverage, slx, sux;
        float glx, gly, gux, guy, gw, gh, gx0, gx1, tx0, ty0, tx1, ty1;
        int *counts;
        
        x0 = (edge->x0 - tx) * scale, y0 = (edge->y0 - ty) * scale, x1 = (edge->x1 - tx) * scale, y1 = (edge->y1 - ty) * scale;
        if (x0 == x1 && y0 == y1)
            return;
        
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        gly = std::floor(ly), guy = std::ceil(uy), gh = guy - gly;
        for (cly = gly, cuy = gly + 1; gh--; cly = cuy, cuy += 1) {
            sy0 = y0 < cly ? cly : y0 > cuy ? cuy : y0;
            sy1 = y1 < cly ? cly : y1 > cuy ? cuy : y1;
            coverage = sy1 - sy0;
            
            sx0 = y0 == y1 ? x0 : x0 + (sy0 - y0) / (y1 - y0) * (x1 - x0);
            sx1 = y0 == y1 ? x1 : x0 + (sy1 - y0) / (y1 - y0) * (x1 - x0);
            slx = sx0 < sx1 ? sx0 : sx1;
            sux = sx0 > sx1 ? sx0 : sx1;
            
            glx = std::floor(slx), gux = slx == sux ? glx + 1 : std::ceil(sux), gw = gux - glx;
            if (count) {
                for (counts = & edgeCounts[glx + int(cly) * iw]; gw--; counts++)
                    (*counts)++;
            } else {
                for (counts = & edgeCounts[glx + int(cly) * iw], gx0 = glx; gx0 < gux; gx0++, counts++) {
                    gx1 = gx0 + 1;
                    tx0 = gx0 < slx ? slx : gx0 > sux ? sux : gx0;
                    tx1 = gx1 < slx ? slx : gx1 > sux ? sux : gx1;
                    ty0 = sx0 == sx1 ? sy0 : sy0 + (tx0 - sx0) / (sx1 - sx0) * (sy1 - sy0);
                    ty1 = sx0 == sx1 ? sy1 : sy0 + (tx1 - sx0) / (sx1 - sx0) * (sy1 - sy0);
                    
//                    new (& cells[(*counts)++]) Cell(ei, ty1 - ty0);
                    new (& cells[(*counts)++]) Cell(ei, gx0 == glx ? coverage : 0);
                }
            }
        }
    }
    
    float lx, ly, ux, uy;
    float scale, tx, ty;
    float width, height;
    int iw, ih;
    vector<Cell> cells;
    vector<int> edgeCounts;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        TESStesselator *tess = tessNewTess(NULL);
        
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        int ix, iy, count, base;
        float winding, x0, y0, x1, y1, iy1, dx, dy, ix0, iy0, ix1;
        Cell *cell, *cend;
        EdgeClipper polygon, _llx, _lly, _urx, _ury;
        for (Path& path : scene._paths)
            path.update();
        
        Bounds bounds = scene._paths[0]._bounds;
        for (Path& path : scene._paths)
            bounds.unite(path._bounds);
        
        count = 0;
        for (Path& path : scene._paths)
            count += path.countPoints();
        
        Memory<Edge> edges0(count);
        Edge *edges = edges0.get(), *begin = edges, *end = begin, *edge;
        for (Path& path : scene._paths)
            end = path.writeEdges(end, (int) (& path - & scene._paths[0]), 0, 0);
        
        Tess::writeCurves(mesh, scene);
        
        Grid grid(bounds.lx, bounds.ly, bounds.ux, bounds.uy);
        for (edge = begin; edge < end; edge++)
            grid.writeEdgeCells(edge, (int)(edge - begin), true);
        
        base = count = 0;
        for (auto& c : grid.edgeCounts)
            base = c, c = count, count += base;
        grid.cells.resize(count);
        
        for (edge = begin; edge < end; edge++)
            grid.writeEdgeCells(edge, (int)(edge - begin), false);
        
        mesh._colors.resize(grid.iw * grid.ih);
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
        RGBA8 rgba;
        PathCell pathCells[scene._paths.size()];
        int iend, i, idst, isrc, ei0, ei1, ecount, nz, a, p;
        
        cell = & grid.cells[0];
        for (iy = 0; iy < grid.ih; iy++) {
            iend = 0, rgba = RGBA8(0, 0, 0, 0);
            y0 = float(iy) / grid.scale + grid.ty, y1 = float(iy + 1) / grid.scale + grid.ty;
            for (ix = 0; ix < grid.iw; ix++) {
                for (i = 0; i < iend; i++)
                    pathCells[i].ecount = 0;
                
                for (cend = & grid.cells[grid.edgeCounts[ix + iy * grid.iw]]; cell < cend; cell++) {
                    for (i = 0; i < iend; i++)
                        if (pathCells[i].p == begin[cell->ei].p)
                            break;
                    if (i == iend) {
                        PathCell pathCell(begin[cell->ei].p);
                        idst = iend++, isrc = idst - 1;
                        while (idst > 0 && pathCell.p < pathCells[isrc].p)
                            pathCells[idst--] = pathCells[isrc--];
                        pathCells[idst] = pathCell;
                        i = idst;
                    }
                    pathCells[i].addCell(*cell);
                }
                
                rgba = RGBA8(0, 0, 0, 0);
                winding = ei0 = ei1 = ecount = p = 0;
                for (i = 0; i < iend; i++) {
                    p = pathCells[i].p;
                    nz = std::abs(pathCells[i].winding) * 65535;
                    a = scene._paths[pathCells[i].p]._evenOdd ? 65535 - std::abs((nz % 131070) - 65535) : nz & 0xFFFF;
                    
                    if (pathCells[i].ecount)
                        ecount += pathCells[i].ecount, ei0 = pathCells[i].ei0, ei1 = pathCells[i].ei1;
                    else if (a == 65535) {
                        rgba = RGBA8::over(scene._paths[pathCells[i].p]._color.multiply(a >> 8), rgba);
                        ei0 = ei1 = ecount = 0;
                    }
                    winding += pathCells[i].winding;
                }
                rgba = ecount ? RGBA8(0, 0, 0, 255) : rgba;
                mesh._colors[ix + iy * grid.iw] = rgba;
                x0 = float(ix) / grid.scale + grid.tx, x1 = float(ix + 1) / grid.scale + grid.tx;
                
                if (ecount == 0 && rgba.a != 0)
                    mesh.writeRect(x0, y0, x1, y1, ix + iy * grid.iw);
                else if (ecount) {
                    edge = begin + ei0;
                    dx = edge->x1 - edge->x0, dy = edge->y1 - edge->y0;
                    if (std::abs(dx) > std::abs(dy)) {
                        ix0 = edge->x0 < x0 ? x0 : edge->x0 > x1 ? x1 : edge->x0;
                        ix1 = edge->x1 < x0 ? x0 : edge->x1 > x1 ? x1 : edge->x1;
                        iy0 = edge->y0 + (ix0 - edge->x0) / dx * dy;
                        iy1 = edge->y0 + (ix1 - edge->x0) / dx * dy;
                    } else {
                        iy0 = edge->y0 < y0 ? y0 : edge->y0 > y1 ? y1 : edge->y0;
                        iy1 = edge->y1 < y0 ? y0 : edge->y1 > y1 ? y1 : edge->y1;
                        ix0 = edge->x0 + (iy0 - edge->y0) / dy * dx;
                        ix1 = edge->x0 + (iy1 - edge->y0) / dy * dx;
                    }
                    mesh.writeRect(x0, y0, x1, y1, ix + iy * grid.iw);
                } else if (ecount) {
                     /*
                     Bounds bounds(x0, y0, x1, y1);
                     Path& path = scene._paths[p];
                     for (SubPath& s : path._subPaths)
                     if (s._bounds.intersects(bounds)) {
                         auto out = polygon.make((Vector *)s._points.get(), s._npoints);
                         out = _llx.clip(out, EdgeClipper::LLX, x0);
                         out = _lly.clip(out, EdgeClipper::LLY, y0);
                         out = _urx.clip(out, EdgeClipper::URX, x1);
                         out = _ury.clip(out, EdgeClipper::URY, y1);
                         
                         if (out->size() > 2)
                             tessAddContour(tess, 2, & (*out)[0], kPointStride, (int)out->size());
                     }
                     libTess::writeTriangles(mesh, tess, grid.iw * grid.ih + p);
                     */
                }
            }
        }
        tessDeleteTess(tess);
    }
};
