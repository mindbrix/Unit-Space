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
#import "Grid.hpp"

constexpr float kPointDeviceRadius = 8;
constexpr float kLargePointDeviceRadius = kPointDeviceRadius * 1.41;

constexpr uint64_t kPathBitmaskTriangle = 1 << 0;
constexpr uint64_t kPathBitmaskLozenge = 1 << 1;
constexpr uint64_t kPathBitmaskQuad = 1 << 2;
constexpr uint64_t kPathBitmaskAll = kPathBitmaskTriangle | kPathBitmaskLozenge | kPathBitmaskQuad;

using namespace std;

@interface View () <LayerDelegate>

@property(nonatomic) MetalLayer *metalLayer;
@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) CGPoint mouseLocation;
@property(nonatomic) uint64_t flags;
@property(nonatomic) uint64_t pathBitmask;
@property(nonatomic) BOOL showCurves;
@property(nonatomic) BOOL showColors;
@property(nonatomic) BOOL showEdges;
@property(nonatomic) BOOL showEllipse;
@property(nonatomic) BOOL showInterior;
@property(nonatomic) BOOL showPoints;
@property(nonatomic) BOOL showTriangle;
@property(nonatomic) BOOL showVisibles;
@property(nonatomic) BOOL useDelaunay;
@property(nonatomic) BOOL useMask;
@property(nonatomic) BOOL useStrokeColor;
@property(nonatomic) vector<Vector> triangle;
@property(nonatomic) vector<Vector> lozenge;
@property(nonatomic) vector<Vector> quad;
@property(nonatomic) Mesh mesh;
@property(nonatomic) Scene scene;
@property(nonatomic) float f;
@property(nonatomic) int count;

@end

@implementation View

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    
    [self reset];
    
    [self setWantsLayer:YES];
    [self setLayer:[MetalLayer layer]];
    self.metalLayer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    self.metalLayer.layerDelegate = self;
    self.transform = [VGAffineTransform new];
    self.mouseLocation = { 0, 0 };
    self.showColors = YES;
    self.showInterior = YES;
    self.showTriangle = YES;
    self.useMask = NO;
    self.pathBitmask = kPathBitmaskTriangle;
    
    self.f = 20;
    self.count = 1000;
    
    [self redraw];

    return self;
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
            if (self.pathBitmask & kPathBitmaskTriangle)
                dragged = dragged ?:[self mouseDragPoints:event points:_triangle];
            
            if (self.pathBitmask & kPathBitmaskLozenge)
                dragged = dragged ?:[self mouseDragPoints:event points:_lozenge];
            
            if (self.pathBitmask & kPathBitmaskQuad)
                dragged = dragged ?:[self mouseDragPoints:event points:_quad];
            
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
    
    if (d0 < kLargePointDeviceRadius) {
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
    [self reset];
    [self redraw];
}

- (IBAction)shiftPathBitmask:(id)sender {
    _pathBitmask = _pathBitmask << 1;
    if ((_pathBitmask & kPathBitmaskAll) == 0)
        _pathBitmask = 1;
    
    [self reset];
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
    _showEdges = !_showEdges;
    [self redraw];
}
- (IBAction)toggleCurves:(id)sender {
    _showCurves = !_showCurves;
    [self redraw];
}
- (IBAction)toggleShowColors:(id)sender {
    _showColors = !_showColors;
    [self updateFromState];
    [self redraw];
}

- (IBAction)toggleEllipse:(id)sender {
    _showEllipse = !_showEllipse;
    [self redraw];
}

- (IBAction)togglePoints:(id)sender {
    _showPoints = !_showPoints;
    [self redraw];
}

