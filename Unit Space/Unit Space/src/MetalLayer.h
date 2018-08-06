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
- (Call *)drawCallsWithCount:(size_t *)count forLayer:(CALayer *)layer fromMesh:(Mesh **)mesh;
@end


@interface MetalLayer : CAMetalLayer

@property(weak) id <LayerDelegate> layerDelegate;

- (void)renderMesh:(Mesh &)mesh calls:(vector<Call> &)calls meshState:(MeshState)meshState state:(State)state;
- (void)uploadMesh:(Mesh &)mesh;

@end
