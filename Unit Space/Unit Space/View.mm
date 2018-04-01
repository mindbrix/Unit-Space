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
#import <vector>

#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"

#import "tesselator.h"

constexpr float kPointDeviceRadius = 8;
constexpr float kLargePointDeviceRadius = kPointDeviceRadius * 1.41;

using namespace std;

@interface View () <LayerDelegate>

@property(nonatomic) MetalLayer *metalLayer;
@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) vector<Call> calls;
@property(nonatomic) uint64_t flags;
@property(nonatomic) BOOL showEllipse;
@property(nonatomic) BOOL showPoints;
@property(nonatomic) BOOL showTriangle;
@property(nonatomic) AffineTransform unitCTM;
@property(nonatomic) AffineTransform ctm0;
@property(nonatomic) AffineTransform ctm1;
@property(nonatomic) AffineTransform ctm2;
@property(nonatomic) AffineTransform ctm3;
@property(nonatomic) Quadratics quadratics;
@property(nonatomic) Triangles triangles;
@property(nonatomic) Triangles2 triangles2;

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
            for (int j = 0; j < _triangles.count; j++)
                if ((dragged = [self mouseDragCTM:event CTM:_triangles.ctms[j]]))
                    break;
        }
    }
    if (!dragged)
        [self.transform translateByX:event.deltaX andY:-event.deltaY];
    
    [self redraw];
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

- (IBAction)toggleEllipse:(id)sender {
    _showEllipse = !_showEllipse;
    [self redraw];
}

- (IBAction)togglePoints:(id)sender {
    _showPoints = !_showPoints;
    [self redraw];
}

- (IBAction)toggleTriangle:(id)sender {
    _showTriangle = !_showTriangle;
    [self reset];
    [self redraw];
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
    
    Vector *q = _showTriangle ? triangle : lozenge;
    int count = _showTriangle ?  sizeof(triangle) / sizeof(triangle[0]) :  sizeof(lozenge) / sizeof(lozenge[0]);
    
    AffineTransform identity = { 1, 0, 0, 1, 0, 0 };
    _quadratics.count = WriteQuadratics(& _quadratics.ctms[0], identity, q, count);
    
    // Load
    NSString *path = [[NSBundle mainBundle] pathForResource:@"nano.svg" ofType:nil];
    
    struct NSVGimage* image = nsvgParseFromFile([path UTF8String], "px", 96);
    if (image) {
        printf("size: %f x %f\n", image->width, image->height);
        // Use...
        
        int k = 0, pi = 0;
        _triangles2.colors[0] = { 0, 0, 0, 0.333 };
        
        for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
            for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
                for (int i = 0; i < path->npts; i++)
                    path->pts[i * 2 + 1] = image->height - path->pts[i * 2 + 1];
                static const TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
                constexpr int kPolySize = 6;
                TESStesselator *tess = tessNewTess(NULL);
                tessAddContour(tess, 2, path->pts, sizeof(float) * 2, (int)path->npts, false);
                
                if (tessTesselate(tess, shape->fillRule == NSVG_FILLRULE_EVENODD ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                    float *points = (float *)tessGetVertices(tess);
                    int npoints = tessGetVertexCount(tess);
                    
                    if (pi + npoints < kTrianglesPointsSize) {
                        memcpy(& _triangles2.points[pi], points, sizeof(Vector) * npoints);
                        
                        const int nelems = tessGetElementCount(tess);
                        auto polygon = (TESSindex *)tessGetElements(tess);
                        
                        for (int i = 0; i < nelems; i++, polygon += kPolySize) {
                            for (int j = 0; j < kPolySize - 2 && polygon[j + 2] != TESS_UNDEF; j++) {
                                _triangles2.add(k++, polygon[0] + pi, polygon[j + 1] + pi, polygon[j + 2] + pi, 0);
                            }
                        }
                        pi += npoints;
                    }
                }
                tessDeleteTess(tess);
            }
        }
        _triangles2.count = k;
        // Delete
        nsvgDelete(image);
    } else {
        _triangles2.colors[0] = { 1, 0, 0, 1 };
        _triangles2.colors[1] = { 0, 1, 0, 1 };
        _triangles2.colors[2] = { 0, 0, 1, 1 };
        _triangles2.colors[3] = { 0, 0, 0, 1 };
        
        _triangles2.points[0] = { 0, 0 };
        _triangles2.points[1] = { 100, 0 };
        _triangles2.points[2] = { 100, 100 };
        _triangles2.points[3] = { 0, 100 };
        _triangles2.points[4] = { 200, 0 };
        _triangles2.points[5] = { 200, 100 };
        
        _triangles2.add(0, 0, 1, 2, 0);
        _triangles2.add(1, 2, 3, 0, 1);
        _triangles2.add(2, 2, 1, 4, 2);
        _triangles2.add(3, 4, 5, 2, 3);
        
        _triangles2.count = 4;
    }
}

