//
//  Headers.metal
//  Unit Space
//
//  Created by Nigel Barber on 22/02/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include <metal_stdlib>
#import "Shared.metal.hpp"

using namespace metal;

float cirleStrokeDistance(float2 unitPoint, float w);
float ellipseDistance(float2 unitPoint);
float ellipseStrokeDistance(float2 unitPoint, float w);
float4 quadraticColor(float2 devicePoint, AffineTransform op);
float quadraticDistance(float x0, float y0, float x1, float y1, float x2, float y2);
float quadraticWinding(float2 devicePoint, AffineTransform op);
float rectDistance(float2 unitPoint);
float unitDistance(float2 unitPoint);
float unitQuadraticWinding(float2 unitPoint);
