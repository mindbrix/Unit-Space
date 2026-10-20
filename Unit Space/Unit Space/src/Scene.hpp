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
#import "Mesh.hpp"
#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"


struct Scene {
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
    void addSVG(const void *bytes, size_t size) {
        char *data = (char *)malloc(size + 1);
        if (data == NULL)
            return;
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = nsvgParse(data, "px", 96);
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
        Path p(shape->paths->closed, shape->fillRule == NSVG_FILLRULE_EVENODD, filled ? paintColor(shape->fill, shape->opacity) : RGBA8());
        // Strokes are filled outlines (nonzero), painted over the fill.
        Path strokePath(true, false, stroked ? paintColor(shape->stroke, shape->opacity) : RGBA8());
        strokePath._outline = true, strokePath._penWidth = shape->strokeWidth;
        p._source = strokePath._source = source;
        Stroke stroke(strokePath, shape->strokeWidth,
                      shape->strokeLineJoin == NSVG_JOIN_ROUND ? Stroke::kRound : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? Stroke::kBevel : Stroke::kMiter,
                      shape->strokeLineCap == NSVG_CAP_ROUND ? Stroke::kRoundCap : shape->strokeLineCap == NSVG_CAP_SQUARE ? Stroke::kSquare : Stroke::kButt,
                      shape->miterLimit);
        if (shape->strokeDashCount > 0)
            stroke.setDashes(shape->strokeDashArray, shape->strokeDashCount, shape->strokeDashOffset);
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
            if (stroked) {
                // The stroke's centreline keeps the cubics' tangents, so smooth joins stay smooth.
                SubPath c(pts, path->npts, SubPath::kCubic, true);
                float limit = vertexMergeLimit(c._bounds);
                c.mergeVertices(limit, path->closed);
                // A zero-length subpath (M x y l 0 0) converts to no quadratics, but still strokes as a cap-shaped dot.
                bool point = c._quadratics.count() == 0 && path->npts > 0;
                for (int i = 1; point && i < path->npts; i++)
                    point = pts[i * 2] == pts[0] && pts[i * 2 + 1] == pts[1];
                if (point)
                    stroke.addSubPath(pts, 1, path->closed, limit);
                else
                    stroke.addSubPath(c._quadratics.get(), (int)c._quadratics.count() / 2, path->closed, limit);
            }
        }
        if (filled)
            addPath(p);
        if (stroked)
            addPath(strokePath);
    }
    void empty() { _paths.resize(0), _CTMs.resize(0); }
    
    vector<Path>            _paths;
    vector<AffineTransform> _CTMs;
    Bounds                  _bounds;
};
