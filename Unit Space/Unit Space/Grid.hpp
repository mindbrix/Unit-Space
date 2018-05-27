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

constexpr int kDivisions = 16;

struct Grid {
    struct Cell {
        Cell() {}
        Cell(int ei, float x0, float y0, float x1, float y1) : ei(ei), x0(x0), y0(y0), x1(x1), y1(y1) {}
        int ei;
        float x0, y0, x1, y1;
    };
    struct ActivePath {
        ActivePath() {}
        ActivePath(int p) : p(p), winding(0), ecount(0) {}
        void addCell(Cell& cell) {
            winding += cell.y1 - cell.y0;
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
        scale = float(kDivisions) / dmax, dimension = dmax / float(kDivisions), tx = lx, ty = ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
        edgeCounts.resize(iw * ih + 1);
        memset(& edgeCounts[0], 0, edgeCounts.size() * sizeof(edgeCounts[0]));
    }
    
    void writeEdgeCells(Edge *edge, int ei, bool count) {
        float x0, y0, x1, y1;
        float ly, uy, cly, cuy, sx0, sy0, sx1, sy1, slx, sux;
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
            
            sx0 = y0 == y1 ? x0 : x0 + (sy0 - y0) / (y1 - y0) * (x1 - x0);
            sx1 = y0 == y1 ? x1 : x0 + (sy1 - y0) / (y1 - y0) * (x1 - x0);
            slx = sx0 < sx1 ? sx0 : sx1, sux = sx0 > sx1 ? sx0 : sx1;
            
            glx = std::floor(slx), gux = slx == sux ? glx + 1 : std::ceil(sux), gw = gux - glx;
            if (count) {
                for (counts = & edgeCounts[glx + int(cly) * iw]; gw--; counts++)
                    (*counts)++;
            } else {
                for (counts = & edgeCounts[glx + int(cly) * iw], gx0 = glx, gx1 = gx0 + 1; gx0 < gux; gx0 = gx1, gx1++, counts++) {
                    tx0 = sx0 < gx0 ? gx0 : sx0 > gx1 ? gx1 : sx0;
                    tx1 = sx1 < gx0 ? gx0 : sx1 > gx1 ? gx1 : sx1;
                    ty0 = sx0 == sx1 ? sy0 : sy0 + (tx0 - sx0) / (sx1 - sx0) * (sy1 - sy0);
                    ty1 = sx0 == sx1 ? sy1 : sy0 + (tx1 - sx0) / (sx1 - sx0) * (sy1 - sy0);
                    new (& cells[(*counts)++]) Cell(ei, tx0, ty0, tx1, ty1);
                }
            }
        }
    }
    
    void writeCellPolygon(Mesh& mesh, Cell *c0, Cell *c1, float gx, float gy, bool right, int p) {
        float x0, y0, x1, y1;
        int pv, nv, count, pi, i;
        x0 = c0->x0 - gx, y0 = c0->y0 - gy, x1 = c0->x1 - gx, y1 = c0->y1 - gy;
        if (right)
            std::swap(x0, x1), std::swap(y0, y1);
        pv = y0 == 0 ? (x0 == 0 ? 3 : 0) : x0 == 1 ? (y0 == 0 ? 0 : 1) : y0 == 1 ? (x0 == 1 ? 1 : 2) : (y0 == 1 ? 2 : 3);
        nv = y1 == 0 ? (x1 == 1 ? 2 : 1) : x1 == 1 ? (y1 == 1 ? 3 : 2) : y1 == 1 ? (x1 == 0 ? 0 : 3) : (y1 == 0 ? 1 : 0);
        count = pv == nv ? 1 : pv == (nv + 1) % 4 ? 2 : pv == (nv + 2) % 4 ? 3 : 0;
        if (count == 0)
            return;
        pi = (int)mesh._points.size();
        mesh._points.emplace_back((x0 + gx) * dimension + tx, (y0 + gy) * dimension + ty);
        mesh._points.emplace_back((x1 + gx) * dimension + tx, (y1 + gy) * dimension + ty);
        for (i = 0; i < count; i++, nv = (nv + 1) % 4)
            mesh._points.emplace_back((nv == 3 || nv == 0 ? gx : gx + 1) * dimension + tx, (nv < 2 ? gy : gy + 1) * dimension + ty);
        for (i = 0; i < count; i++)
            mesh._triangles.emplace_back(pi + 0, pi + i + 1, pi + i + 2, p);
    }
    
