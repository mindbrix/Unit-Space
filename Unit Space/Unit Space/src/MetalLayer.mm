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
@property (nonatomic, strong) id <MTLBuffer> pathsBuffer;
@property (nonatomic, strong) id <MTLBuffer> pointsBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> arcPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> circlePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> flagPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> lineSOAPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorBlendPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshTrianglePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> pathInteriorSOAPipelineState;
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
    self.magnificationFilter = kCAFilterNearest;
    
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
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"flags_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"flags_fragment_main"];
    descriptor.label = @"flagPipelineState";
    self.flagPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"interior_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"interior_fragment_main"];
    descriptor.label = @"Interior";
    self.meshInteriorPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"pathInteriorSOA_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"pathInteriorSOA_fragment_main"];
    descriptor.label = @"pathInteriorSOAPipelineState";
    self.pathInteriorSOAPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.label = @"InteriorBlend";
    self.meshInteriorBlendPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"arc_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"arc_fragment_main"];
    descriptor.label = @"Arc";
    self.arcPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"circleSOA_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"circle_fragment_main"];
    descriptor.label = @"Circle";
    self.circlePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"lineSOA_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"line_fragment_main"];
    descriptor.label = @"lineSOAPipelineState";
    self.lineSOAPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"triangle_fragment_main"];
    descriptor.label = @"Triangle";
    self.meshTrianglePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    return self;
}

- (void)display {
    @autoreleasepool {
        self.drawableSize = CGSizeMake(self.bounds.size.width * self.contentsScale, self.bounds.size.height * self.contentsScale);
        [self draw];
    }
}

