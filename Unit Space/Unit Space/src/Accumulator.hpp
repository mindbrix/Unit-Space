//
//  Accumulator.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Scene.hpp"

struct Accumulator {
    static constexpr int kAccumulationFloatCount = 10;
    
    static void writeQuadraticAccumulations(vector<Accumulation>& accumulations, float *points, size_t nfloats, int p, int s) {
        float *end = points + nfloats, x0, y0, x1, y1, x2, y2, *pts = nullptr, *ptsend = nullptr;
        
        x0 = *(end - 2), y0 = *(end - 1), x2 = *points++, y2 = *points++, x1 = (x0 + x2) * 0.5, y1 = (y0 + y2) * 0.5;
        do {
            if (pts == ptsend) {
                accumulations.emplace_back();
                Accumulation& back = accumulations.back();
                back.p = p, back.s = s, pts = & back.x0, ptsend = pts + kAccumulationFloatCount;
                *pts++ = x0, *pts++ = y0;
            }
            *pts++ = x2, *pts++ = y2;
            
            if (points >= end)
                break;
            
            x0 = x2, y0 = y2, x1 = *points++, y1 = *points++, x2 = *points++, y2 = *points++;
        } while (1);
        
        while (pts < ptsend)
            *pts = *(pts - 2), pts++, *pts = *(pts - 2), pts++;
    }
    
    static void writePathAccumulations(Mesh& mesh, Path& path, int p) {
        mesh._accumulationIndices.emplace_back((int)mesh._accumulations.size());
        
        for (int s = 0; s < path._subPaths.size(); s++)
            writeQuadraticAccumulations(mesh._accumulations, path._subPaths[s]._quadratics.get(), path._subPaths[s]._quadratics.count(), p, s);
        
    }
};
