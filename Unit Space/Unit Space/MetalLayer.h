//
//  MetalLayer.h
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <QuartzCore/QuartzCore.h>

@protocol LayerDelegate <NSObject>
- (void)drawLayer;
@end

@interface MetalLayer : CAMetalLayer

@property(nullable, weak) id <LayerDelegate> layerDelegate;

@end
