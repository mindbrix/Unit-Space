//
//  MetalLayer.m
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "MetalLayer.h"
#import <Metal/Metal.h>
#import "Shared.metal.hpp"

@interface MetalLayer ()

@property (nonatomic, strong) id <MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id <MTLLibrary> defaultLibrary;
@property (nonatomic, strong) id <MTLRenderPipelineState> pipelineState;

@end


@implementation MetalLayer

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    
    self.opaque = YES;
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
    
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.colorAttachments[0].blendingEnabled = NO;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"fragment_main"];
    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
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
    
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id <CAMetalDrawable> drawable = [self nextDrawable];
    
    if (drawable == nil) {
        return;
    }
    
    MTLRenderPassDescriptor *descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = drawable.texture;
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    descriptor.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    
    id <MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
    
    [commandEncoder setRenderPipelineState:_pipelineState];
    
    Uniforms uniforms;
    uniforms.CTM = { 1, 0, 0, 1, 0, 0 };
    uniforms.Color = { 0, 0, 0, 1 };
    [commandEncoder setVertexBytes:&uniforms length:sizeof(Uniforms) atIndex:0];
    [commandEncoder setFragmentBytes:&uniforms length:sizeof(Uniforms) atIndex:0];
    
    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:0
                       vertexCount:6];
    
    [commandEncoder endEncoding];
    
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
