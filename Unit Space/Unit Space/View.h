//
//  View.h
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface View : NSView

@property(nonatomic, strong) NSData *fontData;
@property(nonatomic, strong) NSData *svgData;

- (IBAction)clockwise:(id)sender;
- (IBAction)anticlockwise:(id)sender;

- (IBAction)resetModel:(id)sender;

- (IBAction)cycleShapes:(id)sender;
- (IBAction)resetViewCTM:(id)sender;

- (IBAction)toggleShowPolygons:(id)sender;
- (IBAction)toggleEdges:(id)sender;
- (IBAction)toggleInterior:(id)sender;
- (IBAction)toggleShowColors:(id)sender;
- (IBAction)toggleShowTriangles:(id)sender;
- (IBAction)toggleRetina:(id)sender;
- (IBAction)toggleCoreGraphics:(id)sender;
- (IBAction)toggleUseStrokeColor:(id)sender;
- (IBAction)toggleTimer:(id)sender;

@end
