//
//  Scene.hpp
//  Unit Space
//
//  Created by Nigel Barber on 02/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
#import "Algorithms.hpp"
#import "Memory.hpp"
#import "Path.hpp"
#import "Mesh.hpp"
#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb_truetype.h"


struct Scene {
    void addPath(Path& path, AffineTransform *CTM = nullptr) {
        if (path._subPaths.size() != 0) {
            if (_paths.size() == 0)
                _bounds = path._bounds;
            _bounds.unite(path._bounds);
            _paths.emplace_back(path);
            AffineTransform identity = { 1, 0, 0, 1, 0, 0 };
            _CTMs.emplace_back(CTM ? *CTM : identity);
        }
    }
    void addFont(const void *bytes, size_t size) {
        int retval, i, j, nverts, count, ncontours, d, ix, iy;
        int flx, fly, fux, fuy, fw, fh, fdim, fsize = 32;
        int lx, ly, ux, uy;
        stbtt_fontinfo font;
        stbtt_vertex *vertices, *vertex;
        const unsigned char *ttf_buffer = (const unsigned char *)bytes;
        retval = stbtt_InitFont(& font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));
        if (retval) {
            stbtt_GetFontBoundingBox(& font, & flx, & fly, & fux, & fuy);
            fw = fux - flx, fh = fuy - fly, fdim = fw < fh ? fw : fh;
            d = std::ceil(std::sqrt((float)font.numGlyphs));
            AffineTransform scale = { float(fsize) / float(fdim), 0, 0, float(fsize) / float(fdim), 0, 0 };
            
            for (i = 0; i < font.numGlyphs; i++) {
                ix = i % d, iy = i / d;
                AffineTransform CTM = { 1, 0, 0, 1, float(ix * (fux - flx)), float(iy * (fuy - fly)) };
                CTM = AffineTransformConcat(CTM, scale);
                if (stbtt_IsGlyphEmpty(& font, i) == 0) {
                    retval = stbtt_GetGlyphBox(& font, i, & lx, & ly, & ux, & uy);
                    nverts = stbtt_GetGlyphShape(& font, i, & vertices);
                    if (nverts) {
                        for (ncontours = 0, vertex = vertices, j = 0; j < nverts; j++, vertex++)
                            if (vertex->type == STBTT_vmove)
                                ncontours++;
                        
                        count = (nverts - ncontours) * 3 + ncontours;
                        float buffer[count * 2], *dst = buffer, px = 0, py = 0, kOneThird = 1.0 / 3.0, kTwoThirds = 2.0 / 3.0;
                        int bases[ncontours], *base = bases, begin, end;
                        
                        for (vertex = vertices, j = 0; j < nverts; j++, vertex++) {
                            switch (vertex->type) {
                                case STBTT_vmove:
                                    *base++ = (int)(dst - buffer) / 2;
                                    break;
                                case STBTT_vline:
                                    *dst++ = lerp(kOneThird, px, vertex->x), *dst++ = lerp(kOneThird, py, vertex->y);
                                    *dst++ = lerp(kTwoThirds, px, vertex->x), *dst++ = lerp(kTwoThirds, py, vertex->y);
                                    break;
                                case STBTT_vcurve:
                                    *dst++ = lerp(kTwoThirds, px, vertex->cx), *dst++ = lerp(kTwoThirds, py, vertex->cy);
                                    *dst++ = lerp(kTwoThirds, vertex->x, vertex->cx), *dst++ = lerp(kTwoThirds, vertex->y, vertex->cy);
                                    break;
                                case STBTT_vcubic:
                                    *dst++ = vertex->cx, *dst++ = vertex->cy;
                                    *dst++ = vertex->cx1, *dst++ = vertex->cy1;
                                    break;
                            }
                            *dst++ = px = vertex->x, *dst++ = py = vertex->y;
                        }
                        
                        Path p(false, 0, RGBA8(0, 0, 0, 255));
                        for (base = bases, j = 0; j < ncontours; j++, base++) {
                            begin = *base;
                            end = j + 1 == ncontours ? count : *(base + 1);
                            
                            SubPath s(& buffer[begin * 2], end - begin, SubPath::kCubic, kFlattenQuadraticPrecision / scale.a);
                            p.addSubPath(s);
                        }
                        addPath(p, & CTM);
                        stbtt_FreeShape(& font, vertices);
                    }
                }
            }
        }
    }
    void addSVG(const void *bytes, size_t size) {
        char *data = (char *)malloc(size + 1);
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = data ? nsvgParse(data, "px", 96) : NULL;
        
        if (image) {
            printf("size: %f x %f\n", image->width, image->height);            
            int limit = 60000;
            for (NSVGshape *shape = image->shapes; shape != NULL && limit; shape = shape->next, limit--) {
                RGBA8 fillColor, strokeColor;
                
                if (shape->fill.type != NSVG_PAINT_NONE) {
                    int r, g, b, a, nstops, i;
                    unsigned int rgba;
                    
                    if (shape->fill.type == NSVG_PAINT_COLOR) {
                        rgba = shape->fill.color;
                        r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF, a = rgba >> 24;
                    } else {
                        nstops = 1;// shape->fill.gradient->nstops;
                        for (r = b = g = a = i = 0; i < nstops; i++) {
                            rgba = shape->fill.gradient->stops[i].color;
                            r += rgba & 0xFF, g += (rgba >> 8) & 0xFF, b += (rgba >> 16) & 0xFF, a += rgba >> 24;
                        }
                        r /= nstops, g /= nstops, b /= nstops, a /= nstops;
                    }
                    fillColor = RGBA8(r, g, b, a);
                }
                if (shape->stroke.type == NSVG_PAINT_COLOR)
                    strokeColor = RGBA8(shape->stroke.color & 0xFF, (shape->stroke.color >> 8) & 0xFF, (shape->stroke.color >> 16) & 0xFF, shape->stroke.color >> 24);
                
                Path p(shape->fillRule == NSVG_FILLRULE_EVENODD, 0, fillColor);
                for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
                    for (int i = 0; i < path->npts; i++)
                        path->pts[i * 2 + 1] = image->height - path->pts[i * 2 + 1];
                    SubPath s(path->pts, path->npts, SubPath::kCubic);
                    p.addSubPath(s);
                }
                if (fillColor.a)
                    addPath(p);
                if (strokeColor.a) {
                    Path strokePath(false, 1, strokeColor, p._subPaths);
                    addPath(strokePath);
                }
            }
            // Delete
            nsvgDelete(image);
        }
        free(data);
    }
    void empty() { _paths.resize(0); }
    
    vector<Path>            _paths;
    vector<AffineTransform> _CTMs;
    Bounds                  _bounds;
};
