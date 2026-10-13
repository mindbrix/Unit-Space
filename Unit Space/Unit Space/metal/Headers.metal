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

float simpleBarycentricDistance(float u, float v, bool ef0, bool ef1, bool ef2);
