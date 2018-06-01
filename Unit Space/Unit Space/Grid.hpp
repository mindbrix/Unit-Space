//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Tess.hpp"
#import "libTess.hpp"
#import <vector>

constexpr int kDivisions = 512;

struct Grid {
    struct Cell {
        Cell(int ei, float x0, float y0, float x1, float y1) : ei(ei), x0(x0), y0(y0), x1(x1), y1(y1) {}
        int ei;
        float x0, y0, x1, y1;
    };
    struct CellElement {
        CellElement(int ei, float x0, float y0, float x1, float y1) : next(0), cell(ei, x0, y0, x1, y1) {}
        int next;
        Cell cell;
    };
    struct GridElement {
         int begin, last;
    };
    struct ActivePath {
        ActivePath() {}
        ActivePath(int p, bool evenOdd) : p(p), evenOdd(evenOdd), winding(0), a(0), ecount(0), c0(nullptr), c1(nullptr) {}
        void addCell(Cell *cell) {
            winding += cell->y1 - cell->y0;
            w = winding * 65535;
            a = evenOdd ? 65535 - std::abs(((w & 0xFFFF) % 131070) - 65535) : w & 0xFFFF;
            c0 = ecount == 0 ? cell : c0 < cell ? c0 : cell;
            c1 = ecount == 0 ? cell : c1 > cell ? c1 : cell;
            ecount++;
        }
        int p;
        bool evenOdd;
        float winding;
        int w, a, ecount;
        Cell *c0, *c1;
    };
    Grid(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy), width(ux - lx), height(uy - ly) {
        float dmax = width > height ? width : height;
        scale = float(kDivisions) / dmax, dimension = dmax / float(kDivisions), tx = lx, ty = ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
    }
    
    int edgeCellCount(Edge *edge) {
        return (std::abs(edge->x0 - edge->x1) + std::abs(edge->y0 - edge->y1)) * scale + 2;
    }
    
    void writeEdgeCells(Edge *edge, int ei) {
        float x0, y0, x1, y1;
        float ly, uy, cly, cuy, sx0, sy0, sx1, sy1, slx, sux;
        float glx, gly, gux, guy, gw, gh, gx0, gx1, tx0, ty0, tx1, ty1;
        GridElement *element;
        CellElement *cellElement;
        
        x0 = (edge->x0 - tx) * scale, y0 = (edge->y0 - ty) * scale, x1 = (edge->x1 - tx) * scale, y1 = (edge->y1 - ty) * scale;
        if (x0 == x1 && y0 == y1)
            return;
        
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        gly = std::floor(ly), guy = std::ceil(uy), gh = guy - gly;
        for (cly = gly, cuy = gly + 1; gh--; cly = cuy, cuy += 1) {
            sy0 = y0 < cly ? cly : y0 > cuy ? cuy : y0;
            sy1 = y1 < cly ? cly : y1 > cuy ? cuy : y1;
            
            sx0 = y0 == y1 || sy0 == y0 ? x0 : x0 + (sy0 - y0) / (y1 - y0) * (x1 - x0);
            sx1 = y0 == y1 || sy1 == y1 ? x1 : x0 + (sy1 - y0) / (y1 - y0) * (x1 - x0);
            slx = sx0 < sx1 ? sx0 : sx1, sux = sx0 > sx1 ? sx0 : sx1;
            
            glx = std::floor(slx), gux = slx == sux ? glx + 1 : std::ceil(sux), gw = gux - glx;
            element = & elements[int(glx) + int(cly) * iw];
            for (gx0 = glx, gx1 = gx0 + 1; gx0 < gux; gx0 = gx1, gx1++, element++) {
                tx0 = sx0 < gx0 ? gx0 : sx0 > gx1 ? gx1 : sx0;
                tx1 = sx1 < gx0 ? gx0 : sx1 > gx1 ? gx1 : sx1;
                ty0 = sx0 == sx1 || tx0 == sx0 ? sy0 : sy0 + (tx0 - sx0) / (sx1 - sx0) * (sy1 - sy0);
                ty1 = sx0 == sx1 || tx1 == sx1 ? sy1 : sy0 + (tx1 - sx0) / (sx1 - sx0) * (sy1 - sy0);
                
                cellElement = new (& cellElements[cellIndex])CellElement(ei, tx0, ty0, tx1, ty1);
                if (element->begin == 0)
                    element->begin = element->last = cellIndex;
                else
                    cellElements[element->last].next = cellIndex, element->last = cellIndex;
                cellIndex++;
            }
        }
    }
    
