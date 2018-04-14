//
//  delaunay.hpp
//  Unit Space
//
//  Created by Nigel Barber on 12/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

// Source: http://paulbourke.net/papers/triangulate/triangulate.c
//
#ifndef delaunay_h
#define delaunay_h

#include <stdlib.h>

#define EPSILON (1e-6)

typedef struct {
    int p1,p2,p3;
} ITRIANGLE;
typedef struct {
    int p1,p2;
} IEDGE;
typedef struct {
    float x,y;
    int i;
} XYZ;
typedef struct {
    double x,y,r;
} CIRCLE;


double TriangleArea(double x1, double y1, double x2, double y2, double x3, double y3) {
    double v0x = x2 - x1, v0y = y2 - y1;
    double v1x = x3 - x2, v1y = y3 - y2;
    return v0x * v1y - v0y * v1x;
}

double incirclefast(double xp, double yp, double x1, double y1, double x2, double y2, double x3, double y3)
{
    double adx, ady, bdx, bdy, cdx, cdy;
    double abdet, bcdet, cadet;
    double alift, blift, clift;
    
    adx = x1 - xp;
    ady = y1 - yp;
    bdx = x2 - xp;
    bdy = y2 - yp;
    cdx = x3 - xp;
    cdy = y3 - yp;
    
    abdet = adx * bdy - bdx * ady;
    bcdet = bdx * cdy - cdx * bdy;
    cadet = cdx * ady - adx * cdy;
    alift = adx * adx + ady * ady;
    blift = bdx * bdx + bdy * bdy;
    clift = cdx * cdx + cdy * cdy;
    
    return alift * bcdet + blift * cadet + clift * abdet;
}

/*
 Return TRUE if a point (xp,yp) is inside the circumcircle made up
 of the points (x1,y1), (x2,y2), (x3,y3)
 The circumcircle centre is returned in (xc,yc) and the radius r
 NOTE: A point on the edge is inside the circumcircle
 */
int CircumCircle(double xp,double yp,
                 double x1,double y1,double x2,double y2,double x3,double y3,
                 double *xc,double *yc,double *rsqr)
{
    double m1,m2,mx1,mx2,my1,my2;
    double dx,dy,drsqr;
    double fabsy1y2 = fabs(y1-y2);
    double fabsy2y3 = fabs(y2-y3);
    
    /* Check for coincident points */
    if (fabsy1y2 < EPSILON && fabsy2y3 < EPSILON)
        return(FALSE);
    
    if (fabsy1y2 < EPSILON) {
        m2 = - (x3-x2) / (y3-y2);
        mx2 = (x2 + x3) / 2.0;
        my2 = (y2 + y3) / 2.0;
        *xc = (x2 + x1) / 2.0;
        *yc = m2 * (*xc - mx2) + my2;
    } else if (fabsy2y3 < EPSILON) {
        m1 = - (x2-x1) / (y2-y1);
        mx1 = (x1 + x2) / 2.0;
        my1 = (y1 + y2) / 2.0;
        *xc = (x3 + x2) / 2.0;
        *yc = m1 * (*xc - mx1) + my1;
    } else {
        m1 = - (x2-x1) / (y2-y1);
        m2 = - (x3-x2) / (y3-y2);
        mx1 = (x1 + x2) / 2.0;
        mx2 = (x2 + x3) / 2.0;
        my1 = (y1 + y2) / 2.0;
        my2 = (y2 + y3) / 2.0;
        *xc = (m1 * mx1 - m2 * mx2 + my2 - my1) / (m1 - m2);
        if (fabsy1y2 > fabsy2y3) {
            *yc = m1 * (*xc - mx1) + my1;
        } else {
            *yc = m2 * (*xc - mx2) + my2;
        }
    }
    
    dx = x2 - *xc;
    dy = y2 - *yc;
    *rsqr = dx*dx + dy*dy;
    
    dx = xp - *xc;
    dy = yp - *yc;
    drsqr = dx*dx + dy*dy;
    
    // Original
    //return((drsqr <= *rsqr) ? TRUE : FALSE);
    // Proposed by Chuck Morris
    return((drsqr - *rsqr) <= EPSILON ? TRUE : FALSE);
}


