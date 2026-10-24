//
//  SVG.hpp
//  Unit Space
//
//  SVG files loaded into scenes with nanosvg: a convenient source of bulk test data. Scenes keep nothing of nanosvg's.
//
#import "Scene.hpp"
#import <string.h>
#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"

struct SVG {
    static void addSVG(Scene& scene, const void *bytes, size_t size) {
        char *data = (char *)malloc(size + 1);
        if (data == NULL)
            return;
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = nsvgParse(data, "px", 96);
        if (image) {
            printf("size: %f x %f\n", image->width, image->height);
            addImage(scene, image);
            nsvgDelete(image);
        }
        free(data);
    }
    static void addImage(Scene& scene, NSVGimage *image) {
        int source = 0;
        for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next, source++)
            addShape(scene, shape, image->height, source);
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
    // Adds a shape's fill path, then its stroke path: its subpaths as cubic contours, y flipped about the image height. A shape
    // with no contour a path keeps draws nothing. The shape is unchanged, so it can be edited and added again.
    static void addShape(Scene& scene, NSVGshape *shape, float height, int source) {
        bool filled = shape->fill.type != NSVG_PAINT_NONE, stroked = shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth > 0;
        Path fill(shape->fillRule == NSVG_FILLRULE_EVENODD);
        int npoints = 0, ncontours = 0;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next)
            npoints += path->npts, ncontours++;
        fill.reserve(npoints, ncontours);
        vector<float> buffer;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            buffer.assign(path->pts, path->pts + path->npts * 2);
            for (int i = 0; i < path->npts; i++)
                buffer[i * 2 + 1] = height - buffer[i * 2 + 1];
            fill.add(Path::kCubics, buffer.data(), path->npts, path->closed);
        }
        if (fill._contours.empty())
            return;
        if (filled)
            scene.addInstance(scene.addPath(fill), paintColor(shape->fill, shape->opacity), source);
        if (stroked) {
            // Strokes are painted over the fill.
            Path stroke = fill;
            stroke.stroke(shape->strokeWidth,
                          shape->strokeLineJoin == NSVG_JOIN_ROUND ? Path::kRound : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? Path::kBevel : Path::kMiter,
                          shape->strokeLineCap == NSVG_CAP_ROUND ? Path::kRoundCap : shape->strokeLineCap == NSVG_CAP_SQUARE ? Path::kSquare : Path::kButt,
                          shape->miterLimit, shape->strokeDashArray, shape->strokeDashCount, shape->strokeDashOffset);
            scene.addInstance(scene.addPath(stroke), paintColor(shape->stroke, shape->opacity), source);
        }
    }
};