    void writeCellPolygon(Mesh& mesh, Cell *c0, Cell *c1, float gx, float gy, bool right, int p) {
        float x0, y0, x1, y1, det;
        int pv, nv, count, tcount, v, pi, i;
        
        x0 = c0->x0 - gx, y0 = c0->y0 - gy, x1 = (c1 ? c1->x1 : c0->x1) - gx, y1 = (c1 ? c1->y1 : c0->y1) - gy;
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
        
        if (mesh.useBig) {
            pi = (int)mesh.points.count();
            new (mesh.points.next()) Vector(c1 ? c0->x1 : x0 + gx, c1 ? c0->y1 : y0 + gy);
            new (mesh.points.next()) Vector(x1 + gx, y1 + gy);
            for (i = 0, v = nv; i < count; i++, v = (v + 1) % 4)
                new (mesh.points.next()) Vector(gx + ((v >> 1) ^ (v & 1)), gy + (v >> 1));
            if (c1)
                new (mesh.points.next()) Vector(x0 + gx, y0 + gy);
            for (i = 0; i < tcount; i++)
                new (mesh.triangles.next()) Triangle(pi + 0, pi + i + 1, pi + i + 2, p);
        } else {
            pi = (int)mesh._points.size();
            mesh._points.emplace_back(c1 ? c0->x1 : x0 + gx, c1 ? c0->y1 : y0 + gy);
            mesh._points.emplace_back(x1 + gx, y1 + gy);
            for (i = 0; i < count; i++, nv = (nv + 1) % 4)
                mesh._points.emplace_back(gx + ((nv >> 1) ^ (nv & 1)), gy + (nv >> 1));
            if (c1)
                mesh._points.emplace_back(x0 + gx, y0 + gy);
            for (i = 0; i < tcount; i++)
                mesh._triangles.emplace_back(pi + 0, pi + i + 1, pi + i + 2, p);
        }
}
    
    float lx, ly, ux, uy;
    float scale, dimension, tx, ty;
    float width, height;
    int iw, ih, cellIndex;
    GridElement *elements;
    CellElement *cellElements;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        TESStesselator *tess = tessNewTess(NULL);
        
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        int ix, iy, count;
        Cell *c0, *c1;
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            path->update();
        
        Bounds bounds = scene._paths[0]._bounds;
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            bounds.unite(path->_bounds);
        
        count = 0;
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            count += path->countPoints();
        
        Memory<Edge> edges0(count);
        Edge *edges = edges0.get(), *begin = edges, *end = begin, *edge;
        for (Path *path = & scene._paths[0], *pbegin = path, *pend = path + scene._paths.size(); path < pend; path++)
            end = path->writeEdges(end, (int)(path - pbegin));
        
//        Tess::writeCurves(mesh, scene);
        
        Grid grid(bounds.lx, bounds.ly, bounds.ux, bounds.uy);
        mesh._CTM = { grid.dimension, 0, 0, grid.dimension, grid.tx, grid.ty };
        
        for (count = 0, edge = begin; edge < end; edge++)
            count += grid.edgeCellCount(edge);
        
