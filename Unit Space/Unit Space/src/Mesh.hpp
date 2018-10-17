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
			const float limit = 64 * std::sqrt(std::abs(inv.a * inv.d - inv.b * inv.c));
			
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
				for (j = p; !visibles[j] && j < pathsSize; j++) {}
				for (p = j; visibles[p] && p < pathsSize; p++) {}
				if (j != p)
					ranges.emplace_back(j), ranges.emplace_back(p);
			}
            if (meshState.showQuadratics) {
                for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._quadraticIndices[ranges[i]];
                    end = ranges[i + 1] == pathsSize ? (int)mesh._quadraticFloats.count(): mesh._quadraticIndices[ranges[i + 1]];
                    
                    calls.emplace_back(kCallTypeQuadraticLines, ((end - base) / meshState.quadraticStride - 1) * meshState.subdivisionCount, base / meshState.quadraticStride * meshState.subdivisionCount, state, meshState);
                }
            } else if (meshState.showAccumulations) {
                MeshState resolveMeshState = meshState;
                resolveMeshState.cellSize = 1;
                MeshState clipState = resolveMeshState;
                clipState.clipBounds = { 0, 0, std::ceil(clipBounds.ux / resolveMeshState.cellSize), std::ceil(clipBounds.uy / resolveMeshState.cellSize) };
                
                State windingState = state;
                AffineTransform cell = { 1.0f / resolveMeshState.cellSize, 0, 0, 1.0f / resolveMeshState.cellSize, 0, 0 };
                windingState.CTM = AffineTransformConcat(state.CTM, cell);
                for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._quadraticIndices[ranges[i]];
                    end = ranges[i + 1] == pathsSize ? (int)mesh._quadraticFloats.count() : mesh._quadraticIndices[ranges[i + 1]];
                    calls.emplace_back(kCallTypeQuadraticAccumulate, (end - base) / meshState.quadraticStride - 1, base / meshState.quadraticStride, windingState, clipState);
                }
                calls.emplace_back(kCallTypeResolveQuadSOA, 1, 0, state, resolveMeshState);
                
            } else {
                call = nullptr;
                if (meshState.showInterior) {
                    for (end = (int)mesh._tris.count(), p = (int)pathsSize - 1; p >= 0; p--, end = base) {
                        base = mesh._idxs[p];
                        if (visibles[p] && opaques[p]) {
                            count = end - base;
                            if (call == nullptr || call->baseInstance != end) {
                                calls.emplace_back(kCallTypeMeshInterior, 0, base, state, meshState);
                                call = & calls.back();
                                call->meshState.reverseCount = end;
                            }
                            call->baseInstance = base;
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
            
            if (meshState.showVisibles) {
                calls.emplace_back(kCallTypeFlags, (int)pathsSize, 0, state, meshState);
				mesh._flags.w = mesh._flags.h = 64;
                for (p = 0; p < pathsSize; p++) {
                    i = p >> 5, j = p % 32;
                    if (i == kFlagsSize)
                        break;
                    if (j == 0)
                        mesh._flags.flags[i] = 0;
                    if (visibles[p])
                        mesh._flags.flags[i] |= (1 << j);
                }
            }
        }
    }
    
    static void writeSOACalls(Mesh& mesh, State state, MeshState meshState, CallType type, size_t count) {
        int i, end;
        for (i = 0; i < count; i += 1024) {
            end = i + 1024 > count ? (int)count : i + 1024;
            mesh._calls.emplace_back(type, end - i, i, state, meshState);
        }
    }
    
    struct ShapesSOA {
        static inline AffineTransform circleUnit(float x, float y, float r) {
            return { std::abs(r) * 2.f, 0, 0, std::abs(r) * 2.f, x - std::abs(r), y - std::abs(r) };
        }
        void addCircle(float x, float y, float r, RGBA8 c) {
            AffineTransform unit = circleUnit(x, y, r);
            _types.push_back(kShapeTypeEllipse);
            _as.push_back(unit.a), _bs.push_back(unit.b), _cs.push_back(unit.c), _ds.push_back(unit.d), _txs.push_back(unit.tx), _tys.push_back(unit.ty);
            _colors.push_back(c);
        }
        void empty() {
            _count = 0;
            _types = Memory<short>();
            _as = Memory<float>(), _bs = Memory<float>(), _cs = Memory<float>(), _ds = Memory<float>(), _txs = Memory<float>(), _tys = Memory<float>(),
            _colors = Memory<RGBA8>();
        }
        int                             _count;
        Memory<short>                   _types;
        Memory<float>                   _as, _bs, _cs, _ds, _txs, _tys;
        Memory<RGBA8>                   _colors;
    };
    
    struct Timeline {
		inline void writeCTMForTime(AffineTransform& ctm, float t, const MeshPath& p) {
			ctm.a = p.ctm.a, ctm.b = p.ctm.b, ctm.c = p.ctm.c, ctm.d = p.ctm.d;
            ctm.tx = p.ctm.tx + (p.bounds.ux - p.bounds.lx) * t;
            ctm.ty = p.ctm.ty + (p.bounds.uy - p.bounds.ly) * -t;
		}
        void empty() {
            _paths.resize(0);
        }
        vector<MeshPath>& basePaths() {
            return _paths;
        }
        void writePathsForTime(double time, MeshPath *paths) {
            if (time == 0) {
                for (int p = 0; p < _paths.size(); p++)
                    paths[p] = _paths[p];
                return;
            }
            time *= 0.2;
            for (int p = 0; p < _paths.size(); p++) {
                double t = time - std::floor(time);
                t = 1 - std::abs(1 - t * 2);
                t = (3 - 2 * t) * t * t;
                writeCTMForTime(paths[p].ctm, t, _paths[p]);
            }
        }
    private:
        vector<MeshPath>                _paths;
    };
    
    vector<Call>                        _calls;
    
    Bounds                              _bounds;
    
    Memory<float, true>                 _quadraticFloats;
    Memory<int, true>                   _quadraticPs;
    Memory<float, true>                 _quadraticTs;
    Memory<int>                         _quadraticIndices;
    
    Timeline                            _timeline;
    
    vector<int>                         _idxs;
    Memory<Triangle, true>              _tris;
    
    ShapesSOA                           _shapesSOA;
	Flags								_flags;
	
    void empty() {
        _bounds = Bounds(0, 0, 0, 0);
        _quadraticFloats = Memory<float, true>(), _quadraticPs = Memory<int, true>(), _quadraticTs = Memory<float, true>(), _quadraticIndices = Memory<int>();
        _timeline.empty(), _idxs.resize(0), _tris = Memory<Triangle, true>();
    }
};
