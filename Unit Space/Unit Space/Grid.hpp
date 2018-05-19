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
            Piece(int p, int si) : p(p), si(si) {}
            int p, si;
        };
        void writePath(int ip, int si) {
            if (si == 0)
                pieces.resize(0);
            pieces.emplace_back(ip, si);
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
        float dxdy, ly, uy, ily, iuy, cly, cuy, cy0, cy1, coverage, cx0, cx1, clx, cux;
        int ilx, iux, iy, ix;
        bool xedge, yedge;
        Cell *cell;
        
        dxdy = (x1 - x0) / (y1 - y0);
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        ily = std::floor(ly / dimension) * dimension;
        iuy = std::floor(uy / dimension) * dimension;
        for (cly = ily; cly <= iuy; cly += dimension) {
            yedge = cly != ily && cly != iuy;
            cuy = cly + dimension;
            cy0 = y0 > cly ? y0 : cly;
            cy0 = cy0 < cuy ? cy0 : cuy;
            cy1 = y1 > cly ? y1 : cly;
            cy1 = cy1 < cuy ? cy1 : cuy;
            coverage = cy1 - cy0;
            
            cx0 = y0 == y1 ? x0 : (cy0 - y0) * dxdy + x0;
            cx1 = y0 == y1 ? x1 : (cy1 - y0) * dxdy + x0;
            clx = cx0 < cx1 ? cx0 : cx1;
            cux = cx0 > cx1 ? cx0 : cx1;
            ilx = clx / dimension;
            iux = cux / dimension;
            iy = cly / dimension;
            
            cell = & cells[ilx + iy * iw];
            for (ix = ilx; ix <= iux; ix++, cell++) {
                xedge = ix != ilx && ix != iux;
                if (cell->si)
                    sixFloats[cell->si] = SixFloat(0, 0, 0, 0, 0, 0);
                else {
                    cell->si = (int)sixFloats.size();
                    sixFloats.emplace_back(0, 0, 0, 0, 0, 0);
                }
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
        
        for (iy = ily; iy <= iuy; iy++) {
            winding = 0;
            cell = & cells[ilx + iy * iw];
            pathCell = & pathCells[ilx + iy * iw];
            for (ix = ilx; ix <= iux; ix++, cell++, pathCell++) {
                winding += cell->winding / dimension;
                if (cell->si || std::abs(winding) > 1e-3)
                    pathCell->writePath(ip, cell->si);
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
        float x0, y0, x1, y1;
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
                        if (sixFloat->ix0 == 0 && sixFloat->ix1 == 0 && sixFloat->iy0 == 0 && sixFloat->iy1 == 0) {
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
