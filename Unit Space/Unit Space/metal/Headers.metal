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

float barycentricDistance(float u, float v, bool ef0, bool ef1, bool ef2);
float simpleBarycentricDistance(float u, float v);
float adjustedT(float t);
float edgeWinding(float x0, float y0, float x1, float y1);
float quadraticWinding(float x0, float y0, float x1, float y1, float x2, float y2);
float windingToAlpha(float winding, bool evenOdd);
float sRGB(float x);
float ellipseDistance(float2 unitPoint, float dilation, float width);
float rectDistance(float x, float y, float dilation, float f);
float roundedRectDistance(float2 unitPoint, float radius, float dilation, float width);
float lozengeDistance(float2 unitPoint, float dilation);
float quadraticDistance(float2 unitPoint, float dilation);
float triangleDistance(float2 unitPoint, float dilation, float width);
float ellipseStrokeDistance(float2 unitPoint, float w);
float det(float2 a, float2 b);
float implicitDistance(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2);
float4 quadraticColor(float2 devicePoint, AffineTransform op);
float unitQuadraticDistanceChainRule(float2 unitPoint, bool line);
float unitQuadraticWinding(float2 unitPoint);
float2 unitComplexMultiply(float2 unit);
float2 unitInverse(float2 unit);
float2 unitRepeat(float2 unit, bool rx, bool ry);
bool isLine(AffineTransform ctm);
AffineTransform lineCTM(AffineTransform ctm);
float distanceBoolean(float d0, float d1, int op /* AND = 0, OR = 1, XOR = 2 */);
