//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Tess.hpp"

constexpr int kDivisions = 128;

struct Grid {
    struct Cell {
        Cell(Edge *e, float x0, float y0, float x1, float y1) : next(0), si(e->si), p(e->p), x0(x0), y0(y0), x1(x1), y1(y1) {}
        int next;
        unsigned short int si, p;
        float x0, y0, x1, y1;
    };
    struct CellList {
         int begin, last;
    };
    struct ActivePath {
        ActivePath(int p) : p(p), winding(0), ecount(0) {}
        void addCell(int i, float coverage) {
            if (ecount++ == 0) cli = i;
            winding += coverage;
        }
        bool inside(bool evenOdd) { float w = std::abs(winding); return w < 0.0000152590219 ? false : evenOdd ? int(std::ceil(w)) & 1 : true; }
        int p, ecount, cli;
        float winding;
    };
    Grid(Scene &scene) : bounds(scene._bounds), width(bounds.ux - bounds.lx), height(bounds.uy - bounds.ly), cellIndex(1), cellCount(0) {
        float dmax = width > height ? width : height;
        scale = float(kDivisions) / dmax, dimension = dmax / float(kDivisions), tx = bounds.lx, ty = bounds.ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
        listsMemory = Memory<CellList>(iw * ih);
        cellLists = listsMemory.zero().get();
        edgesMemory = scene.writeEdges(cellCount, scale, tx, ty);
        cellsMemory = Memory<Cell>(cellCount + 1);
        cells = cellsMemory.get();
        
        float x0, y0, x1, y1;
        float lx, ly, ux, uy, gy0, gy1, sx0, sy0, sx1, sy1, slx, sux;
        float glx, gly, gux, guy, gw, gh, gx0, gx1, tx0, ty0, tx1, ty1;
        CellList *list;
        for (Edge *edge = edgesMemory.get(), *end = edge + scene._npoints; edge < end; edge++) {
            x0 = edge->x0, y0 = edge->y0, x1 = edge->x1, y1 = edge->y1;
            if (x0 == x1 && y0 == y1)
                continue;
            
            lx = x0 < x1 ? x0 : x1, ux = x0 > x1 ? x0 : x1;
            ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
            glx = std::floor(lx), gly = std::floor(ly);
            if (ux - glx <= 1 && uy - gly <= 1)
                writeCell(& cellLists[int(glx) + int(gly) * iw], edge, x0 - glx, y0 - gly, x1 - glx, y1 - gly);
            else {
                guy = std::ceil(uy), gh = guy - gly;
                for (gy0 = gly, gy1 = gy0 + 1; gh--; gy0 = gy1, gy1 += 1) {
                    sy0 = y0 < gy0 ? gy0 : y0 > gy1 ? gy1 : y0;
                    sy1 = y1 < gy0 ? gy0 : y1 > gy1 ? gy1 : y1;
                    
                    sx0 = y0 == y1 || sy0 == y0 ? x0 : x0 + (sy0 - y0) / (y1 - y0) * (x1 - x0);
                    sx1 = y0 == y1 || sy1 == y1 ? x1 : x0 + (sy1 - y0) / (y1 - y0) * (x1 - x0);
                    slx = sx0 < sx1 ? sx0 : sx1, sux = sx0 > sx1 ? sx0 : sx1;
                    
                    glx = std::floor(slx), gux = slx == sux ? glx + 1 : std::ceil(sux), gw = gux - glx;
                    list = & cellLists[int(glx) + int(gy0) * iw];
                    for (gx0 = glx, gx1 = gx0 + 1; gx0 < gux; gx0 = gx1, gx1++, list++) {
                        tx0 = sx0 < gx0 ? gx0 : sx0 > gx1 ? gx1 : sx0;
                        tx1 = sx1 < gx0 ? gx0 : sx1 > gx1 ? gx1 : sx1;
                        ty0 = sx0 == sx1 || tx0 == sx0 ? sy0 : sy0 + (tx0 - sx0) / (sx1 - sx0) * (sy1 - sy0);
                        ty1 = sx0 == sx1 || tx1 == sx1 ? sy1 : sy0 + (tx1 - sx0) / (sx1 - sx0) * (sy1 - sy0);
                        writeCell(list, edge, tx0 - gx0, ty0 - gy0, tx1 - gx0, ty1 - gy0);
                    }
                }
            }
        }
    }
    inline void writeCell(CellList *list, Edge *e, float x0, float y0, float x1, float y1) {
        new (& cells[cellIndex]) Cell(e, x0, y0, x1, y1);
        *(list->begin == 0 ? & list->begin : & cells[list->last].next) = cellIndex;
        list->last = cellIndex++;
    }
    void writeCellPolygon(Mesh& mesh, Cell *c0, Cell *c1, float gx, float gy, bool right, int p) {
        float x0, y0, x1, y1, det;
        int pv, nv, count, tcount, pi, i;
        
        x0 = c0->x0, y0 = c0->y0, x1 = c1 ? c1->x1 : c0->x1, y1 = c1 ? c1->y1 : c0->y1;
        if (right)
            std::swap(x0, x1), std::swap(y0, y1);
        
        pv = y0 == 0 ? (x0 == 0 ? 3 : 0) : x0 == 1 ? (y0 == 0 ? 0 : 1) : y0 == 1 ? (x0 == 1 ? 1 : 2) : (y0 == 1 ? 2 : 3);
        nv = y1 == 0 ? (x1 == 1 ? 2 : 1) : x1 == 1 ? (y1 == 1 ? 3 : 2) : y1 == 1 ? (x1 == 0 ? 0 : 3) : (y1 == 0 ? 1 : 0);
        count = pv == nv ? 1 : pv == (nv + 1) % 4 ? 2 : pv == (nv + 2) % 4 ? 3 : 4;
        if (c1 == nullptr && count == 4)
            return;
        if (count == 4) {
            det = (c0->x1 - c0->x0) * (c1->y1 - c1->y0) - (c0->y1 - c0->y0) * (c1->x1 - c1->x0);
            count = (det < 0) == !right ? 4 : 0;
        }
        tcount = c1 ? count + 1 : count;
        pi = (int)mesh._points.size();
        mesh._points.emplace_back((c1 ? c0->x1 : x0) + gx, (c1 ? c0->y1 : y0) + gy);
        mesh._points.emplace_back(x1 + gx, y1 + gy);
        for (i = 0; i < count; i++, nv = (nv + 1) % 4)
            mesh._points.emplace_back(gx + ((nv >> 1) ^ (nv & 1)), gy + (nv >> 1));
        if (c1)
            mesh._points.emplace_back(x0 + gx, y0 + gy);
        for (i = 0; i < tcount; i++)
            mesh._triangles.emplace_back(pi + 0, pi + i + 1, pi + i + 2, p);
    }
    Bounds bounds;
    float width, height, scale, dimension, tx, ty;
    int iw, ih, cellIndex, cellCount;
    Memory<Edge> edgesMemory;
    Memory<CellList> listsMemory;
    CellList *cellLists;
    Memory<Cell> cellsMemory;
    Cell *cells;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        
        Tess::writeCurves(mesh, scene);
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            mesh._colors.emplace_back(path->_color);
        
