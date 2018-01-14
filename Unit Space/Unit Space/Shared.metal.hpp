//
//  Shared.metal
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <simd/simd.h>

struct RGBA { float r, g, b, a; };

struct AffineTransform    { float a, b, c, d, tx, ty; };

struct Uniforms
{
    RGBA                Color;
    AffineTransform     CTM;
};
