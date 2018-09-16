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

@property (nonatomic, strong) dispatch_semaphore_t inflight_semaphore;
@property (nonatomic, strong) id <MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id <MTLLibrary> defaultLibrary;
@property (nonatomic, strong) id <MTLBuffer> pathsBuffer;
@property (nonatomic, strong) id <MTLBuffer> trianglesBuffer;
@property (nonatomic, strong) id <MTLBuffer> quadraticFloatsBuffer;
@property (nonatomic, strong) id <MTLBuffer> quadraticPsBuffer;
@property (nonatomic, strong) id <MTLBuffer> quadraticTsBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> shapePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> flagPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> resolveQuadSOAPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> resolveSOAPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorBlendPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshTrianglePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> quadraticLinePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> quadraticAccumulatePipelineState;
@property (nonatomic, strong) id <MTLDepthStencilState> depthState;
@property (nonatomic, strong) id <MTLTexture> depthTexture;
@property (nonatomic, strong) id <MTLTexture> accumulationTexture;

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
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    self.colorspace = rgb;
    CGColorSpaceRelease(rgb);
    
    self.commandQueue = [self.device newCommandQueue];
    self.defaultLibrary = [self.device newDefaultLibrary];
    self.inflight_semaphore = dispatch_semaphore_create(1);
    
    MTLDepthStencilDescriptor *depthStencilDescriptor = [MTLDepthStencilDescriptor new];
    depthStencilDescriptor.depthWriteEnabled = YES;
    depthStencilDescriptor.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    self.depthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.colorAttachments[0].pixelFormat = self.pixelFormat;
    descriptor.colorAttachments[1].blendingEnabled = YES;
    descriptor.colorAttachments[1].pixelFormat = MTLPixelFormatR32Float;
    descriptor.colorAttachments[1].rgbBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[1].alphaBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[1].sourceRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[1].sourceAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[1].destinationRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[1].destinationAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[1].writeMask = MTLColorWriteMaskNone;
    
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    
    descriptor.colorAttachments[0].blendingEnabled = NO;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"resolveQuadSOA_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"resolveSOA_fragment_main"];
    descriptor.label = @"resolveSOAPipelineState";
    self.resolveQuadSOAPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"flags_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"flags_fragment_main"];
    descriptor.label = @"flagPipelineState";
    self.flagPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
	
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
    descriptor.label = @"InteriorBlend";
    self.meshInteriorBlendPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"shapesSOA_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"shapesSOA_fragment_main"];
    descriptor.label = @"shapePipelineState";
    self.shapePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];

    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"quadratic_line_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"line_fragment_main"];
    descriptor.label = @"quadraticLinePipelineState";
    self.quadraticLinePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"resolveSOA_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"resolveSOA_fragment_main"];
    descriptor.label = @"resolveSOAPipelineState";
    self.resolveSOAPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"triangle_fragment_main"];
    descriptor.label = @"Triangle";
    self.meshTrianglePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
    descriptor.colorAttachments[1].writeMask = MTLColorWriteMaskAll;
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"quadratic_accumulate_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"accumulate_fragment_main"];
    descriptor.label = @"quadraticAccumulatePipelineState";
    self.quadraticAccumulatePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    return self;
}

- (void)display {
    @autoreleasepool {
        self.drawableSize = CGSizeMake(self.bounds.size.width * self.contentsScale, self.bounds.size.height * self.contentsScale);
        dispatch_semaphore_wait(_inflight_semaphore, DISPATCH_TIME_FOREVER);
        [self draw];
    }
}

