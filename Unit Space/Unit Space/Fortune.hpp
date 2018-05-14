//
//  Fortune.hpp
//  Unit Space
//
//  Created by Nigel Barber on 09/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "Tess.hpp"
#import <vector>
#import "tesselator.h"
#import "Algorithms.hpp"
#import "VGQuadTree.hpp"


using namespace std;

struct Fortune {
#define DELETED -2
#define le 0
#define re 1
    
    template<typename T>
    struct Ref {
        Ref(T &ref) : _ref(& ref)                           {}
        inline operator T* () const                         { return _ref; }
        inline bool operator< (const Ref& other) const      { return *_ref < *other._ref; }
    private:
        T *_ref;
    };
    
    struct Freenode {
        struct Freenode * nextfree;
    };
    
    struct Freelist {
        Freenode * head;
        int nodesize;
    };
    
    struct Point {
        double x, y ;
        
        inline Point() {}
        inline Point(double x, double y) : x(x), y(y)          {}
        inline bool operator< (const Point& other) const     { return y < other.y || (y == other.y && x < other.x); }
    };
    
    struct Site {
        Point coord ;
        int sitenbr, refcnt ;
        
        Site(double x, double y, int sitenbr) : coord(x, y), sitenbr(sitenbr), refcnt(0)      {}
        inline Site *ref()                                   { refcnt++; return this; }
        inline bool operator< (const Site& other) const      { return coord < other.coord; }
    };
    
    struct Edge {
        double a, b, c ;
        Site *ep[2], *reg[2] ;
        int edgenbr ;
        
        Edge(double a, double b, double c, Site *s1, Site *s2, int edgenbr) : a(a), b(b), c(c), ep { NULL, NULL }, reg { s1->ref(), s2->ref() }, edgenbr(edgenbr) {}
    };
    
    struct Halfedge {
        struct Halfedge *ELleft, *ELright ;
        Edge * ELedge ;
        int ELrefcnt ;
        char ELpm ;
        Site * vertex ;
        double ystar ;
        struct Halfedge * PQnext ;
        
        Halfedge(Edge *e, int pm) : ELedge(e), ELpm(pm), PQnext(NULL), vertex(NULL), ELrefcnt(0) {}
    };
    
    
    int _sorted = 0, _triangulate = 1, _plot = 0, debug = 0, siteidx = 0, edge_bot = 0, edge_top = 0;
    float xmin, xmax, ymin, ymax, deltax, deltay, PQhashsizeMul;
    int nedges = 0, sqrt_nsites, nvertices = 0;
    int PQmin, PQcount, PQhashsize ;
    int ELhashsize ;
    int ntry = 0, totalsearch = 0;
    
    Freelist sfl, hfl, efl ;
    Site * bottomsite ;
    Halfedge * PQhash, * ELleftend, * ELrightend, **ELhash ;
    
    int                         _total_alloc = 0;
    vector<shared_ptr<void>>    _memory;
    vector<Site>                _sites;
    Site                        *_lastSite;
    vector<Ref<Site>>           _sitesIndex;
    Mesh                        *_mesh;
    
    Fortune() {}
    
    
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
        triangulate(mesh, & mesh._points[0].x, (int)mesh._points.size());
    }
    
    void triangulate(Mesh& mesh, const float *points, int npoints) {
        float x, y, range, scale;
        xmin = ymin = FLT_MAX, xmax = ymax = -FLT_MAX;
        for (int i = 0; i < npoints; i++) {
            x = points[i * 2], y = points[i * 2 + 1];
            if (x < xmin)  xmin = x;
            if (x > xmax)  xmax = x;
            if (y < ymin)  ymin = y;
            if (y > ymax)  ymax = y;
        }
        deltay = ymax - ymin, deltax = xmax - xmin;
        
        range = deltax > deltay ? deltax : deltay, scale = 1.0 / range;
        _sites = vector<Site>();
        for (int i = 0; i < npoints; i++) {
            x = points[i * 2], y = points[i * 2 + 1];
//            x = (x - xmin) * scale, y = (y - ymin) * scale;
//            encodeBits(& x, 0xFFFF, 0), encodeBits(& y, 0xFFFF, 0);
//            x = x * range + xmin, y = y * range + ymin;
            _sites.emplace_back(x, y, i);
        }
        _sitesIndex = vector<Ref<Site>>();
        for (auto& site : _sites)
            _sitesIndex.emplace_back(site);
        sort(_sitesIndex.begin(), _sitesIndex.end());
        
        siteidx = 0;
        _lastSite = nullptr;
        freeinit(&sfl, sizeof(Site)) ;
        freeinit(&efl, sizeof(Edge)) ;
        nvertices = nedges = 0 ;
        sqrt_nsites = sqrt(_sites.size() + 4) ;
        _mesh = & mesh;
        voronoi();
//        for (Site *site : _sitesIndex)
//            mesh._points[site->sitenbr] = { float(site->coord.x), float(site->coord.y) };
    }
    
