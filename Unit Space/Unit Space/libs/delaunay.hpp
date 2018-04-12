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

#define EPSILON (1e-12)
#define kMinDistance 1e-1f

typedef struct {
    int p1,p2,p3;
} ITRIANGLE;
typedef struct {
    int p1,p2;
} IEDGE;
typedef struct {
    float x,y;
} XYZ;


float TriangleArea(float x1, float y1, float x2, float y2, float x3, float y3) {
    float v0x = x2 - x1, v0y = y2 - y1;
    float v1x = x3 - x2, v1y = y3 - y2;
    return v0x * v1y - v0y * v1x;
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

int Triangulate(int nv, XYZ *pxyz, ITRIANGLE *v, int *ntri)
{
    int *complete = NULL;
    IEDGE *edges = NULL;
    int nedge = 0;
    int trimax,emax = 200;
    int status = 0;
    
    int inside;
    int i,j,k;
    double xp,yp,x1,y1,x2,y2,x3,y3,xc,yc,r;
    double xmin,xmax,ymin,ymax,xmid,ymid;
    double dx,dy,dmax;
    
    qsort((void *)pxyz, (size_t)nv, sizeof(XYZ), XYZCompare);
    
    /* Allocate memory for the completeness list, flag for each triangle */
    trimax = 4 * nv;
    if ((complete = (int *)malloc(trimax*sizeof(int))) == NULL) {
        status = 1;
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
    pxyz[nv+0].x = xmid - 20 * dmax;
    pxyz[nv+0].y = ymid - dmax;
    pxyz[nv+1].x = xmid;
    pxyz[nv+1].y = ymid + 20 * dmax;
    pxyz[nv+2].x = xmid + 20 * dmax;
    pxyz[nv+2].y = ymid - dmax;
    v[0].p1 = nv;
    v[0].p2 = nv+1;
    v[0].p3 = nv+2;
    complete[0] = FALSE;
    *ntri = 1;
    
    xp = yp = FLT_MAX;
    /*
     Include each point one at a time into the existing mesh
     */
    for (i=0;i<nv;i++) {
        
//        if (std::abs(xp - pxyz[i].x) < kMinDistance && std::abs(yp - pxyz[i].y) < kMinDistance)
//            continue;
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
        for (j=0;j<(*ntri);j++) {
            if (complete[j])
                continue;
            x1 = pxyz[v[j].p1].x;
            y1 = pxyz[v[j].p1].y;
            x2 = pxyz[v[j].p2].x;
            y2 = pxyz[v[j].p2].y;
            x3 = pxyz[v[j].p3].x;
            y3 = pxyz[v[j].p3].y;
            
            inside = CircumCircle(xp,yp,x1,y1,x2,y2,x3,y3,&xc,&yc,&r);
            if (xc < xp && ((xp-xc)*(xp-xc)) > r)
                complete[j] = TRUE;
            if (inside) {
                /* Check that we haven't exceeded the edge list size */
                if (nedge+3 >= emax) {
                    emax += 100;
                    if ((edges = (IEDGE *)realloc(edges,emax*(long)sizeof(IEDGE))) == NULL) {
                        status = 3;
                        goto skip;
                    }
                }
                int i1 = v[j].p1, i2 = v[j].p2, i3 = v[j].p3;
                float area = TriangleArea(x1,y1,x2,y2,x3,y3);
                if (area > 0)
                    std::swap(i1, i3);

                edges[nedge+0].p1 = i1;
                edges[nedge+0].p2 = i2;
                edges[nedge+1].p1 = i2;
                edges[nedge+1].p2 = i3;
                edges[nedge+2].p1 = i3;
                edges[nedge+2].p2 = i1;
                nedge += 3;
                v[j] = v[(*ntri)-1];
                complete[j] = complete[(*ntri)-1];
                (*ntri)--;
                j--;
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
            if ((*ntri) >= trimax) {
                status = 4;
                goto skip;
            }
            
            float area = -1;
            int i1 = edges[j].p1, i2 = edges[j].p2, i3 = i;
//            x1 = pxyz[i1].x, y1 = pxyz[i1].y;
//            x2 = pxyz[i2].x, y2 = pxyz[i2].y;
//            x3 = pxyz[i3].x, y3 = pxyz[i3].y;
//            
//            area = TriangleArea(x1,y1,x2,y2,x3,y3);
//            if (area > 0)
//                std::swap(i1, i3);
            
            v[*ntri].p1 = i1;
            v[*ntri].p2 = i2;
            v[*ntri].p3 = i3;
            
            if (area != 0) {
                complete[*ntri] = FALSE;
                (*ntri)++;
            } else {
                area = area;
            }
            
        }
    }
    
    /*
     Remove triangles with supertriangle vertices
     These are triangles which have a vertex number greater than nv
     */
    for (i=0;i<(*ntri);i++) {
        if (v[i].p1 >= nv || v[i].p2 >= nv || v[i].p3 >= nv) {
            v[i] = v[(*ntri)-1];
            (*ntri)--;
            i--;
        }
    }
    
skip:
    free(edges);
    free(complete);
    return(status);
}



#endif /* delaunay_h */
