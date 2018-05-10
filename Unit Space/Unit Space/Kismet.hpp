//
//  Kismet.hpp
//  Unit Space
//
//  Created by Nigel Barber on 10/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Tess.hpp"

struct Kismet {
    struct Site {
        Site(float x, float y, int i) : x(x), y(y), i(i) {}
        
        inline bool operator< (const Site& other) const     { return y < other.y || (y == other.y && x < other.x); }
        
        float x, y;
        int i;
    };
    
    struct Triangle {
        Triangle(int i0, int i1, int i2) : i0(i0), i1(i1), i2(i2) {}
        
        int i0, i1, i2;
    };
    
    struct TriangleEvent {
        TriangleEvent(float y, bool convex, Site *s, int i0, int i1, int i2) : y(y), convex(convex), s(s), t(i0, i1, i2) {}
        
        float y;
        bool convex;
        Site *s;
        Triangle t;
    };
    
    void triangulate(Mesh& mesh, Scene& scene) {
        int count = 0;
        for (Path& path : scene._paths)
            for (SubPath& s : path._subPaths)
                count += s._npoints;
        
        Memory<Site> sites(count);
        Site *begin = sites.get(), *end = begin, *s = begin;
        
        Memory<Site> beachline(count);
        Site *bbegin = beachline.get(), *bend = bbegin, *b;
        
        Memory<TriangleEvent> triangleEvents(count);
        TriangleEvent *tbegin = triangleEvents.get(), *tend = tbegin, *t;
        
        int sitenbr = 0;
        for (Path& path : scene._paths) {
            mesh._colors.emplace_back(path._color);
            
            for (SubPath& s : path._subPaths) {
                float *points = s._points.get();
                for (int i = 0; i < s._npoints; i++) {
                    new (end++) Site(points[i * 2], points[i * 2 + 1], sitenbr++);
                    mesh._points.emplace_back(points[i * 2], points[i * 2 + 1]);
                }
            }
        }
        sort(begin, end);
        
        do {
            if (s < end) {
                
            }
            
            if (tbegin < tend) {
                
            }
        } while (s < end && tbegin < tend);
    }
};
