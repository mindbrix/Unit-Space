//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 18/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Scene.hpp"

struct Grid {
    struct Sample {
        Sample(int idx, int edge, float coverage) : idx(idx), edge(edge), coverage(coverage) {}
        inline bool operator< (const Sample& other) const { return idx < other.idx; }
        
        int idx, edge;
        float coverage;
    };
    
    
    static void writePathCells(Mesh& mesh, Path& path, AffineTransform ctm, Bounds clipBounds) {
        if (! Bounds::transformUnit(AffineTransformMakeUnit(path._bounds, ctm)).intersects(clipBounds))
            return;
        
        float *points, *end, x0, y0, x1, y1, x2, y2, dx0, dy0, dx1, dy1, dx2, dy2, cy0, cy2, t0, t2, cx0, cx2;
        vector<Sample> samples;
        Bounds windingBounds = { -FLT_MAX, clipBounds.ly, clipBounds.ux, clipBounds.uy }, bounds;
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
                            t0 = (cy0 - dy0) / (dy2 - dy0), t0 = t0 < 0 ? 0 : t0 > 1 ? 1 : t0;
                            t2 = (cy2 - dy0) / (dy2 - dy0), t2 = t2 < 0 ? 0 : t2 > 1 ? 1 : t2;
                            cx0 = lerp(t0, dx0, dx2), cx2 = lerp(t2, dx0, dx2);
                            
                            mesh._circles.add(cx0, cy0, 8);
                            mesh._circles.add(cx2, cy2, 8);
                            
                            samples.emplace_back(0, 0, 0);
                        }
                    }
                    if (points >= end)
                        break;
                    
                    x0 = x2, y0 = y2, x1 = *points++, y1 = *points++, x2 = *points++, y2 = *points++;
                } while (true);
            }
        }
    }
};
