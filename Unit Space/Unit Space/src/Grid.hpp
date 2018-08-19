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
    
    
    static void writePathCells(Path& path, AffineTransform ctm, Bounds clipBounds) {
        if (! Bounds::transformUnit(AffineTransformMakeUnit(path._bounds, ctm)).intersects(clipBounds))
            return;
        
        float *points, *end, x0, y0, x1, y1, x2, y2, cy0, cy2;
        vector<Sample> samples;
        Bounds windingBounds = { -FLT_MAX, clipBounds.ly, clipBounds.ux, clipBounds.uy }, bounds;
        for (SubPath& s : path._subPaths) {
            AffineTransform unit = AffineTransformMakeUnit(s._bounds, ctm);
            if (Bounds::transformUnit(unit).intersects(clipBounds)) {
                points = s._quadratics.get(), end = points + s._quadratics.count();
                x0 = *(end - 2), y0 = *(end - 1), x2 = *points++, y2 = *points++, x1 = (x0 + x2) * 0.5, y1 = (y0 + y2) * 0.5;
                
                do {
                    bounds = { x0 < x2 ? x0 : x2, y0 < y2 ? y0 : y2, x0 > x2 ? x0 : x2, y0 > y2 ? y0 : y2 };
                    if (bounds.intersects(windingBounds)) {
                        cy0 = y0 < bounds.ly ? bounds.ly : y0 > bounds.uy ? bounds.uy : y0;
                        cy2 = y2 < bounds.ly ? bounds.ly : y2 > bounds.uy ? bounds.uy : y2;
                        
                        if (cy0 != cy2) {
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
