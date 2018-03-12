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

using namespace std;

@interface View () <LayerDelegate>

@property(nonatomic) MetalLayer *metalLayer;
@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) vector<Call> calls;
@property(nonatomic) uint64_t flags;
@property(nonatomic) BOOL showPoints;
@property(nonatomic) BOOL showTriangle;

@end

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
    /*
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    float dx = event.deltaX * self.layer.contentsScale, dy = -event.deltaY * self.layer.contentsScale;
    float ux = dx * inv.a + dy * inv.c, uy = dx * inv.b + dy * inv.d;
    
    AffineTransform& unit0 = _calls[0].units.units[0].unitCTM;
    BOOL inside = [self isPoint:event.locationInWindow inUnitEllipse:unit0];
    
    if (inside) {
        unit0.tx += ux;
        unit0.ty += uy;
    } else
     */
    
    [self.transform translateByX:event.deltaX andY:-event.deltaY];
    [self redraw];
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

- (IBAction)togglePoints:(id)sender {
    _showPoints = !_showPoints;
    [self redraw];
}

- (IBAction)toggleTriangle:(id)sender {
    _showTriangle = !_showTriangle;
    [self redraw];
}

#pragma mark -

- (void)redrawWithMouseLocation:(CGPoint)location {
//    BOOL inside = [self isPoint:location inUnitEllipse:_calls[0].units.units[0].unitCTM];
//    _calls[0].state.fillColor.r = inside ? 1 : 0;
    [self redraw];
}

- (void)redraw {
    AffineTransform CTM = self.viewCTM;
    float scale = std::sqrt(std::abs(CTM.a * CTM.d - CTM.b * CTM.c));
    float radius = 8.0 / scale;
    
    State uniforms;
    uniforms.CTM = CTM;
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
        _calls[idx].units.ctms[0] = { 100, 0, 0, 100, 0, 0 };
        idx++;
    }
    
    if (_flags & 2) {
        Vector lozenge[] = {
            { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }, { 0, 0 }
        };
        
        Vector triangle[] = {
            { 0, 0 }, { 50, 50 }, { 100, 100 }, { 50, 100 }, { 0, 100 }, { 0, 50 }, { 0, 0 }
        };
        
        Vector *q = _showTriangle ? triangle : lozenge;
        int count = _showTriangle ?  sizeof(triangle) / sizeof(triangle[0]) :  sizeof(lozenge) / sizeof(lozenge[0]);
        
        uniforms.fillColor = { 0, 0, 0, 1 };
        _calls[idx].type = kCallTypeQuadratics;
        _calls[idx].state = uniforms;
        
        _calls[idx].quadratics.count = WriteQuadratics(& _calls[idx].quadratics.ctms[0], CTM, q, count);
        idx++;
        
        if (_showPoints) {
            uniforms.fillColor = { 1, 0, 0, 1 };
            _calls[idx].type = kCallTypeCircles;
            _calls[idx].state = uniforms;
            Circles& circles = _calls[idx].circles;
            for (int i = 0; i < count; i++)
                circles.circles[i] = { q[i].x, q[i].y, radius };
            circles.count = count;
            idx++;
        }
    }
    
    if (_flags & 4) {
        uniforms.fillColor = { 0, 0, 0, 0.333 };
        _calls[idx].type = kCallTypeFractal;
        _calls[idx].state = uniforms;
        Fractals& fractals = _calls[idx].fractals;
        fractals.unitCTM = { 100, 0, 0, 100, 0, 0 };
        AffineTransform s = { 0.9, 0, 0, 0.9, 0, 0 };
        float a0 =- M_PI / 24.f, cos0 = cosf(a0), sin0 = sinf(a0);
        AffineTransform r = { cos0, sin0, -sin0, cos0, 0, 0 };
        AffineTransform t = { 1, 0, 0, 1, 100, 100 };
        fractals.ctms[0] = AffineTransformConcat(r, AffineTransformConcat(s, t));
        float a1 =- M_PI / 12.f, cos1 = cosf(a1), sin1 = sinf(a1);
        AffineTransform r1 = { cos1, sin1, -sin1, cos1, 0, 0 };
        fractals.ctms[1] = r1;// AffineTransformConcat(s, r);
        fractals.ctms[2] =  { 1, 0, 0, 1, 2000, 0 };
        fractals.ctms[3] =  { 1, 0, 0, 1, -20000, 2000 };
        
        fractals.counts[0] = 30;
        fractals.counts[1] = 24;
        fractals.counts[2] = 10;
        fractals.counts[3] = 2;
        fractals.count = fractals.counts[0] * fractals.counts[1] * fractals.counts[2] * fractals.counts[3];
        idx++;
    }
    
    
    
    
    _calls.resize(idx);
    
    [self.metalLayer setNeedsDisplay];
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
