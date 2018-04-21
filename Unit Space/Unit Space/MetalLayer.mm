//
//  MetalLayer.m
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <Metal/Metal.h>
#import "MetalLayer.h"

@interface MetalLayer ()

@property (nonatomic, strong) id <MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id <MTLLibrary> defaultLibrary;
@property (nonatomic, strong) id <MTLBuffer> indexBuffer;
@property (nonatomic, strong) id <MTLBuffer> colorsBuffer;
@property (nonatomic, strong) id <MTLBuffer> curvesBuffer;
@property (nonatomic, strong) id <MTLBuffer> pointsBuffer;
@property (nonatomic, strong) id <MTLBuffer> trianglesBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> unitPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> arcPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> circlePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> fractalPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> linePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshTrianglePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshCurvePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> quadraticPipelineState;

@end


@implementation MetalLayer

- (id)init {
    self = [super init];
    if (!self)
        return nil;

    self.opaque = NO;
    self.needsDisplayOnBoundsChange = YES;
    self.actions = @{ @"onOrderIn": [NSNull null],
                      @"onOrderOut": [NSNull null],
                      @"sublayers": [NSNull null],
                      @"contents": [NSNull null],
                      @"backgroundColor": [NSNull null],
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
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"circle_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"circle_fragment_main"];
    descriptor.label = @"Circle";
    self.circlePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"fractal_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"fractal_fragment_main"];
    descriptor.label = @"Fractal";
    self.fractalPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"line_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"line_fragment_main"];
    descriptor.label = @"Line";
    self.linePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"quadratic_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"quadratic_fragment_main"];
    descriptor.label = @"Quadratic";
    self.quadraticPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"curve_fragment_main"];
    descriptor.label = @"Curve";
    self.meshCurvePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"triangle_fragment_main"];
    descriptor.label = @"Mesh";
    self.meshTrianglePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
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
    Call *calls = nullptr;
    size_t count = 0;
    if ([self.layerDelegate respondsToSelector:@selector(drawCallsWithCount:forLayer:)])
        calls = [self.layerDelegate drawCallsWithCount:&count forLayer:self];

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id <CAMetalDrawable> drawable = [self nextDrawable];
    
    if (drawable == nil)
        return;
    
    MTLRenderPassDescriptor *descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = drawable.texture;
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    
    id <MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
    
    while (count--) {
        size_t instanceCount = 1;
        
        switch (calls->type) {
            case kCallTypeArcs:
                [commandEncoder setRenderPipelineState:_arcPipelineState];
                [commandEncoder setVertexBytes:&calls->arcs length:sizeof(Arcs) atIndex:1];
                instanceCount = std::ceil(1.0 / calls->state.flatness) * calls->arcs.count;
                break;
            case kCallTypeCircles:
                [commandEncoder setRenderPipelineState:_circlePipelineState];
                [commandEncoder setVertexBytes:&calls->circles length:sizeof(Circles) atIndex:1];
                instanceCount = calls->circles.count;
                break;
            case kCallTypeFractal:
                [commandEncoder setRenderPipelineState:_fractalPipelineState];
                [commandEncoder setVertexBytes:&calls->fractals length:sizeof(Fractals) atIndex:1];
                [commandEncoder setFragmentBytes:&calls->fractals length:sizeof(Fractals) atIndex:1];
                instanceCount = calls->fractals.count;
                break;
            case kCallTypeLines:
                [commandEncoder setRenderPipelineState:_linePipelineState];
                [commandEncoder setVertexBytes:&calls->lines length:sizeof(Lines) atIndex:1];
                instanceCount = calls->lines.count;
                break;
            case kCallTypeMeshTriangles:
                [commandEncoder setRenderPipelineState:_meshTrianglePipelineState];
                [commandEncoder setVertexBytes:&calls->meshState length:sizeof(MeshState) atIndex:1];
                [commandEncoder setVertexBuffer:_colorsBuffer offset:0 atIndex:2];
                [commandEncoder setVertexBuffer:_pointsBuffer offset:0 atIndex:3];
                [commandEncoder setVertexBuffer:_trianglesBuffer offset:0 atIndex:4];
                instanceCount = _trianglesBuffer.length / sizeof(Triangle);
                break;
            case kCallTypeQuadratics:
                [commandEncoder setRenderPipelineState:_quadraticPipelineState];
                [commandEncoder setVertexBytes:&calls->quadratics length:sizeof(Quadratics) atIndex:1];
                break;
            case kCallTypeUnits:
                [commandEncoder setRenderPipelineState:_unitPipelineState];
                [commandEncoder setVertexBytes:&calls->units length:sizeof(Units) atIndex:1];
                break;
        }
        
        State state = calls->state;
        state.drawableSize = { float(drawable.texture.width), float(drawable.texture.height) };
        [commandEncoder setVertexBytes:&state length:sizeof(State) atIndex:0];
        [commandEncoder setFragmentBytes:&state length:sizeof(State) atIndex:0];
        
        if (calls->type == kCallTypeMeshTriangles) {
            [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                               vertexStart:0
                               vertexCount:3
                             instanceCount:instanceCount
                              baseInstance:0];
        } else {
            [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                       indexCount:6
                                        indexType:MTLIndexTypeUInt16
                                      indexBuffer:_indexBuffer
                                indexBufferOffset:0
                                    instanceCount:instanceCount];
        }
        
        calls++;
    }
    
    [commandEncoder endEncoding];
    
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

#pragma mark - Public

- (void)createColorsBuffer:(RGBA8 *)colors ncolors:(size_t)ncolors {
    _colorsBuffer = [self.device newBufferWithBytes:(void *)colors
                                                   length:ncolors * sizeof(RGBA8)
                                                  options:MTLResourceStorageModeShared];
}

- (void)createCurvesBuffer:(Triangle *)curves ncurves:(size_t)ncurves {
    _curvesBuffer = [self.device newBufferWithBytes:(void *)curves
                                                length:ncurves * sizeof(Triangle)
                                               options:MTLResourceStorageModeShared];
}

- (void)createPointsBuffer:(Vector *)points npoints:(size_t)npoints {
    _pointsBuffer = [self.device newBufferWithBytes:(void *)points
                                                   length:npoints * sizeof(Vector)
                                                  options:MTLResourceStorageModeShared];
}

- (void)createTrianglesBuffer:(Triangle *)triangles ntriangles:(size_t)ntriangles {
    _trianglesBuffer = [self.device newBufferWithBytes:(void *)triangles
                                                   length:ntriangles * sizeof(Triangle)
                                                  options:MTLResourceStorageModeShared];
}

@end
