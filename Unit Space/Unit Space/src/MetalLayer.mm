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
@property (nonatomic, strong) id <MTLBuffer> pathsBuffer;
@property (nonatomic, strong) id <MTLBuffer> pointsBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> arcPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> circlePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> flagPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> lineAOSPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorBlendPipelineState;
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
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"circle_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"circle_fragment_main"];
    descriptor.label = @"Circle";
    self.circlePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"lineAOS_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"line_fragment_main"];
    descriptor.label = @"lineAOSPipelineState";
    self.lineAOSPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"triangle_fragment_main"];
    descriptor.label = @"Triangle";
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
    Mesh *mesh = nullptr;
    if ([self.layerDelegate respondsToSelector:@selector(drawCallsWithCount:forLayer:fromMesh:)])
        calls = [self.layerDelegate drawCallsWithCount:&count forLayer:self fromMesh:& mesh];
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
                [commandEncoder setVertexBytes:&call->arcs length:sizeof(Arcs) atIndex:1];
                instanceCount = std::ceil(1.0 / call->state.flatness) * call->arcs.count;
                break;
            case kCallTypeCircles:
                [commandEncoder setRenderPipelineState:_circlePipelineState];
                [commandEncoder setVertexBytes:&call->circles length:sizeof(Circles) atIndex:1];
                instanceCount = call->circles.count;
                break;
            case kCallTypeFlags:
                [commandEncoder setRenderPipelineState:_flagPipelineState];
                [commandEncoder setVertexBytes:&call->flags length:sizeof(Flags) atIndex:1];
                instanceCount = call->instanceCount;
                break;
           case kCallTypeMeshInterior:
            case kCallTypeMeshInteriorBlend:
            case kCallTypeMeshTriangles:
                if (call->meshState.hideTriangles)
                    continue;
                [commandEncoder setRenderPipelineState:call->type == kCallTypeMeshTriangles ? _meshTrianglePipelineState : call->type == kCallTypeMeshInterior ? _meshInteriorPipelineState : _meshInteriorBlendPipelineState];
                [commandEncoder setVertexBytes:&call->meshState length:sizeof(MeshState) atIndex:1];
                [commandEncoder setVertexBuffer:_pathsBuffer offset:0 atIndex:9];
                [commandEncoder setVertexBuffer:_pointsBuffer offset:0 atIndex:10];
                instanceCount = call->instanceCount, baseInstance = call->baseInstance;
                drawQuads = NO;
                break;
            case kCallTypeLinesAOS:
                [commandEncoder setRenderPipelineState:_lineAOSPipelineState];
                [commandEncoder setVertexBytes:& mesh->_xs[call->baseInstance] length:sizeof(float) * (call->instanceCount + 1) atIndex:10];
                [commandEncoder setVertexBytes:& mesh->_ys[call->baseInstance] length:sizeof(float) * (call->instanceCount + 1) atIndex:11];
                [commandEncoder setVertexBytes:& mesh->_bridges[call->baseInstance] length:sizeof(char) * (call->instanceCount + 1) atIndex:13];
                instanceCount = call->instanceCount, baseInstance = 0;
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
                                    instanceCount:instanceCount
                                       baseVertex:0
                                     baseInstance:baseInstance
             ];
        else
            [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                               vertexStart:0
                               vertexCount:3
                             instanceCount:instanceCount
                              baseInstance:baseInstance];
    }
    [commandEncoder endEncoding];

    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

#pragma mark - Public

