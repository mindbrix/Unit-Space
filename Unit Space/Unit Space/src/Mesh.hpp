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
    static void writePathCalls(Mesh& mesh, Bounds clipBounds, State state, MeshState meshState) {
        int base, end, count, p, i, j, size;
        vector<Call> & calls = mesh._calls;
        AffineTransform ctm;
        Call *call;
        if (mesh._triangles.size()) {
            const vector<MeshPath> & paths = mesh._timeline.pathsForTime(1);
            bool opaques[paths.size()], visibles[paths.size()], smalls[paths.size()];
            AffineTransform units[paths.size()];
            for (p = 0; p < paths.size(); p++) {
                units[p] = ctm = AffineTransformMakeUnit(paths[p].bounds, AffineTransformConcat(paths[p].ctm, state.CTM));
                opaques[p] = paths[p].color.a == 255;
                smalls[p] = ctm.a * ctm.a + ctm.b * ctm.b < 400 || ctm.c * ctm.c + ctm.d * ctm.d < 400;
            }
            for (p = 0; p < paths.size(); p += 8) {
                bool set = false;
                for (j = 0; !set && j < 8 && p + j < paths.size(); j++)
                    set |= Bounds::transformUnit(units[p + j]).intersects(clipBounds);
                for (j = 0; j < 8 && p + j < paths.size(); j++)
                    visibles[p + j] = set;
            }
            vector<int> ranges;
            for (p = 0; p < paths.size(); p += 8) {
                int end = p + 8 < paths.size() ? p + 8 : (int)paths.size();
                if (visibles[p]) {
                    if (ranges.size() && ranges.back() == p)
                        ranges.back() = end;
                    else
                        ranges.emplace_back(p), ranges.emplace_back(end);
                }
            }
            if (meshState.showQuadratics) {
                for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._quadraticIndices[ranges[i]];
                    end = ranges[i + 1] == paths.size() ? (int)mesh._quadraticFloats.count(): mesh._quadraticIndices[ranges[i + 1]];
                    
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
                    end = ranges[i + 1] == paths.size() ? (int)mesh._quadraticFloats.count() : mesh._quadraticIndices[ranges[i + 1]];
                    calls.emplace_back(kCallTypeQuadraticAccumulate, (end - base) / meshState.quadraticStride - 1, base / meshState.quadraticStride, windingState, clipState);
                }
                calls.emplace_back(kCallTypeResolveQuadSOA, 1, 0, state, resolveMeshState);
                
            } else if (meshState.showAccumulationsSOA) {
                mesh._accumulationsSOA.empty();
                mesh._resolutionsSOA.empty();
                
                short *lxs, *uxs, *lys, *txs, *tys, sy0, sy1, w, ax, ay;
                int *ps, *idxs, base, rows;
                rows = (int)(clipBounds.uy / kFatScanlineHeight);
                short allocations[rows], *allocation = allocations, *aend = allocation + rows;
                memset(allocations, 0, sizeof(allocations));
                for (i = 0; i < ranges.size(); i += 2) {
                    count = ranges[i + 1] - ranges[i];
                    Bounds bounds[count], tb, ib;
                    bool smalls[count];
                    for (j = 0; j < count; j++) {
                        p = j + ranges[i];
                        AffineTransform& u = units[p];
                        float scale = 63.0 / std::sqrt(std::abs(AffineTransformArea(u)));
                        if (scale < 1) {
                            u.a *= scale, u.b *= scale, u.c *= scale, u.d *= scale;
                        }
                        tb = Bounds::transformUnit(u);
                        ib = { std::floor(tb.lx), std::floor(tb.ly), std::ceil(tb.ux), std::ceil(tb.uy) };
                        bounds[j] = ib.intersected(clipBounds);
                        smalls[j] = bounds[j].lx != bounds[j].ux && bounds[j].ly != bounds[j].uy && std::abs(AffineTransformArea(u)) < 4096;
                    }
                    size = 0;
                    for (j = 0; j < count; j++)
                        if (smalls[j])
                            size += std::ceil((bounds[j].uy - bounds[j].ly) / kFatScanlineHeight);
                    
                    base = mesh._resolutionsSOA._count;
                    mesh._resolutionsSOA.alloc(size);
                    lxs = & mesh._resolutionsSOA._lxs[base], uxs = & mesh._resolutionsSOA._uxs[base], lys = & mesh._resolutionsSOA._lys[base];
                    txs = & mesh._resolutionsSOA._txs[base], tys = & mesh._resolutionsSOA._tys[base];
                    ps = & mesh._resolutionsSOA._ps[base], idxs = & mesh._resolutionsSOA._idxs[base];
                    
                    float m0, abx0, abx1, m1, cdx0, cdx1, dx0, dx1, lx0, ux0, lx1, ux1, ilx, iux, wa, wc, lxy, uxy;
                    for (j = 0; j < count; j++) {
                        if (!smalls[j])
                            continue;
                        p = j + ranges[i];
                        AffineTransform& u = units[p];
                        Bounds& b = bounds[j];
                        abx0 = abx1 = cdx0 = cdx1 = dx0 = dx1 = lxy = uxy = FLT_MAX;
                        if (!(u.a == 0 || u.c == 0) && b.uy - b.ly > kFatScanlineHeight) {
                            m0 = -u.b / u.a, abx0 = (b.ly - u.ty) * m0 + u.tx, abx1 = abx0 + u.a - u.b * m0;
                            if (abx0 > abx1)
                                std::swap(abx0, abx1);
                            m1 = -u.d / u.c, cdx0 = (b.ly - u.ty) * m1 + u.tx, cdx1 = cdx0 + u.c - u.d * m1;
                            if (cdx0 > cdx1)
                                std::swap(cdx0, cdx1);
                            dx0 = m0 * kFatScanlineHeight, dx1 = m1 * kFatScanlineHeight;
                            wa = u.a < 0 ? 1 : 0, wc = u.c < 0 ? 1 : 0;
                            lxy = u.ty + wa * u.b + wc * u.d, uxy = u.ty + (1 - wa) * u.b + (1 - wc) * u.d;
                        }
                        for (lx0 = FLT_MAX, ux0 = -FLT_MAX, sy0 = (short)b.ly, sy1 = sy0 + kFatScanlineHeight; sy0 < b.uy; sy0 = sy1, sy1 += kFatScanlineHeight) {
                            ilx = b.lx, iux = b.ux;
                            if (abx0 != FLT_MAX) {
                                if (sy1 < b.uy)
                                    abx0 += dx0, abx1 += dx0, cdx0 += dx1, cdx1 += dx1;
                                
                                lx1 = abx0 < cdx0 ? cdx0 : abx0 > cdx1 ? cdx1 : abx0;
                                ux1 = abx1 < cdx0 ? cdx0 : abx1 > cdx1 ? cdx1 : abx1;
                                
                                ilx = lxy >= sy0 && lxy < sy1 ? b.lx : std::floor(lx0 < lx1 ? lx0 : lx1);
                                iux = uxy >= sy0 && uxy < sy1 ? b.ux : std::ceil(ux0 > ux1 ? ux0 : ux1);
                                
                                lx0 = lx1, ux0 = ux1;
                            }
                            w = (short)(iux - ilx);
                            while (allocation < aend && *allocation + w > clipBounds.ux)
                                allocation++;
                            if (allocation < aend) {
                                *lxs++ = (short)ilx, *uxs++ = (short)iux, *lys++ = sy0;
                                ax = *allocation, ay = (short)(allocation - allocations) * kFatScanlineHeight;
                                *allocation += w;
                                *txs++ = ax - (short)ilx, *tys++ = ay - sy0;
                            } else
                                *lxs++ = *uxs++ = *lys++, *txs++ = *tys++ = 0;
                            *ps++ = p, *idxs++ = 0;
                        }
                    }
                }
                writeSOACalls(mesh, state, meshState, kCallTypeResolveSOA, mesh._resolutionsSOA._count);
                
            } else {
                call = nullptr;
                if (meshState.showInterior) {
                    for (end = (int)mesh._triangles.count() / kMeshTriangleSize, p = (int)paths.size() - 1; p >= 0; p--, end = base) {
                        base = mesh._idxs[p];
                        if (visibles[p] && (opaques[p] || smalls[p])) {
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
                if (!meshState.showEdges) {
                    for (i = 0; i < ranges.size(); i += 2)
                        for (call = nullptr, p = ranges[i]; p < ranges[i + 1]; p++) {
                            base = mesh._idxs[p];
                            end = p + 1 == paths.size() ? (int)mesh._triangles.count() / kMeshTriangleSize : mesh._idxs[p + 1];
                            count = end - base;
                            if (opaques[p] || smalls[p]) {
                                if (call == nullptr) {
                                    calls.emplace_back(kCallTypeMeshTriangles, 0, base, state, meshState);
                                    call = & calls.back();
                                }
                                call->instanceCount += count;
                            } else {
                                calls.emplace_back(kCallTypeMeshInteriorBlend, count, base, state, meshState);
                                calls.back().meshState.reverseCount = end;
                                calls.emplace_back(kCallTypeMeshTriangles, count, base, state, meshState);
                                call = nullptr;
                            }
                        }
                }
            }
            
            if (meshState.showVisibles) {
                calls.emplace_back(kCallTypeFlags, (int)paths.size(), 0, state, meshState);
                call = & calls.back();
                call->flags.w = call->flags.h = 64;
                for (p = 0; p < paths.size(); p++) {
                    i = p >> 5, j = p % 32;
                    if (j == 0)
                        call->flags.flags[i] = 0;
                    if (visibles[p])
                        call->flags.flags[i] |= (1 << j);
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
    
    struct AccumulationSOA {
        void alloc(int allocation) {
            _count += allocation;
            _lxs.new_ptr(allocation), _uxs.new_ptr(allocation), _lys.new_ptr(allocation), _txs.new_ptr(allocation), _tys.new_ptr(allocation), _ps.new_ptr(allocation), _idxs.new_ptr(allocation);
        }
        void empty() {
            _count = 0,
            _lxs = Memory<short>(1024), _uxs = Memory<short>(1024), _lys = Memory<short>(1024), _txs = Memory<short>(1024), _tys = Memory<short>(1024);
            _ps = Memory<int>(1024), _idxs = Memory<int>(1024);
        }
        int                             _count;
        Memory<short>                   _lxs, _uxs, _lys, _txs, _tys;
        Memory<int>                     _ps, _idxs;
    };

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
		inline void writeCTMForTime(AffineTransform& ctm, float time, MeshPath& p) {
			ctm = p.ctm;
			ctm.tx += (p.bounds.ux - p.bounds.lx) * time;
		}
        void empty() {
            _paths.resize(0), _tpaths.resize(0);
        }
        vector<MeshPath>& pathsForTime(double time) {
            if (time == 0 || _tpaths.size() == 0)
                return _paths;
            return _tpaths;
        }
        vector<MeshPath>& writePathsForTime(double time) {
            if (_tpaths.size() == 0)
                for (MeshPath& path : _paths)
                    _tpaths.emplace_back(path);
            time *= 0.2;
            for (int p = 0; p < _paths.size(); p++) {
                double t = time - std::floor(time);
                t = 1 - std::abs(1 - t * 2);
                t = 3 * t * t - 2 * t * t *t;
				writeCTMForTime(_tpaths[p].ctm, t, _paths[p]);
            }
            return _tpaths;
        }
    private:
        vector<MeshPath>                _paths, _tpaths;
    };
    vector<Call>                        _calls;
    
    Memory<float, true>                 _quadraticFloats;
    Memory<int, true>                   _quadraticPs;
    Memory<float, true>                 _quadraticTs;
    Memory<int>                         _quadraticIndices;
    
    AccumulationSOA                     _accumulationsSOA;
    AccumulationSOA                     _resolutionsSOA;
    
    Timeline                            _timeline;
    
    vector<int>                         _idxs;
    Memory<Vector, true>                _triangles;
    Memory<Triangle, true>              _tris;
    
    ShapesSOA                           _shapesSOA;
    
    void empty() {
        _quadraticFloats = Memory<float, true>(), _quadraticPs = Memory<int, true>(), _quadraticTs = Memory<float, true>(), _quadraticIndices = Memory<int>();
        _timeline.empty(), _idxs.resize(0), _triangles = Memory<Vector, true>(), _tris = Memory<Triangle, true>();
    }
};
