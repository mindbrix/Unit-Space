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
        TriangleEvent(Site *s0, Site *s1, Site *s2) : uy(FLT_MAX), s(s1), t(s0->i, s1->i, s2->i) {
            float cross, ax, ay, bx, by, cx, cy, a, b, c, d, u, v, w, x, y, r, dx, dy;
            
            cross = (s1->x - s0->x) * (s2->y - s1->y) - (s1->y - s0->y) * (s2->x - s1->x);
            convex = cross < 0;
            if (cross != 0) {
                ax = s2->x - s1->x, ay = s2->y - s1->y, a = ax * ax + ay * ay;
                bx = s0->x - s2->x, by = s0->y - s2->y, b = bx * bx + by * by;
                cx = s1->x - s0->x, cy = s1->y - s0->y, c = cx * cx + cy * cy;
                d = 4.0 * cross * cross;
                u = a * (b + c - a) / d, v = b * (c + a - b) / d, w = c * (a + b - c) / d;
                x = s0->x * u + s1->x * v + s2->x * w;
                y = s0->y * u + s1->y * v + s2->y * w;
                dx = x - s0->x, dy = y - s0->y;
                r = sqrtf(dx * dx + dy * dy);
                uy = y + r;
            }
        }
        float uy;
        bool convex;
        Site *s;
        Triangle t;
    };
    
    Memory<Site> writeSceneSites(Mesh& mesh, Scene& scene) {
        int count = 0;
        for (Path& path : scene._paths)
            for (SubPath& s : path._subPaths)
                count += s._npoints;
        Memory<Site> sites(count);
        Site *site = sites.get();
        
        int sitenbr = 0;
        for (Path& path : scene._paths) {
            mesh._colors.emplace_back(path._color);
            
            for (SubPath& s : path._subPaths) {
                float *points = s._points.get();
                for (int i = 0; i < s._npoints; i++) {
                    new (site++) Site(points[i * 2], points[i * 2 + 1], sitenbr++);
                    mesh._points.emplace_back(points[i * 2], points[i * 2 + 1]);
                }
            }
        }
        return sites;
    }
    void triangulate(Mesh& mesh, Scene& scene) {
        Memory<Site> sites = writeSceneSites(mesh, scene);
        size_t count = sites.count();
        
        if (count < 3)
            return;
        
        Site *begin = sites.get(), *end = begin + count, *s = begin;
        
        Memory<Site> beachline(count);
        Site *bbegin = beachline.get(), *bend = bbegin, *bsrc, *bdst;
        
        Memory<TriangleEvent> triangleEvents(count);
        TriangleEvent *tbegin = triangleEvents.get(), *tend = tbegin, *t;
        
        sort(begin, end);
        
        TriangleEvent te(begin, begin + 1, begin + 2);
        
        do {
            float y = s->y;
            while (s < end && s->y == y) {
                bdst = bend++, bsrc = bdst - 1;
                while (bdst > bbegin && s->x < bsrc->x)
                    *bdst-- = *bsrc--;
                *bdst = *s++;
            }
            
            if (tbegin < tend) {
                
            }
        } while (s < end);
    }
};
