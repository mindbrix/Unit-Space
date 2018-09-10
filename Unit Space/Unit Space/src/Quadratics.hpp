//
//  Quadratics.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Scene.hpp"

struct Quadratics {
    static constexpr int kQuadraticFloatStride = 8;
    
    static int subPathFloatCount(SubPath& s) {
        int nfloats = (int)s._quadratics.count();
        bool closed = s._quadratics[0] == s._quadratics[nfloats - 2] && s._quadratics[1] == s._quadratics[nfloats - 1];
        return ((nfloats + 4 + (closed ? 0 : 4)) + kQuadraticFloatStride - 1) / kQuadraticFloatStride * kQuadraticFloatStride;
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
    
    static float quadraticLength(float *q, float *qend) {
        float d0, d1, length, x0, y0, x1, y1, x2, y2;
        
        for (length = 0, x0 = *q++, y0 = *q++; q < qend; x0 = x2, y0 = y2) {
            x1 = *q++, y1 = *q++, x2 = *q++, y2 = *q++;
            if (x1 != FLT_MAX) {
                d0 = ::quadraticLength(x0, y0, x1, y1, x2, y2, 0.0);
                d1 = ::quadraticLength(x0, y0, x1, y1, x2, y2, 1.0);
                length += d1 - d0;
            }
        }
        return length;
    }
    
    static void writeScene(Mesh& mesh, Scene& scene) {
        int p, s, nsubpaths, nfloats, begin, end;
        
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
        mesh._quadraticPs = Memory<int, true>(nfloats / kQuadraticFloatStride);
        mesh._quadraticFloats = Memory<float, true>(nfloats);
        mesh._quadraticTs = Memory<float, true>(nfloats / kQuadraticFloatStride / 2);
        
        float length;
        for (base = p = 0; p < scene._paths.size(); p++)
            for (s = 0; s < scene._paths[p]._subPaths.size(); s++) {
                begin = bases[base++];
                end = base == nsubpaths ? (int)mesh._quadraticFloats.count() : bases[base];
                for (int i = begin / kQuadraticFloatStride; i < end / kQuadraticFloatStride; i++)
                    mesh._quadraticPs[i] = p;
                writeSubPath(& mesh._quadraticFloats[begin], & mesh._quadraticFloats[end], scene._paths[p]._subPaths[s]);
                
                length = quadraticLength(& mesh._quadraticFloats[begin], & mesh._quadraticFloats[end]);
            }
    }
};
