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
        if (mesh._points.size()) {
            bool opaques[mesh._paths.size()], visibles[mesh._paths.size()], smalls[mesh._paths.size()];
            AffineTransform units[mesh._paths.size()];
            for (p = 0; p < mesh._paths.size(); p++) {
                units[p] = ctm = AffineTransformMakeUnit(mesh._paths[p].bounds, AffineTransformConcat(mesh._paths[p].ctm, state.CTM));
                opaques[p] = mesh._paths[p].color.a == 255;
                smalls[p] = ctm.a * ctm.a + ctm.b * ctm.b < 400 || ctm.c * ctm.c + ctm.d * ctm.d < 400;
            }
            for (p = 0; p < mesh._paths.size(); p += 8) {
                bool set = false;
                for (j = 0; !set && j < 8 && p + j < mesh._paths.size(); j++)
                    set |= Bounds::transformUnit(units[p + j]).intersects(clipBounds);
                for (j = 0; j < 8 && p + j < mesh._paths.size(); j++)
                    visibles[p + j] = set;
            }
            vector<int> ranges;
            for (p = 0; p < mesh._paths.size(); p += 8) {
                int end = p + 8 < mesh._paths.size() ? p + 8 : (int)mesh._paths.size();
                if (visibles[p]) {
                    if (ranges.size() && ranges.back() == p)
                        ranges.back() = end;
                    else
                        ranges.emplace_back(p), ranges.emplace_back(end);
                }
            }
            if (meshState.showAccumulationsSOA) {
                mesh._accumulationsSOA.empty();
                mesh._resolutionsSOA.empty();
                
                short *lxs, *uxs, *lys, *txs, *tys, y, w, ax, ay;
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
                        Bounds& b =  bounds[j];
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
                        for (lx0 = FLT_MAX, ux0 = -FLT_MAX, y = (short)b.ly; y < b.uy; y += kFatScanlineHeight) {
                            ilx = b.lx, iux = b.ux;
                            if (abx0 != FLT_MAX) {
                                if (y + kFatScanlineHeight < b.uy)
                                    abx0 += dx0, abx1 += dx0, cdx0 += dx1, cdx1 += dx1;
                                
                                lx1 = abx0 < cdx0 ? cdx0 : abx0 > cdx1 ? cdx1 : abx0;
                                ux1 = abx1 < cdx0 ? cdx0 : abx1 > cdx1 ? cdx1 : abx1;
                                
                                ilx = std::floor(lx0 < lx1 ? lx0 : lx1), iux = std::ceil(ux0 > ux1 ? ux0 : ux1);
                                if (lxy >= y && lxy < y + kFatScanlineHeight)
                                    ilx = b.lx;
                                if (uxy >= y && uxy < y + kFatScanlineHeight)
                                    iux = b.ux;
                                lx0 = lx1, ux0 = ux1;
                            }
                            w = (short)(iux - ilx);
                            while (allocation < aend && *allocation + w > clipBounds.ux)
                                allocation++;
                            if (allocation < aend) {
                                *lxs++ = (short)ilx, *uxs++ = (short)iux, *lys++ = y;
                                ax = *allocation, ay = (short)(allocation - allocations) * kFatScanlineHeight;
                                *allocation += w;
                                *txs++ = ax - (short)ilx, *tys++ = ay - y;
                            } else
                                *lxs++ = *uxs++ = *lys++, *txs++ = *tys++ = 0;
                            *ps++ = p, *idxs++ = 0;
                        }
                    }
                }
                writeSOACalls(mesh, state, meshState, kCallTypeResolveSOA, mesh._resolutionsSOA._count);
                
            } else if (meshState.showAccumulations) {
                MeshState resolveMeshState = meshState;
                resolveMeshState.cellSize = 1;
                MeshState clipState = resolveMeshState;
                clipState.clipBounds = { 0, 0, std::ceil(clipBounds.ux / resolveMeshState.cellSize), std::ceil(clipBounds.uy / resolveMeshState.cellSize) };
                
				State windingState = state;
                AffineTransform cell = { 1.0f / resolveMeshState.cellSize, 0, 0, 1.0f / resolveMeshState.cellSize, 0, 0 };
                windingState.CTM = AffineTransformConcat(state.CTM, cell);
				for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._accumulationIndices[ranges[i]];
                    end = ranges[i + 1] == mesh._paths.size() ? (int)mesh._accumulations.size(): mesh._accumulationIndices[ranges[i + 1]];
                    calls.emplace_back(kCallTypeAccumulate, end - base, base, windingState, clipState);
                }
                
                calls.emplace_back(kCallTypeResolveQuadSOA, 1, 0, state, resolveMeshState);
                
            } else if (meshState.showPolygons) {
                for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._edges[ranges[i]];
                    end = ranges[i + 1] == mesh._paths.size() ? (int)mesh._xs.size(): mesh._edges[ranges[i + 1]];
                    for (j = base; j < end; j += 1023) {
                        count = (j + 1024 < end ? j + 1024 : end) - j;
                        calls.emplace_back(meshState.showLineWinding ? kCallTypeLinesWindingSOA : kCallTypeLinesSOA, count - 1, j, state, meshState);
                    }
                }
                if (meshState.showLineWinding)
                    calls.emplace_back(kCallTypeResolveQuadSOA, 1, 0, state, meshState);
            } else {
                call = nullptr;
                for (end = (int)mesh._points.size() / kMeshTriangleSize, p = (int)mesh._paths.size() - 1; p >= 0; p--, end = base) {
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
                
                if (!meshState.showEdges) {
                    for (i = 0; i < ranges.size(); i += 2)
                        for (call = nullptr, p = ranges[i]; p < ranges[i + 1]; p++) {
                            base = mesh._idxs[p];
                            end = p + 1 == mesh._paths.size() ? (int)mesh._points.size() / kMeshTriangleSize : mesh._idxs[p + 1];
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
                calls.emplace_back(kCallTypeFlags, (int)mesh._paths.size(), 0, state, meshState);
                call = & calls.back();
                call->flags.w = call->flags.h = 64;
                for (p = 0; p < mesh._paths.size(); p++) {
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
        int                                     _count;
        Memory<short>                           _lxs, _uxs, _lys, _txs, _tys;
        Memory<int>                             _ps, _idxs;
        
        void alloc(int allocation) {
            _count += allocation;
            if (_count > _lxs.count()) {
                size_t size = 1 << int(std::ceil(log2f(_count)));
                _lxs.alloc(size), _uxs.alloc(size), _lys.alloc(size), _txs.alloc(size), _tys.alloc(size), _ps.alloc(size), _idxs.alloc(size);
            }
        }
        void empty() {
            _count = 0,
            _lxs = Memory<short>(1024), _uxs = Memory<short>(1024), _lys = Memory<short>(1024), _txs = Memory<short>(1024), _tys = Memory<short>(1024),
            _ps = Memory<int>(1024), _idxs = Memory<int>(1024);
        }
    };

    struct CirclesSOA {
        void add(float x, float y, float r, RGBA8 c)    { _xs.push_back(x), _ys.push_back(y), _rs.push_back(std::abs(r)), _colors.push_back(c); }
        void empty()                                    { _xs = Memory<float>(), _ys = Memory<float>(), _rs = Memory<float>(), _colors = Memory<RGBA8>(); }
        
        Memory<float>                           _xs, _ys, _rs;
        Memory<RGBA8>                           _colors;
    };
    
    vector<Accumulation>                _accumulations;
    vector<int>                         _accumulationIndices;
    
    vector<MeshPath>                    _paths;
    vector<int>                         _idxs, _edges;
    vector<Vector>                      _points;
    vector<float>                       _xs, _ys, _as, _bs;
    vector<int>                         _ps;
    
    vector<Call>                        _calls;
    CirclesSOA                          _circles;
    
    AccumulationSOA                     _accumulationsSOA;
    AccumulationSOA                     _resolutionsSOA;
    
    void empty() { _accumulations.resize(0), _accumulationIndices.resize(0), _paths.resize(0), _idxs.resize(0), _edges.resize(0), _points.resize(0), _xs.resize(0), _ys.resize(0), _as.resize(0), _bs.resize(0), _ps.resize(0); }
};