- (void)draw {
    Mesh *mesh = nullptr;
    if ([self.layerDelegate respondsToSelector:@selector(meshForLayer:)])
        mesh = [self.layerDelegate meshForLayer:self];
    Call *calls = mesh ? & mesh->_calls[0] : nullptr;
    size_t count = mesh ? mesh->_calls.size() : 0;
    
    memcpy(_pathsBuffer.contents, & mesh->_timeline.pathsForTime(1)[0], _pathsBuffer.length);
    [_pathsBuffer didModifyRange:NSMakeRange(0, _pathsBuffer.length)];
    [[self.commandQueue commandBuffer] commit];
    
    id <CAMetalDrawable> drawable = calls == nullptr || count == 0 ? nil : [self nextDrawable];
    if (drawable == nil) {
        dispatch_semaphore_signal(_inflight_semaphore);
        return;
    }
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    if (self.drawableSize.width != self.depthTexture.width || self.drawableSize.height != self.depthTexture.height) {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                        width:self.drawableSize.width
                                                                                       height:self.drawableSize.height
                                                                                    mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget;
        self.depthTexture = [self.device newTextureWithDescriptor:desc];
        [self.depthTexture setLabel:@"depthTexture"];
        
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.pixelFormat = MTLPixelFormatR32Float;
        self.accumulationTexture = [self.device newTextureWithDescriptor:desc];
        [self.accumulationTexture setLabel:@"accumulationTexture"];
    }
	MTLRenderPassDescriptor *descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = drawable.texture;
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    
    descriptor.colorAttachments[1].texture = _accumulationTexture;
    descriptor.colorAttachments[2].storeAction = MTLStoreActionDontCare;
    descriptor.colorAttachments[1].loadAction = MTLLoadActionClear;
    descriptor.colorAttachments[1].clearColor = MTLClearColorMake(0, 0, 0, 0);
    
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
            case kCallTypeShapesSOA:
                [commandEncoder setRenderPipelineState:_shapePipelineState];
                [commandEncoder setVertexBytes:& mesh->_shapesSOA._types[call->baseInstance] length:sizeof(short) * call->instanceCount atIndex:10];
                [commandEncoder setVertexBytes:& mesh->_shapesSOA._as[call->baseInstance] length:sizeof(float) * call->instanceCount atIndex:11];
                [commandEncoder setVertexBytes:& mesh->_shapesSOA._bs[call->baseInstance] length:sizeof(float) * call->instanceCount atIndex:12];
                [commandEncoder setVertexBytes:& mesh->_shapesSOA._cs[call->baseInstance] length:sizeof(float) * call->instanceCount atIndex:13];
                [commandEncoder setVertexBytes:& mesh->_shapesSOA._ds[call->baseInstance] length:sizeof(float) * call->instanceCount atIndex:14];
                [commandEncoder setVertexBytes:& mesh->_shapesSOA._txs[call->baseInstance] length:sizeof(float) * call->instanceCount atIndex:15];
                [commandEncoder setVertexBytes:& mesh->_shapesSOA._tys[call->baseInstance] length:sizeof(float) * call->instanceCount atIndex:16];
                [commandEncoder setVertexBytes:& mesh->_shapesSOA._colors[call->baseInstance] length:sizeof(RGBA8) * call->instanceCount atIndex:17];
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
                [commandEncoder setRenderPipelineState:call->type == kCallTypeMeshTriangles ? _meshTrianglePipelineState : call->type == kCallTypeMeshInterior ? _meshInteriorPipelineState : _meshInteriorBlendPipelineState];
                [commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                [commandEncoder setVertexBuffer:_trianglesBuffer offset:0 atIndex:10];
                instanceCount = call->instanceCount, baseInstance = call->baseInstance;
                drawQuads = NO;
                break;
           case kCallTypeResolveQuadSOA:
                [commandEncoder textureBarrier];
                [commandEncoder setRenderPipelineState:_resolveQuadSOAPipelineState];
                [commandEncoder setFragmentTexture:_accumulationTexture atIndex:1];
                break;
            case kCallTypeResolveSOA:
                [commandEncoder textureBarrier];
                [commandEncoder setRenderPipelineState:_resolveSOAPipelineState];
                [commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                [commandEncoder setVertexBytes:& mesh->_resolutionsSOA._lxs[call->baseInstance] length:sizeof(short) * call->instanceCount atIndex:10];
                [commandEncoder setVertexBytes:& mesh->_resolutionsSOA._uxs[call->baseInstance] length:sizeof(short) * call->instanceCount atIndex:11];
                [commandEncoder setVertexBytes:& mesh->_resolutionsSOA._lys[call->baseInstance] length:sizeof(short) * call->instanceCount atIndex:12];
                [commandEncoder setVertexBytes:& mesh->_resolutionsSOA._txs[call->baseInstance] length:sizeof(short) * call->instanceCount atIndex:13];
                [commandEncoder setVertexBytes:& mesh->_resolutionsSOA._tys[call->baseInstance] length:sizeof(short) * call->instanceCount atIndex:14];
                [commandEncoder setVertexBytes:& mesh->_resolutionsSOA._ps[call->baseInstance] length:sizeof(int) * call->instanceCount atIndex:15];
				[commandEncoder setFragmentTexture:_accumulationTexture atIndex:1];
                instanceCount = call->instanceCount;
                break;
            case kCallTypeQuadraticLines:
            case kCallTypeQuadraticAccumulate:
                [commandEncoder setRenderPipelineState:call->type == kCallTypeQuadraticLines ? _quadraticLinePipelineState : _quadraticAccumulatePipelineState];
                [commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                [commandEncoder setVertexBuffer:_quadraticFloatsBuffer offset:0 atIndex:10];
                [commandEncoder setVertexBuffer:_quadraticPsBuffer offset:0 atIndex:11];
                [commandEncoder setVertexBuffer:_quadraticTsBuffer offset:0 atIndex:12];
                instanceCount = call->instanceCount, baseInstance = call->baseInstance;
                break;
        }
        if (instanceCount == 0)
            continue;
        
        State state = call->state;
        state.drawableSize = { float(drawable.texture.width), float(drawable.texture.height) };
        [commandEncoder setVertexBytes:& state length:sizeof(State) atIndex:0];
        [commandEncoder setVertexBytes:& call->meshState length:sizeof(MeshState) atIndex:1];
        [commandEncoder setFragmentBytes:& state length:sizeof(State) atIndex:0];
		[commandEncoder setFragmentBytes:& call->meshState length:sizeof(MeshState) atIndex:1];
		
        [commandEncoder drawPrimitives:drawQuads ? MTLPrimitiveTypeTriangleStrip : MTLPrimitiveTypeTriangle
                           vertexStart:0
                           vertexCount:drawQuads ? 4 : 3
                         instanceCount:instanceCount
                          baseInstance:baseInstance];
    }
    [commandEncoder endEncoding];
    __block dispatch_semaphore_t block_sema = _inflight_semaphore;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        dispatch_semaphore_signal(block_sema);
    }];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

