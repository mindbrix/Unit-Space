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

constexpr int kDivisions = 128;

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
    
    void writeCellPolygon(Mesh& mesh, Cell *c0, Cell *c1, float winding, int gx, int gy, bool left, int p) {
        float x0, y0, x1, y1;
        int pv0, nv1;
        x0 = c0->x0 - float(gx), y0 = c0->y0 - float(gy), x1 = c0->x1 - float(gx), y1 = c0->y1 - float(gy);
        pv0 = y0 == 0 ? (x0 == 0 ? 3 : 0) : x0 == 1 ? (y0 == 0 ? 0 : 1) : y0 == 1 ? (x0 == 1 ? 1 : 2) : (y0 == 1 ? 2 : 3);
        nv1 = y1 == 0 ? (x1 == 1 ? 2 : 1) : x1 == 1 ? (y1 == 1 ? 3 : 2) : y1 == 1 ? (x1 == 0 ? 0 : 3) : (y1 == 0 ? 1 : 0);
        assert(1);
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
        float x0, y0, x1, y1, singleWinding, sx0, sy0, sx1, sy1, slx, sly, sux, suy;
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
        bool up;
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
                    ci = begin[c0->ei].p + grid.iw * grid.ih;
                    grid.writeCellPolygon(mesh, c0, c1, singleWinding, ix, iy, true, ci);
                        
                    sx0 = c0->x0 * grid.dimension + grid.tx, sy0 = c0->y0 * grid.dimension + grid.ty;
                    sx1 = c0->x1 * grid.dimension + grid.tx, sy1 = c0->y1 * grid.dimension + grid.ty;
                    slx = sx0 < sx1 ? sx0 : sx1, sux = sx0 > sx1 ? sx0 : sx1;
                    sly = sy0 < sy1 ? sy0 : sy1, suy = sy0 > sy1 ? sy0 : sy1;
                    up = (sx0 > sx1) == (sy0 > sy1);
                    
                    if (slx == x0 && sux == x1) {
                        if (singleWinding == 0) {
                            if (up)
                                mesh.writeTriangle(x0, y0, x1, y0, x1, suy, ci);
                            else
                                mesh.writeTriangle(x0, y1, x1, sly, x1, y1, ci);
                        } else {
                            if ((singleWinding < 0) == (sx1 < sx0))
                                mesh.writeQuad(x0, y0, x1, y0, x1, up ? suy : sly, x0, up ? sly : suy, ci);
                            else
                                mesh.writeQuad(x0, up ? sly : suy, x1, up ? suy : sly, x1, y1, x0, y1, ci);
                        }
                    } else if (sly == y0 && suy == y1) {
                        if (singleWinding == 0)
                            mesh.writeQuad(up ? slx : sux, y0, x1, y0, x1, y1, up ? sux : slx, y1, ci);
                        else
                            mesh.writeQuad(x0, y0, sy0 < sy1 ? sx0 : sx1, y0, sy0 < sy1 ? sx1 : sx0, y1, x0, y1, ci);
                    } else {
                        if (slx == x0 && sly == y0 && singleWinding != 0) {
                            if ((singleWinding < 0) == (sx1 < sx0))
                                mesh.writeTriangle(x0, y0, sux, y0, x0, suy, ci);
                            else {
                                mesh.writeTriangle(x0, suy, x1, y1, x0, y1, ci);
                                mesh.writeQuad(x0, suy, sux, y0, x1, y0, x1, y1, ci);
                            }
                        } else if (sux == x1 && sly == y0) {
                            if (singleWinding == 0)
                                mesh.writeTriangle(slx, y0, x1, y0, x1, suy, ci);
                            else {
                                mesh.writeTriangle(x1, suy, x1, y1, x0, y1, ci);
                                mesh.writeQuad(x0, y0, slx, y0, x1, suy, x0, y1, ci);
                            }
                        } else if (sux == x1 && suy == y1) {
                            if (singleWinding == 0)
                                mesh.writeTriangle(x1, sly, x1, y1, slx, y1, ci);
                            else {
                                mesh.writeTriangle(x0, y0, x1, y0, x1, sly, ci);
                                mesh.writeQuad(x0, y0, x1, sly, slx, y1, x0, y1, ci);
                            }
                        } else if (slx == x0 && suy == y1 && singleWinding != 0) {
                            if ((singleWinding < 0) != (sx1 < sx0))
                                mesh.writeTriangle(x0, sly, sux, y1, x0, y1, ci);
                            else {
                                mesh.writeTriangle(x0, y0, x1, y0, x0, sly, ci);
                                mesh.writeQuad(x0, sly, x1, y0, x1, y1, sux, y1, ci);
                            }
                        }
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
