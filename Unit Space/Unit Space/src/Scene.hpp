//
//  Scene.hpp
//  Unit Space
//
//  Created by Nigel Barber on 02/04/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
#import "Path.hpp"


// A path drawn with a transform and a paint. A scene draws its instances in order, and instances of one path share its
// triangles.
struct Instance {
    int                 path;
    AffineTransform     ctm;
    RGBA8               color;
    int                 source;     // the index of the shape it came from in its source (an SVG's), or -1
};

// Paths, and the instances that draw them.
struct Scene {
    // Adds a path to be drawn by instances, returning its index, or -1 if it has no contours.
    int addPath(const Path& path) {
        if (path._contours.empty())
            return -1;
        _paths.emplace_back(path);
        return (int)_paths.size() - 1;
    }
    // Draws path (an index from addPath()) next, unless it is -1.
    void addInstance(int path, RGBA8 color, int source = -1, AffineTransform ctm = { 1, 0, 0, 1, 0, 0 }) {
        if (path >= 0)
            _instances.push_back({ path, ctm, color, source });
    }
    void empty() { _paths.resize(0), _instances.resize(0); }
    
    vector<Path>            _paths;
    vector<Instance>        _instances;
};
