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
        static int intersection(Site *s0, Site *s1, Site *ns) {
            assert(s0->x < s1->x);
            assert(ns->x >= s0->x && ns->x <= s1->x);
            float vy0 = (s0->y + ns->y) * 0.5, vy1 = (s1->y + ns->y) * 0.5;
            float dx0 = ns->x - s0->x, dx1 = ns->x - s1->x;
            float y0 = vy0 - dx0 * dx0, y1 = vy1 - dx1 * dx1;
            return y0 > y1 ? 0 : 1;
        }
        static float intersection(Site *s0, Site *s1, float ly) {
            float vy0 = (s0->y + ly) * 0.5, vy1 = (s1->y + ly) * 0.5;
            return 0.5 * (vy1 - vy0 + s0->x * s0->x - s1->x * s1->x) / (s0->x - s1->x);
        }
        float x, y;
        int i;
    };
    
    struct BeachSite {
        BeachSite(Site *s, float x, int n) : x(x), n(n), s(s) {}
    
        float x;
        int n;
        Site *s;
    };
    
    struct CircleEvent {
        CircleEvent(Site *s0, Site *s1, Site *s2, int n) : x(s1->x), y(1e20), n(n) {
            float cross, ax, ay, bx, by, cx, cy, a, b, c, d, u, v, w, ccx, ccy, ccr, dx, dy;
            cross = (s1->x - s0->x) * (s2->y - s1->y) - (s1->y - s0->y) * (s2->x - s1->x);
            if (cross != 0) {
                ax = s2->x - s1->x, ay = s2->y - s1->y, a = ax * ax + ay * ay;
                bx = s0->x - s2->x, by = s0->y - s2->y, b = bx * bx + by * by;
                cx = s1->x - s0->x, cy = s1->y - s0->y, c = cx * cx + cy * cy;
                d = 4.0 * cross * cross;
                u = a * (b + c - a) / d, v = b * (c + a - b) / d, w = c * (a + b - c) / d;
                ccx = s0->x * u + s1->x * v + s2->x * w;
                ccy = s0->y * u + s1->y * v + s2->y * w;
                dx = ccx - s0->x, dy = ccy - s0->y;
                ccr = sqrtf(dx * dx + dy * dy);
                x = ccx, y = ccy + ccr;
            }
        }
        inline bool operator< (const CircleEvent& other) const     { return y < other.y || (y == other.y && x < other.x); }
        
        float x, y;
        int n;
    };
    struct Triangle {
        Triangle(int i0, int i1, int i2) : i0(i0), i1(i1), i2(i2) {}
        int i0, i1, i2;
    };
    
    Memory<Site> writeSceneSites(Mesh& mesh, Scene& scene) {
        int count = 0;
        for (Path& path : scene._paths)
            for (SubPath& s : path._subPaths)
                count += s._npoints;
        Memory<Site> sites(count);
        Site *site = sites.get();
        
        int idx = 0;
        for (Path& path : scene._paths) {
            mesh._colors.emplace_back(path._color);
            
            for (SubPath& s : path._subPaths) {
                float *points = s._points.get();
                for (int i = 0; i < s._npoints; i++) {
                    new (site++) Site(points[i * 2], points[i * 2 + 1], idx++);
                    mesh._points.emplace_back(points[i * 2], points[i * 2 + 1]);
                }
            }
        }
        return sites;
    }
    void triangulate(Mesh& mesh, Scene& scene) {
        mesh.empty();
        Memory<Site> sites = writeSceneSites(mesh, scene);
        Site *begin = sites.get(), *end = begin + sites.count(), *s = begin, *s0, *s1, *s2;
        
        if (sites.count() < 3)
            return;
        
        Memory<BeachSite> beachline(sites.count() * 2 + 1);
        BeachSite *bbegin = beachline.get(), *bend = bbegin, *bsrc, *bdst, *b0, *b1, *b2;
        
        Memory<CircleEvent> circleEvents(sites.count() - 2);
        CircleEvent *cbegin = circleEvents.get(), *cend = cbegin, *csrc, *cdst;

        sort(begin, end);
        float y, ynext;
        int n = 0;
        
        do {
            for (y = s->y; s < end && s->y == y; s++) {
                if (s > begin && s->x == (s - 1)->x && s->y == (s - 1)->y)
                    continue;
                
                if (bbegin == bend) {
                    new (bend++) BeachSite(s, s->x, n++);
                    continue;
                }
                for (bdst = bbegin; bdst < bend && bdst->x < s->x; bdst++) {}
                if (bdst == bend) {
                    b0 = bdst - 1;
                    new (bend++) BeachSite(s, s->x, n++);
                    new (bend++) BeachSite(b0->s, s->x, n++);
                    b0 = bend - 4;
                } else if (bdst == bbegin) {
                    bend += 2;
                    for (bdst = bend - 1, bsrc = bdst - 2; bsrc >= bbegin; *bdst-- = *bsrc--) {}
                    bdst = bbegin, b0 = bbegin + 2;
                    new (bdst++) BeachSite(b0->s, s->x, n++);
                    new (bdst++) BeachSite(s, s->x, n++);
                    b0 = bbegin + 1;
                } else {
                    b0 = bdst - 1, b1 = bdst, bend += 2;
                    for (bdst = bend - 1, bsrc = bdst - 2; bsrc >= b1; *bdst-- = *bsrc--) {}
                    bdst = b1, b1 += 2;
                    if (Site::intersection(b0->s, b1->s, s) == 0) {
                        new (bdst++) BeachSite(s, s->x, n++);
                        new (bdst++) BeachSite(b0->s, s->x, n++);
                        b0 = bdst - 2;
                    } else {
                        new (bdst++) BeachSite(b1->s, s->x, n++);
                        new (bdst++) BeachSite(s, s->x, n++);
                        b0 = bdst - 3;
                    }
                    
                }
                b1 = b0 + 1, b2 = b0 + 2;
                if (b0 >= bbegin && b2 < bend) {
                    CircleEvent cnew(b0->s, b1->s, b2->s, b1->n);
                    for (cdst = cend++, csrc = cdst - 1; cdst > cbegin && cnew < *csrc; *cdst-- = *csrc--) {}
                    *cdst = cnew;
                }
            }
            
            ynext = s < end ? s->y : FLT_MAX;
            for (csrc = cbegin; csrc < cend && csrc->y < ynext; csrc++) {
                for (bdst = bbegin; bdst < bend && bdst->n != csrc->n; bdst++) {}
                assert(bdst->n == csrc->n);
                
                s0 = (bdst - 1)->s, s1 = bdst->s, s2 = (bdst + 1)->s;
                if ((s1->x - s0->x) * (s2->y - s1->y) - (s1->y - s0->y) * (s2->x - s1->x) < 0)
                    std::swap(s0, s2);
                mesh._triangles.emplace_back(s0->i, s1->i, s2->i, 0);
                for (bsrc = bdst + 1; bsrc < bend; *bdst++ = *bsrc++) {}
                bend = bdst;
//                b0 = bend - 2, b1 = bend - 1;
//                b1->x = Site::intersection(b0->s, b1->s, csrc->y);
            }
            for (cdst = cbegin; csrc < cend; *cdst++ = *csrc++) {}
            cend = cdst;
        } while (s < end || cbegin < cend);
    }
};