- (IBAction)toggleShowInterior:(id)sender {
    _showInterior = !_showInterior;
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
    _useMask = !_useMask;
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

- (void)reset {
    if (_svgData || _fontData) {
        libTess::triangulate(_mesh, _scene, self.useMask);
        [self.metalLayer uploadMesh:_mesh];
    } else {
        _triangle = { { 0, 0 }, { 100, 100 }, { 0, 100 } };
        _lozenge = {  { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }, { 0, 0 } };
		_quad = { { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 } };
        [self triangulate];
    }
}

- (void)triangulate {
    RGBA8 red(255, 0, 0, 255);
    RGBA8 green(0, 255, 0, 255);
    RGBA8 blue(0, 0, 255, 255);
    RGBA8 black(0, 0, 0, 255);
    RGBA8 alpha(255, 0, 0, 128);
    
    Path p0(false, red, { SubPath((float *)& _triangle[0], (int)_triangle.size(), SubPath::kLine) });
    Path p1(true, red, { SubPath((float *)& _lozenge[0], (int)_lozenge.size(), SubPath::kQuadratic) });
    Path p2(false, red, { SubPath((float *)& _quad[0], (int)_quad.size(), SubPath::kLine) });
	
    _scene.empty();
    if (self.pathBitmask & kPathBitmaskTriangle)
        _scene.addPath(p0);
    
    if (self.pathBitmask & kPathBitmaskLozenge)
        _scene.addPath(p1);
    
    if (self.pathBitmask & kPathBitmaskQuad)
        _scene.addPath(p2);
    
    libTess::triangulate(_mesh, _scene, self.useMask);
    [self.metalLayer uploadMesh:_mesh];
}

- (void)writeDrawCalls {
    AffineTransform CTM = self.viewCTM;
    CGPoint user = CGPointApplyAffineTransform(self.mouseLocation, CGAffineTransformInvert(self.transform.affineTransform));
    
    float scale = std::sqrt(std::abs(CTM.a * CTM.d - CTM.b * CTM.c));
    float largeRadius = kLargePointDeviceRadius / scale;
    float radius = kPointDeviceRadius / scale;
    
    State uniforms;
    uniforms.CTM = CTM;
    uniforms.fillColor = { 0, 0, 0, 1 };
    uniforms.strokeColor = { 0, 0, 0, 1 };
    uniforms.flatness = 0.03125;
    uniforms.width = 1;
    uniforms.cap = 8;
    uniforms.scaleFactor = (float)self.metalLayer.contentsScale;
    
    MeshState meshState;
    meshState.disableInteriorVector = false;
    meshState.hideCurves = !_showCurves;
    meshState.hideTriangles = !_showTriangle;
    meshState.showEdges = _showEdges;
    meshState.showColors = _showColors;
    meshState.showInterior = _showInterior;
    meshState.showPolygons = false;
    meshState.showVisibles = _showVisibles;
    meshState.useStrokeColor = _useStrokeColor;
    meshState.colorsSize = (int)_mesh._paths.size();
    scale /= uniforms.scaleFactor;
    meshState.subdivisionCount = !_useDelaunay || scale <= 1 ? 1 : ceil(sqrt(scale));
    
    _mesh._calls.resize(0);
    int p = -1;
    
    float ux = float(user.x), uy = float(user.y);
    for (int i = 0; _showPoints && i < _scene._paths.size(); i++) {
        AffineTransform inv = AffineTransformInvert(_mesh._paths[i].ctm);
        if (_scene._paths[i].contains(ux * inv.a + uy * inv.c + inv.tx, ux * inv.b + uy * inv.d + inv.ty))
            p = i;
    }
    for (int i = 0; i < _scene._paths.size(); i++)
        _mesh._paths[i].color = i == p ? RGBA8(255, 0, 0, 255) : _showColors ? _scene._paths[i]._color : RGBA8(0, 0, 0, 255);
    
    if (_flags & 1)
        meshState.showPolygons = true;
    if (_flags & 2) {
        int dimension = 330, r = 10, g = 24, count = dimension * dimension, idx;
        float *dst;
        _mesh._circles._xs = Memory<float>(count);
        _mesh._circles._ys = Memory<float>(count);
        _mesh._circles._rs = Memory<float>(count);
        _mesh._circles._colors = Memory<RGBA8>(count);
        
        for (dst = &_mesh._circles._xs[0], idx = 0; idx < count; idx++)
            *dst++ = (idx % dimension) * g;
        for (dst = &_mesh._circles._ys[0], idx = 0; idx < count; idx++)
            *dst++ = idx / dimension * g;
        for (dst = &_mesh._circles._rs[0], idx = 0; idx < count; idx++)
            *dst++ = r;
        for (idx = 0; idx < count; idx++)
            _mesh._circles._colors[idx] = RGBA8(idx & 0xFF, idx & 0xFF, idx & 0xFF, 255);
        
        Mesh::writeCircleCalls(_mesh, uniforms, meshState);
        return;
    }
    if (_flags & 4) {}
    if (_flags & 8) {}
    if (_flags & 16) {}
    
    [self.metalLayer writePathCalls:_mesh state:uniforms meshState:meshState];
    
    if (_showPoints) {
        uniforms.fillColor = { 1, 0, 0, 1 };
        _mesh._circles.empty();
        _mesh._interiors.empty();
        
        Bounds clipBounds = { 0, 0, float(std::ceil(self.metalLayer.bounds.size.width * self.metalLayer.contentsScale)), float(std::ceil(self.metalLayer.bounds.size.height * self.metalLayer.contentsScale)) };
        for (int i = 0; i < _scene._paths.size(); i++)
            Grid::writePathCells(_mesh, _scene._paths[i], AffineTransformConcat(_mesh._paths[i].ctm, CTM), clipBounds);
        uniforms.CTM = { 1, 0, 0, 1, 0, 1 };
        
//        if (_svgData == nullptr && _fontData  == nullptr) {
//            if (self.pathBitmask & kPathBitmaskTriangle)
//                for (int j = 0; j < _triangle.size(); j++)
//                    _mesh._circles.add(_triangle[j].x, _triangle[j].y, radius);
//
//            if (self.pathBitmask & kPathBitmaskLozenge)
//                for (int j = 0; j < _lozenge.size(); j++)
//                    _mesh._circles.add(_lozenge[j].x, _lozenge[j].y, radius);
//
//            if (self.pathBitmask & kPathBitmaskQuad)
//                for (int j = 0; j < _quad.size(); j++)
//                    _mesh._circles.add(_quad[j].x, _quad[j].y, radius);
//        } else if (p != -1) {
//            AffineTransform m = _mesh._paths[p].ctm;
//            float scale = 1.f / std::sqrt(std::abs(m.a * m.d - m.b * m.d));
//            uniforms.CTM = AffineTransformConcat(m, CTM);
//            for (SubPath& s : _scene._paths[p]._subPaths) {
//                float *points = s._quadratics.get();
//                int npoints = (int)s._quadratics.count() / 2;
//                int stride = 2;
//                for (int j = 0; j < npoints; j += stride)
//                    _mesh._circles.add(points[j * 2], points[j * 2 + 1], j == 0 ? largeRadius * scale : j == stride ? (largeRadius + radius) * scale * 0.5f : radius * scale);
//            }
//        }
        Mesh::writeCircleCalls(_mesh, uniforms, meshState);
    }
}

- (void)redraw {
    [self writeDrawCalls];
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

- (Mesh *)meshForLayer:(CALayer *)layer {
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
    [self reset];
    [self redraw];
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    
    if (_svgData) {
        _scene.empty();
        _scene.addSVG(_svgData.bytes, _svgData.length);
    }
    _flags |= 8;
    [self reset];
    [self redraw];
}

- (AffineTransform)viewCTM {
    CGAffineTransform viewCTM = self.transform.affineTransform;
    
    AffineTransform retina = { float(self.layer.contentsScale), 0, 0, float(self.layer.contentsScale), 0, 0 };
    AffineTransform ctm = {
        float(viewCTM.a), float(viewCTM.b),
        float(viewCTM.c), float(viewCTM.d),
        float(viewCTM.tx), float(viewCTM.ty) };
    
    return AffineTransformConcat(ctm, retina);
}

@end
