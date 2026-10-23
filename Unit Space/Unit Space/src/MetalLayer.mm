//
//  MetalLayer.m
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <Metal/Metal.h>
#import "MetalLayer.h"

// One mesh's buffers. The layer draws several meshes, each call naming its own, over the one paths buffer.
@interface MeshBuffers : NSObject
@property (nonatomic, strong) id <MTLBuffer> trisBuffer;
@property (nonatomic, strong) id <MTLBuffer> triangleInstancesBuffer;
@property (nonatomic, strong) id <MTLBuffer> interiorInstancesBuffer;
@property (nonatomic, strong) id <MTLBuffer> interiorVertsBuffer;
@property (nonatomic, strong) id <MTLBuffer> interiorEdgesBuffer;
@end

@implementation MeshBuffers
@end

@interface MetalLayer ()

@property (nonatomic, strong) dispatch_semaphore_t inflight_semaphore;
@property (nonatomic, strong) id <MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id <MTLLibrary> defaultLibrary;
@property (nonatomic, strong) id <MTLBuffer> pathsBuffer;
@property (nonatomic, strong) NSArray<MeshBuffers *> *meshes;
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
    self.meshes = @[ [MeshBuffers new], [MeshBuffers new], [MeshBuffers new] ];
    
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
    
    // Drawn even with no calls, so the pass clears the screen rather than leaving the last frame up.
    id <CAMetalDrawable> drawable = [self nextDrawable];
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
        BOOL interior = NO, debug = call->meshState.showTriangles || call->meshState.showPolygons;
        MeshBuffers *buffers = _meshes[call->mesh];
        if (instanceCount == 0 || buffers.trisBuffer == nil)
            continue;
        switch (call->type) {
            case kCallTypeMeshTriangles:
				[commandEncoder setRenderPipelineState:_meshTrianglePipelineState];
				[commandEncoder setVertexBuffer:buffers.trisBuffer offset:0 atIndex:8];
				[commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
				[commandEncoder setVertexBuffer:buffers.triangleInstancesBuffer offset:0 atIndex:10];
                break;
            case kCallTypeMeshInteriorIndexed:
                if (buffers.interiorVertsBuffer == nil)
                    continue;
                debug = debug && _meshInteriorDebugPipelineState != nil;
                [commandEncoder setRenderPipelineState:debug ? _meshInteriorDebugPipelineState : _meshInteriorIndexedPipelineState];
                [commandEncoder setVertexBuffer:buffers.interiorVertsBuffer offset:0 atIndex:8];
                [commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                [commandEncoder setVertexBuffer:buffers.interiorInstancesBuffer offset:0 atIndex:10];
                if (debug) {
                    [commandEncoder setFragmentBuffer:buffers.interiorEdgesBuffer offset:0 atIndex:2];
                    [commandEncoder setFragmentBuffer:buffers.interiorInstancesBuffer offset:0 atIndex:3];
                }
                interior = YES;
                break;
        }
		
		State state = call->state;
		state.drawableSize = { float(drawable.texture.width), float(drawable.texture.height) };
		[commandEncoder setVertexBytes:& state length:sizeof(State) atIndex:0];
		[commandEncoder setVertexBytes:& call->meshState length:sizeof(MeshState) atIndex:1];
		[commandEncoder setFragmentBytes:& state length:sizeof(State) atIndex:0];
		[commandEncoder setFragmentBytes:& call->meshState length:sizeof(MeshState) atIndex:1];
	
        if (interior) {
            // Interior triangles are counter-clockwise in path space; a mirroring CTM flips them.
            [commandEncoder setCullMode:MTLCullModeBack];
            [commandEncoder setFrontFacingWinding:AffineTransformArea(call->state.CTM) < 0 ? MTLWindingClockwise : MTLWindingCounterClockwise];
            [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:baseInstance * 3 vertexCount:instanceCount * 3];
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
    [self uploadMesh:mesh index:0];
    _pathsBuffer = mesh._timeline.basePaths().size() == 0 ? nil : [self.device newBufferWithBytes:(void *)& mesh._timeline.basePaths()[0]
                                                                                           length:mesh._timeline.basePaths().size() * sizeof(MeshPath)
                                                                                          options:MTLResourceStorageModeManaged];
}

// A buffer that is the mesh's own page-aligned memory, not a copy of it: it keeps a reference to the memory until Metal
// releases it, so the mesh can be emptied or triangulated again while frames that use it are in flight.
template<typename T>
static id<MTLBuffer> newBuffer(id<MTLDevice> device, Memory<T, true>& memory) {
    if (memory.pagedSize() == 0)
        return nil;
    Memory<T, true> kept = memory;
    return [device newBufferWithBytesNoCopy:memory.get() length:memory.pagedSize() options:MTLResourceStorageModeShared
                                deallocator:^(void *, NSUInteger) { (void)kept; }];
}

- (void)uploadMesh:(Mesh &)mesh index:(NSUInteger)index {
    MeshBuffers *buffers = _meshes[index];
    buffers.trisBuffer = newBuffer(self.device, mesh._tris);
    buffers.triangleInstancesBuffer = newBuffer(self.device, mesh._triangleInstances);
    buffers.interiorInstancesBuffer = newBuffer(self.device, mesh._interiorInstances);
    buffers.interiorVertsBuffer = newBuffer(self.device, mesh._interiorVerts);
    buffers.interiorEdgesBuffer = newBuffer(self.device, mesh._interiorEdges);
}

- (void)reservePaths:(size_t)count {
    size_t length = count * sizeof(MeshPath);
    if (length <= _pathsBuffer.length)
        return;
    id <MTLBuffer> paths = [self.device newBufferWithLength:length options:MTLResourceStorageModeManaged];
    if (_pathsBuffer)
        memcpy(paths.contents, _pathsBuffer.contents, _pathsBuffer.length);
    _pathsBuffer = paths;
}

@end
