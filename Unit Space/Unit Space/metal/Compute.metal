//
//  Compute.metal
//  Nu
//
//  Created by Nigel Barber on 21/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include <metal_stdlib>
#include "Shared.metal.hpp"

using namespace metal;

kernel void prefix_sum_kernel(texture2d<float, access::read> inTexture [[texture(0)]],
                              texture2d<float, access::write> outTexture [[texture(1)]],
                              constant State &state [[buffer(0)]],
                              ushort2 gid [[thread_position_in_grid]],
                              ushort2 size [[threads_per_grid]])
{
    if (gid.x > 0)
        return;
    
    float sum = 0.0;
    ushort width = state.drawableSize.w;
    
    for (ushort x = 0; x < width; x++) {
        uint2 pos = uint2(x, gid.y);
        float4 inColor = inTexture.read(pos);
        sum += inColor.x;
       // sum = select(sum, floor(sum), floor(abs(sum)) < 1e-2);
        outTexture.write(sum, pos);
    }
}

// Data type, has to be the same as in the Swift file
typedef float DataType;

kernel void parsum(const device DataType* data [[ buffer(0) ]],
                   const device uint& dataLength [[ buffer(1) ]],
                   device DataType* sums [[ buffer(2) ]],
                   const device uint& elementsPerSum [[ buffer(3) ]],
                   
                   const uint tgPos [[ threadgroup_position_in_grid ]],
                   const uint tPerTg [[ threads_per_threadgroup ]],
                   const uint tPos [[ thread_position_in_threadgroup ]]) {
    uint resultIndex = tgPos * tPerTg + tPos; // This is the index of the individual result, this var is unique to this thread
    uint dataIndex = resultIndex * elementsPerSum; // Where the summation should begin
    uint endIndex = dataIndex + elementsPerSum < dataLength ? dataIndex + elementsPerSum : dataLength; // The index where summation should end
    
    for (; dataIndex < endIndex; dataIndex++)
        sums[resultIndex] += data[dataIndex];
}
