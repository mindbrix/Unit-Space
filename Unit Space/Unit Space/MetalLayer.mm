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
@property (nonatomic, strong) id <MTLBuffer> ptsBuffer;
@property (nonatomic, strong) id <MTLBuffer> pathCTMsBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> unitPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> arcPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> circlePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> flagPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> fractalPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> linePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshUnitPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorBlendPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshTrianglePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> quadraticPipelineState;
@property (nonatomic, strong) id <MTLDepthStencilState> depthState;
@property (nonatomic, strong) id <MTLDepthStencilState> noDepthState;
@property (nonatomic, strong) id <MTLTexture> depthTexture;
@property (nonatomic, strong) id <MTLTexture> multisampleTexture;

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
    
    depthStencilDescriptor.depthWriteEnabled = NO;
    depthStencilDescriptor.depthCompareFunction = MTLCompareFunctionAlways;
    self.noDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.colorAttachments[0].pixelFormat = self.pixelFormat;
    descriptor.colorAttachments[0].blendingEnabled = NO;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"unit_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"unit_fragment_main"];
    descriptor.label = @"Unit";
    self.unitPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
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
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"triangle_fragment_main"];
    descriptor.label = @"Triangle";
    self.meshTrianglePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_unit_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"mesh_unit_fragment_main"];
    descriptor.label = @"meshBoundsGridPipelineState";
    self.meshUnitPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
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
            case kCallTypeMeshInterior:
            case kCallTypeMeshInteriorBlend:
            case kCallTypeMeshTriangles:
                if (call->meshState.hideTriangles)
                    continue;
                [commandEncoder setRenderPipelineState:call->type == kCallTypeMeshTriangles ? _meshTrianglePipelineState : call->type == kCallTypeMeshInterior ? _meshInteriorPipelineState : _meshInteriorBlendPipelineState];
                [commandEncoder setVertexBytes:&call->meshState length:sizeof(MeshState) atIndex:1];
                [commandEncoder setVertexBuffer:_colorsBuffer offset:0 atIndex:2];
                [commandEncoder setVertexBuffer:_pathCTMsBuffer offset:0 atIndex:7];
                [commandEncoder setVertexBuffer:_ptsBuffer offset:0 atIndex:10];
                instanceCount = call->instanceCount, baseInstance = call->baseInstance;
                drawQuads = NO;
                break;
            case kCallTypeMeshUnits:
                [commandEncoder setRenderPipelineState:_meshUnitPipelineState];
                [commandEncoder setVertexBytes:&call->boundsGrid length:sizeof(BoundsGrid) atIndex:1];
                [commandEncoder setFragmentBytes:&call->boundsGrid length:sizeof(BoundsGrid) atIndex:1];
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

