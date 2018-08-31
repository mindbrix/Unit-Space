//
//  Accumulator.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Scene.hpp"

struct Accumulator {
    static constexpr int kAccumulationFloatCount = 10, kQuadraticFloatCount = 8;
    
    static int subPathFloatCount(SubPath& s) {
        int nfloats = (int)s._quadratics.count();
        bool closed = s._quadratics[0] == s._quadratics[nfloats - 2] && s._quadratics[1] == s._quadratics[nfloats - 1];
        return ((nfloats + 4 + (closed ? 0 : 4)) + kQuadraticFloatCount - 1) / kQuadraticFloatCount * kQuadraticFloatCount;
    }
    
    static void writeSubPath(float *q, float *qend, SubPath& s) {
        float x0, y0, x1, y1;
        int nfloats = (int)s._quadratics.count();
        memcpy(q, s._quadratics.get(), nfloats * sizeof(float));
        q += nfloats;
        x0 = s._quadratics[nfloats - 2], y0 = s._quadratics[nfloats - 1], x1 = s._quadratics[0], y1 = s._quadratics[1];
        if (x0 != x1 || y0 != y1) {
            *q++ = (x0 + x1) * 0.5, *q++ = (y0 + y1) * 0.5;
            *q++ = x1, *q++ = y1;
        }
        while (q < qend)
            *q++ = FLT_MAX;
    }
    
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
            //*pts++ = x1, *pts++ = y1;
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
    
    static void writeScene(Mesh& mesh, Scene& scene) {
        int p, s, nsubpaths, nfloats, begin, end;
        
        for (p = 0; p < scene._paths.size(); p++)
            writePathAccumulations(mesh, scene._paths[p], p);
        
        for (nsubpaths = p = 0; p < scene._paths.size(); p++)
            nsubpaths += (int)scene._paths[p]._subPaths.size();
        int bases[nsubpaths], base;
        mesh._quadraticIndices = Memory<int>(scene._paths.size());
        
        for (nfloats = base = p = 0; p < scene._paths.size(); p++) {
            mesh._quadraticIndices[p] = nfloats;
            for (s = 0; s < scene._paths[p]._subPaths.size(); s++) {
                bases[base++] = nfloats;
                nfloats += subPathFloatCount(scene._paths[p]._subPaths[s]);
            }
        }
        mesh._quadraticPs = Memory<int>(nfloats / kQuadraticFloatCount);
        mesh._quadraticFloats = Memory<float>(nfloats);
        
        for (base = p = 0; p < scene._paths.size(); p++)
            for (s = 0; s < scene._paths[p]._subPaths.size(); s++) {
                begin = bases[base++];
                end = base == nsubpaths ? (int)mesh._quadraticFloats.count() : bases[base];
                for (int i = begin / kQuadraticFloatCount; i < end / kQuadraticFloatCount; i++)
                    mesh._quadraticPs[i] = p;
                writeSubPath(& mesh._quadraticFloats[begin], & mesh._quadraticFloats[end], scene._paths[p]._subPaths[s]);
            }
    }
};
