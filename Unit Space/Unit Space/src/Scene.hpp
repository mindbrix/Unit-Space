//
//  Scene.hpp
//  Unit Space
//
//  Created by Nigel Barber on 02/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
#import <memory>
#import "Algorithms.hpp"
#import "Memory.hpp"
#import "Path.hpp"
#import "Stroke.hpp"
#import "Mesh.hpp"
#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"


// A path drawn with a transform and a paint. A scene draws its instances in order, and instances of one path share its
// triangles.
struct Instance {
    int                 path;
    AffineTransform     ctm;
    RGBA8               color;
    int                 source;     // the index of the SVG shape it came from, or -1
};

struct Scene {
    // Adds a path to be drawn by instances, returning its index, or -1 if it has no contours.
    int addPath(Path& path) {
        if (path._contours.empty())
            return -1;
        // libTess sees closed contours, merging vertices within this of each other, so merge them first, cleanly.
        path.mergeVertices(vertexMergeLimit(path._bounds));
        _paths.emplace_back(path);
        return (int)_paths.size() - 1;
    }
    // Draws path (an index from addPath()) next, unless it is -1.
    void addInstance(int path, RGBA8 color, int source = -1, AffineTransform ctm = { 1, 0, 0, 1, 0, 0 }) {
        if (path >= 0)
            _instances.push_back({ path, ctm, color, source });
    }
    // Parses an SVG, which the scene keeps, as its paths are its shapes'.
    void addSVG(const void *bytes, size_t size) {
        char *data = (char *)malloc(size + 1);
        if (data == NULL)
            return;
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = nsvgParse(data, "px", 96);
        if (image) {
            printf("size: %f x %f\n", image->width, image->height);
            _image = std::shared_ptr<NSVGimage>(image, nsvgDelete);
            addImage(image);
        }
        free(data);
    }
    // Adds an image's shapes, which must outlive the scene's use of them.
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
    // Adds an SVG shape's fill path, then its stroke path. Converting them is left to convert(), when they are
    // triangulated, so they are only the shape and the bounds of its subpaths' points: those convert() keeps, which are
    // finite and within coordinateLimit(). A shape with none draws nothing. The shape is unchanged, so it can be edited
    // and added again.
    void addShape(NSVGshape *shape, float height, int source) {
        bool filled = shape->fill.type != NSVG_PAINT_NONE, stroked = shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth > 0;
        Bounds bounds;
        bool any = false;
        float limit = coordinateLimit();
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            Bounds b;
            if (!finite(path))
                continue;
            // Flipping y about height reverses the order of y, rounding included, so the flipped points' bounds are
            // the points' bounds flipped.
            boundsLimits(& b.lx, path->pts, path->pts + path->npts * 2, kPointStride);
            b = Bounds(b.lx, height - b.uy, b.ux, height - b.ly);
            if (b.lx >= -limit && b.ux <= limit && b.ly >= -limit && b.uy <= limit)
                bounds = any ? bounds.united(b) : b, any = true;
        }
        if (!any)
            return;
        // Strokes are filled outlines (nonzero), painted over the fill.
        Path p(shape->paths->closed, shape->fillRule == NSVG_FILLRULE_EVENODD), strokePath(true, false);
        strokePath._outline = true, strokePath._penWidth = shape->strokeWidth;
        for (Path *path : { & p, & strokePath })
            path->_shape = shape, path->_height = height, path->_bounds = bounds;
        if (filled)
            _paths.emplace_back(p), addInstance((int)_paths.size() - 1, paintColor(shape->fill, shape->opacity), source);
        if (stroked)
            _paths.emplace_back(strokePath), addInstance((int)_paths.size() - 1, paintColor(shape->stroke, shape->opacity), source);
    }
    // A path as triangulation needs it: an SVG shape's fill or stroke converted from the shape's points, else a copy.
    // It only reads the shape, so paths can convert in parallel.
    static Path convert(const Path& path) {
        const NSVGshape *shape = path._shape;
        if (shape == nullptr)
            return path;
        Path p(path._closed, path._evenOdd);
        p._outline = path._outline, p._penWidth = path._penWidth, p._bounds = path._bounds;
        Stroke stroke(p, shape->strokeWidth,
                      shape->strokeLineJoin == NSVG_JOIN_ROUND ? Stroke::kRound : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? Stroke::kBevel : Stroke::kMiter,
                      shape->strokeLineCap == NSVG_CAP_ROUND ? Stroke::kRoundCap : shape->strokeLineCap == NSVG_CAP_SQUARE ? Stroke::kSquare : Stroke::kButt,
                      shape->miterLimit);
        if (path._outline && shape->strokeDashCount > 0)
            stroke.setDashes(shape->strokeDashArray, shape->strokeDashCount, shape->strokeDashOffset);
        vector<float> buffer;
        Path centre;
        for (NSVGpath *sp = shape->paths; sp != NULL; sp = sp->next) {
            if (!flipped(sp, path._height, buffer))
                continue;
            float *pts = buffer.data();
            if (!path._outline) {
                p.addCubics(pts, sp->npts);
                continue;
            }
            // The stroke's centreline keeps the cubics' tangents, so smooth joins stay smooth, and merges within its own
            // points' limit.
            Bounds b;
            boundsLimits(& b.lx, pts, pts + sp->npts * 2, kPointStride);
            float limit = vertexMergeLimit(b);
            centre.clear(), centre.addCubics(pts, sp->npts, true), centre.mergeVertices(limit, sp->closed);
            // A zero-length subpath (M x y l 0 0) converts to no quadratics, but still strokes as a cap-shaped dot.
            bool point = centre._points.empty() && sp->npts > 0;
            for (int i = 1; point && i < sp->npts; i++)
                point = pts[i * 2] == pts[0] && pts[i * 2 + 1] == pts[1];
            if (point)
                stroke.addSubPath(pts, 1, sp->closed, limit);
            else
                stroke.addSubPath(centre._points.data(), (int)centre._points.size() / 2, sp->closed, limit);
        }
        // libTess sees closed contours, merging vertices within this of each other, so merge them first, cleanly. A
        // fill's limit is from its shape's points, which its contours are; an outline's from its own bounds.
        p.mergeVertices(vertexMergeLimit(path._outline ? p._bounds : path._bounds));
        return p;
    }
    // A path's share of the work of triangulating: its points, three times over for stroke outlines, which cross
    // themselves.
    int64_t cost(int p) {
        Path& path = _paths[p];
        int64_t n = 0;
        if (path._shape)
            for (NSVGpath *sp = path._shape->paths; sp != NULL; sp = sp->next)
                n += sp->npts;
        else
            n = (int64_t)path._points.size();
        return n * (path._outline ? 3 : 1);
    }
    void empty() { _paths.resize(0), _instances.resize(0), _image = nullptr; }
    
    vector<Path>            _paths;
    vector<Instance>        _instances;
    std::shared_ptr<NSVGimage> _image;  // an image addSVG() parsed, whose shapes its paths are
    
private:
    // Whether a path's points are all finite: parser output can be not (degenerate arcs), which would poison bounds and
    // tessellation.
    static bool finite(const NSVGpath *path) {
        for (int i = 0; i < path->npts * 2; i++)
            if (!std::isfinite(path->pts[i]))
                return false;
        return true;
    }
    // A path's points with y flipped about height, unless any is not finite.
    static bool flipped(const NSVGpath *path, float height, vector<float>& buffer) {
        if (!finite(path))
            return false;
        buffer.assign(path->pts, path->pts + path->npts * 2);
        for (int i = 0; i < path->npts; i++)
            buffer[i * 2 + 1] = height - buffer[i * 2 + 1];
        return true;
    }
};
