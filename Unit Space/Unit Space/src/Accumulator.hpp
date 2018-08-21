//
//  Accumulator.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Scene.hpp"


struct Accumulator {
    static void writePathAccumulations(Mesh& mesh, Path& path, int p) {
        float *points, *end, x0, y0, x1, y1, x2, y2, *pts, *ptsend;
        Accumulation *accumulation;
        bool first;
        
        mesh._accumulationIndices.emplace_back((int)mesh._accumulations.size());
        
        for (int s = 0; s < path._subPaths.size(); s++) {
            accumulation = nullptr, pts = ptsend = nullptr, first = true;
            points = path._subPaths[s]._quadratics.get(), end = points + path._subPaths[s]._quadratics.count();
            x0 = *(end - 2), y0 = *(end - 1), x2 = *points++, y2 = *points++, x1 = (x0 + x2) * 0.5, y1 = (y0 + y2) * 0.5;
            
            do {
                if (accumulation == nullptr) {
                    mesh._accumulations.emplace_back();
                    accumulation = & mesh._accumulations.back();
                    accumulation->p = p, accumulation->s = s;
                    pts = & accumulation->x0, ptsend = pts + 12;
                    
                    if (first)
                        *pts++ = x0, *pts++ = y0;
                    else
                        *pts++ = (accumulation - 1)->x5, *pts++ = (accumulation - 1)->y5;
                }
                
                *pts++ = x2, *pts++ = y2;
                if (pts == ptsend)
                    accumulation = nullptr, first = false;
                
                if (points >= end)
                    break;
                
                x0 = x2, y0 = y2, x1 = *points++, y1 = *points++, x2 = *points++, y2 = *points++;
            } while (1);
            
            while (pts < ptsend)
                *pts = *(pts - 2), pts++, *pts = *(pts - 2), pts++;
        }
    }
};
