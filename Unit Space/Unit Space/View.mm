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

#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"

#import "Tess.hpp"
#import "libTess.hpp"
#import "Grid.hpp"
#import "Revolution.hpp"
#import "Fortune.hpp"

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
@property(nonatomic) vector<Call> calls;
@property(nonatomic) uint64_t flags;
@property(nonatomic) uint64_t pathBitmask;
@property(nonatomic) BOOL showCurves;
@property(nonatomic) BOOL showColors;
@property(nonatomic) BOOL showEdges;
@property(nonatomic) BOOL showEllipse;
@property(nonatomic) BOOL showInterior;
@property(nonatomic) BOOL showPoints;
@property(nonatomic) BOOL showTriangle;
@property(nonatomic) BOOL useDelaunay;
@property(nonatomic) BOOL useMask;
@property(nonatomic) AffineTransform unitCTM;
@property(nonatomic) AffineTransform ctm0;
@property(nonatomic) AffineTransform ctm1;
@property(nonatomic) AffineTransform ctm2;
@property(nonatomic) AffineTransform ctm3;
@property(nonatomic) Quadratics quadratics;
@property(nonatomic) vector<Vector> triangle;
@property(nonatomic) vector<Vector> lozenge;
@property(nonatomic) vector<Vector> quad;
@property(nonatomic) Mesh mesh;
@property(nonatomic) Scene scene;
@property(nonatomic) libTess libtess;
@property(nonatomic) Tess tess;
@property(nonatomic) Fortune fortune;
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
    [self redrawWithMouseLocation:event.locationInWindow];
}

- (void)mouseDown:(NSEvent *)event {
//    NSLog(@"%@", event.locationInWindow);
}

