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

- (void)createColorsBuffer:(RGBA8 *)colors ncolors:(size_t)ncolors;
- (void)createCurvesBuffer:(Triangle *)curves ncurves:(size_t)ncurves;
- (void)createPointsBuffer:(Vector *)points npoints:(size_t)npoints;
- (void)createTrianglesBuffer:(Triangle *)triangles ntriangles:(size_t)ntriangles;

@end
