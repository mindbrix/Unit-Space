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
        sixFloats.emplace_back(0, 0, 0, 0, 0, 0);
        cells.resize(iw * ih);
        pathCells.resize(iw * ih);
    }
    
    void writeEdgeCells(float x0, float y0, float x1, float y1) {
        if (x0 == x1 && y0 == y1)
            return;
        x0 -= lx, y0 -= ly, x1 -= lx, y1 -= ly;
        float dxdy, dydx, ly, uy, ily, iuy, cly, cuy, sy0, sy1, coverage, sx0, sx1, slx, sux, clx, cux, cx0, cy0, cx1, cy1;
        float ilx, iux, glx, gly, gux, guy, gw, gh;
        bool xedge, yedge;
        Cell *cell;
        
        dxdy = (x1 - x0) / (y1 - y0), dydx = (y1 - y0) / (x1 - x0);
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        
        gly = std::floor(ly / dimension), guy = uy == ly ? gly + 1 : std::ceil(uy / dimension), gh = guy - gly;
        ily = gly * dimension, iuy = guy * dimension;
        for (cly = ily, cuy = ily + dimension; gh--; cly = cuy, cuy += dimension) {
            yedge = cly != ily && cuy != iuy;
            sy0 = y0 < cly ? cly : y0 > cuy ? cuy : y0;
            sy1 = y1 < cly ? cly : y1 > cuy ? cuy : y1;
            coverage = sy1 - sy0;
            
            sx0 = y0 == y1 ? x0 : (sy0 - y0) * dxdy + x0;
            sx1 = y0 == y1 ? x1 : (sy1 - y0) * dxdy + x0;
            slx = sx0 < sx1 ? sx0 : sx1;
            sux = sx0 > sx1 ? sx0 : sx1;
            
            glx = std::floor(slx / dimension), gux = slx == sux ? glx + 1 : std::ceil(sux / dimension), gw = gux - glx;
            ilx = glx * dimension, iux = gux * dimension;
            cell = & cells[glx + int(cly / dimension) * iw];
            for (clx = ilx, cux = clx + dimension; gw--; clx = cux, cux += dimension, cell++) {
                xedge = clx != ilx && cux != iux;
                if (cell->si)
                    sixFloats[cell->si] = SixFloat(0, 0, 0, 0, 0, 0);
                else {
                    cell->si = (int)sixFloats.size();
                    cx0 = cy0 = cx1 = cy1 = 0;
                    if (xedge || yedge) {
                        cx0 = sx0 < clx ? clx : sx0 > cux ? cux : sx0;
                        cx1 = sx1 < clx ? clx : sx1 > cux ? cux : sx1;
                        cy0 = x0 == x1 ? sy0 : (cx0 - sx0) * dydx + sy0;
                        cy1 = x0 == x1 ? sy1 : (cx1 - sx0) * dydx + sy0;
                    }
                    sixFloats.emplace_back(0, 0, cx0, cy0, cx1, cy1);
                }
                if (clx == ilx)
                    cell->winding += coverage;
            }
        }
    }
    void writePathCells(Path& path, int ip) {
        float *points, x0, y0, x1, y1, winding;
        int glx, gly, gux, guy, gx, gy;
        Cell *cell;
        PathCell *pathCell;
        
        glx = std::floor((path._bounds.lx - lx) / dimension);
        gux = path._bounds.lx == path._bounds.ux ? glx + 1 : std::ceil((path._bounds.ux - lx) / dimension);
        gly = std::floor((path._bounds.ly - ly) / dimension);
        guy = path._bounds.ly == path._bounds.uy ? gly + 1 : std::ceil((path._bounds.uy - ly) / dimension);
        
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
            winding = 0;
            cell = & cells[glx + gy * iw];
            pathCell = & pathCells[glx + gy * iw];
            for (gx = glx; gx < gux; gx++, cell++, pathCell++) {
                if (cell->si || std::abs(winding) > 1e-3)
                    pathCell->writePath(ip, cell->si, winding);
                winding += cell->winding / dimension;
            }
        }
    }
    
    float lx, ly, ux, uy;
    float width, height;
    float dimension;
    int iw, ih;
    vector<SixFloat> sixFloats;
    vector<Cell> cells;
    vector<PathCell> pathCells;
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        TESStesselator *tess = tessNewTess(NULL);
        
        mesh.empty();
        if (scene._paths.size() == 0)
            return;
        int ix, iy;
        float x0, y0, x1, y1, ix0, iy0, ix1, iy1, ilx, ily, iux, iuy;
        PathCell *pathCell;
        PathCell::Piece* piece;
        SixFloat *sixFloat;
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
        
        for (iy = 0; iy < grid.ih; iy++) {
            y0 = iy * grid.dimension + grid.ly, y1 = (iy + 1) * grid.dimension+ grid.ly;
            for (ix = 0; ix < grid.iw; ix++) {
                pathCell = & grid.pathCells[ix + iy * grid.iw];
                if (pathCell->pieces.size()) {
                    x0 = ix * grid.dimension + grid.lx, x1 = (ix + 1) * grid.dimension + grid.lx;
                    
                    piece = & pathCell->pieces.back();
                    if (piece->si == 0)
                        mesh.writeRect(x0, y0, x1, y1, piece->p);
                    else {
                        sixFloat = & grid.sixFloats[piece->si];
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
                    }
                }
            }
        }
        tessDeleteTess(tess);
    }
};
