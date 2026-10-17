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
#import "Stroke.hpp"
#import <CoreGraphics/CoreGraphics.h>
#import "Mesh.hpp"
#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb_truetype.h"


struct Scene {
    // Core Graphics strokes the shape's centreline, and its outline becomes the stroke path's cubic subpaths (lines and
    // quadratics raised to cubics), so strokes share the fills' cubic conversion.
    static void addCGStroke(Path& out, CGPathRef centreline, NSVGshape *shape) {
        CGPathRef dashed = nullptr;
        if (shape->strokeDashCount > 0) {
            // An odd dash list repeats to make an even one.
            int count = shape->strokeDashCount * (shape->strokeDashCount & 1 ? 2 : 1);
            vector<CGFloat> lengths(count);
            for (int i = 0; i < count; i++)
                lengths[i] = shape->strokeDashArray[i % shape->strokeDashCount];
            dashed = CGPathCreateCopyByDashingPath(centreline, nullptr, shape->strokeDashOffset, lengths.data(), count);
        }
        CGLineCap cap = shape->strokeLineCap == NSVG_CAP_ROUND ? kCGLineCapRound : shape->strokeLineCap == NSVG_CAP_SQUARE ? kCGLineCapSquare : kCGLineCapButt;
        CGLineJoin join = shape->strokeLineJoin == NSVG_JOIN_ROUND ? kCGLineJoinRound : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? kCGLineJoinBevel : kCGLineJoinMiter;
        CGPathRef outline = CGPathCreateCopyByStrokingPath(dashed ? dashed : centreline, nullptr, shape->strokeWidth, cap, join, shape->miterLimit);
        // Relative to the pen, as an outline's two sides are only its width apart, however long its cubics.
        float precision = std::min(kSplitCubicPrecision, 0.01f * shape->strokeWidth);
        vector<float> pts;
        auto add = [&]() {
            if (pts.size() > 2) {
                SubPath s(pts.data(), (int)pts.size() / 2, SubPath::kCubic, false, precision);
                out.addSubPath(s);
            }
            pts.resize(0);
        };
        auto cubic = [&](float x1, float y1, float x2, float y2, float x3, float y3) { pts.insert(pts.end(), { x1, y1, x2, y2, x3, y3 }); };
        auto move = [&](float x, float y) { add(), pts.insert(pts.end(), { x, y }); };
        auto line = [&](float x, float y) {
            float x0 = pts[pts.size() - 2], y0 = pts[pts.size() - 1];
            cubic(x0 + (x - x0) / 3, y0 + (y - y0) / 3, x0 + (x - x0) * 2 / 3, y0 + (y - y0) * 2 / 3, x, y);
        };
        auto quad = [&](float cx, float cy, float x, float y) {
            float x0 = pts[pts.size() - 2], y0 = pts[pts.size() - 1];
            cubic(x0 + (cx - x0) * 2 / 3, y0 + (cy - y0) * 2 / 3, x + (cx - x) * 2 / 3, y + (cy - y) * 2 / 3, x, y);
        };
        auto close = [&]() {
            if (pts.size() > 2 && (pts[0] != pts[pts.size() - 2] || pts[1] != pts[pts.size() - 1]))
                line(pts[0], pts[1]);
            add();
        };
        // The block holds copies of the lambdas, which still refer to pts.
        CGPathApplyWithBlock(outline, ^(const CGPathElement *e) {
            const CGPoint *p = e->points;
            switch (e->type) {
                case kCGPathElementMoveToPoint:
                    move(p[0].x, p[0].y);
                    break;
                case kCGPathElementAddLineToPoint:
                    line(p[0].x, p[0].y);
                    break;
                case kCGPathElementAddQuadCurveToPoint:
                    quad(p[0].x, p[0].y, p[1].x, p[1].y);
                    break;
                case kCGPathElementAddCurveToPoint:
                    cubic(p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y);
                    break;
                case kCGPathElementCloseSubpath:
                    close();
                    break;
            }
        });
        add();
        CGPathRelease(outline);
        if (dashed)
            CGPathRelease(dashed);
    }

