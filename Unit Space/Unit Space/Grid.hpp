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
        Cell(int ei, int si, float x0, float y0, float x1, float y1) : ei(ei), si(si), x0(x0), y0(y0), x1(x1), y1(y1) {}
        int ei, si;
        float x0, y0, x1, y1;
    };
    struct CellElement {
        CellElement(int ei, int si, float x0, float y0, float x1, float y1) : next(0), cell(ei, si, x0, y0, x1, y1) {}
        int next;
        Cell cell;
    };
    struct GridElement {
         int begin, last;
    };
    struct ActivePath {
        ActivePath() {}
        ActivePath(int p) : p(p), winding(0), ecount(0) {}
        void addCell(int i, float coverage) {
            if (ecount++ == 0) eli = i;
            winding += coverage;
        }
        int alpha(bool evenOdd) { return evenOdd ? 65535 - std::abs(((int(winding * 65535) & 0xFFFF) % 131070) - 65535) : int(winding * 65535) & 0xFFFF; }
        int p, ecount, eli;
        float winding;
    };
    Grid(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy), width(ux - lx), height(uy - ly) {
        float dmax = width > height ? width : height;
        scale = float(kDivisions) / dmax, dimension = dmax / float(kDivisions), tx = lx, ty = ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
    }
    
    int edgeCellCount(Edge *edge) { return (std::abs(edge->x0 - edge->x1) + std::abs(edge->y0 - edge->y1)) * scale + 2; }
    
    void writeEdgeCells(Edge *edge, int ei, int si) {
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
                
                cellElement = new (& cellElements[cellIndex]) CellElement(ei, si, tx0, ty0, tx1, ty1);
                *(element->begin == 0 ? & element->begin : & cellElements[element->last].next) = cellIndex;
                element->last = cellIndex++;
            }
        }
    }
    
    void writeCellPolygon(Mesh& mesh, Cell *c0, Cell *c1, float gx, float gy, bool right, int p) {
        float x0, y0, x1, y1, det;
        int pv, nv, count, tcount, pi, i;
        
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
    float lx, ly, ux, uy, width, height;
    float scale, dimension, tx, ty;
    int iw, ih, cellIndex;
    GridElement *elements;
    CellElement *cellElements;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        int ix, iy, count, subpathIndex;
        Cell *c0, *c1;
        
        Memory<Edge> edges0(scene._npoints);
        Edge *edges = edges0.get(), *begin = edges, *end = begin, *edge;
        for (Path *path = & scene._paths[0], *pbegin = path, *pend = path + scene._paths.size(); path < pend; path++)
            end = path->writeEdges(end, (int)(path - pbegin));
        
        Tess::writeCurves(mesh, scene);
        
        Grid grid(scene._bounds.lx, scene._bounds.ly, scene._bounds.ux, scene._bounds.uy);
        mesh._CTM = { grid.dimension, 0, 0, grid.dimension, grid.tx, grid.ty };
        
        for (count = 0, edge = begin; edge < end; edge++)
            count += grid.edgeCellCount(edge);
        
        Memory<GridElement> elements(grid.iw * grid.ih);
        grid.elements = elements.zero().get();
        Memory<CellElement> cellElements(count + 1);
        grid.cellElements = cellElements.get();
        for (grid.cellIndex = 1, subpathIndex = 0, edge = begin; edge < end; edge++) {
            if (edge > begin && !(edge->p == (edge - 1)->p && edge->ci == (edge - 1)->ci))
                subpathIndex++;
            grid.writeEdgeCells(edge, (int)(edge - begin), subpathIndex);
        }
        
        for (Path *path = & scene._paths[0], *pend = path + scene._paths.size(); path < pend; path++)
            mesh._colors.emplace_back(path->_color);
        ActivePath activePaths[scene._paths.size()];
        int iend, i, idst, isrc, plast, ci, eli, w, sw, p, lp, lix;
        float det, subpathWindings[subpathIndex + 1];
        bool right, inside;
        CellElement *cellElement;
        for (iy = 0; iy < grid.ih; iy++) {
            iend = 0, lp = -1, lix = 0;
            memset(subpathWindings, 0, sizeof(subpathWindings));
            for (ix = 0; ix < grid.iw; ix++) {
                ci = ix + iy * grid.iw;
                
                for (idst = isrc = 0; isrc < iend; isrc++)
                    if (activePaths[isrc].alpha(scene._paths[activePaths[isrc].p]._evenOdd) != 0)
                        activePaths[idst++] = activePaths[isrc];
                
                iend = idst;
                for (i = 0; i < iend; i++)
                    activePaths[i].ecount = 0;
                
                if ((eli = grid.elements[ci].begin)) {
                    do {
                        cellElement = & grid.cellElements[eli];
                        for (p = begin[cellElement->cell.ei].p, i = 0; i < iend; i++)
                            if (activePaths[i].p == p)
                                break;
                        if (i == iend) {
                            for (iend++, isrc = i - 1; i > 0 && p < activePaths[isrc].p; i--, isrc--)
                                activePaths[i] = activePaths[isrc];
                            new (& activePaths[i]) ActivePath(p);
                        }
                        subpathWindings[cellElement->cell.si] += cellElement->cell.y1 - cellElement->cell.y0;
                        activePaths[i].addCell(eli, cellElement->cell.y1 - cellElement->cell.y0);
                    } while ((eli = cellElement->next ?: 0));
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
                            eli = activePaths[iend - 1].eli, c0 = & grid.cellElements[eli].cell;
                            eli = grid.cellElements[eli].next ?: 0, c1 = eli ? & grid.cellElements[eli].cell : nullptr;
                            
                            if (c1 && !(c0->x1 == c1->x0 && c0->y1 == c1->y0))
                                std::swap(c0, c1);
                            w = subpathWindings[c0->si] * 65535;
                            sw = (c0->y1 - c0->y0 + (c1 ? c1->y1 - c1->y0 : 0)) * 65535;
                            det = c1 ? (c0->x1 - c0->x0) * (c1->y1 - c1->y0) - (c0->y1 - c0->y0) * (c1->x1 - c1->x0) : 0;
                            right = w == sw ? (sw == 0 ? det < 0 : sw > 0) : w > sw;
                            inside = c1 ? (c0->x1 == c1->x0 && c0->y1 == c1->y0) && (c0->x1 >= ix && c0->x1 <= (ix + 1) && c0->y1 >= iy && c0->y1 <= (iy + 1)) : !(w == 0 && sw == 0);
                            
                            if (inside) {
                                ci = begin[c0->ei].p;
                                plast = iend > 1 && activePaths[iend - 2].ecount == 0 ? activePaths[iend - 2].p : -1;
                                grid.writeCellPolygon(mesh, c0, c1, ix, iy, right, ci);
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
