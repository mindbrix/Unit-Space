//
//  View.m
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import "View.h"
#import "MetalLayer.h"
#import "VGAffineTransform.h"
#import "Algorithms.hpp"
#import "Scene.hpp"
#import "libTess.hpp"
#import "Quadratics.hpp"

constexpr float kPointDeviceRadius = 4;
constexpr float kLargePointDeviceRadius = kPointDeviceRadius * 1.41;


using namespace std;

@interface View () <LayerDelegate>

@property(nonatomic) MetalLayer *metalLayer;
@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) CGPoint mouseLocation;
@property(nonatomic) uint64_t flags;
@property(nonatomic) BOOL showColors;
@property(nonatomic) BOOL showEdgeTriangles;
@property(nonatomic) BOOL showInterior;
@property(nonatomic) BOOL useQuadratics;
@property(nonatomic) BOOL showPolygons;
@property(nonatomic) BOOL showTriangles;
@property(nonatomic) BOOL showPoints;
@property(nonatomic) BOOL showVisibles;
@property(nonatomic) BOOL useDelaunay;
@property(nonatomic) BOOL useEvenOdd;
@property(nonatomic) BOOL useStrokeColor;
@property(nonatomic) BOOL showAccumulationsAllocations;
@property(nonatomic) size_t shapeIndex;
@property(nonatomic) vector<vector<Vector>> shapes;
@property(nonatomic) vector<SubPath::Type> shapeTypes;
@property(nonatomic) Mesh mesh;
@property(nonatomic) Scene scene;
@property(nonatomic) float f;
@property(nonatomic) int count;
@property(nonatomic) CVDisplayLinkRef displayLink;

- (void)timerFired;

@end

static CVReturn OnDisplayLinkFrame(CVDisplayLinkRef displayLink,
								   const CVTimeStamp *now,
								   const CVTimeStamp *outputTime,
								   CVOptionFlags flagsIn,
								   CVOptionFlags *flagsOut,
								   void *displayLinkContext) {
	View *view = (__bridge View *)displayLinkContext;
	@autoreleasepool {
		dispatch_async(dispatch_get_main_queue(), ^{
			[view timerFired];
		});
	}
	return kCVReturnSuccess;
}

@implementation View

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    
    [self setWantsLayer:YES];
    [self setLayer:[MetalLayer layer]];
    self.metalLayer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    self.metalLayer.layerDelegate = self;
    self.transform = [VGAffineTransform new];
    self.mouseLocation = { 0, 0 };
    self.showInterior = YES;
    self.showColors = YES;
    self.useEvenOdd = YES;
    self.f = 20;
    self.count = 1000;
    [self resetShapes];
    [self redraw];
    return self;
}

- (void)removeFromSuperview {
	[self stopTimer];
    [super removeFromSuperview];
}

#pragma mark - NSTimer

- (void)stopTimer {
	if (_displayLink) {
		CVDisplayLinkStop(_displayLink);
		CVDisplayLinkRelease(_displayLink);
		_displayLink = nil;
	}
}
	
- (void)resetTimer {
	[self stopTimer];
	CVReturn cvReturn = CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);
	cvReturn = CVDisplayLinkSetOutputCallback(_displayLink, &OnDisplayLinkFrame, (__bridge void *)self);
	cvReturn = CVDisplayLinkSetCurrentCGDisplay(_displayLink, CGMainDisplayID());
	CVDisplayLinkStart(_displayLink);
}

- (IBAction)toggleTimer:(id)sender {
	if (_displayLink)
		[self stopTimer];
	else
		[self resetTimer];
    [self redraw];
}

- (void)timerFired {
	[self redraw];
}

#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    self.window.acceptsMouseMovedEvents = YES;
    return YES;
}

- (void)magnifyWithEvent:(NSEvent *)event {
    [self.transform scaleBy:1.0f + event.magnification bounds:self.bounds];
    [self redraw];
}

- (void)mouseDown:(NSEvent *)event {}

- (void)mouseDragged:(NSEvent *)event {
    BOOL dragged = NO;
    if (_showPoints) {
        if (_svgData) {} else {
            dragged = dragged ?:[self mouseDragPoints:event points:_shapes[_shapeIndex]];
            if (dragged)
                [self triangulate];
        }
    }
    if (!dragged)
        [self.transform translateByX:event.deltaX andY:-event.deltaY];
    
    [self redraw];
}