#pragma mark - Public

- (void)uploadMesh:(Mesh &)mesh {
    if (mesh._quadraticFloats.pagedSize())
        _quadraticFloatsBuffer = [self.device newBufferWithBytes:(void *)mesh._quadraticFloats.get()
                                                          length:mesh._quadraticFloats.pagedSize()
                                                         options:MTLResourceStorageModeShared];
    if (mesh._quadraticPs.pagedSize())
        _quadraticPsBuffer = [self.device newBufferWithBytes:(void *)mesh._quadraticPs.get()
                                                      length:mesh._quadraticPs.pagedSize()
                                                     options:MTLResourceStorageModeShared];
    if (mesh._quadraticTs.pagedSize())
        _quadraticTsBuffer = [self.device newBufferWithBytes:(void *)mesh._quadraticTs.get()
                                                      length:mesh._quadraticTs.pagedSize()
                                                     options:MTLResourceStorageModeShared];
    if (mesh._triangles.pagedSize())
        _trianglesBuffer = [self.device newBufferWithBytes:(void *)mesh._triangles.get()
                                                    length:mesh._triangles.pagedSize()
                                                   options:MTLResourceStorageModeShared];
    if (mesh._timeline.pathsForTime(0).size())
        _pathsBuffer = [self.device newBufferWithBytes:(void *)& mesh._timeline.pathsForTime(0)[0]
                                                length:mesh._timeline.pathsForTime(0).size() * sizeof(MeshPath)
                                               options:MTLResourceStorageModeManaged];
}

@end