int XYZCompare(const void *v1,const void *v2)
{
    XYZ *p1,*p2;
    p1 = (XYZ *)v1;
    p2 = (XYZ *)v2;
    if (p1->x < p2->x)
        return(-1);
    else if (p1->x > p2->x)
        return(1);
    else
        return(0);
}

/*
 Triangulation subroutine
 Takes as input NV vertices in array pxyz
 Returned is a list of ntri triangular faces in the array v
 These triangles are arranged in a consistent clockwise order.
 The triangle array 'v' should be malloced to 3 * nv
 The vertex array pxyz must be big enough to hold 3 more points
 The vertex array must be sorted in increasing x values say
 */

int Triangulate(int nv, XYZ *pxyz, ITRIANGLE *v, int vsize, int *ntri)
{
    ITRIANGLE *completed = v, *vend = v + vsize, *vstart = vend, *tri;
    CIRCLE *centres = NULL, *centre;
    int *complete = NULL;
    IEDGE *edges = NULL;
    int nedge = 0;
    int trimax,emax = 200;
    int status = 0;
    
    int inside;
    int i,j,k;
    double xp,yp,x1,y1,x2,y2,x3,y3,xc,yc,r, area;
    double xmin,xmax,ymin,ymax,xmid,ymid;
    double dx,dy,dmax;
    
    qsort((void *)pxyz, (size_t)nv, sizeof(XYZ), XYZCompare);
    
    /* Allocate memory for the completeness list, flag for each triangle */
    trimax = 4 * nv;
    if ((complete = (int *)malloc(trimax*sizeof(int))) == NULL) {
        status = 1;
        goto skip;
    }
    
    if ((centres = (CIRCLE *)malloc(vsize*sizeof(CIRCLE))) == NULL) {
        status = 5;
        goto skip;
    }
    
    /* Allocate memory for the edge list */
    if ((edges = (IEDGE *)malloc(emax*(long)sizeof(IEDGE))) == NULL) {
        status = 2;
        goto skip;
    }
    
    /*
     Find the maximum and minimum vertex bounds.
     This is to allow calculation of the bounding triangle
     */
    xmin = pxyz[0].x;
    ymin = pxyz[0].y;
    xmax = xmin;
    ymax = ymin;
    for (i=1;i<nv;i++) {
        if (pxyz[i].x < xmin) xmin = pxyz[i].x;
        if (pxyz[i].x > xmax) xmax = pxyz[i].x;
        if (pxyz[i].y < ymin) ymin = pxyz[i].y;
        if (pxyz[i].y > ymax) ymax = pxyz[i].y;
    }
    dx = xmax - xmin;
    dy = ymax - ymin;
    dmax = (dx > dy) ? dx : dy;
    xmid = (xmax + xmin) / 2.0;
    ymid = (ymax + ymin) / 2.0;
    
    /*
     Set up the supertriangle
     This is a triangle which encompasses all the sample points.
     The supertriangle coordinates are added to the end of the
     vertex list. The supertriangle is the first triangle in
     the triangle list.
     */
    pxyz[nv+0].x = x1 = xmid - 20 * dmax;
    pxyz[nv+0].y = y1 = ymid - dmax;
    pxyz[nv+1].x = x2 = xmid;
    pxyz[nv+1].y = y2 = ymid + 20 * dmax;
    pxyz[nv+2].x = x3 = xmid + 20 * dmax;
    pxyz[nv+2].y = y3 = ymid - dmax;
    
    vstart--;
    vstart->p1 = nv, vstart->p2 = nv + 1, vstart->p3 = nv + 2;
    CircumCircle(0,0,x1,y1,x2,y2,x3,y3,&xc,&yc,&r);
    centres[vsize - 1].x = xc, centres[vsize - 1].y = yc, centres[vsize - 1].r = r;
    
    xp = yp = FLT_MAX;
    /*
     Include each point one at a time into the existing mesh
     */
    for (i=0;i<nv;i++) {
        
        if (xp == pxyz[i].x && yp == pxyz[i].y)
            continue;
        
        xp = pxyz[i].x;
        yp = pxyz[i].y;
        nedge = 0;
        
        /*
         Set up the edge buffer.
         If the point (xp,yp) lies inside the circumcircle then the
         three edges of that triangle are added to the edge buffer
         and that triangle is removed.
         */
        for (tri = vstart; tri < vend; tri++) {
            centre = & centres[tri - v];
            xc = centre->x, yc = centre->y, r = centre->r;
            
            x1 = pxyz[tri->p1].x;
            y1 = pxyz[tri->p1].y;
            x2 = pxyz[tri->p2].x;
            y2 = pxyz[tri->p2].y;
            x3 = pxyz[tri->p3].x;
            y3 = pxyz[tri->p3].y;
            double incircle = incirclefast(xp,yp,x1,y1,x2,y2,x3,y3);
            inside = incircle < 0;
            
            if (xc < xp && incirclefast(xp,yc,x1,y1,x2,y2,x3,y3) >= 0) {
//            if (xc < xp && (dx * dx) > r) {
                 if (!(tri->p1 >= nv || tri->p2 >= nv || tri->p3 >= nv))
                     *completed++ = *tri;
                
                centres[tri - v] = centres[vstart - v];
                *tri = *vstart++;
            }
            if (inside) {
                /* Check that we haven't exceeded the edge list size */
                if (nedge+3 >= emax) {
                    emax += 100;
                    if ((edges = (IEDGE *)realloc(edges,emax*(long)sizeof(IEDGE))) == NULL) {
                        status = 3;
                        goto skip;
                    }
                }
                edges[nedge+0].p1 = tri->p1;
                edges[nedge+0].p2 = tri->p2;
                edges[nedge+1].p1 = tri->p2;
                edges[nedge+1].p2 = tri->p3;
                edges[nedge+2].p1 = tri->p3;
                edges[nedge+2].p2 = tri->p1;
                nedge += 3;
                
                centres[tri - v] = centres[vstart - v];
                *tri = *vstart++;
            }
        }
        
        /*
         Tag multiple edges
         Note: if all triangles are specified anticlockwise then all
         interior edges are opposite pointing in direction.
         */
        for (j=0;j<nedge-1;j++) {
            for (k=j+1;k<nedge;k++) {
                if ((edges[j].p1 == edges[k].p2) && (edges[j].p2 == edges[k].p1)) {
                    edges[j].p1 = -1;
                    edges[j].p2 = -1;
                    edges[k].p1 = -1;
                    edges[k].p2 = -1;
                }
                /* Shouldn't need the following, see note above */
                if ((edges[j].p1 == edges[k].p1) && (edges[j].p2 == edges[k].p2)) {
                    edges[j].p1 = -1;
                    edges[j].p2 = -1;
                    edges[k].p1 = -1;
                    edges[k].p2 = -1;
                }
            }
        }
        
        /*
         Form new triangles for the current point
         Skipping over any tagged edges.
         All edges are arranged in clockwise order.
         */
        for (j=0;j<nedge;j++) {
            if (edges[j].p1 < 0 || edges[j].p2 < 0)
                continue;
            
            if (vstart == completed) {
                status = 4;
                goto skip;
            }
            
            --vstart;
            vstart->p1 = edges[j].p1;
            vstart->p2 = edges[j].p2;
            vstart->p3 = i;
            
            x1 = pxyz[vstart->p1].x;
            y1 = pxyz[vstart->p1].y;
            x2 = pxyz[vstart->p2].x;
            y2 = pxyz[vstart->p2].y;
            x3 = pxyz[vstart->p3].x;
            y3 = pxyz[vstart->p3].y;
            
            area = TriangleArea(x1,y1,x2,y2,x3,y3);
            if (area > 0)
                std::swap(vstart->p1, vstart->p3);
            
            inside = CircumCircle(xp,yp,x1,y1,x2,y2,x3,y3,&xc,&yc,&r);
            
            centres[vstart - v].x = xc, centres[vstart - v].y = yc, centres[vstart - v].r = r;
        }
    }
    
    /*
     Remove triangles with supertriangle vertices
     These are triangles which have a vertex number greater than nv
     */
    for (tri = vstart; tri < vend; tri++)
        if (!(tri->p1 >= nv || tri->p2 >= nv || tri->p3 >= nv))
            *completed++ = *tri;
    
    (*ntri) = (int)(completed - v);
    
    
skip:
    free(centres);
    free(edges);
    free(complete);
    return(status);
}



#endif /* delaunay_h */