- (BOOL)mouseDragPoints:(NSEvent *)event points:(vector<Vector>&)points {
	BOOL dragged = NO;
	
	for (int j = 0; j < points.size(); j++)
		if ((dragged = [self mouseDragVector:event CTM:points[j]]))
			break;
	
	return dragged;
}

- (BOOL)mouseDragCTM:(NSEvent *)event CTM:(AffineTransform &)CTM {
    float d0 = [self deviceDistance:event userX:CTM.tx userY:CTM.ty];
    float d1 = [self deviceDistance:event userX:CTM.tx + CTM.a userY:CTM.ty + CTM.b];
    float d2 = [self deviceDistance:event userX:CTM.tx + CTM.a + CTM.c userY:CTM.ty + CTM.b + CTM.d];
    
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    float dx = event.deltaX * self.layer.contentsScale, dy = -event.deltaY * self.layer.contentsScale;
    float ux = dx * inv.a + dy * inv.c, uy = dx * inv.b + dy * inv.d;
    
    if (d0 < kLargePointDeviceRadius) {
        CTM.tx += ux;
        CTM.ty += uy;
    } else if (d1 < kPointDeviceRadius) {
        CTM.a += ux;
        CTM.b += uy;
        CTM.c -= ux;
        CTM.d -= uy;
    } else if (d2 < kPointDeviceRadius) {
        CTM.c += ux;
        CTM.d += uy;
    } else
        return NO;
    
    return YES;
}

- (BOOL)mouseDragVector:(NSEvent *)event CTM:(Vector &)v {
    float d0 = [self deviceDistance:event userX:v.x userY:v.y];
    
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    float dx = event.deltaX * self.layer.contentsScale, dy = -event.deltaY * self.layer.contentsScale;
    float ux = dx * inv.a + dy * inv.c, uy = dx * inv.b + dy * inv.d;
    
    if (d0 < kLargePointDeviceRadius * self.layer.contentsScale) {
        v.x += ux;
        v.y += uy;
        return YES;
    }
    return NO;
}

- (void)mouseMoved:(NSEvent *)event {
    self.mouseLocation = event.locationInWindow;
    [self redraw];
}

- (void)rotateWithEvent:(NSEvent *)event {
    [self.transform rotateBy:event.rotation / 10.f bounds:self.bounds];
    [self redraw];
}

- (void)scrollWheel:(NSEvent *)event {
    BOOL isInverted = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]);
    CGFloat inversion = isInverted ? 1.0f : -1.0f;
    
    if ([event modifierFlags] & NSEventModifierFlagShift) {
        _count += event.deltaX;
        if (_count < 0)
            _count = 0;
        _f += event.deltaY * 0.1;
    } else
        [self.transform translateByX:event.deltaX * inversion andY:-event.deltaY * inversion];
    
    [self redraw];
}

#pragma mark - IBAction

- (IBAction)clockwise:(id)sender {
    [self.transform rotateSteppedBy:-M_PI / 12.0f bounds:self.bounds];
    [self redraw];
}
- (IBAction)anticlockwise:(id)sender {
    [self.transform rotateSteppedBy:M_PI / 12.0f bounds:self.bounds];
    [self redraw];
}

- (IBAction)resetModel:(id)sender {
    [self resetShapes];
    [self triangulate];
    [self redraw];
}

- (IBAction)cycleShapes:(id)sender {
    _shapeIndex++;
    if (_shapeIndex == _shapes.size())
        _shapeIndex = 0;
    
    [self resetShapes];
    [self triangulate];
    [self redraw];
}
- (IBAction)resetViewCTM:(id)sender {
    [self.transform reset];
    [self redraw];
}
- (IBAction)toggleOne:(id)sender {
    _flags &= 1;
    _flags ^= 1;
    [self redraw];
}

- (IBAction)toggleTwo:(id)sender {
    _flags &= 2;
    _flags ^= 2;
    [self redraw];
}

- (IBAction)toggleThree:(id)sender {
    _flags &= 4;
    _flags ^= 4;
    [self redraw];
}

- (IBAction)toggleFour:(id)sender {
    _flags &= 8;
    _flags ^= 8;
    [self redraw];
}

- (IBAction)toggleFive:(id)sender {
    _flags &= 16;
    _flags ^= 16;
    [self redraw];
}

