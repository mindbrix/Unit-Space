//
//  MetalLayer.m
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "MetalLayer.h"
#import <Metal/Metal.h>
#import <simd/simd.h>

@interface MetalLayer ()

@property (nonatomic, strong) id <MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id <MTLLibrary> defaultLibrary;

@end


@implementation MetalLayer

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    
    self.opaque = NO;
    self.needsDisplayOnBoundsChange = TRUE;
    self.actions = @{ @"onOrderIn": [NSNull null],
                      @"onOrderOut": [NSNull null],
                      @"sublayers": [NSNull null],
                      @"contents": [NSNull null],
                      @"bounds": [NSNull null] };
    self.device = MTLCreateSystemDefaultDevice();
    self.pixelFormat = MTLPixelFormatBGRA8Unorm;
    
    self.commandQueue = [self.device newCommandQueue];
    self.defaultLibrary = [self.device newDefaultLibrary];
    
    return self;
}

- (void)display {
    @autoreleasepool {
        [self draw];
    }
}

- (void)draw {
    if ([self.layerDelegate respondsToSelector:@selector(drawLayer:)])
        [self.layerDelegate drawLayer:self];
    
    MTLClearColor zeroes = MTLClearColorMake(0, 1, 0, 1);
    
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id <CAMetalDrawable> drawable = [self nextDrawable];
    
    if (drawable == nil) {
        return;
    }
    
    MTLRenderPassDescriptor *descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = drawable.texture;
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    descriptor.colorAttachments[0].clearColor = zeroes;
    
    id <MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
    
    [commandEncoder endEncoding];
    

    
    
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