private:
    void out_triple(Site * s1, Site * s2, Site * s3) {
        int i0 = s1->sitenbr, i1 = s2->sitenbr, i2 = s3->sitenbr;
        _mesh->_triangles.emplace_back(i2, i1, i0, 0);
    }
    
    Site *nextone(void) {
        Site *site = siteidx < _sitesIndex.size() ? _sitesIndex[siteidx++] : NULL;
        float dx = FLT_MAX, dy = FLT_MAX;
        if (site && _lastSite)
            dx = site->coord.x - _lastSite->coord.x, dy = site->coord.y - _lastSite->coord.y;
        
        if (dx * dx + dy * dy < 1e-6)
            return nextone();
        
        _lastSite = site;
        return site;
    }
    
#pragma mark - voronoi.c
    
    void voronoi() {
        Site *newsite, *bot, *top, *p, *v ;
        Point newintstar ;
        int pm ;
        Halfedge *lbnd, *rbnd, *llbnd, *rrbnd, *bisector ;
        Edge *e ;
        
        PQinitialize() ;
        bottomsite = nextone() ; // out_site(bottomsite) ;
        ELinitialize() ;
        newsite = nextone() ;
        while (1) {
            if (!PQempty()) {
                while (PQhash[PQmin].PQnext == NULL)
                    ++PQmin;
                auto next = PQhash[PQmin].PQnext;
                newintstar = Point(next->vertex->coord.x, next->ystar);
            }
            if (newsite && (PQempty() || newsite->coord < newintstar)) { // out_site(newsite) ;
                lbnd = ELleftbnd(&(newsite->coord)) ;
                rbnd = lbnd->ELright ;
                bot = rightreg(lbnd) ;
                e = bisect(bot, newsite) ; // out_bisector(e) ;
                bisector = HEcreate(e, le) ;
                ELinsert(lbnd, bisector) ;
                if ((p = intersect(lbnd, bisector))) {
                    PQdelete(lbnd) ;
                    PQinsert(lbnd, p, newsite) ;
                }
                lbnd = bisector ;
                bisector = HEcreate(e, re) ;
                ELinsert(lbnd, bisector) ;
                if ((p = intersect(bisector, rbnd)))
                    PQinsert(bisector, p, newsite) ;
                
                newsite = nextone() ;
            }
            else if (!PQempty())   /* intersection is smallest */
            {
                lbnd = PQextractmin() ;
                llbnd = lbnd->ELleft ;
                rbnd = lbnd->ELright ;
                rrbnd = rbnd->ELright ;
                bot = leftreg(lbnd), top = rightreg(rbnd) ;
                out_triple(bot, top, rightreg(lbnd)) ;
                v = lbnd->vertex ;  // v->sitenbr = nvertices++;    // out_vertex(v) ;
                endpoint(lbnd->ELedge, lbnd->ELpm, v);
                endpoint(rbnd->ELedge, rbnd->ELpm, v);
                ELdelete(lbnd) ;
                PQdelete(rbnd) ;
                ELdelete(rbnd) ;
                pm = bot->coord.y > top->coord.y ? re : le ;
                if (pm == re)
                    std::swap(bot, top);
                
                e = bisect(bot, top) ;      // out_bisector(e) ;
                bisector = HEcreate(e, pm) ;
                ELinsert(llbnd, bisector) ;
                endpoint(e, re - pm, v) ;
                deref(v) ;
                if ((p = intersect(llbnd, bisector))) {
                    PQdelete(llbnd) ;
                    PQinsert(llbnd, p, bot) ;
                }
                if ((p = intersect(bisector, rrbnd)))
                    PQinsert(bisector, p, bot) ;
            } else
                break ;
        }
        //        for (lbnd = ELright(ELleftend) ; lbnd != ELrightend ; lbnd = ELright(lbnd)) {
        //            e = lbnd->ELedge ;
        //            out_ep(e) ;
        //        }
    }
    
    