- (void)renderMesh:(Mesh &)mesh calls:(vector<Call> &)calls meshState:(MeshState)meshState uniforms:(State)uniforms {
    meshState.colorsSize = (int)mesh._colors.size();
    int base, end, count, p, i, j;
    AffineTransform ctm;
    Bounds bounds, clipBounds = { 0, 0, float(std::ceil(self.bounds.size.width * self.contentsScale)), float(std::ceil(self.bounds.size.height * self.contentsScale)) };
    float w, h, x0, y0, x1, y1, l0, l1;
    Call tmpl, *call;
    tmpl.meshState = meshState;
    tmpl.state = uniforms;
    tmpl.instanceCount = tmpl.meshState.reverseCount = tmpl.baseInstance = 0;
    if (mesh._pts.size()) {
        bool opaques[mesh._colors.size()], visibles[mesh._colors.size()];
        for (p = 0; p < mesh._colors.size(); p++) {
            ctm = AffineTransformConcat(mesh._pathCTMs[p], uniforms.CTM);
            bounds = mesh._bounds[p];
            if (meshState.showVisibles)
                visibles[p] = bounds.transformed(ctm).intersects(clipBounds);
            else
                visibles[p] = mesh._colors[p].a == 255 || bounds.transformed(ctm).intersects(clipBounds);
            w = bounds.ux - bounds.lx, h = bounds.uy - bounds.ly;
            x0 = w * ctm.a, y0 = w * ctm.b;
            x1 = h * ctm.c, y1 = h * ctm.d;
            l0 = x0 * x0 + y0 * y0, l1 = x1 * x1 + y1 * y1;
            opaques[p] = l0 < 400 || l1 < 400 || mesh._colors[p].a == 255;
        }
        call = nullptr;
//        for (end = (int)mesh._pts.size() / kMeshTriangleSize, p = (int)mesh._colors.size() - 1; p >= 0; p--, end = base) {
//            base = mesh._triangleIndices[p];
//            if (visibles[p] && opaques[p]) {
//                count = end - base;
//                if (call == nullptr || call->baseInstance != end) {
//                    calls.emplace_back(tmpl);
//                    call = & calls.back();
//                    call->type = kCallTypeMeshInterior;
//                    call->baseInstance = base;
//                    call->meshState.reverseCount = end;
//                }
//                call->baseInstance = base;
//                call->instanceCount += count;
//            }
//        }
        
        if (!meshState.showEdges) {
            call = nullptr;
            for (p = 0; p < mesh._colors.size(); p++) {
                if (!visibles[p]) {
                    call = nullptr;
                    continue;
                }
                base = mesh._triangleIndices[p];
                end = p + 1 == mesh._colors.size() ? (int)mesh._pts.size() / kMeshTriangleSize : mesh._triangleIndices[p + 1];
                count = end - base;
                if (opaques[p]) {
                    if (call == nullptr) {
                        calls.emplace_back(tmpl);
                        call = & calls.back();
                        call->type = kCallTypeMeshTriangles;
                        call->baseInstance = base;
                    }
                    call->instanceCount += count;
                } else {
                    calls.emplace_back(tmpl);
                    call = & calls.back();
                    call->type = kCallTypeMeshInteriorBlend;
                    call->baseInstance = base;
                    call->instanceCount = count;
                    call->meshState.reverseCount = end;
                    
                    calls.emplace_back(tmpl);
                    call = & calls.back();
                    call->type = kCallTypeMeshTriangles;
                    call->baseInstance = base;
                    call->instanceCount = count;
                    call = nullptr;
                }
            }
        }
        if (meshState.showVisibles) {
            calls.emplace_back();
            call = & calls.back();
            call->type = kCallTypeFlags;
            call->state = uniforms;
            call->instanceCount = (int)mesh._colors.size();
            call->flags.w = call->flags.h = 64;
            
            for (p = 0; p < mesh._colors.size(); p++) {
                i = p >> 5, j = p % 32;
                if (j == 0)
                    call->flags.flags[i] = 0;
                if (visibles[p])
                    call->flags.flags[i] |= (1 << j);
            }
        }
        calls.emplace_back();
        call = & calls.back();
        call->type = kCallTypeMeshUnits;
        call->state = uniforms;
        call->instanceCount = 1;
        call->state.fillColor = { 0, 0, 0, 1 };
        call->state.CTM = AffineTransformConcat(mesh._pathCTMs[0], call->state.CTM);
        call->state.CTM = AffineTransformMakeUnit(mesh._bounds[0], call->state.CTM);
        BoundsGrid& grid = call->boundsGrid;
        grid.bounds = { 0, 0, 1, 1 };
        memset(grid.x, 0, sizeof(grid.x));
        memset(grid.y, 0, sizeof(grid.y));
        p = 0;
        base = mesh._triangleIndices[p];
        end = p + 1 == mesh._colors.size() ? (int)mesh._pts.size() / kMeshTriangleSize : mesh._triangleIndices[p + 1];
        
        float w = mesh._bounds[p].ux - mesh._bounds[p].lx, h = mesh._bounds[p].uy - mesh._bounds[p].ly;
        float x, y, lx, ly, ux, uy;
        unsigned int idx, i, mask;
        for (idx = base; idx < end && idx - base < 32; idx++) {
            lx = ly = FLT_MAX, ux = uy = -FLT_MAX;
            for (i = 0; i < 3; i++) {
                x = mesh._pts[idx * kMeshTriangleSize + 1 + i * 2].x, y = mesh._pts[idx * kMeshTriangleSize + 1 + i * 2].y;
                x = (x - mesh._bounds[p].lx) / w, y = (y - mesh._bounds[p].ly) / h;
                lx = lx < x ? lx : x, ly = ly < y ? ly : y, ux = ux > x ? ux : x, uy = uy > y ? uy : y;
                grid.points[idx * 3 + i] = { x, y };
            }
            mask = 1 << (idx - base);
            grid.x[int(lx * kBoundsGridDimension)] |= mask, grid.x[int(ux * kBoundsGridDimension)] |= mask;
            grid.y[int(ly * kBoundsGridDimension)] |= mask, grid.y[int(uy * kBoundsGridDimension)] |= mask;
        }
        for (i = mask = 0; i <= kBoundsGridDimension; i++)
            mask ^= grid.x[i], grid.x[i] = mask;
        for (i = mask = 0; i <= kBoundsGridDimension; i++)
            mask ^= grid.y[i], grid.y[i] = mask;
    }
    [self syncMesh:mesh];
}
- (void)syncMesh:(Mesh &)mesh {
    memcpy(_colorsBuffer.contents, & mesh._colors[0], _colorsBuffer.length);
    [_colorsBuffer didModifyRange:NSMakeRange(0, _colorsBuffer.length)];
    memcpy(_pathCTMsBuffer.contents, & mesh._pathCTMs[0], _pathCTMsBuffer.length);
    [_pathCTMsBuffer didModifyRange:NSMakeRange(0, _pathCTMsBuffer.length)];
}

- (void)uploadMesh:(Mesh &)mesh {
    if (mesh._colors.size())
        _colorsBuffer = [self.device newBufferWithBytes:(void *)& mesh._colors[0]
                                                 length:mesh._colors.size() * sizeof(RGBA8)
                                                options:MTLResourceStorageModeManaged];
    if (mesh._pts.size())
        _ptsBuffer = [self.device newBufferWithBytes:(void *)& mesh._pts[0]
                                              length:mesh._pts.size() * sizeof(Vector)
                                             options:MTLResourceStorageModeShared];
    if (mesh._pathCTMs.size())
        _pathCTMsBuffer = [self.device newBufferWithBytes:(void *)& mesh._pathCTMs[0]
                                                   length:mesh._pathCTMs.size() * sizeof(AffineTransform)
                                                  options:MTLResourceStorageModeManaged];
}

@end
