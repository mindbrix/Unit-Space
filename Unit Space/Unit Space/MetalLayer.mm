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
@property (nonatomic, strong) id <MTLBuffer> concaveCurvesBuffer;
@property (nonatomic, strong) id <MTLBuffer> trianglesBuffer;
@property (nonatomic, strong) id <MTLBuffer> pointsBuffer;
@property (nonatomic, strong) id <MTLBuffer> trianglesScalesBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> unitPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> arcPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> circlePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> fractalPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> linePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshCurvePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshConcaveCurvePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshTrianglePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> quadraticPipelineState;
@property (nonatomic, strong) id <MTLDepthStencilState> depthState;
@property (nonatomic, strong) id <MTLTexture> depthTexture;

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
    
    MTLDepthStencilDescriptor *depthStencilDescriptor = [MTLDepthStencilDescriptor new];
    depthStencilDescriptor.depthWriteEnabled = YES;
    depthStencilDescriptor.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    self.depthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.colorAttachments[0].pixelFormat = self.pixelFormat;
    descriptor.colorAttachments[0].blendingEnabled = NO;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"unit_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"unit_fragment_main"];
    descriptor.label = @"Unit";
    self.unitPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"interior_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"interior_fragment_main"];
    descriptor.label = @"Interior";
    self.meshInteriorPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
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
    
//    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
//    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"triangle_fragment_main"];
    descriptor.label = @"Triangle";
    self.meshTrianglePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"curve_fragment_main"];
    descriptor.label = @"Curve";
    self.meshCurvePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationReverseSubtract;
    descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationReverseSubtract;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"curve_fragment_main"];
    descriptor.label = @"Concave Curve";
    self.meshConcaveCurvePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
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
    
    if (self.drawableSize.width != self.depthTexture.width || self.drawableSize.height != self.depthTexture.height) {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                        width:self.drawableSize.width
                                                                                       height:self.drawableSize.height
                                                                                    mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget;
        self.depthTexture = [self.device newTextureWithDescriptor:desc];
        [self.depthTexture setLabel:@"Depth"];
    }

    MTLRenderPassDescriptor *descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = drawable.texture;
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    descriptor.depthAttachment.texture = _depthTexture;
    descriptor.depthAttachment.loadAction = MTLLoadActionClear;
    descriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
    descriptor.depthAttachment.clearDepth = 0;
    
    id <MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
    [commandEncoder setDepthStencilState:_depthState];
    
    Call *end = calls + count;
    for (Call *call = calls; call < end; call++) {
        size_t instanceCount = 1;
        BOOL drawQuads = YES;
        switch (call->type) {
            case kCallTypeArcs:
                [commandEncoder setRenderPipelineState:_arcPipelineState];
                [commandEncoder setVertexBytes:&call->arcs length:sizeof(Arcs) atIndex:1];
                instanceCount = std::ceil(1.0 / call->state.flatness) * call->arcs.count;
                break;
            case kCallTypeCircles:
                [commandEncoder setRenderPipelineState:_circlePipelineState];
                [commandEncoder setVertexBytes:&call->circles length:sizeof(Circles) atIndex:1];
                instanceCount = call->circles.count;
                break;
            case kCallTypeFractal:
                [commandEncoder setRenderPipelineState:_fractalPipelineState];
                [commandEncoder setVertexBytes:&call->fractals length:sizeof(Fractals) atIndex:1];
                [commandEncoder setFragmentBytes:&call->fractals length:sizeof(Fractals) atIndex:1];
                instanceCount = call->fractals.count;
                break;
            case kCallTypeLines:
                [commandEncoder setRenderPipelineState:_linePipelineState];
                [commandEncoder setVertexBytes:&call->lines length:sizeof(Lines) atIndex:1];
                instanceCount = call->lines.count;
                break;
            case kCallTypeMeshCurves:
                if (call->meshState.hideCurves)
                    continue;
                [commandEncoder setRenderPipelineState:_meshCurvePipelineState];
                [commandEncoder setVertexBytes:&call->meshState length:sizeof(MeshState) atIndex:1];
                [commandEncoder setVertexBuffer:_colorsBuffer offset:0 atIndex:2];
                [commandEncoder setVertexBuffer:_pointsBuffer offset:0 atIndex:3];
                [commandEncoder setVertexBuffer:_curvesBuffer offset:0 atIndex:4];
                instanceCount = _curvesBuffer.length / sizeof(Triangle);
                drawQuads = NO;
                break;
            case kCallTypeMeshConcaveCurves:
                if (call->meshState.hideCurves)
                    continue;
                [commandEncoder setRenderPipelineState:call->meshState.showInterior && !call->meshState.hideTriangles ? _meshConcaveCurvePipelineState : _meshCurvePipelineState];
                [commandEncoder setVertexBytes:&call->meshState length:sizeof(MeshState) atIndex:1];
                [commandEncoder setVertexBuffer:_colorsBuffer offset:0 atIndex:2];
                [commandEncoder setVertexBuffer:_pointsBuffer offset:0 atIndex:3];
                [commandEncoder setVertexBuffer:_concaveCurvesBuffer offset:0 atIndex:4];
                instanceCount = _concaveCurvesBuffer.length / sizeof(Triangle);
                drawQuads = NO;
                break;
            case kCallTypeMeshInterior:
            case kCallTypeMeshTriangles:
                if (call->meshState.hideTriangles)
                    continue;
                
                [commandEncoder setRenderPipelineState:call->type == kCallTypeMeshTriangles ? _meshTrianglePipelineState : _meshInteriorPipelineState];
                [commandEncoder setVertexBytes:&call->meshState length:sizeof(MeshState) atIndex:1];
                [commandEncoder setVertexBuffer:_colorsBuffer offset:0 atIndex:2];
                [commandEncoder setVertexBuffer:_pointsBuffer offset:0 atIndex:3];
                [commandEncoder setVertexBuffer:_trianglesBuffer offset:0 atIndex:4];
                [commandEncoder setVertexBuffer:_trianglesScalesBuffer offset:0 atIndex:5];
                instanceCount = _trianglesBuffer.length / sizeof(Triangle);
                drawQuads = NO;
                break;
            case kCallTypeQuadratics:
                [commandEncoder setRenderPipelineState:_quadraticPipelineState];
                [commandEncoder setVertexBytes:&call->quadratics length:sizeof(Quadratics) atIndex:1];
                break;
            case kCallTypeUnits:
                [commandEncoder setRenderPipelineState:_unitPipelineState];
                [commandEncoder setVertexBytes:&call->units length:sizeof(Units) atIndex:1];
                break;
        }
        if (instanceCount == 0)
            continue;
        
        State state = call->state;
        state.drawableSize = { float(drawable.texture.width), float(drawable.texture.height) };
        [commandEncoder setVertexBytes:&state length:sizeof(State) atIndex:0];
        [commandEncoder setFragmentBytes:&state length:sizeof(State) atIndex:0];
        
        if (drawQuads)
            [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                       indexCount:6
                                        indexType:MTLIndexTypeUInt16
                                      indexBuffer:_indexBuffer
                                indexBufferOffset:0
                                    instanceCount:instanceCount];
        else
            [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                               vertexStart:0
                               vertexCount:3
                             instanceCount:instanceCount
                              baseInstance:0];
    }
    
    [commandEncoder endEncoding];
    
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

