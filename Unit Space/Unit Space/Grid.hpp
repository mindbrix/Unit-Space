//
//  Grid.hpp
//  Unit Space
//
//  Created by Nigel Barber on 17/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Tess.hpp"
#import <vector>

struct Grid {
    void triangulate(Mesh& mesh, Scene& scene) {
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
