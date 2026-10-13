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
@property (nonatomic, strong) id <MTLBuffer> trisBuffer;
@property (nonatomic, strong) id <MTLBuffer> interiorVertsBuffer;
@property (nonatomic, strong) id <MTLBuffer> interiorIdxsBuffer;
@property (nonatomic, strong) id <MTLBuffer> interiorEdgesBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorIndexedPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorDebugPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshTrianglePipelineState;
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
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    
    // The interior is opaque, drawn front to back against the depth buffer.
    descriptor.colorAttachments[0].blendingEnabled = NO;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"interior_indexed_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"interior_indexed_fragment_main"];
    descriptor.label = @"InteriorIndexed";
    self.meshInteriorIndexedPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    // The debug outlines need barycentric coordinates, which not every GPU has. Without them the interior draws plain.
    if (self.device.supportsShaderBarycentricCoordinates) {
        descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"interior_indexed_debug_fragment_main"];
        descriptor.label = @"InteriorDebug";
        self.meshInteriorDebugPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    }
	
    // The edges blend premultiplied colour over the interior.
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"triangle_fragment_main"];
    descriptor.label = @"Triangle";
    self.meshTrianglePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
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
    if ([self.layerDelegate respondsToSelector:@selector(meshForLayer:paths:)])
        mesh = [self.layerDelegate meshForLayer:self paths:(MeshPath *)_pathsBuffer.contents];
    Call *calls = mesh && mesh->_calls.size() ? & mesh->_calls[0] : nullptr;
    size_t count = mesh ? mesh->_calls.size() : 0;
    
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
        size_t instanceCount = call->instanceCount, baseInstance = call->baseInstance;
        BOOL drawIndexed = NO, debug = call->meshState.showTriangles || call->meshState.showPolygons;
        switch (call->type) {
            case kCallTypeMeshTriangles:
				[commandEncoder setRenderPipelineState:_meshTrianglePipelineState];
				[commandEncoder setVertexBuffer:_trisBuffer offset:0 atIndex:8];
				[commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                break;
            case kCallTypeMeshInteriorIndexed:
                debug = debug && _meshInteriorDebugPipelineState != nil;
                [commandEncoder setRenderPipelineState:debug ? _meshInteriorDebugPipelineState : _meshInteriorIndexedPipelineState];
                [commandEncoder setVertexBuffer:_interiorVertsBuffer offset:0 atIndex:8];
                [commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                if (debug)
                    [commandEncoder setFragmentBuffer:_interiorEdgesBuffer offset:0 atIndex:2];
                drawIndexed = YES;
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
	
        if (drawIndexed) {
            // Index buffer triangles are counter-clockwise in path space; a mirroring CTM flips them.
            [commandEncoder setCullMode:MTLCullModeBack];
            [commandEncoder setFrontFacingWinding:AffineTransformArea(call->state.CTM) < 0 ? MTLWindingClockwise : MTLWindingCounterClockwise];
            [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                       indexCount:instanceCount * 3
                                        indexType:MTLIndexTypeUInt32
                                      indexBuffer:_interiorIdxsBuffer
                                indexBufferOffset:baseInstance * 3 * sizeof(uint32_t)];
            [commandEncoder setCullMode:MTLCullModeNone];
        } else
            [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:baseInstance * 3 vertexCount:instanceCount * 3];
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
    if (mesh._tris.pagedSize())
        _trisBuffer = [self.device newBufferWithBytes:(void *)mesh._tris.get()
                                               length:mesh._tris.pagedSize()
                                              options:MTLResourceStorageModeShared];
    if (mesh._interiorVerts.pagedSize())
        _interiorVertsBuffer = [self.device newBufferWithBytes:(void *)mesh._interiorVerts.get()
                                                        length:mesh._interiorVerts.pagedSize()
                                                       options:MTLResourceStorageModeShared];
    if (mesh._interiorIdxs.pagedSize())
        _interiorIdxsBuffer = [self.device newBufferWithBytes:(void *)mesh._interiorIdxs.get()
                                                       length:mesh._interiorIdxs.pagedSize()
                                                      options:MTLResourceStorageModeShared];
    if (mesh._interiorEdges.pagedSize())
        _interiorEdgesBuffer = [self.device newBufferWithBytes:(void *)mesh._interiorEdges.get()
                                                        length:mesh._interiorEdges.pagedSize()
                                                       options:MTLResourceStorageModeShared];
    if (mesh._timeline.basePaths().size())
        _pathsBuffer = [self.device newBufferWithBytes:(void *)& mesh._timeline.basePaths()[0]
                                                length:mesh._timeline.basePaths().size() * sizeof(MeshPath)
                                               options:MTLResourceStorageModeManaged];
}

@end
