//
//  MetalLayer.h
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <QuartzCore/QuartzCore.h>
#import "Shared.metal.hpp"
#import "Mesh.hpp"

@protocol LayerDelegate <NSObject>
- (Mesh *)meshForLayer:(CALayer *)layer;
@end


@interface MetalLayer : CAMetalLayer

@property(weak) id <LayerDelegate> layerDelegate;

- (void)renderMesh:(Mesh &)mesh state:(State)state meshState:(MeshState)meshState showPolygons:(BOOL)showPolygons;
- (void)uploadMesh:(Mesh &)mesh;

@end
