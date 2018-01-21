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
@property(nonatomic, assign) vector<Call> calls;

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
    return YES;
}

- (void)magnifyWithEvent:(NSEvent *)theEvent {
    [self.transform scaleBy:1.0f + theEvent.magnification bounds:self.bounds];
    [self redraw];
}

- (void)mouseDragged:(NSEvent *)theEvent {
    [self.transform translateByX:theEvent.deltaX andY:-theEvent.deltaY];
    [self redraw];
}

- (void)rotateWithEvent:(NSEvent *)event {
    [self.transform rotateBy:event.rotation / 10.f bounds:self.bounds];
    [self redraw];
}

- (void)scrollWheel:(NSEvent *)theEvent {
    BOOL isInverted = ([theEvent respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [theEvent isDirectionInvertedFromDevice]);
    CGFloat inversion = isInverted ? 1.0f : -1.0f;
    
    [self.transform translateByX:theEvent.deltaX * inversion andY:-theEvent.deltaY * inversion];
    [self redraw];
}


#pragma mark -

- (void)redraw {
    self.metalLayer.viewTransform = self.transform.affineTransform;
    [self.metalLayer setNeedsDisplay];
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

@end