- (void)draw {
    Mesh *mesh = nullptr;
    if ([self.layerDelegate respondsToSelector:@selector(meshForLayer:)])
        mesh = [self.layerDelegate meshForLayer:self];
    Call *calls = mesh ? & mesh->_calls[0] : nullptr;
    size_t count = mesh ? mesh->_calls.size() : 0;
    
    if (calls == nullptr || count == 0)
        return;
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
        [self.depthTexture setLabel:@"depthTexture"];
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
        size_t instanceCount = 1, baseInstance = 0;
        BOOL drawQuads = YES;
        switch (call->type) {
            case kCallTypeArcs:
                [commandEncoder setRenderPipelineState:_arcPipelineState];
                [commandEncoder setVertexBytes:& call->arcs length:sizeof(Arcs) atIndex:10];
                instanceCount = std::ceil(1.0 / call->state.flatness) * call->arcs.count;
                break;
            case kCallTypeCirclesSOA:
                [commandEncoder setRenderPipelineState:_circlePipelineState];
                [commandEncoder setVertexBytes:& mesh->_circles._xs[call->baseInstance] length:sizeof(float) * (call->instanceCount) atIndex:10];
                [commandEncoder setVertexBytes:& mesh->_circles._ys[call->baseInstance] length:sizeof(float) * (call->instanceCount) atIndex:11];
                [commandEncoder setVertexBytes:& mesh->_circles._rs[call->baseInstance] length:sizeof(float) * (call->instanceCount) atIndex:12];
                [commandEncoder setVertexBytes:& mesh->_circles._colors[call->baseInstance] length:sizeof(RGBA8) * (call->instanceCount) atIndex:13];
                instanceCount = call->instanceCount;
                break;
            case kCallTypeFlags:
                [commandEncoder setRenderPipelineState:_flagPipelineState];
                [commandEncoder setVertexBytes:& call->flags length:sizeof(Flags) atIndex:10];
                instanceCount = call->instanceCount;
                break;
            case kCallTypeMeshInterior:
            case kCallTypeMeshInteriorBlend:
            case kCallTypeMeshTriangles:
                if (call->meshState.hideTriangles)
                    continue;
                [commandEncoder setRenderPipelineState:call->type == kCallTypeMeshTriangles ? _meshTrianglePipelineState : call->type == kCallTypeMeshInterior ? _meshInteriorPipelineState : _meshInteriorBlendPipelineState];
                [commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                [commandEncoder setVertexBuffer:_pointsBuffer offset:0 atIndex:10];
                instanceCount = call->instanceCount, baseInstance = call->baseInstance;
                drawQuads = NO;
                break;
            case kCallTypeLinesSOA:
                [commandEncoder setRenderPipelineState:_lineSOAPipelineState];
                [commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                [commandEncoder setVertexBytes:& mesh->_xs[call->baseInstance] length:sizeof(float) * (call->instanceCount + 1) atIndex:10];
                [commandEncoder setVertexBytes:& mesh->_ys[call->baseInstance] length:sizeof(float) * (call->instanceCount + 1) atIndex:11];
                [commandEncoder setVertexBytes:& mesh->_ps[call->baseInstance] length:sizeof(int)   * (call->instanceCount + 1) atIndex:12];
                [commandEncoder setVertexBytes:& mesh->_as[call->baseInstance] length:sizeof(float) * (call->instanceCount + 1) atIndex:13];
                [commandEncoder setVertexBytes:& mesh->_bs[call->baseInstance] length:sizeof(float) * (call->instanceCount + 1) atIndex:14];
                instanceCount = call->instanceCount * call->meshState.subdivisionCount;
                break;
            case kCallTypePathInteriorSOA:
                [commandEncoder setRenderPipelineState:_pathInteriorSOAPipelineState];
                [commandEncoder setVertexBytes:& mesh->_interiors._glxs[call->baseInstance] length:sizeof(unsigned short) * (call->instanceCount) atIndex:10];
                [commandEncoder setVertexBytes:& mesh->_interiors._guxs[call->baseInstance] length:sizeof(unsigned short) * (call->instanceCount) atIndex:11];
                [commandEncoder setVertexBytes:& mesh->_interiors._gys[call->baseInstance] length:sizeof(unsigned short) * (call->instanceCount) atIndex:12];
                [commandEncoder setVertexBytes:& mesh->_interiors._colors[call->baseInstance] length:sizeof(RGBA8) * (call->instanceCount) atIndex:13];
                instanceCount = call->instanceCount;
                break;
        }
        if (instanceCount == 0)
            continue;
        
        State state = call->state;
        state.drawableSize = { float(drawable.texture.width), float(drawable.texture.height) };
        [commandEncoder setVertexBytes:& state length:sizeof(State) atIndex:0];
        [commandEncoder setVertexBytes:& call->meshState length:sizeof(MeshState) atIndex:1];
        [commandEncoder setFragmentBytes:& state length:sizeof(State) atIndex:0];
        
        [commandEncoder drawPrimitives:drawQuads ? MTLPrimitiveTypeTriangleStrip : MTLPrimitiveTypeTriangle
                           vertexStart:0
                           vertexCount:drawQuads ? 4 : 3
                         instanceCount:instanceCount
                          baseInstance:baseInstance];
    }
    [commandEncoder endEncoding];

    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

#pragma mark - Public

- (void)writePathCalls:(Mesh &)mesh state:(State)state meshState:(MeshState)meshState {
    Bounds clipBounds = { 0, 0, float(std::ceil(self.bounds.size.width * self.contentsScale)), float(std::ceil(self.bounds.size.height * self.contentsScale)) };
    Mesh::writePathCalls(mesh, clipBounds, state, meshState);
    [self syncMesh:mesh];
}

- (void)syncMesh:(Mesh &)mesh {
    memcpy(_pathsBuffer.contents, & mesh._paths[0], _pathsBuffer.length);
    [_pathsBuffer didModifyRange:NSMakeRange(0, _pathsBuffer.length)];
}

- (void)uploadMesh:(Mesh &)mesh {
    if (mesh._paths.size())
        _pathsBuffer = [self.device newBufferWithBytes:(void *)& mesh._paths[0]
                                                 length:mesh._paths.size() * sizeof(MeshPath)
                                                options:MTLResourceStorageModeManaged];
    if (mesh._points.size())
        _pointsBuffer = [self.device newBufferWithBytes:(void *)& mesh._points[0]
                                              length:mesh._points.size() * sizeof(Vector)
                                             options:MTLResourceStorageModeShared];
}

@end
