//
//  Compute.metal
//  Nu
//
//  Created by Nigel Barber on 21/08/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include <metal_stdlib>
using namespace metal;

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
