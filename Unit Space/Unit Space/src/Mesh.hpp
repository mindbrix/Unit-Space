//
//  Mesh.hpp
//  Unit Space
//
//  Created by Nigel Barber on 30/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
#import "Memory.hpp"
using namespace std;

#pragma clang diagnostic ignored "-Wcomma"

struct Mesh {
    // An instance's interior draws only where it covers more pixels than this per edge of its path. Below it the edge
    // pass, which shades every pixel near an edge anyway, is cheaper: measured across the test files at 0.5-4x, where
    // one pixel per edge was much slower than no interiors (paris, contour) and this kept every interior that paid.
    static constexpr float kInteriorPixelsPerEdge = 256;
    
    // paths are the scene's instances, one MeshPath each, as are the paths in the calls and the shaders.
    // Paths [skipBegin, skipEnd) are left out, as when another mesh draws them. Each call is tagged with meshIndex,
    // and edge calls with their first path, offset by pathBase (paths is then that far into the paths buffer).
    static void writePathCalls(Mesh& mesh, MeshPath *paths, size_t pathsSize, Bounds clipBounds, State state, MeshState meshState,
                               int meshIndex = 0, int pathBase = 0, int skipBegin = 0, int skipEnd = 0) {
        int base, end, count, p, i, j;
        vector<Call> & calls = mesh._calls;
        Call *call;
        if (mesh._triangleInstances.count()) {
            bool opaques[pathsSize], *opaque = opaques, visibles[pathsSize], *visible = visibles;
            AffineTransform inv = AffineTransformInvert(state.CTM);
            float mx, my, w, h, rp, dx, dy, a, b, c, d, d0, d1;
            w = clipBounds.ux - clipBounds.lx, h = clipBounds.uy - clipBounds.ly;
            mx = (clipBounds.lx + clipBounds.ux) * 0.5, my = (clipBounds.ly + clipBounds.uy) * 0.5;
            a = inv.a * w, b = inv.b * w, c = inv.c * h, d = inv.d * h;
            d0 = (a + c) * (a + c) + (b + d) * (b + d);
            d1 = (a - c) * (a - c) + (b - d) * (b - d);
            const float rd = 0.5 * std::sqrt(d0 > d1 ? d0 : d1);
            const float cx = mx * inv.a + my * inv.c + inv.tx, cy = mx * inv.b + my * inv.d + inv.ty;
            // An interior only pays where it covers enough pixels per edge of its path, except in the debug views, which
            // show every interior triangle at any scale.
            const bool everyInterior = meshState.showTriangles || meshState.showPolygons;
            const float viewArea = std::abs(AffineTransformArea(state.CTM)) / (float(kUnsignedShortMax) * float(kUnsignedShortMax));

            const MeshPath *path = & paths[0];
            for (p = 0; p < pathsSize; p++, path++) {
                w = path->bounds.ux - path->bounds.lx, h = path->bounds.uy - path->bounds.ly;
                mx = (path->bounds.lx + path->bounds.ux) * 0.5, my = (path->bounds.ly + path->bounds.uy) * 0.5;
                a = path->ctm.a * w, b = path->ctm.b * w, c = path->ctm.c * h, d = path->ctm.d * h;
				d0 = (a + c) * (a + c) + (b + d) * (b + d);
				d1 = (a - c) * (a - c) + (b - d) * (b - d);
                rp = 0.5 * std::sqrt(d0 > d1 ? d0 : d1);
                // Its pixels per quantised unit of area, against its path's edges per unit of interior.
                float pixels = std::abs(a * d - b * c) * viewArea;
                *opaque++ = path->color.a == 255 && meshState.showInterior
                    && (everyInterior || pixels > kInteriorPixelsPerEdge * mesh._interiorDensities[mesh._instancePaths[p]]);
                dx = cx - (mx * path->ctm.a + my * path->ctm.c + path->ctm.tx), dy = cy - (mx * path->ctm.b + my * path->ctm.d + path->ctm.ty);
                *visible++ = std::sqrt(dx * dx + dy * dy) < rd + rp;
            }
            for (p = 0; p < pathsSize; p += 8) {
                bool set = false;
                end = p + 8 > pathsSize ? (int)pathsSize : p + 8;
                for (j = p; j < end && !set; j++)
                set |= visibles[j];
                for (j = p; j < end && set; j++)
                   visibles[j] = true;
            }
            for (p = skipBegin; p < skipEnd; p++)
                visibles[p] = false;
            vector<int> ranges;
			for (p = 0; p < pathsSize; ) {
				for (j = p; j < pathsSize && !visibles[j]; j++) {}
				for (p = j; p < pathsSize && visibles[p]; p++) {}
				if (j != p)
					ranges.emplace_back(j), ranges.emplace_back(p);
			}
            call = nullptr;
            if (meshState.showInterior) {
                // Calls address the stream reversed, front to back, so a merged run keeps its start and only grows.
                int total = (int)mesh._triangleInstances.count();
                for (p = (int)pathsSize - 1; p >= 0; p--) {
                    int first = mesh._idxs[p];
                    count = mesh._idxs[p + 1] - first;
                    if (!visibles[p] || !opaques[p] || count == 0)
                        continue;
                    base = total - first - count;
                    if (call == nullptr || call->baseInstance + call->instanceCount != base) {
                        calls.emplace_back(kCallTypeMeshInteriorIndexed, 0, base, state, meshState);
                        call = & calls.back();
                        call->mesh = meshIndex;
                        call->meshState.baseTriangle = base, call->meshState.triangleInstances = total;
                    }
                    call->instanceCount += count;
                }
            }
            if (!meshState.showEdgeTriangles) {
                for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._idxs[ranges[i]], end = mesh._idxs[ranges[i + 1]], count = end - base;
                    calls.emplace_back(kCallTypeMeshTriangles, count, base, state, meshState);
                    calls.back().mesh = meshIndex, calls.back().firstPath = pathBase + ranges[i];
                }
            }
        }
    }
    
    struct Timeline {
        void empty()                    { _basePaths.resize(0); }
        vector<MeshPath>& basePaths()   { return _basePaths; }
        
		void writePathsForTime(double time, MeshPath *paths) {
            if (time == 0) {
                memcpy(paths, _basePaths.data(), _basePaths.size() * sizeof(MeshPath));
                return;
            }
            time *= 0.2;
            for (int i = 0; i < _basePaths.size(); i++) {
                double t = time - std::floor(time);
                t = 1 - std::abs(1 - t * 2);
                t = (3 - 2 * t) * t * t;
                AffineTransform& ctm = paths[i].ctm;
                const MeshPath& p = _basePaths[i];
                ctm.a = p.ctm.a, ctm.b = p.ctm.b, ctm.c = p.ctm.c, ctm.d = p.ctm.d;
                ctm.tx = p.ctm.tx + (p.bounds.ux - p.bounds.lx) * t;
                ctm.ty = p.ctm.ty + (p.bounds.uy - p.bounds.ly) * -t;
            }
        }
    private:
        vector<MeshPath>                _basePaths;
    };
    
    vector<Call>                        _calls;
    
    Timeline                            _timeline;
    
    vector<int>                         _pathIdxs;          // each scene path's first triangle in _tris, then their end
    Memory<Triangle, true>              _tris;              // each path's triangles, once however many instances draw it
    vector<int>                         _idxs;              // each instance's first triangle instance, then their end
    Memory<TriangleInstance, true>      _triangleInstances; // every instance's triangles, in instance order
    vector<float>                       _interiorDensities; // per path, its edges per quantised unit of interior area
    vector<int>                         _instancePaths;     // each instance's path
	
    // The stream, replacing any before, for count instances numbered from base: instance base + k draws the triangles
    // of the mesh's path paths[k], which indexes _pathIdxs. It is allocated once, at its size.
    void writeTriangleInstances(const int *paths, int count, int base) {
        _idxs.resize(0), _triangleInstances = Memory<TriangleInstance, true>();
        size_t total = 0;
        for (int k = 0; k < count; k++)
            total += _pathIdxs[paths[k] + 1] - _pathIdxs[paths[k]];
        TriangleInstance *instance = total ? _triangleInstances.new_ptr(total) : nullptr;
        for (int k = 0, end = 0; k < count; k++) {
            int first = _pathIdxs[paths[k]], n = _pathIdxs[paths[k] + 1] - first;
            _idxs.emplace_back(end);
            for (int i = 0; i < n; i++)
                *instance++ = { (unsigned int)(base + k), (unsigned int)(first + i) };
            end += n;
        }
        _idxs.emplace_back((int)total);
        _instancePaths.assign(paths, paths + count);
    }
    
    void empty() {
        _timeline.empty(), _pathIdxs.resize(0), _idxs.resize(0), _tris = Memory<Triangle, true>(), _triangleInstances = Memory<TriangleInstance, true>();
        _interiorDensities.resize(0), _instancePaths.resize(0);
    }
};
