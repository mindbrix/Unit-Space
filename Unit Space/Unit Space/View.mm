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
//    [self.transform rotateBy:M_PI bounds:self.bounds];
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
    
    Vector q[] = {
        { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }, { 0, 0 }
    };
    
//    Vector q[] = {
//        { 0, 0 }, { 50, 50 }, { 100, 100 }, { 50, 100 }, { 0, 100 }, { 0, 50 }, { 0, 0 }
//    };
    
    int count = sizeof(q) / sizeof(q[0]);
    
    State uniforms;
    uniforms.CTM = CTM;
    uniforms.strokeColor = { 1, 0, 0, 1 };
    uniforms.flatness = 0.03125;
    uniforms.width = 0.5;
    uniforms.cap = 8;
    
    _calls.resize(3);
    int idx = 0;
    
//    uniforms.fillColor = { 0, 0, 1, 1 };
//    _calls[idx].type = kCallTypeUnits;
//    _calls[idx].state = uniforms;
//    _calls[idx].units.count = WriteQuadratics(& _calls[idx].units.ctms[0], CTM, q, count);
//    idx++;
    
//    uniforms.fillColor = { 0, 0, 0, 1 };
//    _calls[idx].type = kCallTypeQuadratics;
//    _calls[idx].state = uniforms;
//
//    _calls[idx].quadratics.count = WriteQuadratics(& _calls[idx].quadratics.ctms[0], CTM, q, count);
//    idx++;
//
//    uniforms.fillColor = { 1, 0, 0, 1 };
//    _calls[idx].type = kCallTypeCircles;
//    _calls[idx].state = uniforms;
//    Circles& circles = _calls[idx].circles;
//    for (int i = 0; i < count; i++)
//        circles.circles[i] = { q[i].x, q[i].y, radius };
//    circles.count = count;
//    idx++;
//
    uniforms.fillColor = { 0, 0, 0, 1 };
    _calls[idx].type = kCallTypeFractal;
    _calls[idx].state = uniforms;
    Fractals& fractals = _calls[idx].fractals;
    fractals.ctms[0] = { 100, 0, 0, 100, 0, 0 };
    AffineTransform s = { 0.9, 0, 0, 0.9, 0, 0 };
    float angle =- M_PI / 12.f, cosine = cosf(angle), sine = sinf(angle);
    AffineTransform r = { cosine, sine, -sine, cosine, 0, 0 };
    AffineTransform t = { 1, 0, 0, 1, 100, 100 };
    fractals.ctms[1] = AffineTransformConcat(r, AffineTransformConcat(s, t));
    fractals.count = 100;
    idx++;
    
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
