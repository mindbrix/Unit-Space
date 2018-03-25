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
float implicitDistance(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2);
float4 quadraticColor(float2 devicePoint, AffineTransform op);
float quadraticDistance(float x0, float y0, float x1, float y1, float x2, float y2);
float quadraticWinding(float2 devicePoint, AffineTransform op);
float rectDistance(float2 unitPoint);
float unitQuadraticDistanceChainRule(float2 unitPoint);
float unitQuadraticWinding(float2 unitPoint);
float2 unitComplexMultiply(float2 unit);
float2 unitInverse(float2 unit);
float2 unitRepeat(float2 unit, bool rx, bool ry);
float unitRoundedRectDistance(float2 unitPoint, float radius);