    float lx, ly, ux, uy;
    float scale, dimension, tx, ty;
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
        float x0, y0, x1, y1, singleWinding;
        Cell *cell, *cend, *c0, *c1;
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
            end = path.writeEdges(end, (int) (& path - & scene._paths[0]));
        
        Tess::writeCurves(mesh, scene);
        
        Grid grid(bounds.lx, bounds.ly, bounds.ux, bounds.uy);
        for (edge = begin; edge < end; edge++)
            grid.writeEdgeCells(edge, (int)(edge - begin), true);
        mesh._CTM = { grid.dimension, 0, 0, grid.dimension, grid.tx, grid.ty };
        grid.dimension = 1, grid.tx = grid.ty = 0;
        
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
        ActivePath activePaths[scene._paths.size()];
        int iend, i, idst, isrc, ei0, ei1, ecount, nz, a, p, ci;
        bool right;
        cell = & grid.cells[0];
        for (iy = 0; iy < grid.ih; iy++) {
            iend = 0, rgba = RGBA8(0, 0, 0, 0);
            y0 = float(iy) * grid.dimension + grid.ty, y1 = float(iy + 1) * grid.dimension + grid.ty;
            
            for (ix = 0; ix < grid.iw; ix++) {
                ci = ix + iy * grid.iw;
                for (i = 0; i < iend; i++)
                    activePaths[i].ecount = 0;
                
                for (cend = & grid.cells[grid.edgeCounts[ci]]; cell < cend; cell++) {
                    for (i = 0; i < iend; i++)
                        if (activePaths[i].p == begin[cell->ei].p)
                            break;
                    if (i == iend) {
                        ActivePath active(begin[cell->ei].p);
                        idst = iend++, isrc = idst - 1;
                        while (idst > 0 && active.p < activePaths[isrc].p)
                            activePaths[idst--] = activePaths[isrc--];
                        activePaths[idst] = active;
                        i = idst;
                    }
                    activePaths[i].addCell(*cell);
                }
                
                rgba = RGBA8(0, 0, 0, 0);
                c0 = c1 = nullptr;
                singleWinding = ei0 = ei1 = ecount = p = 0;
                for (i = 0; i < iend; i++) {
                    p = activePaths[i].p;
                    nz = std::abs(activePaths[i].winding) * 65535;
                    a = scene._paths[activePaths[i].p]._evenOdd ? 65535 - std::abs((nz % 131070) - 65535) : nz & 0xFFFF;
                    
                    if (activePaths[i].ecount) {
                        ecount += activePaths[i].ecount, ei0 = activePaths[i].ei0, ei1 = activePaths[i].ei1;
                        if (ecount == 1)
                            c0 = cell - 1, singleWinding = activePaths[i].winding - (c0->y1 - c0->y0);
                        else if (ecount == 2)
                            c0 = cell - 2, c1 = cell - 1, singleWinding = activePaths[i].winding - (c0->y1 - c0->y0) - (c1->y1 - c1->y0);
                    } else if (a == 65535) {
                        rgba = RGBA8::over(scene._paths[activePaths[i].p]._color, rgba);
                        ei0 = ei1 = ecount = 0;
                    }
                }
                rgba = ecount ? RGBA8(0, 0, 0, 255) : rgba;
                mesh._colors[ci] = rgba;
                x0 = float(ix) * grid.dimension + grid.tx, x1 = float(ix + 1) * grid.dimension + grid.tx;
                
                if (ecount == 0 && rgba.a != 0)
                    mesh.writeRect(x0, y0, x1, y1, ci);
                else if (ecount == 1 && c0) {
                    if (singleWinding != 0 || c0->y1 != c0->y0) {
                        ci = begin[c0->ei].p + grid.iw * grid.ih;
                        right = singleWinding == 0 ? c0->y1 > c0->y0 : singleWinding > 0;
                        grid.writeCellPolygon(mesh, c0, c1, ix, iy, right, ci);
                    }
                } else if (ecount == 2 && c0 && c1) {
                    c0 = c0;
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
