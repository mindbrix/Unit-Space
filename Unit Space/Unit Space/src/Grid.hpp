//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 18/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Scene.hpp"

constexpr float kGridPixelSize = 16, kGridPixelSizeRecip = 1 / kGridPixelSize;

struct Grid {
    struct Sample {
        Sample(float gx, float gy, unsigned int edge, float coverage) : idx(((unsigned int)gy << 16) | (unsigned int)gx), edge(edge), coverage(coverage) {}
        inline bool operator< (const Sample& other) const { return idx < other.idx; }
        
        unsigned int idx, edge;
        float coverage;
    };
    
    
    static void writePathCells(Mesh& mesh, Path& path, AffineTransform ctm, Bounds clipBounds) {
        if (! Bounds::transformUnit(AffineTransformMakeUnit(path._bounds, ctm)).intersects(clipBounds))
            return;
        
        float *points, *end, gmax, x0, y0, x1, y1, x2, y2, dx0, dy0, dx1, dy1, dx2, dy2, cy0, cy2, t0, t2, cx0, cx2, cly, cuy, gly, guy, gy, clx, cux, glx, gux, gx, gy0, gy2, coverage, x;
        vector<Sample> samples;
        Bounds windingBounds = { -FLT_MAX, std::floor(clipBounds.ly * kGridPixelSizeRecip) * kGridPixelSize, FLT_MAX, std::ceil(clipBounds.uy * kGridPixelSizeRecip) * kGridPixelSize }, bounds;
        gmax = std::ceil(clipBounds.ux * kGridPixelSizeRecip) * kGridPixelSize;
        for (SubPath& s : path._subPaths) {
            AffineTransform unit = AffineTransformMakeUnit(s._bounds, ctm);
            if (Bounds::transformUnit(unit).intersects(clipBounds)) {
                points = s._quadratics.get(), end = points + s._quadratics.count();
                x0 = *(end - 2), y0 = *(end - 1), x2 = *points++, y2 = *points++, x1 = (x0 + x2) * 0.5, y1 = (y0 + y2) * 0.5;
                
                do {
                    dx0 = x0 * ctm.a + y0 * ctm.c + ctm.tx, dy0 = x0 * ctm.b + y0 * ctm.d + ctm.ty;
                    dx1 = x1 * ctm.a + y1 * ctm.c + ctm.tx, dy1 = x1 * ctm.b + y1 * ctm.d + ctm.ty;
                    dx2 = x2 * ctm.a + y2 * ctm.c + ctm.tx, dy2 = x2 * ctm.b + y2 * ctm.d + ctm.ty;
                    
                    bounds = { dx0 < dx2 ? dx0 : dx2, dy0 < dy2 ? dy0 : dy2, dx0 > dx2 ? dx0 : dx2, dy0 > dy2 ? dy0 : dy2 };
                    if (bounds.intersects(windingBounds)) {
                        cy0 = dy0 < windingBounds.ly ? windingBounds.ly : dy0 > windingBounds.uy ? windingBounds.uy : dy0;
                        cy2 = dy2 < windingBounds.ly ? windingBounds.ly : dy2 > windingBounds.uy ? windingBounds.uy : dy2;
                        
                        if (cy0 != cy2) {
                            cly = cy0 < cy2 ? cy0 : cy2, cuy = cy0 > cy2 ? cy0 : cy2;
                            gly = std::floor(cly * kGridPixelSizeRecip) * kGridPixelSize;
                            guy = std::ceil(cuy * kGridPixelSizeRecip) * kGridPixelSize;
                            
                            for (gy = gly; gy < guy; gy += kGridPixelSize) {
                                t0 = (gy - dy0) / (dy2 - dy0), t0 = t0 < 0 ? 0 : t0 > 1 ? 1 : t0;
                                t2 = (gy + kGridPixelSize - dy0) / (dy2 - dy0), t2 = t2 < 0 ? 0 : t2 > 1 ? 1 : t2;
                                cx0 = lerp(t0, dx0, dx2), cx2 = lerp(t2, dx0, dx2);
                                
                                clx = cx0 < cx2 ? cx0 : cx2, cux = cx0 > cx2 ? cx0 : cx2;
                                glx = std::floor(clx * kGridPixelSizeRecip) * kGridPixelSize;
                                gux = std::ceil(cux * kGridPixelSizeRecip) * kGridPixelSize;
                                
                                glx = glx < 0 ? 0 : glx > gmax ? gmax : glx;
                                gux = gux < 0 ? 0 : gux > gmax ? gmax : gux;
                                
                                gy0 = dy0 < gy ? gy : dy0 > gy + kGridPixelSize ? gy + kGridPixelSize : dy0;
                                gy2 = dy2 < gy ? gy : dy2 > gy + kGridPixelSize ? gy + kGridPixelSize : dy2;
                                coverage = gy2 - gy0;
                                
                                if (glx == gux) {
                                    samples.emplace_back(glx == 0 ? 0 : glx + kGridPixelSize, gy, 0, coverage);
                                } else
                                    for (gx = glx; gx < gux; gx += kGridPixelSize)
                                        samples.emplace_back(gx + kGridPixelSize, gy, 0, coverage), coverage = 0;
                            }
                        }
                    }
                    if (points >= end)
                        break;
                    
                    x0 = x2, y0 = y2, x1 = *points++, y1 = *points++, x2 = *points++, y2 = *points++;
                } while (true);
            }
        }        
        RGBA8 color = path._color;
        sort(samples.begin(), samples.end());
        coverage = 0, gx = gy = FLT_MAX;
        for (Sample sample : samples) {
            glx = (sample.idx & 0xFFFF) - kGridPixelSize, gly = sample.idx >> 16;
            if (gy != gly)
                gx = glx, gy = gly, coverage = 0;
            if (gx != glx) {
                if (glx - gx > kGridPixelSize && std::abs(coverage) >= kGridPixelSize) {
                    for (x = gx + kGridPixelSize; x < glx; x += kGridPixelSize)
                        mesh._circles.add(kGridPixelSize * 0.5 + x, kGridPixelSize * 0.5 + gy, coverage * 0.5, color);
                }
                gx = glx;
            }
            coverage += sample.coverage;
        }
    }
};
