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
        Cell(int ei, float coverage) : ei(ei), coverage(coverage) {}
        int ei;
        float coverage;
    };
    struct PathCell {
        PathCell() {}
        PathCell(int p, float winding, bool edge) : p(p), winding(winding), edge(edge) {}
        int p;
        float winding;
        bool edge;
    };
    Grid(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy), width(ux - lx), height(uy - ly) {
        float dmax = width > height ? width : height;
        scale = float(kDivisions) / dmax, tx = lx, ty = ly;
        iw = std::ceil(width * scale) + 1, ih = std::ceil(height * scale) + 1;
        dimension = 1;
        edgeCells.resize(iw * ih);
    }
    
    void writeEdgeCells(Edge *edge, int ei) {
        float x0, y0, x1, y1;
        float ly, uy, ily, iuy, cly, cuy, sx0, sy0, sx1, sy1, coverage, t0, t1, slx, sux, clx, cux;
        float ilx, iux, glx, gly, gux, guy, gw, gh;
        vector<Cell> *cell;
        
        x0 = (edge->x0 - tx) * scale, y0 = (edge->y0 - ty) * scale, x1 = (edge->x1 - tx) * scale, y1 = (edge->y1 - ty) * scale;
        if (y0 == y1 || (x0 == x1 && y0 == y1))
            return;
        
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        gly = std::floor(ly / dimension), guy = std::ceil(uy / dimension), gh = guy - gly;
        ily = gly * dimension, iuy = guy * dimension;
        for (cly = ily, cuy = ily + dimension; gh--; cly = cuy, cuy += dimension) {
            sy0 = y0 < cly ? cly : y0 > cuy ? cuy : y0;
            sy1 = y1 < cly ? cly : y1 > cuy ? cuy : y1;
            coverage = sy1 - sy0;
            
            t0 = (sy0 - y0) / (y1 - y0);
            sx0 = t0 <= 0 ? x0 : t0 >= 1 ? x1 : x0 + t0 * (x1 - x0);
            t1 = (sy1 - y0) / (y1 - y0);
            sx1 = t1 <= 0 ? x0 : t1 >= 1 ? x1 : x0 + t1 * (x1 - x0);
            slx = sx0 < sx1 ? sx0 : sx1;
            sux = sx0 > sx1 ? sx0 : sx1;
            
            glx = std::floor(slx / dimension), gux = slx == sux ? glx + 1 : std::ceil(sux / dimension), gw = gux - glx;
            ilx = glx * dimension, iux = gux * dimension;
            cell = & edgeCells[glx + int(cly / dimension) * iw];
            for (clx = ilx, cux = clx + dimension; gw--; clx = cux, cux += dimension, cell++)
                cell->emplace_back(ei, clx == ilx ? coverage : 0);
        }
    }
    
    float lx, ly, ux, uy;
    float scale, tx, ty;
    float width, height;
    float dimension;
    int iw, ih;
    vector<vector<Cell>> edgeCells;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        TESStesselator *tess = tessNewTess(NULL);
        
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        int ix, iy, p;
        float x0, y0, x1, y1, ix0, iy0, ix1, iy1, ilx, ily, iux, iuy;
        vector<Cell> *edgeCells;
        EdgeClipper polygon, _llx, _lly, _urx, _ury;
        for (Path& path : scene._paths)
            path.update();
        
        Bounds bounds = scene._paths[0]._bounds;
        for (Path& path : scene._paths)
            bounds.unite(path._bounds);
        
        int count = 0;
        for (Path& path : scene._paths)
            count += path.countPoints();
        
        Memory<Edge> edges0(count);
        Edge *edges = edges0.get(), *begin = edges, *end = begin, *edge;
        for (Path& path : scene._paths)
            end = path.writeEdges(end, (int) (& path - & scene._paths[0]), 0, 0);
        
        Tess::writeCurves(mesh, scene);
        
        Grid grid(bounds.lx, bounds.ly, bounds.ux, bounds.uy);
        for (edge = begin; edge < end; edge++)
            grid.writeEdgeCells(edge, (int)(edge - begin));
        
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
        mesh._colors.emplace_back(RGBA8(255, 0, 0, 255));
        mesh._colors.resize(grid.iw * grid.ih);
        RGBA8 rgba;
        PathCell pathCells[scene._paths.size()];
        float nonzero, alpha;
        int iend, i, idst, isrc;
        bool isInterior, isEdge;
        
        for (iy = 0; iy < grid.ih; iy++) {
            iend = 0, p = -1, rgba = RGBA8(0, 0, 0, 0);
            y0 = float(iy) / grid.scale + grid.ty, y1 = float(iy + 1) / grid.scale + grid.ty;
            for (ix = 0; ix < grid.iw; ix++) {
                edgeCells = & grid.edgeCells[ix + iy * grid.iw];
                if (1) {
                    for (i = 0; i < iend; i++)
                        pathCells[i].edge = false;
                    
                    for (auto& cell :*edgeCells) {
                        for (i = 0; i < iend; i++)
                            if (pathCells[i].p == begin[cell.ei].p)
                                break;
                        if (i == iend) {
                            PathCell pathCell(begin[cell.ei].p, 0, false);
                            idst = iend++, isrc = idst - 1;
                            while (idst > 0 && pathCell.p < pathCells[isrc].p)
                                pathCells[idst--] = pathCells[isrc--];
                            pathCells[idst] = pathCell;
                            i = idst;
                        }
                        pathCells[i].winding += cell.coverage;
                        pathCells[i].edge = true;
                    }
                    
                    isInterior = isEdge = false;
                    rgba = RGBA8(0, 0, 0, 0);
                    p = -1;
                    for (i = 0; i < iend; i++) {
                        nonzero = std::abs(pathCells[i].winding / grid.dimension);
                        alpha = !scene._paths[pathCells[i].p]._evenOdd ? fmin(nonzero, 1.0f) : 1.f - std::abs(fmod(nonzero, 2.f) - 1.f);
                        if (pathCells[i].edge || alpha > 1e-3) {
                            p = pathCells[i].p;
                            isEdge = pathCells[i].edge || (alpha > 1e-3 && alpha < 0.999);
                            isInterior = alpha > 0.999;
                            rgba = isInterior ? RGBA8::over(scene._paths[p]._color.multiply(alpha * 255), rgba) : rgba;
                        }
                    }
                    rgba = isEdge ? RGBA8(0, 0, 0, 255) : isInterior ? rgba : RGBA8(0, 0, 0, 0);
                    mesh._colors[ix + iy * grid.iw] = rgba;
                    x0 = float(ix) / grid.scale + grid.tx, x1 = float(ix + 1) / grid.scale + grid.tx;
                    
                    if (rgba.a != 0)
                        mesh.writeRect(x0, y0, x1, y1, ix + iy * grid.iw);
                    else if (0) {
                        /*
                        Bounds bounds(x0, y0, x1, y1);
                        Path& path = scene._paths[piece->p];
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
                        libTess::writeTriangles(mesh, tess, piece->p);
                         */
                    }
                }
            }
        }
        tessDeleteTess(tess);
    }
};
