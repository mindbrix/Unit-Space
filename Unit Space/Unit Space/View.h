//
//  View.h
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface View : NSView

@property(nonatomic, strong) NSData *svgData;

- (IBAction)clockwise:(id)sender;
- (IBAction)anticlockwise:(id)sender;

- (IBAction)resetModel:(id)sender;

- (IBAction)shiftPathBitmask:(id)sender;

- (IBAction)toggleOne:(id)sender;
- (IBAction)toggleTwo:(id)sender;
- (IBAction)toggleThree:(id)sender;
- (IBAction)toggleFour:(id)sender;

- (IBAction)toggleEllipse:(id)sender;
- (IBAction)togglePoints:(id)sender;
- (IBAction)toggleShowColors:(id)sender;
- (IBAction)toggleShowInterior:(id)sender;
- (IBAction)toggleTriangle:(id)sender;
- (IBAction)toggleUseMask:(id)sender;

@end
