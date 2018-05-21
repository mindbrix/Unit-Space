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
    struct SixFloat {
        SixFloat(float x, float y, float ix0, float iy0, float ix1, float iy1) : x(x), y(y), ix0(ix0), iy0(iy0), ix1(ix1), iy1(iy1) {}
        float x, y, ix0, iy0, ix1, iy1;
    };
    struct Cell {
        Cell() : si(0), winding(0) {}
        int si;
        float winding;
    };
    
    struct PathCell {
        struct Piece {
            Piece() {}
            Piece(int p, int si, float winding) : p(p), si(si), winding(winding) {}
            int p, si;
            float winding;
        };
        void writePath(int ip, int si, float winding) {
            if (si == 0)
                pieces.resize(0);
            pieces.emplace_back(ip, si, winding);
        }
        vector<Piece> pieces;
    };
    
    Grid(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy), width(ux - lx), height(uy - ly) {
        float dmax = width > height ? width : height;
        dimension = std::ceil(dmax / float(kDivisions));
        iw = std::ceil(width / dimension) + 1, ih = std::ceil(height / dimension) + 1;
        edgeLists.emplace_back(vector<int>({ 1 }));
        cells.resize(iw * ih);
        pathCells.resize(iw * ih);
    }
    
    void writeEdgeCells(float x0, float y0, float x1, float y1) {
        x0 -= lx, y0 -= ly, x1 -= lx, y1 -= ly;
        if (y0 == y1 || (x0 == x1 && y0 == y1))
            return;
        
        float ly, uy, ily, iuy, cly, cuy, sx0, sy0, sx1, sy1, coverage, t0, t1, slx, sux, clx, cux, cx0, cy0, cx1, cy1;
        float ilx, iux, glx, gly, gux, guy, gw, gh;
        bool xedge, yedge;
        Cell *cell;
        vector<int> *edgeList;
        
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        gly = std::floor(ly / dimension), guy = std::ceil(uy / dimension), gh = guy - gly;
        ily = gly * dimension, iuy = guy * dimension;
        for (cly = ily, cuy = ily + dimension; gh--; cly = cuy, cuy += dimension) {
            yedge = cly != ily && cuy != iuy;
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
            cell = & cells[glx + int(cly / dimension) * iw];
            for (clx = ilx, cux = clx + dimension; gw--; clx = cux, cux += dimension, cell++) {
                xedge = clx != ilx && cux != iux;
                if (cell->si == 0) {
                    cell->si = (int)edgeLists.size();
                    edgeLists.emplace_back(vector<int>());
                }
                edgeList = & edgeLists[cell->si];
                
                cx0 = cy0 = cx1 = cy1 = 0;
                if (xedge || yedge) {
                    cx0 = sx0 < clx ? clx : sx0 > cux ? cux : sx0;
                    t0 = x0 == x1 ? 0 : (cx0 - sx0) / (sx1 - sx0);
                    cy0 = t0 <= 0 ? sy0 : t0 >= 1 ? sy1 : sy0 + t0 * (sy1 - sy0);
                    cx1 = sx1 < clx ? clx : sx1 > cux ? cux : sx1;
                    t1 = x0 == x1 ? 1 : (cx1 - sx0) / (sx1 - sx0);
                    cy1 = t1 <= 0 ? sy0 : t1 >= 1 ? sy1 : sy0 + t1 * (sy1 - sy0);
                }
                edgeList->emplace_back((int)sixFloats.size());
                sixFloats.emplace_back(0, 0, cx0, cy0, cx1, cy1);
                if (clx == ilx)
                    cell->winding += coverage;
            }
        }
    }
    void writePathCells(Path& path, int ip) {
        float *points, x0, y0, x1, y1;
        int glx, gly, gux, guy, gx, gy;
        Cell *cell;
        PathCell *pathCell;
        
        glx = std::floor((path._bounds.lx - lx) / dimension), gly = std::floor((path._bounds.ly - ly) / dimension);
        gux = std::ceil((path._bounds.ux - lx) / dimension) + 1, guy = std::ceil((path._bounds.uy - ly) / dimension);
        
        for (gy = gly; gy < guy; gy++) {
            cell = & cells[glx + gy * iw];
            for (gx = glx; gx < gux; gx++, cell++)
                cell->si = 0, cell->winding = 0;
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
        
        for (gy = gly; gy < guy; gy++) {
            cell = & cells[glx + gy * iw];
            pathCell = & pathCells[glx + gy * iw];
            for (gx = glx; gx < gux; gx++, cell++, pathCell++)
                if (cell->si)
                    pathCell->writePath(ip, cell->si, cell->winding / dimension);
        }
    }
    
    float lx, ly, ux, uy;
    float width, height;
    float dimension;
    int iw, ih;
    vector<vector<int>> edgeLists;
    vector<SixFloat> sixFloats;
    vector<Cell> cells;
    vector<PathCell> pathCells;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        TESStesselator *tess = tessNewTess(NULL);
        
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        int ix, iy, si, p;
        float x0, y0, x1, y1, ix0, iy0, ix1, iy1, ilx, ily, iux, iuy;
        PathCell *pathCell;
        PathCell::Piece* piece;
        EdgeClipper polygon, _llx, _lly, _urx, _ury;
        for (Path& path : scene._paths)
            path.update();
        
        Tess::writeCurves(mesh, scene);
        
        Bounds bounds = scene._paths[0]._bounds;
        for (Path& path : scene._paths)
            bounds.unite(path._bounds);
        
        Grid grid(bounds.lx, bounds.ly, bounds.ux, bounds.uy);
        for (Path& path : scene._paths)
            grid.writePathCells(path, (int)(& path - &scene._paths[0]));
        
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
        mesh._colors.emplace_back(RGBA8(255, 0, 0, 255));
        mesh._colors.resize(grid.iw * grid.ih);
        RGBA8 rgba;
        float windings[scene._paths.size()], nonzero, alpha;
        int indices[scene._paths.size()], iend, i;
        for (iy = 0; iy < grid.ih; iy++) {
            iend = 0, p = -1, rgba = RGBA8(0, 0, 0, 0);
            y0 = iy * grid.dimension + grid.ly, y1 = (iy + 1) * grid.dimension + grid.ly;
            for (ix = 0; ix < grid.iw; ix++) {
                pathCell = & grid.pathCells[ix + iy * grid.iw];
                if (1) {
                    for (auto& piece : pathCell->pieces) {
                        for (i = 0; i < iend; i++)
                            if (indices[i] == piece.p)
                                break;
                        if (i == iend)
                            indices[iend++] = piece.p, windings[i] = 0;
                        windings[i] += piece.winding;
                    }
                    if (pathCell->pieces.size()) {
                        rgba = RGBA8(0, 0, 0, 0);
                        for (i = 0; i < iend; i++) {
                            nonzero = std::abs(windings[i]);
                            alpha = !scene._paths[indices[i]]._evenOdd ? fmin(nonzero, 1.0f) : 1.f - std::abs(fmod(nonzero, 2.f) - 1.f);
                            rgba = RGBA8::over(scene._paths[indices[i]]._color.multiply(alpha * 255), rgba);
                        }
                    }
                    mesh._colors[ix + iy * grid.iw] = rgba;
                    
                    x0 = ix * grid.dimension + grid.lx, x1 = (ix + 1) * grid.dimension + grid.lx;
                    
    
                    if (rgba.a != 0)// || piece->si != 0)
                        mesh.writeRect(x0, y0, x1, y1, ix + iy * grid.iw);
                    else if (0) {
                        si = grid.edgeLists[piece->si].back();
                        /*
                        if (0 && !(sixFloat->ix0 == 0 && sixFloat->ix1 == 0 && sixFloat->iy0 == 0 && sixFloat->iy1 == 0)) {
                            ix0 = sixFloat->ix0 + grid.lx, iy0 = sixFloat->iy0 + grid.ly, ix1 = sixFloat->ix1 + grid.lx, iy1 = sixFloat->iy1 + grid.ly;
                            ilx = ix0 < ix1 ? ix0 : ix1, ily = iy0 < iy1 ? iy0 : iy1;
                            iux = ix0 > ix1 ? ix0 : ix1, iuy = iy0 > iy1 ? iy0 : iy1;
                            if (ilx == x0 && iux == x1) {
                                if (piece->winding * (ix1 - ix0) > 0)
                                    mesh.writeQuad(x0, y0, x1, y0, x1, iy1, x0, iy0, piece->p);
                                else
                                    mesh.writeQuad(x0, iy0, x1, iy1, x1, y1, x0, y1, piece->p);
                            } else if (ily == y0 && iuy == y1) {
                                if (piece->winding == 0)
                                    mesh.writeQuad(ix0, y0, x1, y0, x1, y1, ix1, y1, piece->p);
                                else
                                    mesh.writeQuad(x0, y0, ix1, y0, ix1, y1, x0, y1, piece->p);
                            }
                        } else {
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
                        }
                         */
                    }
                }
            }
        }
        tessDeleteTess(tess);
    }
};
