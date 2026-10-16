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
- (Mesh *)meshForLayer:(CALayer *)layer paths:(MeshPath *)paths;
@end


@interface MetalLayer : CAMetalLayer

@property(weak) id <LayerDelegate> layerDelegate;

// The scene's mesh, with its paths.
- (void)uploadMesh:(Mesh &)mesh;
// Another mesh, drawn by calls with that mesh index over the same paths: 1 for paths being edited, 2 for editing handles.
- (void)uploadMesh:(Mesh &)mesh index:(NSUInteger)index;
// Room for this many paths in the paths buffer, which the delegate fills each frame.
- (void)reservePaths:(size_t)count;

@end