        Memory<GridElement> elements(grid.iw * grid.ih);
        elements.zero();
        grid.elements = elements.get();
        Memory<CellElement> cellElements(count + 1);
        grid.cellElements = cellElements.get();
        grid.cellIndex = 1;
        for (edge = begin; edge < end; edge++)
            grid.writeEdgeCells(edge, (int)(edge - begin));
        
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            mesh._colors.emplace_back(path->_color);
        RGBA8 rgba;
        ActivePath activePaths[scene._paths.size()];
        int iend, i, idst, isrc, plast, ci, sw, p;
        float det;
        bool right, inside;
        CellElement *cellElement;
        for (iy = 0; iy < grid.ih; iy++) {
            iend = 0, rgba = RGBA8(0, 0, 0, 0);
            for (ix = 0; ix < grid.iw; ix++) {
                ci = ix + iy * grid.iw;
                
                idst = 0, isrc = 0;
                while (isrc < iend) {
                    if (activePaths[isrc].a != 0)
                        activePaths[idst++] = activePaths[isrc];
                    isrc++;
                }
                iend = idst;
                
                for (i = 0; i < iend; i++)
                    activePaths[i].ecount = 0;
                
                if (grid.elements[ci].begin) {
                    cellElement = & grid.cellElements[grid.elements[ci].begin];
                    do {
                        p = begin[cellElement->cell.ei].p;
                        for (i = 0; i < iend; i++)
                            if (activePaths[i].p == p)
                                break;
                        if (i == iend) {
                            ActivePath active(p, scene._paths[p]._evenOdd);
                            idst = iend++, isrc = idst - 1;
                            while (idst > 0 && p < activePaths[isrc].p)
                                activePaths[idst--] = activePaths[isrc--];
                            activePaths[idst] = active;
                            i = idst;
                        }
                        activePaths[i].addCell(& cellElement->cell);
                    } while ((cellElement = cellElement->next ? & grid.cellElements[cellElement->next] : nullptr));
                }
                
                if (iend) {
                    if (activePaths[iend - 1].ecount == 0)
                        mesh.writeRect(ix, iy, ix + 1, iy + 1, activePaths[iend - 1].p);
                    else {
                        c0 = activePaths[iend - 1].c0, c1 = activePaths[iend - 1].ecount == 2 ? activePaths[iend - 1].c1 : nullptr;
                        if (c1 && !(c0->x1 == c1->x0 && c0->y1 == c1->y0))
                            std::swap(c0, c1);
                        sw = (c0->y1 - c0->y0 + (c1 ? c1->y1 - c1->y0 : 0)) * 65535;
                        det = c1 ? (c0->x1 - c0->x0) * (c1->y1 - c1->y0) - (c0->y1 - c0->y0) * (c1->x1 - c1->x0) : 0;
                        right = activePaths[iend - 1].w == sw ? (sw == 0 ? det < 0 : sw > 0) : activePaths[iend - 1].w > sw;
                        inside = c1 ? (c0->x1 == c1->x0 && c0->y1 == c1->y0) && (c0->x1 >= ix && c0->x1 <= (ix + 1) && c0->y1 >= iy && c0->y1 <= (iy + 1)) : !(activePaths[iend - 1].w == 0 && sw == 0);
                        
                        if (inside && activePaths[iend - 1].ecount < 3) {
                            ci = begin[c0->ei].p;
                            plast = iend > 1 && activePaths[iend - 2].ecount == 0 ? activePaths[iend - 2].p : -1;
                            grid.writeCellPolygon(mesh, c0, c1, ix, iy, right, ci);
                            if (plast != -1)
                                grid.writeCellPolygon(mesh, c0, c1, ix, iy, !right, plast);
                        }
//                        if (!inside) {
//                            float x0 = ix, y0 = iy, x1 = ix + 1, y1 = iy + 1;
//                            Bounds bounds(x0 * grid.dimension + grid.tx, y0 * grid.dimension + grid.ty, x1 * grid.dimension + grid.tx, y1 * grid.dimension + grid.ty);
//                            float rect[8] = { bounds.lx, bounds.ly, bounds.ux, bounds.ly, bounds.ux, bounds.uy, bounds.lx, bounds.uy };
//                            Path& path = scene._paths[activePaths[iend - 1].p];
//                            if (plast != -1) {
//                                libTess::addClippedContours(tess, path, bounds, true);
//                                tessSetOption(tess, TESS_REVERSE_CONTOURS, 0);
//                                tessAddContour(tess, 2, & rect[0], kPointStride, 4);
//                                libTess::writeTriangles(mesh, tess, TESS_WINDING_POSITIVE, grid.iw * grid.ih + plast, grid.scale, -grid.tx, -grid.ty);
//                            }
//                            libTess::addClippedContours(tess, path, bounds, false);
//                            libTess::writeTriangles(mesh, tess, TESS_WINDING_NONZERO, grid.iw * grid.ih + activePaths[iend - 1].p, grid.scale, -grid.tx, -grid.ty);
//                        }
                    }
                }
            }
        }
        tessDeleteTess(tess);
    }
};
