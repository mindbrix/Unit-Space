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

@interface View ()

@property(nonatomic) MetalLayer *metalLayer;
@property(nonatomic) VGAffineTransform *transform;

@end

@implementation View

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    
    [self setWantsLayer:YES];
    [self setLayer:[MetalLayer layer]];
    self.metalLayer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    _transform = [VGAffineTransform new];
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


#pragma mark - Properties

-(MetalLayer *)metalLayer {
    return (MetalLayer *)self.layer;
}

@end
