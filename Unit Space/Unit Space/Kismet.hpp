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
        float x, y;
        int i;
    };
    
    struct BeachSite {
        BeachSite(Site *s, float x, int n) : x(x), n(n), s(s) {}
        
        float x;
        int n;
        Site *s;
    };
    
    // https://en.wikipedia.org/wiki/Circumscribed_circle
    struct CircleEvent {
        CircleEvent(Site *s0, Site *s1, Site *s2, int n) : x(s1->x), y(FLT_MAX), n(n) {
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
    struct TriangleEvent {
        TriangleEvent(Site *s0, Site *s1, Site *s2) : uy(FLT_MAX), s(s1), t(s0->i, s1->i, s2->i) {
            float cross, ax, ay, bx, by, cx, cy, a, b, c, d, u, v, w, ccx, ccy, ccr, dx, dy;
            cross = (s1->x - s0->x) * (s2->y - s1->y) - (s1->y - s0->y) * (s2->x - s1->x);
            convex = cross < 0;
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
                uy = ccy + ccr;
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
        Site *begin = sites.get(), *end = begin + sites.count(), *s = begin;
        
        if (sites.count() < 3)
            return;
        
        Memory<BeachSite> beachline(sites.count() * 2 + 1);
        BeachSite *bbegin = beachline.get(), *bend = bbegin, *bsrc, *bdst, *b0, *b1, *b2;
        
        Memory<CircleEvent> circleEvents(sites.count() - 2);
        CircleEvent *cbegin = circleEvents.get(), *cend = cbegin, *csrc, *cdst;
        
        Memory<TriangleEvent> triangleEvents(sites.count());
        TriangleEvent *tbegin = triangleEvents.get(), *tend = tbegin, *tsrc, *tdst;
        
        sort(begin, end);
        float y, ynext;
        int n = 0;
        
        do {
            for (y = s->y; s < end && s->y == y; s++) {
                if (s > begin && s->x == (s - 1)->x && s->y == (s - 1)->y)
                    continue;
                
                for (bdst = bbegin; bdst < bend && bdst->x < s->x; bdst++) {}
                if (bdst == bend)
                    new (bend++) BeachSite(s, s->x, n++);
                else if (bdst == bbegin) {
                    for (bdst = bend++, bsrc = bdst - 1; bdst > bbegin; *bdst-- = *bsrc--) {}
                    new (bdst) BeachSite(s, s->x, n++);
                } else {
                    b0 = bdst - 1, b1 = bdst, bend += 2;
                    for (bdst = bend - 1, bsrc = bdst - 2; bsrc > b0; *bdst-- = *bsrc--) {}
                    bdst = b1;
                    if (Site::intersection(b0->s, b1->s, s) == 0) {
                        new (bdst++) BeachSite(s, s->x, n++);
                        new (bdst++) BeachSite(b0->s, s->x, n++);
                    } else {
                        new (bdst++) BeachSite(b1->s, s->x, n++);
                        new (bdst++) BeachSite(s, s->x, n++);
                    }
                }
                
                /*
                bdst = bend++, bsrc = bdst - 1;
                while (bdst > bbegin && s->x < bsrc->s->x)
                    *bdst-- = *bsrc--;
                *bdst = BeachSite(s, s->x, n++);
                
                if (bend - bbegin < 3)
                    continue;
                
                if (bdst == bbegin)
                    b0 = bdst;
                else if (bdst == bend - 1)
                    b0 = bdst - 2;
                else
                    b0 = bdst - 1;
                
                b1 = b0 + 1, b2 = b0 + 2;
                TriangleEvent tnew(b0->s, b1->s, b2->s);
                tdst = tend++, tsrc = tdst - 1;
                while (tdst > tbegin && tnew.uy < tsrc->uy)
                    *tdst-- = *tsrc--;
                *tdst = tnew;
                
                if (b0 > bbegin) {
                    for (tdst = tbegin; tdst < tend && tdst->s != b0->s; tdst++) {}
                    if (tdst < tend) {
                        new (tdst) TriangleEvent((b0 - 1)->s, b0->s, b2->s);
                    }
                }
                
                if (b2 < bend - 1) {
                    for (tdst = tbegin; tdst < tend && tdst->s != b2->s; tdst++) {}
                    if (tdst < tend) {
                        new (tdst) TriangleEvent(b0->s->x < b1->s->x ? b0->s : b1->s, b2->s, (b2 + 1)->s);
                    }
                }
                 */
            }
            
                
            ynext = s < end ? s->y : FLT_MAX;
            
            tsrc = tbegin;
            while (tsrc < tend && tsrc->uy < ynext) {
                if (tsrc->convex)
                    mesh._triangles.emplace_back(tsrc->t.i2, tsrc->t.i1, tsrc->t.i0, 0);
                else
                    mesh._triangles.emplace_back(tsrc->t.i0, tsrc->t.i1, tsrc->t.i2, 0);
                tsrc++;
            }
            if (tsrc != tbegin) {
                for (tdst = tbegin; tdst < tsrc; tdst++)
                    if (!tdst->convex) {
                        bsrc = bdst = bbegin;
                        for (; bsrc < bend; bsrc++)
                            if (bsrc->s != tdst->s)
                                *bdst++ = *bsrc;
                        bend = bdst;
                    }
                tdst = tbegin;
                while (tsrc < tend)
                    *tdst++ = *tsrc++;
                tend = tdst;
            }
        } while (s < end || tbegin < tend);
    }
};
