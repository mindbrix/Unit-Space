//
//  Shaders.metal
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include "Shared.metal"

using namespace metal;

float ellipseDistance(float2 unitPoint) {
    float x = 0.5 - unitPoint.x, y = 0.5 - unitPoint.y;
    
    float fx = -2.0 * (x * dfdx(x) + y * dfdx(y));
    float fy = -2.0 * (x * dfdy(x) + y * dfdy(y));
//    return 1.0 - abs(0.25 - (x * x + y * y)) * rsqrt(max(1e-13, fx * fx + fy * fy));
    float sd = (0.25 - (x * x + y * y)) * rsqrt(max(1e-13, fx * fx + fy * fy));
//    float sd = (0.0809 - abs(0.25 - (x * x + y * y))) * rsqrt(fx * fx + fy * fy);
    
    return sd + 0.5;
}


struct Vertex
{
    float4 position [[position]];
    float2 unitPoint;
    float2 size;
    float2 scale;
    
    float2 edges;
    
    float3 triangleEdges;
    float ellipseScale;
    float2 rectUnitPoint, ellipseUnitPoint, quadraticUnitPoint;
    uint32_t flags;
};

vertex Vertex vertex_main(uint vid [[vertex_id]])
{
//    VGAffineTransform CTM = parameters.CTM;
    float2 unitPoint = float2(vid & 1, vid & 2);
    
    Vertex vert;
    vert.position = float4(unitPoint * 2.0 - 1.0, 0.0, 1.0);
    vert.unitPoint = unitPoint;
//    vert.size = float2(CTM.a, CTM.d);
//    vert.scale = { ceil(CTM.a / kCellHeight) * kCellHeight / CTM.a, ceil(CTM.d / kCellHeight) * kCellHeight / CTM.d };
    vert.flags = 0;
    
    return vert;
}

fragment float4 fragment_main(Vertex vert [[stage_in]])
{
    float alpha = ellipseDistance(vert.unitPoint);
    float4 red = float4(1.0, 0.0, 0.0, 1.0), black = float4(0.0, 0.0, 0.0, 1.0), white = float4(1.0, 1.0, 1.0, 1.0);
    
    return mix(white, black, saturate(alpha));
}
