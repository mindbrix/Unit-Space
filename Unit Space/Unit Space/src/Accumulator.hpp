//
//  Accumulator.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Scene.hpp"


struct Accumulator {
    static void writePathAccumulations(Mesh& mesh, Path& path) {
        float *points, *end, gmax, x0, y0, x1, y1, x2, y2, dx0, dy0, dx1, dy1, dx2, dy2, cy0, cy2, t0, t2, cx0, cx2, cly, cuy, gly, guy, gy, clx, cux, glx, gux, gx, gy0, gy2, coverage;
        
        mesh._accumulationIndices.emplace_back((int)mesh._accumulations.size());
        
        for (SubPath& s : path._subPaths) {
            points = s._quadratics.get(), end = points + s._quadratics.count();
            x0 = *(end - 2), y0 = *(end - 1), x2 = *points++, y2 = *points++, x1 = (x0 + x2) * 0.5, y1 = (y0 + y2) * 0.5;
            
            do {
                
                if (points >= end)
                    break;
                
                x0 = x2, y0 = y2, x1 = *points++, y1 = *points++, x2 = *points++, y2 = *points++;
            } while (1);
        }
    }
};