#pragma mark - memory.c
    
    void freeinit(Freelist * fl, int size) {
        fl->head = (Freenode *)NULL ;
        fl->nodesize = size ;
    }
    
    void *getfree(Freelist * fl) {
        int i ;
        void *addr ;
        if (fl->head == (Freenode *)NULL) {
            auto count = _sites.size();
            addr = myalloc(count * fl->nodesize) ;
            for (i = 0 ; i < count ; i++)
                makefree((Freenode *)((char *)addr + i * fl->nodesize), fl) ;
        }
        addr = fl->head ;
        fl->head = (fl->head)->nextfree ;
        return addr;
    }
    
    void makefree(Freenode * curr, Freelist * fl) {
        curr->nextfree = fl->head ;
        fl->head = curr ;
    }
    
    void *myalloc(size_t size) {
        void *addr ;
        if ((addr = malloc(size))) {
            _memory.emplace_back(addr, free);
            _total_alloc += size ;
            return addr;
        }
        fprintf(stderr,"Insufficient memory processing site %d (%d bytes in use)\n", siteidx, _total_alloc) ;
        exit(0) ;
    }
    
#pragma mark - geometry.c
    
    inline Edge *bisect(Site * s1, Site * s2) {
        double a, b, c, dx = s2->coord.x - s1->coord.x, dy = s2->coord.y - s1->coord.y;
        if (std::abs(dx) > std::abs(dy)) {
            a = 1.0;
            b = dy / dx;
            c = s1->coord.x + s1->coord.y * b + (dx + dy * b) * 0.5;
        } else {
            b = 1.0;
            a = dx / dy;
            c = s1->coord.x * a + s1->coord.y + (dx * a + dy) * 0.5;
        }
        return new ((Edge *)getfree(&efl)) Edge(a, b, c, s1, s2, nedges++);
    }
    
    Site *intersect(Halfedge * el1, Halfedge * el2) {
        Edge * e1, * e2, * e ;
        Halfedge * el ;
        double d, xint, yint ;
        int right_of_site ;
        
        e1 = el1->ELedge ;
        e2 = el2->ELedge ;
        if ((e1 == (Edge*)NULL) || (e2 == (Edge*)NULL))
            return ((Site *)NULL) ;
        
        if (e1->reg[1] == e2->reg[1])
            return ((Site *)NULL) ;
        
        d = (e1->a * e2->b) - (e1->b * e2->a) ;
        if (std::abs(d) < 1.0e-10)
            return ((Site *)NULL) ;
        
        xint = (e1->c * e2->b - e2->c * e1->b) / d ;
        yint = (e2->c * e1->a - e1->c * e2->a) / d ;
        if (*e1->reg[1] < *e2->reg[1]) {
            el = el1 ;
            e = e1 ;
        } else {
            el = el2 ;
            e = e2 ;
        }
        right_of_site = xint >= e->reg[1]->coord.x;
        if (right_of_site == (el->ELpm == le))
            return ((Site *)NULL) ;
        
        return new ((Site *)getfree(&sfl)) Site(xint, yint, 0);
    }
    
    /*** returns 1 if p is to right of halfedge e ***/
    
    int right_of(Halfedge * el, Point * p) {
        Edge * e ;
        Site *bot, *top ;
        int right_of_site, above, fast ;
        float dxp, dyp, dxs, t1, t2, t3, yl ;
        
        e = el->ELedge ;
        bot = e->reg[0], top = e->reg[1] ;
        right_of_site = p->x > top->coord.x;
        if (right_of_site && el->ELpm == le)
            return 1;
        
        if (!right_of_site && el->ELpm == re)
            return 0;
        
        if (e->a == 1.0) {
            dyp = p->y - top->coord.y ;
            dxp = p->x - top->coord.x ;
            fast = 0 ;
            if ((!right_of_site & (e->b < 0.0)) || (right_of_site & (e->b >= 0.0))) {
                fast = above = (dyp >= e->b*dxp) ;
            } else {
                above = ((p->x + p->y * e->b) > (e->c));
                if (e->b < 0.0)
                    above = !above ;
                
                if (!above)
                    fast = 1 ;
            }
            if (!fast) {
                dxs = top->coord.x - (e->reg[0])->coord.x ;
                above = (e->b * (dxp*dxp - dyp*dyp)) < (dxs * dyp * (1.0 + 2.0 * dxp / dxs + e->b * e->b)) ;
                if (e->b < 0.0)
                    above = !above ;
            }
        } else {  /*** e->b == 1.0 ***/
            yl = e->c - e->a * p->x ;
            t1 = p->y - yl ;
            t2 = p->x - top->coord.x ;
            t3 = yl - top->coord.y ;
            above = ((t1*t1) > ((t2 * t2) + (t3 * t3))) ;
        }
        return (above + el->ELpm) & 1;
    }
    
    void endpoint(Edge * e, int lr, Site * s) {
        e->ep[lr] = s->ref();
        if (e->ep[re-lr] == (Site *)NULL)
            return ;
        //        out_ep(e) ;
        deref(e->reg[le]) ;
        deref(e->reg[re]) ;
        makefree((Freenode *)e, (Freelist *) &efl) ;
    }
    
    inline void deref(Site * v) {
        if (--(v->refcnt) == 0)
            makefree((Freenode *)v, (Freelist *)&sfl) ;
    }
    