        Grid grid(scene);
        mesh._CTM = { grid.dimension, 0, 0, grid.dimension, grid.tx, grid.ty };
    
        char activeStorage[scene._paths.size() * sizeof(ActivePath)];
        ActivePath *activePaths = (ActivePath *)activeStorage;
        int ix, iy, iend, i, idst, isrc, plast, count, gi, cli, si, w, sw, rw, lw, p, lp, lix, first, step, it;
        float det, subpathWindings[scene._nsubpaths];
        bool right, inside;
        Cell *cell, *c0, *c1;
        
        for (iy = 0; iy < grid.ih; iy++) {
            iend = 0, lp = -1, lix = 0;
            memset(subpathWindings, 0, sizeof(subpathWindings));
            for (ix = 0; ix < grid.iw; ix++) {
                gi = ix + iy * grid.iw;
                
                for (idst = isrc = 0; isrc < iend; isrc++)
                    if (activePaths[isrc].inside(scene._paths[activePaths[isrc].p]._evenOdd))
                        activePaths[idst++] = activePaths[isrc];
                
                iend = idst;
                for (i = 0; i < iend; i++)
                    activePaths[i].ecount = 0;
                
                if ((cli = grid.cellLists[gi].begin)) {
                    do {
                        cell = & grid.cells[cli];
                        
                        first = 0, count = iend, p = cell->p;
                        while (count > 0) {
                            it = first, step = count / 2, it += step;
                            if (activePaths[it].p < p)
                                first = ++it, count -= step + 1;
                            else
                                count = step;
                        }
                        i = first != iend && activePaths[first].p == p ? first : iend;
                        
                        if (i == iend) {
                            for (iend++, isrc = i - 1; i > 0 && p < activePaths[isrc].p; i--, isrc--)
                                activePaths[i] = activePaths[isrc];
                            new (& activePaths[i]) ActivePath(p);
                        }
                        subpathWindings[cell->si] += cell->y1 - cell->y0;
                        activePaths[i].addCell(cli, cell->y1 - cell->y0);
                    } while ((cli = cell->next ?: 0));
                }
                
                if (iend) {
                    if (activePaths[iend - 1].ecount == 0) {
                        if (lp == -1)
                            lp = activePaths[iend - 1].p, lix = ix;
                        else {
                            if (lp != activePaths[iend - 1].p) {
                                mesh.writeRect(lix, iy, lix + 1, iy + 1, lp);
                                lp = activePaths[iend - 1].p, lix = ix;
                            }
                        }
                    } else {
                        if (lp != -1) {
                            mesh.writeRect(lix, iy, ix, iy + 1, lp);
                            lp = -1;
                        }
                        if (activePaths[iend - 1].ecount < 3) {
                            cli = activePaths[iend - 1].cli, c0 = & grid.cells[cli];
                            si = grid.cells[cli].si, p = grid.cells[cli].p;
                            cli = grid.cells[cli].next ?: 0, c1 = cli ? & grid.cells[cli] : nullptr;
                            
                            if (c1 && !(c0->x1 == c1->x0 && c0->y1 == c1->y0))
                                std::swap(c0, c1);
                            w = subpathWindings[si] * 65535;
                            sw = (c0->y1 - c0->y0 + (c1 ? c1->y1 - c1->y0 : 0)) * 65535;
                            det = c1 ? (c0->x1 - c0->x0) * (c1->y1 - c1->y0) - (c0->y1 - c0->y0) * (c1->x1 - c1->x0) : 0;
                            right = w == sw ? (sw == 0 ? det < 0 : sw > 0) : w > sw;
                            inside = c1 ? (c0->x1 == c1->x0 && c0->y1 == c1->y0) && (c0->x1 >= 0 && c0->x1 <= 1 && c0->y1 >= 0 && c0->y1 <= 1) : !(w == 0 && sw == 0);
                            
                            rw = w == sw ? (sw > 0 ? sw : 0) : (w - sw > 0 ? w - sw: w);
                            lw = w == sw ? (sw < 0 ? sw : 0) : (w - sw < 0 ? w - sw: w);
//                            if (!c1 && right)
//                                assert(lw == 0);
                            
                            if (inside) {
                                grid.writeCellPolygon(mesh, c0, c1, ix, iy, right, p);
                                plast = iend > 1 && activePaths[iend - 2].ecount == 0 ? activePaths[iend - 2].p : -1;
                                if (plast != -1)
                                    grid.writeCellPolygon(mesh, c0, c1, ix, iy, !right, plast);
                            }
                        }
                    }
                }
            }
        }
    }
};