    void addPath(Path& path, AffineTransform *CTM = nullptr) {
        if (path._subPaths.size() != 0) {
            // libTess sees closed contours, merging vertices within this of each other, so merge them first, cleanly.
            float limit = vertexMergeLimit(path._bounds);
            for (SubPath& s : path._subPaths)
                s.mergeVertices(limit, true);
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
                        
                        Path p(false, false, 0, RGBA8(0, 0, 0, 255));
                        for (base = bases, j = 0; j < ncontours; j++, base++) {
                            begin = *base;
                            end = j + 1 == ncontours ? count : *(base + 1);
                            
                            SubPath s(& buffer[begin * 2], end - begin, SubPath::kCubic);
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
            addImage(image);
            nsvgDelete(image);
        }
        free(data);
    }
    void addImage(NSVGimage *image) {
        int source = 0;
        for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next, source++)
            addShape(shape, image->height, source);
    }
    // A paint's colour, with gradients approximated by the average of their stops (so ones that fade out are
    // translucent), and the shape's opacity (including its group's), which is separate from the paint's alpha.
    static RGBA8 paintColor(NSVGpaint& paint, float opacity) {
        int r = 0, g = 0, b = 0, a = 0, nstops = paint.type == NSVG_PAINT_COLOR ? 1 : paint.gradient->nstops;
        for (int i = 0; i < nstops; i++) {
            unsigned int rgba = paint.type == NSVG_PAINT_COLOR ? paint.color : paint.gradient->stops[i].color;
            r += rgba & 0xFF, g += (rgba >> 8) & 0xFF, b += (rgba >> 16) & 0xFF, a += rgba >> 24;
        }
        return RGBA8(r / nstops, g / nstops, b / nstops, int(a / nstops * opacity + 0.5f));
    }
    // Adds an SVG shape's fill path, then its stroke path, flipping y about the image height. The shape is unchanged,
    // so it can be edited and added again.
    void addShape(NSVGshape *shape, float height, int source) {
        bool filled = shape->fill.type != NSVG_PAINT_NONE, stroked = shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth > 0;
        Path p(shape->paths->closed, shape->fillRule == NSVG_FILLRULE_EVENODD, 0, filled ? paintColor(shape->fill, shape->opacity) : RGBA8());
        // Strokes are filled outlines (nonzero), painted over the fill.
        Path strokePath(true, false, 0, stroked ? paintColor(shape->stroke, shape->opacity) : RGBA8());
        strokePath._outline = true;
        p._source = strokePath._source = source;
        // Either Stroke expands the centreline, or Core Graphics strokes it (see addCGStroke).
        Stroke stroke(strokePath, shape->strokeWidth,
                      shape->strokeLineJoin == NSVG_JOIN_ROUND ? Stroke::kRound : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? Stroke::kBevel : Stroke::kMiter,
                      shape->strokeLineCap == NSVG_CAP_ROUND ? Stroke::kRoundCap : shape->strokeLineCap == NSVG_CAP_SQUARE ? Stroke::kSquare : Stroke::kButt,
                      shape->miterLimit);
        if (!_cgStroker)
            strokePath._penWidth = shape->strokeWidth;
        if (shape->strokeDashCount > 0)
            stroke.setDashes(shape->strokeDashArray, shape->strokeDashCount, shape->strokeDashOffset);
        CGMutablePathRef centreline = CGPathCreateMutable();
        vector<float> buffer;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            // Parser output can be non-finite (degenerate arcs), which would poison bounds and tessellation.
            bool finite = true;
            for (int i = 0; i < path->npts * 2; i++)
                finite &= std::isfinite(path->pts[i]);
            if (!finite)
                continue;
            buffer.assign(path->pts, path->pts + path->npts * 2);
            float *pts = buffer.data();
            for (int i = 0; i < path->npts; i++)
                pts[i * 2 + 1] = height - pts[i * 2 + 1];
            SubPath s(pts, path->npts, SubPath::kCubic);
            p.addSubPath(s);
            if (stroked && !_cgStroker) {
                // The stroke's centreline keeps the cubics' tangents, so smooth joins stay smooth.
                SubPath c(pts, path->npts, SubPath::kCubic, true);
                c.mergeVertices(vertexMergeLimit(c._bounds), path->closed);
                // A zero-length subpath (M x y l 0 0) converts to no quadratics, but still strokes as a cap-shaped dot.
                bool point = c._quadratics.count() == 0 && path->npts > 0;
                for (int i = 1; point && i < path->npts; i++)
                    point = pts[i * 2] == pts[0] && pts[i * 2 + 1] == pts[1];
                if (point)
                    stroke.addSubPath(pts, 1, path->closed);
                else
                    stroke.addSubPath(c._quadratics.get(), (int)c._quadratics.count() / 2, path->closed);
            } else if (stroked) {
                CGPathMoveToPoint(centreline, nullptr, pts[0], pts[1]);
                // nanosvg's lines are cubics, which Core Graphics would stroke as curves, splitting them.
                for (int i = 1; i + 2 < path->npts; i += 3) {
                    const float *q = pts + i * 2;
                    if (isStraightCubic(q - 2))
                        CGPathAddLineToPoint(centreline, nullptr, q[4], q[5]);
                    else
                        CGPathAddCurveToPoint(centreline, nullptr, q[0], q[1], q[2], q[3], q[4], q[5]);
                }
                if (path->closed)
                    CGPathCloseSubpath(centreline);
            }
        }
        if (filled)
            addPath(p);
        if (stroked) {
            if (_cgStroker)
                addCGStroke(strokePath, centreline, shape);
            addPath(strokePath);
        }
        CGPathRelease(centreline);
    }
    void empty() { _paths.resize(0), _CTMs.resize(0); }
    
    bool                    _cgStroker = false;     // SVG strokes by Core Graphics rather than Stroke
    vector<Path>            _paths;
    vector<AffineTransform> _CTMs;
    Bounds                  _bounds;
};
