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
        
        vector<Sample> samples;
        Bounds windingBounds = clipBounds;
        windingBounds.lx = -FLT_MAX;
        for (SubPath& s : path._subPaths) {
            AffineTransform unit = AffineTransformMakeUnit(s._bounds, ctm);
            if (Bounds::transformUnit(unit).intersects(clipBounds)) {
                float *points, *end, x0, y0, x1, y1, x2, y2;
                points = s._quadratics.get(), end = points + s._quadratics.count();
                x0 = *(end - 2), y0 = *(end - 1);
                x2 = *points++, y2 = *points++;
                x1 = (x0 + x2) * 0.5, y1 = (y0 + y2) * 0.5;
                
                do {
                    Bounds bounds(x0 < x2 ? x0 : x2, y0 < y2 ? y0 : y2, x0 > x2 ? x0 : x2, y0 > y2 ? y0 : y2);
                    
                    if (bounds.intersects(windingBounds)) {
                        samples.emplace_back(0, 0, 0);
                    }
                    if (points >= end)
                        break;
                    
                    x0 = x2, y0 = y2;
                    x1 = *points++, y1 = *points++, x2 = *points++, y2 = *points++;
                } while (true);
            }
        }
    }
};
