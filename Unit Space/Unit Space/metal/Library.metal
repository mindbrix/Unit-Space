//
//  Library.metal
//  Unit Space
//
//  Created by Nigel Barber on 22/02/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#include <metal_stdlib>
#import "Headers.metal"

using namespace metal;

// The debug wireframe: distance to the triangle's flagged edges, in pixels.
float simpleBarycentricDistance(float u, float v, bool ef0, bool ef1, bool ef2) {
    float w = 1.0 - u - v;
	float eu = float(!ef0) * 1e12 + sign(u) * u * u / max(1e-13, (dfdx(u) * dfdx(u) + dfdy(u) * dfdy(u)));
    float ev = float(!ef1) * 1e12 + sign(v) * v * v / max(1e-13, (dfdx(v) * dfdx(v) + dfdy(v) * dfdy(v)));
    float ew = float(!ef2) * 1e12 + sign(w) * w * w / max(1e-13, (dfdx(w) * dfdx(w) + dfdy(w) * dfdy(w)));
    return 1 - ((1 - saturate(0.5 + eu)) + (1 - saturate(0.5 + ev)) + (1 - saturate(0.5 + ew)));
//    float m = min(eu, min(ev, ew));
//    return sign(m) * sqrt(abs(m));
}