- (IBAction)toggleEdges:(id)sender {
    _showEdgeTriangles = !_showEdgeTriangles;
    [self redraw];
}
- (IBAction)toggleInterior:(id)sender {
    _showInterior = !_showInterior;
    [self redraw];
}
- (IBAction)toggleShowPolygons:(id)sender{
    _showPolygons = !_showPolygons;
    _showTriangles = NO;
    [self redraw];
}
- (IBAction)toggleShowColors:(id)sender {
    _showColors = !_showColors;
    [self updateFromState];
    [self redraw];
}

- (IBAction)toggleUseQuadratics:(id)sender {
    _useQuadratics = !_useQuadratics;
    [self redraw];
}

- (IBAction)togglePoints:(id)sender {
    _showPoints = !_showPoints;
    [self redraw];
}

- (IBAction)toggleShowTriangles:(id)sender {
    _showPolygons = NO;
    _showTriangles = !_showTriangles;
    [self updateFromState];
    [self redraw];
}

- (IBAction)toggleRetina:(id)sender {
    if (self.metalLayer.contentsScale == [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width)
        self.metalLayer.contentsScale = 1;
    else
        self.metalLayer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    [self redraw];
}

- (IBAction)toggleUseDelaunay:(id)sender {
    _useDelaunay = !_useDelaunay;
    [self redraw];
}
- (IBAction)toggleUseMask:(id)sender {
    _useEvenOdd = !_useEvenOdd;
    [self redraw];
}
- (IBAction)toggleUseStrokeColor:(id)sender {
    _useStrokeColor = !_useStrokeColor;
    [self redraw];
}
- (IBAction)toggleShowVisibles:(id)sender {
    _showVisibles = !_showVisibles;
    [self redraw];
}
- (void)updateFromState {
    self.metalLayer.backgroundColor = CGColorGetConstantColor(_showColors ? kCGColorClear : kCGColorWhite);
}

#pragma mark -

- (void)resetShapes {
    _shapes =
    {
        {   { 0, 0 }, { 100, 100 }, { 0, 100 }                              },
        {   { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }, { 0, 0 }        },
        {   { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }                  },
        {   { 0, 0 }, { 50, 50 }, { 100, 100 }, { 50, 100 }, { 0, 100 }     },
        {   { 0, 0 }, { 100, 0 }, { 0, 100 }, { 100, 100 }                  },
        {   { 0, 0 }, { 100, 100 }, { 0, 100 }                              },
    };
    _shapeTypes =
    {
        SubPath::kLine,
        SubPath::kQuadratic,
        SubPath::kLine,
        SubPath::kQuadratic,
        SubPath::kLine,
        SubPath::kQuadratic,
    };
}

- (void)triangulate {
    if (_svgData == nil && _fontData == nil) {
        Path p(true, false, 0, RGBA8::Red(), { SubPath((float *)& _shapes[_shapeIndex][0], (int)_shapes[_shapeIndex].size(), _shapeTypes[_shapeIndex]) });
        _scene.empty();
        _scene.addPath(p);
    }
    libTess::triangulate(_mesh, _scene);
    Quadratics::writeScene(_mesh, _scene);
    [self.metalLayer uploadMesh:_mesh];
}

- (void)writeDrawCalls:(MeshPath *)paths {
    AffineTransform CTM = self.viewCTM;
    AffineTransform inv = AffineTransformInvert(CTM);
    float dx = self.mouseLocation.x * self.layer.contentsScale, dy = self.mouseLocation.y * self.layer.contentsScale;
    float ux = dx * inv.a + dy * inv.c + inv.tx, uy = dx * inv.b + dy * inv.d + inv.ty;
    
    float scale = std::sqrt(std::abs(AffineTransformArea(CTM)));
    float largeRadius = kLargePointDeviceRadius / scale * self.layer.contentsScale;
    float radius = kPointDeviceRadius / scale * self.layer.contentsScale;
    
    State uniforms;
    uniforms.CTM = CTM;
    uniforms.fillColor = { 0, 0, 0, 1 };
    uniforms.strokeColor = { 0, 0, 0, 1 };
    uniforms.dashLength = 0;
    uniforms.flatness = 0.03125;
    uniforms.width = 0;
    uniforms.cap = 1;
    uniforms.scaleFactor = (float)self.metalLayer.contentsScale;
    uniforms.evenOdd = _useEvenOdd;
    uniforms.roundCap = false;
    
    MeshState meshState;
    meshState.showEdgeTriangles = _showEdgeTriangles;
    meshState.showInterior = _showInterior;
    meshState.showColors = _showColors;
    meshState.showPolygons = _showPolygons;
    meshState.showTriangles = _showTriangles;
    meshState.showQuadratics = _flags & 1;
    meshState.showAccumulations = _flags & 2;
    meshState.quadraticStride = _flags & 2 ? 8 : 4;
    meshState.useQuadraticWinding = _useQuadratics;
    meshState.showVisibles = _showVisibles;
    meshState.useStrokeColor = _useStrokeColor;
    meshState.colorsSize = (int)_scene._paths.size();
    scale /= uniforms.scaleFactor;
    meshState.subdivisionCount = !_useQuadratics || scale <= 1 ? 1 : ceil(sqrt(scale));
	meshState.clipBounds = { 0, 0, (float)self.metalLayer.drawableSize.width, (float)self.metalLayer.drawableSize.height };
	meshState.cellSize = 1;
    
    _mesh._calls.resize(0);
    int p = -1;
    
    if (_showPoints && (_svgData || _fontData))
        for (int i = 0; _showPoints && i < _scene._paths.size(); i++) {
            AffineTransform inv = AffineTransformInvert(paths[i].ctm);
            if (_scene._paths[i].contains(ux * inv.a + uy * inv.c + inv.tx, ux * inv.b + uy * inv.d + inv.ty))
                p = i;
        }
    MeshPath *path = & paths[0];
    for (int i = 0; i < _scene._paths.size(); i++, path++)
		path->color = _showTriangles || _showPolygons ? RGBA8::Black() : i == p ? RGBA8::Red() : _showColors ? _scene._paths[i]._color : RGBA8::Black();
    
    if (_flags & 16) {
        int dimension = 330, r = 10, g = r * 2 / 0.618, count = dimension * dimension, idx;
        
        _mesh._shapesSOA.empty();
		short *types = _mesh._shapesSOA._types.new_ptr(count);
		float *as = _mesh._shapesSOA._as.new_ptr(count);
        float *bs = _mesh._shapesSOA._bs.new_ptr(count);
        float *cs = _mesh._shapesSOA._cs.new_ptr(count);
        float *ds = _mesh._shapesSOA._ds.new_ptr(count);
        float *txs = _mesh._shapesSOA._txs.new_ptr(count);
        float *tys = _mesh._shapesSOA._tys.new_ptr(count);
        RGBA8 *colors = _mesh._shapesSOA._colors.new_ptr(count);
        ShapeType pattern[8] = {
            kShapeTypeRect, kShapeTypeEllipse, kShapeTypeTriangle, kShapeTypeRoundedRect,
            kShapeTypeLozenge, kShapeTypeRoundedRect, kShapeTypeTriangle, kShapeTypeEllipse };
        memset_pattern16(types, pattern, count * sizeof(*types));
        for (idx = 0; idx < count; idx++)
            *colors++ = _showColors ? RGBA8(idx & 0xFF, idx >> 8, 0, 255) : RGBA8::Black();
        AffineTransform unit = { float(r) * 2, 0, 0, float(r) * 2, -float(r), -float(r) };
        memset_pattern4(as, & unit.a, count * sizeof(unit.a));
        memset_pattern4(bs, & unit.b, count * sizeof(unit.b));
        memset_pattern4(cs, & unit.c, count * sizeof(unit.c));
        memset_pattern4(ds, & unit.d, count * sizeof(unit.d));
        for (idx = 0; idx < count; idx++)
            *txs++ = (idx % dimension) * g - unit.tx;
        for (idx = 0; idx < count; idx++)
            *tys++ = idx / dimension * g - unit.ty;
		
		uniforms.CTM = AffineTransformConcat(paths[0].ctm, uniforms.CTM);
		Mesh::writeSOACalls(_mesh, uniforms, meshState, kCallTypeShapesSOA, count);
        return;
    }
    
    Bounds clipBounds = { 0, 0, float(std::ceil(self.metalLayer.bounds.size.width * self.metalLayer.contentsScale)), float(std::ceil(self.metalLayer.bounds.size.height * self.metalLayer.contentsScale)) };
    Mesh::writePathCalls(_mesh, paths, _scene._paths.size(), clipBounds, uniforms, meshState);
    
    if (_showPoints) {
        uniforms.fillColor = { 1, 0, 0, 1 };
        _mesh._shapesSOA.empty();
        
        if (_svgData == nullptr && _fontData == nullptr) {
			uniforms.CTM = AffineTransformConcat(paths[0].ctm, uniforms.CTM);
            vector<Vector>& shape = _shapes[_shapeIndex];
            SubPath::Type type = _shapeTypes[_shapeIndex];
            for (int j = 0; j < shape.size(); j++)
                _mesh._shapesSOA.addCircle(shape[j].x, shape[j].y, radius, type == SubPath::kLine || (j % 2 == 0) ? RGBA8::Red() : RGBA8::Blue());
        } else if (p != -1) {
            float pathScale = std::sqrt(std::abs(AffineTransformArea(paths[p].ctm)));
			uniforms.CTM = AffineTransformConcat(paths[p].ctm, uniforms.CTM);
			for (SubPath& s : _scene._paths[p]._subPaths) {
                float *points = s._quadratics.get(), r;
                int npoints = (int)s._quadratics.count() / 2;
                int stride = 2;
                for (int j = 0; j < npoints; j += stride) {
                    r = j == 0 ? largeRadius : j == stride ? (largeRadius + radius) * 0.5f : radius;
                    _mesh._shapesSOA.addCircle(points[j * 2], points[j * 2 + 1], r / pathScale, RGBA8::Red());
                }
            }
        }
        Mesh::writeSOACalls(_mesh, uniforms, meshState, kCallTypeShapesSOA, _mesh._shapesSOA._as.count());
    }
}

- (void)redraw {
    [self.metalLayer setNeedsDisplay];
}


- (float)deviceDistance:(NSEvent *)event userX:(float)x userY:(float)y {
    AffineTransform CTM = self.viewCTM;
    float ex = x * CTM.a + y * CTM.c + CTM.tx;
    float ey = x * CTM.b + y * CTM.d + CTM.ty;
    float lx = (event.locationInWindow.x - event.deltaX) * self.layer.contentsScale, ly = (event.locationInWindow.y + event.deltaY) * self.layer.contentsScale;
    float dx = ex - lx, dy = ey - ly;
    return std::sqrt(dx * dx + dy * dy);
}

- (BOOL)isPoint:(CGPoint)point inUnitEllipse:(AffineTransform)unitCTM {
    AffineTransform inv = AffineTransformInvert(AffineTransformConcat(unitCTM, self.viewCTM));
    float x = point.x * self.layer.contentsScale, y = point.y * self.layer.contentsScale;
    float ux = x * inv.a + y * inv.c + inv.tx, uy = x * inv.b + y * inv.d + inv.ty;
    float vx = ux - 0.5, vy = uy - 0.5;
    return vx * vx + vy * vy < 0.25;
}

#pragma mark - LayerDelegate

- (Mesh *)meshForLayer:(CALayer *)layer paths:(MeshPath *)paths {
    double timerTime = _displayLink ? [NSDate date].timeIntervalSinceReferenceDate : 0;
    _mesh._timeline.writePathsForTime(timerTime, paths);
    [self writeDrawCalls: paths];
    return & _mesh;
}

#pragma mark - Properties

-(MetalLayer *)metalLayer {
    return (MetalLayer *)self.layer;
}

- (void)setFontData:(NSData *)fontData {
    _fontData = fontData;
    
    if (_fontData) {
        _scene.empty();
        _scene.addFont(_fontData.bytes, _fontData.length);
    }
    _flags |= 8;
    [self triangulate];
    [self redraw];
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    
    if (_svgData) {
        _scene.empty();
        _scene.addSVG(_svgData.bytes, _svgData.length);
    }
    _flags |= 8;
    [self triangulate];
    [self redraw];
}

- (AffineTransform)viewCTM {
    CGAffineTransform viewCTM = self.transform.affineTransform;
    AffineTransform ctm = { float(viewCTM.a), float(viewCTM.b), float(viewCTM.c), float(viewCTM.d), float(viewCTM.tx), float(viewCTM.ty) };
    AffineTransform retina = { float(self.layer.contentsScale), 0, 0, float(self.layer.contentsScale), 0, 0 };
    AffineTransform skew = { 1, 0, 0, 1, 0, 0 };
    return AffineTransformConcat(skew, AffineTransformConcat(ctm, retina));
}

@end
