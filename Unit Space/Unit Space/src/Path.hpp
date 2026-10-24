//
//  Path.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import "SubPath.hpp"

// Contours of quadratic chains (p0, c, p1, c, p2 ...), one after another in _points.
struct Path {
    Path() {}
    Path(bool closed, bool evenOdd) : _closed(closed), _evenOdd(evenOdd) {}

    // Each add converts one contour, which is kept if it lies within coordinateLimit() and has a quadratic.
    // tangents: cubics convert to tangent-preserving quadratics (see splitCubics).
    void addCubics(const float *points, int npoints, bool tangents = false) {
        Bounds b;
        if (!begin(points, npoints, b))
            return;
        QuadraticChain chain(_points);
        splitCubics(points, npoints, kSplitCubicPrecision, vertexMergeLimit(b), tangents, chain);
        end(b);
    }
    void addQuadratics(const float *points, int npoints) {
        Bounds b;
        if (!begin(points, npoints, b))
            return;
        if (npoints > 2)
            subdivideQuadratics(points, npoints, _points);
        end(b);
    }
    void addLines(const float *points, int npoints) {
        Bounds b;
        if (!begin(points, npoints, b))
            return;
        float x0 = points[0], y0 = points[1], x1, y1;
        _points.push_back(x0), _points.push_back(y0);
        for (int i = 1; i < npoints; i++, x0 = x1, y0 = y1) {
            x1 = points[i * 2], y1 = points[i * 2 + 1];
            _points.push_back((x0 + x1) * 0.5), _points.push_back((y0 + y1) * 0.5);
            _points.push_back(x1), _points.push_back(y1);
        }
        end(b);
    }
    // Merges each contour's vertices (mergeContour()) and closes the gaps they leave.
    void mergeVertices(float limit, bool closed = true) {
        size_t size = 0;
        _flat = true;
        for (int i = 0; i < contours(); i++) {
            size_t first = _contours[i], count = contourSize(i);
            if (size != first)
                memmove(_points.data() + size, _points.data() + first, count * sizeof(float));
            bool flat;
            _contours[i] = size, size += mergeContour(_points.data() + size, count, limit, closed, & flat);
            _flat = _flat && flat;
        }
        _points.resize(size);
    }
    void clear() { _points.resize(0), _contours.resize(0); }

    int contours() const { return (int)_contours.size(); }
    // Contour i's floats, from _points[_contours[i]].
    size_t contourSize(int i) const { return (i + 1 < contours() ? _contours[i + 1] : _points.size()) - _contours[i]; }

    bool contains(float x, float y) {
        if (!_bounds.contains(x, y))
            return false;
        float p[2] = { x, y };
        int wn = 0;
        for (int i = 0; i < contours(); i++)
            wn += (int)pointInPolygon(p, _points.data() + _contours[i], contourSize(i) / 4 + 1, kPointStride * 2);
        return _evenOdd ? wn & 1 : wn;
    }
    bool                        _closed, _evenOdd;
    bool                        _outline = false;   // a stroke expanded into a filled outline, which crosses itself
    bool                        _flat = false;      // only lines, which mergeVertices() decides
    float                       _penWidth = 0;      // an outline's stroke width
    Bounds                      _bounds;
    vector<float>               _points;            // every contour's quadratics
    vector<size_t>              _contours;          // each contour's first float in _points
    // An SVG shape's fill or stroke has no contours of its own: Scene::convert() makes them from the shape's points when
    // they are needed, and its bounds are those points' until then.
    const struct NSVGshape     *_shape = nullptr;
    float                       _height = 0;        // the shape's image height, about which y flips

private:
    // Starts a contour from points, unless they lie beyond coordinateLimit(), with their bounds.
    bool begin(const float *points, int npoints, Bounds& b) {
        boundsLimits(& b.lx, points, points + npoints * 2, kPointStride);
        float limit = coordinateLimit();
        if (!(b.lx >= -limit && b.ux <= limit && b.ly >= -limit && b.uy <= limit))
            return false;
        _contours.push_back(_points.size());
        return true;
    }
    // Keeps the contour begun if it has a quadratic, uniting its points' bounds with the path's.
    void end(const Bounds& b) {
        if (_points.size() - _contours.back() < 6) {
            _points.resize(_contours.back()), _contours.pop_back();
            return;
        }
        if (_contours.size() == 1)
            _bounds = b;
        _bounds.unite(b);
    }
};