- (void)mouseDragged:(NSEvent *)event {
    BOOL dragged = NO;
    if (_showPoints) {
        if (_flags & 4) {
            AffineTransform ctm0 = AffineTransformConcat(_unitCTM, _ctm0);
            if ((dragged = [self mouseDragCTM:event CTM:ctm0]))
                _ctm0 = AffineTransformConcat(AffineTransformInvert(_unitCTM), ctm0);
            else
                dragged = [self mouseDragCTM:event CTM:_unitCTM];
        } else if (_flags & 2)  {
            for (int j = 0; j < _quadratics.count; j++)
                if ((dragged = [self mouseDragCTM:event CTM:_quadratics.ctms[j]]))
                    break;
        } else if (_flags & 8)  {
            if (_svgData) {
                
            } else {
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
    [self redrawWithMouseLocation:event.locationInWindow];
}

- (void)rotateWithEvent:(NSEvent *)event {
    [self.transform rotateBy:event.rotation / 10.f bounds:self.bounds];
    [self redrawWithMouseLocation:event.locationInWindow];
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
    
    [self redrawWithMouseLocation:event.locationInWindow];
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

- (IBAction)toggleTriangle:(id)sender {
    _showTriangle = !_showTriangle;
//    [self reset];
    [self redraw];
}

- (IBAction)toggleUseDelaunay:(id)sender {
    _useDelaunay = !_useDelaunay;
    [self reset];
    [self redraw];
}
- (IBAction)toggleUseMask:(id)sender {
    _useMask = !_useMask;
    [self reset];
    [self redraw];
}

- (void)updateFromState {
    self.metalLayer.backgroundColor = CGColorGetConstantColor(_showInterior ? kCGColorClear : _showColors ? kCGColorBlack : kCGColorWhite);
}

#pragma mark -

- (void)redrawWithMouseLocation:(CGPoint)location {
//    BOOL inside = [self isPoint:location inUnitEllipse:_calls[0].units.units[0].unitCTM];
//    _calls[0].state.fillColor.r = inside ? 1 : 0;
    [self redraw];
}

- (void)reset {
    _unitCTM = { 100, 0, 0, 100, 0, 0 };
    
    AffineTransform s = { 0.9, 0, 0, 0.9, 0, 0 };
    float a0 =- M_PI / 24.f, cos0 = cosf(a0), sin0 = sinf(a0);
    AffineTransform r = { cos0, sin0, -sin0, cos0, 0, 0 };
    AffineTransform t = { 1, 0, 0, 1, 100, 100 };
    _ctm0 = AffineTransformConcat(r, AffineTransformConcat(s, t));
    float a1 =- M_PI / 12.f, cos1 = cosf(a1), sin1 = sinf(a1);
    AffineTransform r1 = { cos1, sin1, -sin1, cos1, 0, 0 };
    _ctm1 = r1;// AffineTransformConcat(s, r);
    _ctm2 =  { 1, 0, 0, 1, 2000, 0 };
    _ctm3 =  { 1, 0, 0, 1, -20000, 2000 };
    
    Vector lozenge[] = {
        { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }, { 0, 0 }
    };
    
    Vector triangle[] = {
        { 0, 0 }, { 50, 50 }, { 100, 100 }, { 50, 100 }, { 0, 100 }, { 0, 50 }, { 0, 0 }
    };
    
    Vector *q = !_showTriangle ? triangle : lozenge;
    int count = !_showTriangle ?  sizeof(triangle) / sizeof(triangle[0]) :  sizeof(lozenge) / sizeof(lozenge[0]);
    
    AffineTransform identity = { 1, 0, 0, 1, 0, 0 };
    _quadratics.count = WriteQuadratics(& _quadratics.ctms[0], identity, q, count);
    
    if (_svgData) {
        if (self.useDelaunay)
//            Revolution::triangulate(_mesh, _scene);
//            _fortune.triangulate(_mesh, scene);
            _tess.triangulate(_mesh, _scene, self.useMask, self.useDelaunay);
        else
            _libtess.triangulate(_mesh, _scene, self.useMask, self.useDelaunay);
        
        [self.metalLayer uploadMesh:_mesh];
    } else {
        _triangle = {
            { 0, 0 }, { 100, 100 }, { 0, 100 }
        };
        
        _lozenge = {
            { 0, 0 }, { 66.666, 0 }, { 100, 33.333 }, { 100, 100 }, { 33.333, 100 }, { 0, 66.666 }, { 0, 0 }
        };

		_quad = {
            { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }
		};
		
        [self triangulate];
    }
}

- (void)triangulate {
    RGBA8 red(255, 0, 0, 255);
    RGBA8 green(0, 255, 0, 255);
    RGBA8 blue(0, 0, 255, 255);
    RGBA8 black(0, 0, 0, 255);
    RGBA8 alpha(255, 0, 0, 128);
    
    SubPath triangle((float *)& _triangle[0], (int)_triangle.size(), SubPath::kLine);
    SubPath lozenge((float *)& _lozenge[0], (int)_lozenge.size(), SubPath::kCubic);
    SubPath quad((float *)& _quad[0], (int)_quad.size(), SubPath::kLine);
    
    Path p0(false, red);
    p0.addSubPath(triangle);
    Path p1(true, red);
    p1.addSubPath(lozenge);
    Path p2(false, black);
	p2.addSubPath(quad);
	
    Scene scene;
    if (self.pathBitmask & kPathBitmaskTriangle)
        scene.addPath(p0);
    
    if (self.pathBitmask & kPathBitmaskLozenge)
        scene.addPath(p1);
    
    if (self.pathBitmask & kPathBitmaskQuad)
        scene.addPath(p2);
    
    
    if (self.useDelaunay)
//        Revolution::triangulate(_mesh, scene);
//        _fortune.triangulate(_mesh, scene);
        _tess.triangulate(_mesh, scene, self.useMask, self.useDelaunay);
    else
        _libtess.triangulate(_mesh, scene, self.useMask, self.useDelaunay);
    
    [self.metalLayer uploadMesh:_mesh];
}

- (void)update {
    AffineTransform CTM = self.viewCTM;
    
    float scale = std::sqrt(std::abs(CTM.a * CTM.d - CTM.b * CTM.c));
    float largeRadius = kLargePointDeviceRadius / scale;
    float radius = kPointDeviceRadius / scale;
    
    State uniforms;
    uniforms.CTM = CTM;
    uniforms.strokeColor = { 1, 0, 0, 1 };
    uniforms.flatness = 0.03125;
    uniforms.width = 0.5;
    uniforms.cap = 8;
    
    _calls.resize(0);
    
    if (_flags & 1) {
        uniforms.fillColor = { 0, 0, 1, 1 };
        
        _calls.emplace_back();
        Call& call = _calls.back();
        
        call.type = kCallTypeUnits;
        call.state = uniforms;
        call.units.count = 1;
        call.units.ctms[0] = _unitCTM;
    }
    
    if (_flags & 2) {
        uniforms.fillColor = { 0, 0, 0, 1 };
        
        _calls.emplace_back();
        Call& call = _calls.back();
        
        call.type = kCallTypeQuadratics;
        call.state = uniforms;
        call.quadratics = _quadratics;
        
        if (_showPoints) {
            _calls.emplace_back();
            Call& call = _calls.back();
            
            uniforms.fillColor = { 1, 0, 0, 1 };
            call.type = kCallTypeCircles;
            call.state = uniforms;
            Circles& circles = call.circles;
            
            int i = 0;
            for (int j = 0; j < _quadratics.count; j++) {
                AffineTransform unit = _quadratics.ctms[j];
                
                circles.circles[i++] = { unit.tx, unit.ty, largeRadius };
                circles.circles[i++] = { unit.tx + unit.a, unit.ty + unit.b, radius };
                circles.circles[i++] = { unit.tx + unit.a + unit.c, unit.ty + unit.b + unit.d, radius };
            }
            circles.count = i;
        }
    }
    
    if (_flags & 4) {
        uniforms.fillColor = { 0, 0, 0, 0.333 };
        _calls.emplace_back();
        Call& call = _calls.back();
        call.type = kCallTypeFractal;
        call.state = uniforms;
        Fractals& fractals = call.fractals;
        fractals.unitCTM = _unitCTM;
        
        fractals.ctms[0] = _ctm0;
        fractals.ctms[1] = _ctm1;
        fractals.ctms[2] = _ctm2;
        fractals.ctms[3] = _ctm3;
        
        fractals.counts[0] = 60;
        fractals.counts[1] = 24;
        fractals.counts[2] = 1;
        fractals.counts[3] = 1;
        fractals.count = fractals.counts[0] * fractals.counts[1] * fractals.counts[2] * fractals.counts[3];
        fractals.ellipse = _showEllipse;
        
        if (_showPoints) {
            uniforms.fillColor = { 1, 0, 0, 1 };
            _calls.emplace_back();
            Call& call = _calls.back();
            call.type = kCallTypeCircles;
            call.state = uniforms;
            Circles& circles = call.circles;
            AffineTransform& unit = _unitCTM;
            
            int i = 0;
            circles.circles[i++] = { unit.tx, unit.ty, largeRadius };
            circles.circles[i++] = { unit.tx + unit.a, unit.ty + unit.b, radius };
            circles.circles[i++] = { unit.tx + unit.a + unit.c, unit.ty + unit.b + unit.d, radius };
            
            AffineTransform ctm0 = AffineTransformConcat(_unitCTM, _ctm0);
            circles.circles[i++] = { ctm0.tx, ctm0.ty, largeRadius };
            circles.circles[i++] = { ctm0.tx + ctm0.a, ctm0.ty + ctm0.b, radius };
            circles.circles[i++] = { ctm0.tx + ctm0.a + ctm0.c, ctm0.ty + ctm0.b + ctm0.d, radius };
            
            circles.count = i;
        }
    }
    
    if (_flags & 8) {
        uniforms.fillColor = { 0, 0, 0, 1 };
        
        MeshState meshState;
        meshState.hideCurves = !_showCurves;
        meshState.hideTriangles = !_showTriangle;
        meshState.showColors = _showColors;
        meshState.showInterior = _showInterior;
        
        if (_mesh._triangles.size()) {
            _calls.emplace_back();
            Call& call = _calls.back();
            call.meshState = meshState;
            call.type = _showEdges ? kCallTypeMeshInterior : kCallTypeMeshTriangles;
            call.state = uniforms;
            call.state.CTM = AffineTransformConcat(_mesh._CTM, call.state.CTM);
        }
        
        if (_mesh._concaveCurves.size()) {
            _calls.emplace_back();
            Call& call = _calls.back();
            call.meshState = meshState;
            call.type = kCallTypeMeshConcaveCurves;
            call.state = uniforms;
        }
        
        if (_mesh._curves.size()) {
            _calls.emplace_back();
            Call& call = _calls.back();
            call.meshState = meshState;
            call.type = kCallTypeMeshCurves;
            call.state = uniforms;
        }
        
        if (_showPoints) {
            uniforms.fillColor = { 1, 0, 0, 1 };
            _calls.emplace_back();
            Call& call = _calls.back();
            call.type = kCallTypeCircles;
            call.state = uniforms;
            Circles& circles = call.circles;
            
            int i = 0;
            if (_svgData) {
                
            } else {
                if (self.pathBitmask & kPathBitmaskTriangle)
                    for (int j = 0; j < _triangle.size() && j < kCirclesSize / 3; j++)
                        circles.circles[i++] = { _triangle[j].x, _triangle[j].y, radius };

                if (self.pathBitmask & kPathBitmaskLozenge)
                    for (int j = 0; j < _lozenge.size() && j < kCirclesSize / 3; j++)
                        circles.circles[i++] = { _lozenge[j].x, _lozenge[j].y, radius };
				
                if (self.pathBitmask & kPathBitmaskQuad)
                    for (int j = 0; j < _quad.size() && j < kCirclesSize / 3; j++)
                        circles.circles[i++] = { _quad[j].x, _quad[j].y, radius };
            }
            circles.count = i;
        }
    }
    if (_flags & 16) {
        uniforms.fillColor = { 0, 0, 0, 1 };
        
        MeshState meshState;
        meshState.hideCurves = !_showCurves;
        meshState.hideTriangles = !_showTriangle;
        meshState.showColors = _showColors;
        meshState.showInterior = _showInterior;
        
        _calls.emplace_back();
        Call& call = _calls.back();
        call.meshState = meshState;
        call.type = _showEdges ? kCallTypeMeshInterior : kCallTypeMeshTriangles;
        call.state = uniforms;
        
        _mesh.empty();
        float cx = self.bounds.size.width / 2, cy = self.bounds.size.height / 2, r = cx < cy ? cx : cy, tmul = 1.0 / float(_count - 1), t;
        float a;
        for (int i = 0; i < _count; i++) {
            t = float(i) * tmul;
            a = t * _f * 2 * M_PI;
            _mesh._points.emplace_back(cx + r * t * cosf(a), cy + r * t * sinf(a));
        }
        _fortune.triangulate(_mesh, & _mesh._points[0].x, (int)_mesh._points.size());
        _mesh._colors.resize(_mesh._triangles.size());
        
        Vector v0, v1, v2, *points = & _mesh._points[0];
        float l0, l1, l2, lmax;
        for (auto& tr : _mesh._triangles) {
            v0 = { points[tr.i0].x - cx, points[tr.i0].y - cy};
            v1 = { points[tr.i1].x - cx, points[tr.i1].y - cy};
            v2 = { points[tr.i2].x - cx, points[tr.i2].y - cy};
            l0 = v0.x * v0.x + v0.y * v0.y;
            l1 = v1.x * v1.x + v1.y * v1.y;
            l2 = v2.x * v2.x + v2.y * v2.y;
            lmax = l0 > l1 ? l0 : l1;
            lmax = l2 > lmax ? l2 : lmax;
            t = std::sqrt(lmax / (r * r));
//            t = int(t * _f * _mesh._triangles.size()) % 2;
            t = t * _f, t = t - std::floor(t);
            RGBA8 color(RGBA::hsv2rgb(t, 1, 1));
//            RGBA8 color(255 * t, 255 * t, 255 * t, 255);
            _mesh._colors[t * _mesh._triangles.size()] = color;
            tr.c = t * _mesh._triangles.size();
        }
        [self.metalLayer uploadMesh:_mesh];
    }
}

- (void)redraw {
    [self update];
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

- (Call *)drawCallsWithCount:(size_t *)count forLayer:(CALayer *)layer {
    *count = _calls.size();
    return & _calls[0];
}

#pragma mark - Properties

-(MetalLayer *)metalLayer {
    return (MetalLayer *)self.layer;
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    NSMutableData *data = nil;
    
    if (_svgData) {
        data = [NSMutableData data];
        unsigned char zeroByte = 0;
        [data appendData:_svgData];
        [data appendBytes:&zeroByte length:1];
    }
    
    struct NSVGimage* image = data ? nsvgParse((char *)data.bytes, "px", 96) : NULL;
    
    if (image) {
        printf("size: %f x %f\n", image->width, image->height);
        // Use...
        
        _scene.empty();
        int limit = 60000;
        for (NSVGshape *shape = image->shapes; shape != NULL && limit; shape = shape->next) {
            if (shape->fill.type == NSVG_PAINT_COLOR /*&& shape->stroke.type == NSVG_PAINT_NONE*/) {
                limit--;
                
                RGBA8 color = RGBA8((unsigned char)((shape->fill.color ) & 0xFF), (unsigned char)((shape->fill.color >> 8) & 0xFF), (unsigned char)((shape->fill.color >> 16) & 0xFF), (unsigned char)(shape->fill.color >> 24));
                Path p(shape->fillRule == NSVG_FILLRULE_EVENODD, color);
                
                for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
                    for (int i = 0; i < path->npts; i++)
                        path->pts[i * 2 + 1] = image->height - path->pts[i * 2 + 1];
                    SubPath s(path->pts, path->npts, SubPath::kCubic);
                    p.addSubPath(s);
                }
                _scene.addPath(p);
            }
        }
        // Delete
        nsvgDelete(image);
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
