//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Tess.hpp"
#import <vector>

constexpr int kDivisions = 32;

struct Grid {
    struct Cell {
        Cell() : edge(false), winding(0) {}
        
        bool edge;
        float winding;
    };
    
    Grid(float w, float h) {
        float dmax = w > h ? w : h;
        dimension = std::ceil(dmax / float(kDivisions));
        iw = std::ceil(w / dimension), ih = std::ceil(h / dimension);
        cells.resize(iw * ih);
    }
    
    float dimension;
    int iw, ih;
    vector<Cell> cells;
    static void triangulate(Mesh& mesh, Scene& scene) {
        Grid grid(scene.w, scene.h);
        
        mesh.empty();
        for (Path& path : scene._paths) {
            mesh._colors.emplace_back(path._color);
            
            for (SubPath& s : path._subPaths) {
                float *points = s._points.get();
                for (int i = 0; i < s._npoints; i++)
                    mesh._points.emplace_back(points[i * 2], points[i * 2 + 1]);
            }
        }
    }
};
