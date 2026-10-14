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
    static void writePathCalls(Mesh& mesh, MeshPath *paths, size_t pathsSize, Bounds clipBounds, State state, MeshState meshState) {
        int base, end, count, p, i, j;
        vector<Call> & calls = mesh._calls;
        Call *call;
        if (mesh._tris.size()) {
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
			// Paths smaller than this on screen are left to the edge pass, except in the debug views, which show every
			// interior triangle at any scale.
			const float limit = meshState.showTriangles || meshState.showPolygons ? 0 : 64 * std::sqrt(std::abs(inv.a * inv.d - inv.b * inv.c));

            const MeshPath *path = & paths[0];
            for (p = 0; p < pathsSize; p++, path++) {
                w = path->bounds.ux - path->bounds.lx, h = path->bounds.uy - path->bounds.ly;
                mx = (path->bounds.lx + path->bounds.ux) * 0.5, my = (path->bounds.ly + path->bounds.uy) * 0.5;
                a = path->ctm.a * w, b = path->ctm.b * w, c = path->ctm.c * h, d = path->ctm.d * h;
				d0 = (a + c) * (a + c) + (b + d) * (b + d);
				d1 = (a - c) * (a - c) + (b - d) * (b - d);
                rp = 0.5 * std::sqrt(d0 > d1 ? d0 : d1);
				*opaque++ = rp > limit && path->color.a == 255;
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
            vector<int> ranges;
			for (p = 0; p < pathsSize; ) {
				for (j = p; j < pathsSize && !visibles[j]; j++) {}
				for (p = j; p < pathsSize && visibles[p]; p++) {}
				if (j != p)
					ranges.emplace_back(j), ranges.emplace_back(p);
			}
            call = nullptr;
            if (meshState.showInterior) {
                // Calls address the reversed index buffer, so a merged run keeps its start and only grows.
                int total = (int)mesh._tris.count(), runBase = -1;
                for (end = total, p = (int)pathsSize - 1; p >= 0; p--, end = base) {
                    base = mesh._idxs[p];
                    if (visibles[p] && opaques[p]) {
                        count = end - base;
                        if (call == nullptr || runBase != end) {
                            calls.emplace_back(kCallTypeMeshInteriorIndexed, 0, total - end, state, meshState);
                            call = & calls.back();
                            call->meshState.baseTriangle = total - end;
                        }
                        runBase = base;
                        call->instanceCount += count;
                    }
                }
            }
            if (!meshState.showEdgeTriangles) {
                for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._idxs[ranges[i]], end = mesh._idxs[ranges[i + 1]], count = end - base;
                    calls.emplace_back(kCallTypeMeshTriangles, count, base, state, meshState);
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
    
    vector<int>                         _idxs;
    Memory<Triangle, true>              _tris;
    vector<float>                       _interiorMiters;    // per triangle, the interior pass's miters: x, y per vertex
    Memory<MeshVertex, true>            _interiorVerts;
    Memory<unsigned int, true>          _interiorIdxs;
    Memory<unsigned char, true>         _interiorEdges;     // debug: per index buffer triangle, bit k set if edge k is a polygon edge
	
    void empty() {
        _timeline.empty(), _idxs.resize(0), _tris = Memory<Triangle, true>();
        _interiorVerts = Memory<MeshVertex, true>(), _interiorIdxs = Memory<unsigned int, true>(), _interiorEdges = Memory<unsigned char, true>();
        _interiorMiters.resize(0);
    }
};