#pragma mark - Public

- (void)renderMesh:(Mesh &)mesh calls:(vector<Call> &)calls meshState:(MeshState)meshState uniforms:(State)uniforms {
    meshState.colorsSize = (int)mesh._colors.size();
    if (mesh._triangles.size()) {
        calls.emplace_back();
        Call& call = calls.back();
        call.meshState = meshState;
        call.meshState.reverseCount = (int)_trianglesBuffer.length / sizeof(Triangle);
        call.type = kCallTypeMeshInterior;
        call.state = uniforms;
        call.state.CTM = AffineTransformConcat(mesh._CTM, call.state.CTM);
        
        if (!meshState.showEdges) {
            calls.emplace_back();
            Call& call = calls.back();
            call.meshState = meshState;
            call.meshState.reverseCount = 0;
            call.type = kCallTypeMeshTriangles;
            call.state = uniforms;
            call.state.CTM = AffineTransformConcat(mesh._CTM, call.state.CTM);
        }
    }
    
    if (mesh._concaveCurves.size()) {
        calls.emplace_back();
        Call& call = calls.back();
        call.meshState = meshState;
        call.type = kCallTypeMeshConcaveCurves;
        call.state = uniforms;
    }
    
    if (mesh._curves.size()) {
        calls.emplace_back();
        Call& call = calls.back();
        call.meshState = meshState;
        call.type = kCallTypeMeshCurves;
        call.state = uniforms;
    }
}

- (void)uploadMesh:(Mesh &)mesh {
    _colorsBuffer = [self.device newBufferWithBytes:(void *)& mesh._colors[0]
                                             length:mesh._colors.size() * sizeof(RGBA8)
                                            options:MTLResourceStorageModeShared];
    if (mesh._triangles.size())
        _trianglesBuffer = [self.device newBufferWithBytes:(void *)& mesh._triangles[0]
                                                    length:mesh._triangles.size() * sizeof(Triangle)
                                                   options:MTLResourceStorageModeShared];
    if (mesh._points.size())
        _pointsBuffer = [self.device newBufferWithBytes:(void *)& mesh._points[0]
                                                 length:mesh._points.size() * sizeof(Vector)
                                                options:MTLResourceStorageModeShared];
    if (mesh._curves.size())
        _curvesBuffer = [self.device newBufferWithBytes:(void *)& mesh._curves[0]
                                                 length:mesh._curves.size() * sizeof(Triangle)
                                                options:MTLResourceStorageModeShared];
    if (mesh._concaveCurves.size())
        _concaveCurvesBuffer = [self.device newBufferWithBytes:(void *)& mesh._concaveCurves[0]
                                                        length:mesh._concaveCurves.size() * sizeof(Triangle)
                                                       options:MTLResourceStorageModeShared];
    if (mesh._triangleScales.size())
        _trianglesScalesBuffer = [self.device newBufferWithBytes:(void *)& mesh._triangleScales[0]
                                                          length:mesh._triangleScales.size() * sizeof(float)
                                                         options:MTLResourceStorageModeShared];
}

@end
