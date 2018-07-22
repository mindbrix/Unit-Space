//
//  Tess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 02/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
#import "tesselator.h"
#import "Algorithms.hpp"
#import "Memory.hpp"
#import "Path.hpp"
#import "Mesh.hpp"
#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
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
                        
                        Path p(false, RGBA8(0, 0, 0, 255));
                        for (base = bases, j = 0; j < ncontours; j++, base++) {
                            begin = *base;
                            end = j + 1 == ncontours ? count : *(base + 1);
                            
                            SubPath s(& buffer[begin * 2], end - begin, SubPath::kCubic, 1.0 / scale.a);
                            p.addSubPath(s);
                        }
                        addPath(p, & CTM);
                        stbtt_FreeShape(& font, vertices);
                    }
                }
            }
        }
    }
    void addPNG(const void *bytes, size_t size) {
        int x, y, channels_in_file;
        stbi_uc *image = stbi_load_from_memory((stbi_uc const *)bytes, (int)size, & x, & y, & channels_in_file, 0);
        if (image) {
            if (channels_in_file == 4) {
                size_t sortSize = x * y * channels_in_file;
                void *sortBuffer = malloc(sortSize);
                unsigned int *sortImage = (unsigned int *)sortBuffer, *sortImageEnd = sortImage + x * y;
                radixSort(sortImage, sortImageEnd);
                
                int count = 1;
                unsigned int color = sortImage[0];
                for (unsigned int *pixel = sortImage; pixel < sortImageEnd; pixel++)
                    if (*pixel != color)
                        color = *pixel, count++;
                free(sortBuffer);
            }
            stbi_image_free(image);
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
                if (shape->fill.type == NSVG_PAINT_COLOR /*&& shape->stroke.type == NSVG_PAINT_NONE*/) {
                    RGBA8 color = RGBA8((unsigned char)((shape->fill.color ) & 0xFF), (unsigned char)((shape->fill.color >> 8) & 0xFF), (unsigned char)((shape->fill.color >> 16) & 0xFF), (unsigned char)(shape->fill.color >> 24));
                    Path p(shape->fillRule == NSVG_FILLRULE_EVENODD, color);
                    
                    for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
                        for (int i = 0; i < path->npts; i++)
                            path->pts[i * 2 + 1] = image->height - path->pts[i * 2 + 1];
                        SubPath s(path->pts, path->npts, SubPath::kCubic);
                        p.addSubPath(s);
                    }
                    addPath(p);
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

struct Tess {
    static void writeCurves(Mesh& mesh, Scene& scene) {
        int pi, c;
        float area, flip;
        for (Path& path : scene._paths) {
            size_t outerIndex = path._outerIndex;
            SubPath* outer = & path._subPaths[outerIndex];
            
            for (SubPath& s : path._subPaths)
                if (s._quadratics.get() != NULL) {
                    const float *points = s._quadratics.get();
                    int npoints = (int)s._quadratics.count() / 2;
                    pi = (int)mesh._points.size();
                    c = (int)(& path - & scene._paths[0]);
                    
                    Vector p0 = { points[0], points[1] }, p1, p2;
                    mesh._points.emplace_back(p0);
                    
                    for (int i = 0; i < npoints - 1; i += 2, p0 = p2) {
                        p1 = { points[i * 2 + 2], points[i * 2 + 3] };
                        p2 = { points[i * 2 + 4], points[i * 2 + 5] };
                        
                        mesh._points.emplace_back(p1);
                        mesh._points.emplace_back(p2);
                        
                        area = (p1.x - p0.x) * (p2.y - p1.y) - (p1.y - p0.y) * (p2.x - p1.x);
                        Triangle tri = { pi + i, pi + i + 1, pi + i + 2, c };
                        
                        if (area != 0) {
                            if (area < 0)
                                std::swap(tri.i0, tri.i2);
                            
                            flip = outer == & s ? 1 : -1;
                            
                            if (flip * area * s._area < 0)
                                mesh._concaveCurves.emplace_back(tri);
                            else
                                mesh._curves.emplace_back(tri);
                        }
                    }
                }
        }
    }
    
    void triangulate(Mesh& mesh, Scene& scene, bool useMasking) {
        mesh.empty();
        writeCurves(mesh, scene);
        for (Path& path : scene._paths)
            mesh._colors.emplace_back(path._color);
    }
};
