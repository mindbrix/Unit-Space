//
//  Algorithms.hpp
//  Unit Space
//
//  Created by Nigel Barber on 05/02/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "Shared.metal.hpp"

void WriteQuadratics(Quadratics& quadratics, AffineTransform &m, Vector *begin, int count) {
    AffineTransform *ctm = & quadratics.ctms[0];
    quadratics.count = 0;
    
    float x0, y0, x1, y1, x2, y2;
    float *q = (float *)begin, *end = (float *)(begin + count);
    AffineTransform unit;
    for (x0 = *q++, y0 = *q++; q < end; x0 = x2, y0 = y2) {
        x1 = *q++, y1 = *q++, x2 = *q++, y2 = *q++;
        
        unit = { x1 - x0, y1 - y0, x2 - x1, y2 - y1, x0, y0 };
        *ctm++ = AffineTransformConcat(unit, m);
        quadratics.count++;
    }
}
