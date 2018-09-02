//
//  Shaders.metal
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include "Shared.metal.hpp"
#include <metal_stdlib>
#include "Headers.metal"

using namespace metal;


#pragma mark - Flags

struct FlagsVertex
{
    float4 position [[position]];
};

vertex FlagsVertex flags_vertex_main(constant State &state [[buffer(0)]], constant MeshState& meshState[[buffer(1)]], constant Flags &flags [[buffer(10)]], uint vid [[vertex_id]], uint iid [[instance_id]])
{
    const float kDimension = 4;
    
    unsigned int flag = flags.flags[iid >> 5];
    bool visible = flag & (1 << (iid % 32));
    unsigned int column = iid % flags.w, row = iid / flags.w;
    float lx = kDimension * column, ly = kDimension * row, ux = lx + kDimension, uy = ly + kDimension;
    float x = select(lx, ux, vid & 1) * state.scaleFactor, y = select(ly, uy, vid > 1) * state.scaleFactor;
    float2 devicePoint = { x, y };
    devicePoint = select(0, devicePoint, visible);
    
    FlagsVertex vert;
    vert.position = float4((float2(devicePoint.x / state.drawableSize.w, devicePoint.y / state.drawableSize.h) * 2.0 - 1.0), 1.0, 1.0);
    return vert;
}

fragment float4 flags_fragment_main(FlagsVertex vert [[stage_in]], constant State &state [[buffer(0)]])
{
    float alpha = 1;
    return float4(state.fillColor.r, state.fillColor.g, state.fillColor.b, state.fillColor.a) * alpha;
}