- (void)renderMesh:(Mesh &)mesh calls:(vector<Call> &)calls meshState:(MeshState)meshState state:(State)state showPolygons:(BOOL)showPolygons {
    int base, end, count, p, i, j;
    AffineTransform ctm;
    Bounds clipBounds = { 0, 0, float(std::ceil(self.bounds.size.width * self.contentsScale)), float(std::ceil(self.bounds.size.height * self.contentsScale)) };
    Call *call;
    if (mesh._points.size()) {
        bool opaques[mesh._paths.size()], visibles[mesh._paths.size()];
        for (p = 0; p < mesh._paths.size(); p++) {
            ctm = AffineTransformMakeUnit(mesh._bounds[p], AffineTransformConcat(mesh._paths[p].ctm, state.CTM));
            opaques[p] = ctm.a * ctm.a + ctm.b * ctm.b < 400 || ctm.c * ctm.c + ctm.d * ctm.d < 400 || mesh._paths[p].color.a == 255;
        }
        for (p = 0; p < mesh._paths.size(); p += 8) {
            bool set = false;
            for (j = 0; !set && j < 8 && p + j < mesh._paths.size(); j++) {
                ctm = AffineTransformMakeUnit(mesh._bounds[p + j], AffineTransformConcat(mesh._paths[p + j].ctm, state.CTM));
                set |= Bounds::transformUnit(ctm).intersects(clipBounds);
            }
            for (j = 0; j < 8 && p + j < mesh._paths.size(); j++)
                visibles[p + j] = set;
        }
        if (showPolygons) {
            int i, end, count, size = (int)mesh._xs.size();
            for (i = 0; i < size; i += 1023) {
                end = i + 1024;
                end = end < size ? end : size;
                count = end - i;
                calls.emplace_back(kCallTypeLinesAOS, count - 1, i, state, meshState);
            }
            
//            for (call = nullptr, p = 0; p < mesh._paths.size(); p++) {
//                if (!visibles[p])
//                    call = nullptr;
//                else {
//                    base = mesh._paths[p].edge;
//                    end = p + 1 == mesh._paths.size() ? (int)mesh._edges.size() : mesh._paths[p + 1].edge;
//                    count = end - base;
//                    if (call == nullptr) {
//                        calls.emplace_back(kCallTypeHairlines, 0, base, state, meshState);
//                        call = & calls.back();
//                    }
//                    call->instanceCount += count;
//                }
//            }
        } else {
            call = nullptr;
            for (end = (int)mesh._points.size() / kMeshTriangleSize, p = (int)mesh._paths.size() - 1; p >= 0; p--, end = base) {
                base = mesh._paths[p].idx;
                if (visibles[p] && opaques[p]) {
                    count = end - base;
                    if (call == nullptr || call->baseInstance != end) {
                        calls.emplace_back(kCallTypeMeshInterior, 0, base, state, meshState);
                        call = & calls.back();
                        call->meshState.reverseCount = end;
                    }
                    call->baseInstance = base;
                    call->instanceCount += count;
                }
            }
            
            if (!meshState.showEdges) {
                for (call = nullptr, p = 0; p < mesh._paths.size(); p++) {
                    if (!visibles[p]) {
                        call = nullptr;
                        continue;
                    }
                    base = mesh._paths[p].idx;
                    end = p + 1 == mesh._paths.size() ? (int)mesh._points.size() / kMeshTriangleSize : mesh._paths[p + 1].idx;
                    count = end - base;
                    if (opaques[p]) {
                        if (call == nullptr) {
                            calls.emplace_back(kCallTypeMeshTriangles, 0, base, state, meshState);
                            call = & calls.back();
                        }
                        call->instanceCount += count;
                    } else {
                        calls.emplace_back(kCallTypeMeshInteriorBlend, count, base, state, meshState);
                        calls.back().meshState.reverseCount = end;
                        calls.emplace_back(kCallTypeMeshTriangles, count, base, state, meshState);
                        call = nullptr;
                    }
                }
            }
        }
        
        if (meshState.showVisibles) {
            calls.emplace_back(kCallTypeFlags, (int)mesh._paths.size(), 0, state, meshState);
            call = & calls.back();
            call->flags.w = call->flags.h = 64;
            for (p = 0; p < mesh._paths.size(); p++) {
                i = p >> 5, j = p % 32;
                if (j == 0)
                    call->flags.flags[i] = 0;
                if (visibles[p])
                    call->flags.flags[i] |= (1 << j);
            }
        }
    }
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
