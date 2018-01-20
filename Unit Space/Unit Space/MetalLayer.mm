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
@property (nonatomic, strong) id <MTLBuffer> indexBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> unitPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> arcPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> linePipelineState;

@end


@implementation MetalLayer

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    
    self.opaque = YES;
    self.needsDisplayOnBoundsChange = YES;
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
    descriptor.colorAttachments[0].pixelFormat = self.pixelFormat;
    descriptor.colorAttachments[0].blendingEnabled = NO;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"unit_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"unit_fragment_main"];
    descriptor.label = @"Unit";
    self.unitPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"arc_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"arc_fragment_main"];
    descriptor.label = @"Arc";
    self.arcPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"line_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"line_fragment_main"];
    descriptor.label = @"Line";
    self.linePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    static const uint16_t indices[] = { 0, 1, 2, 2, 1, 3 };
    self.indexBuffer = [self.device newBufferWithBytes:indices
                                                length:sizeof(indices)
                                               options:MTLResourceOptionCPUCacheModeDefault];
    return self;
}

- (void)display {
    @autoreleasepool {
        self.drawableSize = CGSizeMake(self.bounds.size.width * self.contentsScale, self.bounds.size.height * self.contentsScale);
        
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
    
    [commandEncoder setRenderPipelineState:_unitPipelineState];
    
    Uniforms uniforms;
    AffineTransform retina = { float(self.contentsScale), 0, 0, float(self.contentsScale), 0, 0 };
    AffineTransform ctm = {
        float(self.viewTransform.a), float(self.viewTransform.b),
        float(self.viewTransform.c), float(self.viewTransform.d),
        float(self.viewTransform.tx), float(self.viewTransform.ty) };
    
    uniforms.CTM = AffineTransformConcat(ctm, retina);
    uniforms.Color = { 0, 0, 0, 1 };
    uniforms.strokeColor = { 1, 0, 0, 1 };
    uniforms.drawableSize = { float(drawable.texture.width), float(drawable.texture.height) };
    uniforms.ellipseCTM = { 100, 0, 0, 100, 0, 0 };
    uniforms.flatness = 0.03125;
    uniforms.width = 0.5;
    uniforms.cap = 8;
    [commandEncoder setVertexBytes:&uniforms length:sizeof(Uniforms) atIndex:0];
    [commandEncoder setFragmentBytes:&uniforms length:sizeof(Uniforms) atIndex:0];
    
    [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                               indexCount:6
                                indexType:MTLIndexTypeUInt16
                              indexBuffer:_indexBuffer
                        indexBufferOffset:0];
    
    [commandEncoder setRenderPipelineState:_arcPipelineState];
    [commandEncoder setVertexBytes:&uniforms length:sizeof(Uniforms) atIndex:0];
    [commandEncoder setFragmentBytes:&uniforms length:sizeof(Uniforms) atIndex:0];
    Arcs arcs;
    arcs.count = 96;
    
    float cx = 200, cy = 200, r = 100, theta = TWO_PI / arcs.count;
    
    for (float i = 0; i < arcs.count; i++) {
        float rad = i * theta;
        float x = cx + r * std::cos(rad);
        float y = cy + r * std::sin(rad);
        
        arcs.arcs[int(i)] = { x, y, powf(std::abs(x - y), 1.01f), 0, 1 };
    }
    
    
//    path.arcs[0] = { 50, 50, 50, 0, 1.0 };
//    path.arcs[1] = { 160, 150, 50, 0, 0.5 };
    [commandEncoder setVertexBytes:&arcs length:sizeof(Arcs) atIndex:1];
    
    [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                               indexCount:6
                                indexType:MTLIndexTypeUInt16
                              indexBuffer:_indexBuffer
                        indexBufferOffset:0
                            instanceCount:std::ceil(1.0 / uniforms.flatness) * arcs.count];
    
    
    uniforms.strokeColor = { 0, 0, 1, 1 };
    uniforms.width = 8;
    [commandEncoder setRenderPipelineState:_linePipelineState];
    [commandEncoder setVertexBytes:&uniforms length:sizeof(Uniforms) atIndex:0];
    [commandEncoder setFragmentBytes:&uniforms length:sizeof(Uniforms) atIndex:0];
    Lines lines;
    lines.count = 1;
    lines.lines[0] = { 50, 50, 150, 150 };
    [commandEncoder setVertexBytes:&lines length:sizeof(Lines) atIndex:1];
    
    [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                               indexCount:6
                                indexType:MTLIndexTypeUInt16
                              indexBuffer:_indexBuffer
                        indexBufferOffset:0
                            instanceCount:lines.count];
    
    [commandEncoder endEncoding];
    
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
