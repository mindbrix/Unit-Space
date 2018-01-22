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
    
    Uniforms uniforms;
    uniforms.fillColor = { 0, 0, 0, 1 };
    uniforms.strokeColor = { 1, 0, 0, 1 };
    uniforms.flatness = 0.03125;
    uniforms.width = 0.5;
    uniforms.cap = 8;
    
    _calls.resize(10);
    _calls[0].type = kCallTypeUnits;
    _calls[0].uniforms = uniforms;
    _calls[0].units.count = 1;
    _calls[0].units.units[0].unitCTM = { 100, 0, 0, 100, 0, 0 };
    
    _calls[1].type = kCallTypeArcs;
    _calls[1].uniforms = uniforms;
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
    _calls[2].uniforms = uniforms;
    Lines& lines = _calls[2].lines;
    lines.count = 96;
    
    for (float i = 0; i < lines.count; i++) {
        float rad = i * theta;
        float x = cx + r * std::cos(rad);
        float y = cy + r * std::sin(rad);
        
        lines.lines[int(i)] = { cx, cy, x, y };
    }
    
    [self redraw];

    return self;
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    
    // Drawing code here.
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
    BOOL inside = [self isPoint:event.locationInWindow inUnitEllipse:_calls[0].units.units[0].unitCTM];
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    float dx = event.deltaX * self.layer.contentsScale, dy = -event.deltaY * self.layer.contentsScale;
    float ux = dx * inv.a + dy * inv.c, uy = dx * inv.b + dy * inv.d;
    
    if (inside) {
        _calls[0].units.units[0].unitCTM.tx += ux;
        _calls[0].units.units[0].unitCTM.ty += uy;
    } else
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
    BOOL inside = [self isPoint:location inUnitEllipse:_calls[0].units.units[0].unitCTM];
    _calls[0].uniforms.fillColor.r = inside ? 1 : 0;
    [self redraw];
}

- (void)redraw {
    AffineTransform CTM = self.viewCTM;
    _calls[0].uniforms.CTM = CTM;
    _calls[1].uniforms.CTM = CTM;
    _calls[2].uniforms.CTM = CTM;
    
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
    *count = 3;
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

- (float)viewScale {
    AffineTransform CTM = self.viewCTM;
    return std::sqrt(CTM.a * CTM.a + CTM.b * CTM.b);
}
@end
