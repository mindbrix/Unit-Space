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
@property (nonatomic, strong) id <MTLBuffer> interiorScalesBuffer;
@property (nonatomic, strong) id <MTLBuffer> interiorVectorsBuffer;
@property (nonatomic, strong) id <MTLBuffer> pathCTMsBuffer;
@property (nonatomic, strong) id <MTLRenderPipelineState> unitPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> arcPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> circlePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> fractalPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> linePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshCurvePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshConcaveCurvePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshInteriorBlendPipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> meshTrianglePipelineState;
@property (nonatomic, strong) id <MTLRenderPipelineState> quadraticPipelineState;
@property (nonatomic, strong) id <MTLDepthStencilState> depthState;
@property (nonatomic, strong) id <MTLDepthStencilState> noDepthState;
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
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"mesh_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"curve_fragment_main"];
    descriptor.label = @"Curve";
    self.meshCurvePipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
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
                [commandEncoder setVertexBuffer:_pathCTMsBuffer offset:0 atIndex:7];
                instanceCount = call->instanceCount, baseInstance = call->baseInstance;
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
                [commandEncoder setVertexBuffer:_pathCTMsBuffer offset:0 atIndex:7];
                [commandEncoder setDepthStencilState:_noDepthState];
                instanceCount = call->instanceCount, baseInstance = call->baseInstance;
                drawQuads = NO;
                break;
            case kCallTypeMeshInterior:
            case kCallTypeMeshInteriorBlend:
            case kCallTypeMeshTriangles:
                if (call->meshState.hideTriangles)
                    continue;
                
                [commandEncoder setRenderPipelineState:call->type == kCallTypeMeshTriangles ? _meshTrianglePipelineState : call->type == kCallTypeMeshInterior ? _meshInteriorPipelineState : _meshInteriorBlendPipelineState];
                [commandEncoder setVertexBytes:&call->meshState length:sizeof(MeshState) atIndex:1];
                [commandEncoder setVertexBuffer:_colorsBuffer offset:0 atIndex:2];
                [commandEncoder setVertexBuffer:_pointsBuffer offset:0 atIndex:3];
                [commandEncoder setVertexBuffer:_trianglesBuffer offset:0 atIndex:4];
                [commandEncoder setVertexBuffer:_interiorScalesBuffer offset:0 atIndex:5];
                [commandEncoder setVertexBuffer:_interiorVectorsBuffer offset:0 atIndex:6];
                [commandEncoder setVertexBuffer:_pathCTMsBuffer offset:0 atIndex:7];
                instanceCount = call->instanceCount, baseInstance = call->baseInstance;
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
                              baseInstance:baseInstance];
        [commandEncoder setDepthStencilState:_depthState];
    }
    
    [commandEncoder endEncoding];
    
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

#pragma mark - Public

- (void)renderMesh:(Mesh &)mesh calls:(vector<Call> &)calls meshState:(MeshState)meshState uniforms:(State)uniforms {
    meshState.colorsSize = (int)mesh._colors.size();
    int base, end, count, p;
    AffineTransform ctm;
    Bounds bounds;
    float w, h, x0, y0, x1, y1, l0, l1;
    Call tmpl, *call;
    tmpl.meshState = meshState;
    tmpl.state = uniforms;
    tmpl.instanceCount = tmpl.meshState.reverseCount = tmpl.baseInstance = 0;
    if (mesh._triangles.size()) {
        bool interiors[mesh._colors.size()];
        memset(interiors, 0, sizeof(interiors));
        for (p = 0; p < mesh._colors.size(); p++) {
            ctm = AffineTransformConcat(mesh._pathCTMs[p], uniforms.CTM);
            bounds = mesh._bounds[p];
            w = bounds.ux - bounds.lx, h = bounds.uy - bounds.ly;
            x0 = w * ctm.a, y0 = w * ctm.b;
            x1 = h * ctm.c, y1 = h * ctm.d;
            l0 = x0 * x0 + y0 * y0, l1 = x1 * x1 + y1 * y1;
            if (mesh._colors[p].a == 255)
                interiors[p] = true;
//            if (l0 > 400 && l1 > 400 && mesh._colors[p].a == 255)
//                interiors[p] = true;
        }
        call = nullptr;
        for (end = (int)mesh._triangles.size(), p = (int)mesh._colors.size() - 1; p >= 0; p--, end = base) {
            base = mesh._triangleIndices[p];
            if (interiors[p]) {
                count = end - base;
                if (call == nullptr || call->baseInstance != end) {
                    calls.emplace_back(tmpl);
                    call = & calls.back();
                    call->type = kCallTypeMeshInterior;
                    call->baseInstance = base;
                    call->meshState.reverseCount = end;
                }
                call->baseInstance = base;
                call->instanceCount += count;
            }
        }
        
        if (!meshState.showEdges) {
            call = nullptr;
            for (p = 0; p < mesh._colors.size(); p++) {
                base = mesh._triangleIndices[p];
                end = p + 1 == mesh._colors.size() ? (int)mesh._triangles.size() : mesh._triangleIndices[p + 1];
                count = end - base;
                if (mesh._colors[p].a == 255) {
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
                    
                    calls.emplace_back(tmpl);
                    call = & calls.back();
                    call->type = kCallTypeMeshTriangles;
                    call->baseInstance = base;
                    call->instanceCount = count;
                    call = nullptr;
                }
            }
        }
    }
    if (mesh._concaveCurves.size()) {
        calls.emplace_back(tmpl);
        Call& call = calls.back();
        call.type = kCallTypeMeshConcaveCurves;
        call.instanceCount = (int)_concaveCurvesBuffer.length / sizeof(Triangle);
    }
    if (mesh._curves.size()) {
        calls.emplace_back(tmpl);
        Call& call = calls.back();
        call.type = kCallTypeMeshCurves;
        call.instanceCount = (int)_curvesBuffer.length / sizeof(Triangle);
    }
    memcpy(_colorsBuffer.contents, & mesh._colors[0], _colorsBuffer.length);
    [_colorsBuffer didModifyRange:NSMakeRange(0, _colorsBuffer.length)];
    memcpy(_pathCTMsBuffer.contents, & mesh._pathCTMs[0], _pathCTMsBuffer.length);
    [_pathCTMsBuffer didModifyRange:NSMakeRange(0, _pathCTMsBuffer.length)];
}

- (void)uploadMesh:(Mesh &)mesh {
    _colorsBuffer = [self.device newBufferWithBytes:(void *)& mesh._colors[0]
                                             length:mesh._colors.size() * sizeof(RGBA8)
                                            options:MTLResourceStorageModeManaged];
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
    if (mesh._interiorScales.size())
        _interiorScalesBuffer = [self.device newBufferWithBytes:(void *)& mesh._interiorScales[0]
                                                          length:mesh._interiorScales.size() * sizeof(float)
                                                         options:MTLResourceStorageModeShared];
    if (mesh._interiorVectors.size())
        _interiorVectorsBuffer = [self.device newBufferWithBytes:(void *)& mesh._interiorVectors[0]
                                                          length:mesh._interiorVectors.size() * sizeof(Vector)
                                                         options:MTLResourceStorageModeShared];
    if (mesh._pathCTMs.size())
        _pathCTMsBuffer = [self.device newBufferWithBytes:(void *)& mesh._pathCTMs[0]
                                                          length:mesh._pathCTMs.size() * sizeof(AffineTransform)
                                                         options:MTLResourceStorageModeManaged];
}

@end