#pragma mark - heap.c
    
    void PQinsert(Halfedge * he, Site * v, Site *s) {
        Halfedge * last, * next ;
        
        he->vertex = ref(v);
        double dx = s->coord.x - v->coord.x, dy = s->coord.y - v->coord.y;
        he->ystar = v->coord.y + sqrt(dx * dx + dy * dy);
        last = &PQhash[ PQbucket(he)] ;
        while ((next = last->PQnext) != (Halfedge *)NULL && (he->ystar > next->ystar || (he->ystar == next->ystar && v->coord.x > next->vertex->coord.x)))
            last = next ;
        
        he->PQnext = last->PQnext ;
        last->PQnext = he ;
        PQcount++ ;
    }
    
    void PQdelete(Halfedge * he) {
        if (he->vertex != (Site *) NULL) {
            Halfedge * last = &PQhash[PQbucket(he)] ;
            while (last -> PQnext != he)
                last = last->PQnext ;
            
            last->PQnext = he->PQnext;
            PQcount-- ;
            deref(he->vertex) ;
            he->vertex = (Site *)NULL ;
        }
    }
    
    inline int PQbucket(Halfedge * he) {
        int bucket;
        double ystar = __inline_isnand(he->ystar) || __inline_isinfd(he->ystar) ? 0.f : he->ystar;
        
        if (ystar < ymin)
            bucket = 0;
        else if (ystar >= ymax)
            bucket = PQhashsize - 1;
        else
            bucket = (ystar - ymin) * PQhashsizeMul;
        
        if (bucket < PQmin)
            PQmin = bucket;
        
        return bucket;
    }
    
    inline int PQempty(void) {
        return PQcount == 0;
    }
    
    Halfedge *PQextractmin(void) {
        Halfedge *curr = PQhash[PQmin].PQnext ;
        PQhash[PQmin].PQnext = curr->PQnext ;
        PQcount-- ;
        return curr;
    }
    
    void PQinitialize(void) {
        PQcount = PQmin = 0 ;
        PQhashsize = 32 * sqrt_nsites ;
        PQhashsizeMul = 1.f / deltay * (PQhashsize - 1);
        PQhash = (Halfedge *)myalloc(PQhashsize * sizeof *PQhash) ;
        
        for (int i = 0 ; i < PQhashsize; i++)
            PQhash[i].PQnext = (Halfedge *)NULL ;
    }
    
    
    void ELinitialize(void) {
        freeinit(&hfl, sizeof(Halfedge)) ;
        ELhashsize = 4 * sqrt_nsites ;
        ELhash = (Halfedge **)myalloc( sizeof(*ELhash) * ELhashsize) ;
        for (int i = 0; i < ELhashsize; i++)
            ELhash[i] = (Halfedge *)NULL ;
        
        ELleftend = HEcreate((Edge *)NULL, 0) ;
        ELrightend = HEcreate((Edge *)NULL, 0) ;
        ELleftend->ELleft = (Halfedge *)NULL ;
        ELleftend->ELright = ELrightend ;
        ELrightend->ELleft = ELleftend ;
        ELrightend->ELright = (Halfedge *)NULL ;
        ELhash[0] = ELleftend ;
        ELhash[ELhashsize-1] = ELrightend ;
    }
    
    inline Halfedge *HEcreate(Edge * e, int pm) {
        return new ((Halfedge *)getfree(&hfl)) Halfedge(e, pm);
    }
    
    inline void ELinsert(Halfedge * lb, Halfedge * new0) {
        new0->ELleft = lb ;
        new0->ELright = lb->ELright ;
        (lb->ELright)->ELleft = new0 ;
        lb->ELright = new0 ;
    }
    
    /* Get entry from hash table, pruning any deleted nodes */
    
    Halfedge * ELgethash(int b) {
        if ((b < 0) || (b >= ELhashsize))
            return ((Halfedge *)NULL) ;
        
        Halfedge *he = ELhash[b] ;
        if ((he == (Halfedge *)NULL) || (he->ELedge != (Edge *)DELETED))
            return (he) ;
        
        /* Hash table points to deleted half edge.  Patch as necessary. */
        ELhash[b] = (Halfedge *)NULL ;
        if ((--(he->ELrefcnt)) == 0)
            makefree((Freenode *)he, (Freelist *)&hfl) ;
        
        return ((Halfedge *)NULL) ;
    }
    
    Halfedge * ELleftbnd(Point * p) {
        int i, bucket ;
        Halfedge * he ;
        
        /* Use hash table to get close to desired halfedge */
        bucket = (p->x - xmin) / deltax * ELhashsize ;
        if (bucket < 0)
            bucket = 0 ;
        
        if (bucket >= ELhashsize)
            bucket = ELhashsize - 1 ;
        
        he = ELgethash(bucket) ;
        if (he == (Halfedge *)NULL) {
            for (i = 1; true ; i++) {
                if ((he = ELgethash(bucket-i)) != (Halfedge *)NULL)
                    break ;
                
                if ((he = ELgethash(bucket+i)) != (Halfedge *)NULL)
                    break ;
            }
            totalsearch += i ;
        }
        ntry++ ;
        /* Now search linear list of halfedges for the corect one */
        if (he == ELleftend || (he != ELrightend && right_of(he,p))) {
            do {
                he = he->ELright ;
            } while (he != ELrightend && right_of(he,p)) ;
            he = he->ELleft ;
        } else {
            do {
                he = he->ELleft ;
            } while (he != ELleftend && !right_of(he,p)) ;
        }
        /*** Update hash table and reference counts ***/
        if ((bucket > 0) && (bucket < ELhashsize-1)) {
            if (ELhash[bucket] != (Halfedge *)NULL)
                (ELhash[bucket]->ELrefcnt)-- ;
            
            ELhash[bucket] = he ;
            (ELhash[bucket]->ELrefcnt)++ ;
        }
        return (he) ;
    }
    
    /*** This delete routine can't reclaim node, since pointers from hash table may be present. ***/
    
    inline void ELdelete(Halfedge * he) {
        (he->ELleft)->ELright = he->ELright ;
        (he->ELright)->ELleft = he->ELleft ;
        he->ELedge = (Edge *)DELETED ;
    }
    
    inline Site *leftreg(Halfedge * he) {
        return he->ELedge ? he->ELedge->reg[he->ELpm] : bottomsite;
    }
    
    inline Site *rightreg(Halfedge * he)  {
        return he->ELedge ? he->ELedge->reg[re - he->ELpm] : bottomsite;
    }
};
