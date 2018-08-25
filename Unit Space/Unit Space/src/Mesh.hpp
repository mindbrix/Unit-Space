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
        int base, end, count, p, i, j;
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
            if (meshState.showAccumulations) {
				MeshState windingState = meshState;
				//windingState.winding = 0.333;
                for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._accumulationIndices[ranges[i]];
                    end = ranges[i + 1] == mesh._paths.size() ? (int)mesh._accumulations.size(): mesh._accumulationIndices[ranges[i + 1]];
                    calls.emplace_back(kCallTypeAccumulate, end - base, base, state, windingState);
                }
                calls.emplace_back(kCallTypeResolveSOA, 1, 0, state, meshState);
                
            } else if (meshState.showPolygons) {
                for (i = 0; i < ranges.size(); i += 2) {
                    base = mesh._paths[ranges[i]].edge;
                    end = ranges[i + 1] == mesh._paths.size() ? (int)mesh._xs.size(): mesh._paths[ranges[i + 1]].edge;
                    for (j = base; j < end; j += 1023) {
                        count = (j + 1024 < end ? j + 1024 : end) - j;
                        calls.emplace_back(meshState.showLineWinding ? kCallTypeLinesWindingSOA : kCallTypeLinesSOA, count - 1, j, state, meshState);
                    }
                }
                if (meshState.showLineWinding) {
                    State newState = state;
                    newState.fillColor = { 1, 0, 0, 1 };
                    calls.emplace_back(kCallTypeResolveSOA, 1, 0, newState, meshState);
                }
            } else {
                call = nullptr;
                for (end = (int)mesh._points.size() / kMeshTriangleSize, p = (int)mesh._paths.size() - 1; p >= 0; p--, end = base) {
                    base = mesh._paths[p].idx;
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
                            base = mesh._paths[p].idx;
                            end = p + 1 == mesh._paths.size() ? (int)mesh._points.size() / kMeshTriangleSize : mesh._paths[p + 1].idx;
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
    
    struct CirclesSOA {
        void add(float x, float y, float r, RGBA8 c)    { _xs.push_back(x), _ys.push_back(y), _rs.push_back(std::abs(r)), _colors.push_back(c); }
        void empty()                                    { _xs = Memory<float>(), _ys = Memory<float>(), _rs = Memory<float>(), _colors = Memory<RGBA8>(); }
        
        Memory<float>                           _xs, _ys, _rs;
        Memory<RGBA8>                           _colors;
    };
    
    vector<Accumulation>                _accumulations;
    vector<int>                         _accumulationIndices;
    
    vector<MeshPath>                    _paths;
    vector<Vector>                      _points;
    vector<float>                       _xs, _ys, _as, _bs;
    vector<int>                         _ps;
    
    vector<Call>                        _calls;
    CirclesSOA                          _circles;
    
    void empty() { _accumulations.resize(0), _accumulationIndices.resize(0), _paths.resize(0), _points.resize(0), _xs.resize(0), _ys.resize(0), _as.resize(0), _bs.resize(0), _ps.resize(0); }
};
