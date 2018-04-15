//
//  MetalLayer.h
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <QuartzCore/QuartzCore.h>
#import "Shared.metal.hpp"

@protocol LayerDelegate <NSObject>
- (Call *)drawCallsWithCount:(size_t *)count forLayer:(CALayer *)layer;
@end


@interface MetalLayer : CAMetalLayer

@property(weak) id <LayerDelegate> layerDelegate;

- (void)createColorsBuffer:(RGBA8 *)colors ncolors:(int)ncolors;
- (void)createPointsBuffer:(Vector *)points npoints:(int)npoints;
- (void)createTrianglesBuffer:(Triangle *)triangles ntriangles:(int)ntriangles;

@end