- (void)update {
    AffineTransform CTM = self.viewCTM;
    
    float scale = std::sqrt(std::abs(CTM.a * CTM.d - CTM.b * CTM.c));
    float largeRadius = kLargePointDeviceRadius / scale;
    float radius = kPointDeviceRadius / scale;
    
    State uniforms;
    uniforms.strokeColor = { 1, 0, 0, 1 };
    uniforms.flatness = 0.03125;
    uniforms.width = 0.5;
    uniforms.cap = 8;
    
    _calls.resize(10);
    int idx = 0;
    
    if (_flags & 1) {
        uniforms.fillColor = { 0, 0, 1, 1 };
        _calls[idx].type = kCallTypeUnits;
        _calls[idx].state = uniforms;
        _calls[idx].units.count = 1;
        _calls[idx].units.ctms[0] = _unitCTM;
        idx++;
    }
    
    if (_flags & 2) {
        uniforms.fillColor = { 0, 0, 0, 1 };
        _calls[idx].type = kCallTypeQuadratics;
        _calls[idx].state = uniforms;
        _calls[idx].quadratics = _quadratics;
        idx++;
        
        if (_showPoints) {
            uniforms.fillColor = { 1, 0, 0, 1 };
            _calls[idx].type = kCallTypeCircles;
            _calls[idx].state = uniforms;
            Circles& circles = _calls[idx].circles;
            
            int i = 0;
            for (int j = 0; j < _quadratics.count; j++) {
                AffineTransform unit = _quadratics.ctms[j];
                
                circles.circles[i++] = { unit.tx, unit.ty, largeRadius };
                circles.circles[i++] = { unit.tx + unit.a, unit.ty + unit.b, radius };
                circles.circles[i++] = { unit.tx + unit.a + unit.c, unit.ty + unit.b + unit.d, radius };
            }
            circles.count = i;
            idx++;
        }
    }
    
    if (_flags & 4) {
        uniforms.fillColor = { 0, 0, 0, 0.333 };
        _calls[idx].type = kCallTypeFractal;
        _calls[idx].state = uniforms;
        Fractals& fractals = _calls[idx].fractals;
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
        idx++;
        
        if (_showPoints) {
            uniforms.fillColor = { 1, 0, 0, 1 };
            _calls[idx].type = kCallTypeCircles;
            _calls[idx].state = uniforms;
            Circles& circles = _calls[idx].circles;
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
            idx++;
        }
    }
    
    if (_flags & 8) {
        uniforms.fillColor = { 0, 0, 0, 1 };
        _calls[idx].type = kCallTypeTriangles;
        _calls[idx].state = uniforms;
        _calls[idx].triangles = _triangles;
        _calls[idx].triangles2 = _triangles2;
        idx++;
        
        if (_showPoints) {
            uniforms.fillColor = { 1, 0, 0, 1 };
            _calls[idx].type = kCallTypeCircles;
            _calls[idx].state = uniforms;
            Circles& circles = _calls[idx].circles;
            
            int i = 0;
            for (int j = 0; j < _triangles.count; j++) {
                AffineTransform unit = _triangles.ctms[j];
                
                circles.circles[i++] = { unit.tx, unit.ty, largeRadius };
                circles.circles[i++] = { unit.tx + unit.a, unit.ty + unit.b, radius };
                circles.circles[i++] = { unit.tx + unit.a + unit.c, unit.ty + unit.b + unit.d, radius };
            }
            circles.count = i;
            idx++;
        }
    }
    _calls.resize(idx);
}

- (void)redraw {
    [self update];
    
    AffineTransform CTM = self.viewCTM;
    for (Call& call : _calls)
        call.state.CTM = CTM;
    
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
