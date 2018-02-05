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
    
    State uniforms;
    uniforms.fillColor = { 0, 0, 0, 1 };
    uniforms.strokeColor = { 1, 0, 0, 1 };
    uniforms.flatness = 0.03125;
    uniforms.width = 0.5;
    uniforms.cap = 8;
    
    _calls.resize(4);
    _calls[0].type = kCallTypeQuadratics;
    _calls[0].state = uniforms;
    
    Vector q[5] = {
        { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }, { 0, 0 }
    };
    WriteQuadratics(_calls[0].quadratics, q, 5);
//    _calls[0].type = kCallTypeUnits;
//    _calls[0].state = uniforms;
//    _calls[0].units.count = 2;
//    _calls[0].units.units[0].unitCTM = { 100, 0, 0, 100, 0, 0 };
//    _calls[0].units.units[1].unitCTM = { 100, 0, 0, 100, 50, 50 };
    
    _calls[1].type = kCallTypeArcs;
    _calls[1].state = uniforms;
    Arcs& arcs = _calls[1].arcs;
    arcs.count = 96;
    
    float cx = 200, cy = 200, r = 100, theta = TWO_PI / arcs.count;
    
    for (float i = 0; i < arcs.count; i++) {
        float rad = i * theta;
        float x = cx + r * std::cos(rad);
        float y = cy + r * std::sin(rad);
        
        arcs.arcs[int(i)] = { x, y, powf(std::abs(x - y), 1.01f), 0, 1 };
    }
    
    uniforms.strokeColor = { 0, 0, 1, 1 };
    uniforms.width = 2;
    
    _calls[2].type = kCallTypeLines;
    _calls[2].state = uniforms;
    Lines& lines = _calls[2].lines;
    lines.count = 96;
    
    for (float i = 0; i < lines.count; i++) {
        float rad = i * theta;
        float x0 = cx + r * std::cos(rad), x1 = cx + 2.0 * r * std::cos(rad);
        float y0 = cy + r * std::sin(rad), y1 = cy + 2.0 * r * std::sin(rad);
        
        lines.lines[int(i)] = { x0, y0, x1, y1 };
    }
    
    
    uniforms.fillColor = { 0, 0, 0, 1 };
    _calls[3].type = kCallTypeCircles;
    _calls[3].state = uniforms;
    Circles& circles = _calls[3].circles;
    circles.circles[0] = { 400, 400, 100 };
    circles.count = 1;
    
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
    AffineTransform& unit0 = _calls[0].units.units[0].unitCTM;
    BOOL inside = [self isPoint:event.locationInWindow inUnitEllipse:unit0];
    if (inside) {
        CGAffineTransform _unit0 = { unit0.a, unit0.b, unit0.c, unit0.d, unit0.tx, unit0.ty };
        CGRect _bounds = CGRectApplyAffineTransform(CGRectMake(0, 0, 1, 1), _unit0);
        
        VGAffineTransform *transform = [[VGAffineTransform alloc] initWithTransform:_unit0];
        [transform scaleBy:1.0f + event.magnification bounds:_bounds];
        _unit0 = transform.affineTransform;
        unit0 = {
            float(_unit0.a), float(_unit0.b),
            float(_unit0.c), float(_unit0.d),
            float(_unit0.tx), float(_unit0.ty) };
    } else
        [self.transform scaleBy:1.0f + event.magnification bounds:self.bounds];
    [self redrawWithMouseLocation:event.locationInWindow];
}

- (void)mouseDown:(NSEvent *)event {
//    NSLog(@"%@", event.locationInWindow);
}

- (void)mouseDragged:(NSEvent *)event {
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    float dx = event.deltaX * self.layer.contentsScale, dy = -event.deltaY * self.layer.contentsScale;
    float ux = dx * inv.a + dy * inv.c, uy = dx * inv.b + dy * inv.d;
    
    AffineTransform& unit0 = _calls[0].units.units[0].unitCTM;
    BOOL inside = [self isPoint:event.locationInWindow inUnitEllipse:unit0];
    
    if (inside) {
        unit0.tx += ux;
        unit0.ty += uy;
    } else
        [self.transform translateByX:event.deltaX andY:-event.deltaY];
    [self redraw];
}

- (void)mouseMoved:(NSEvent *)event {
    [self redrawWithMouseLocation:event.locationInWindow];
}

- (void)rotateWithEvent:(NSEvent *)event {
    AffineTransform& unit0 = _calls[0].units.units[0].unitCTM;
    BOOL inside = [self isPoint:event.locationInWindow inUnitEllipse:unit0];
    if (inside) {
        CGAffineTransform _unit0 = { unit0.a, unit0.b, unit0.c, unit0.d, unit0.tx, unit0.ty };
        CGRect _bounds = CGRectApplyAffineTransform(CGRectMake(0, 0, 1, 1), _unit0);
        
        VGAffineTransform *transform = [[VGAffineTransform alloc] initWithTransform:_unit0];
        [transform rotateBy:event.rotation / 10.f bounds:_bounds];
        _unit0 = transform.affineTransform;
        unit0 = {
            float(_unit0.a), float(_unit0.b),
            float(_unit0.c), float(_unit0.d),
            float(_unit0.tx), float(_unit0.ty) };
    } else
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
    for (auto &call : _calls)
        call.state.CTM = CTM;
    
    Lines& lines = _calls[2].lines;
    Circles& circles = _calls[3].circles;
    float scale = std::sqrt(std::abs(CTM.a * CTM.d - CTM.b * CTM.c));
    float radius = 4.0 / scale;
    
    for (int i = 0; i < lines.count; i++) {
        Line &line = lines.lines[i];
        circles.circles[i * 2] = { line.x0, line.y0, radius};
        circles.circles[i * 2 + 1] = { line.x1, line.y1, radius};
    }
    circles.count = lines.count * 2;
    
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
