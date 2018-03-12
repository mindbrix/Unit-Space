//
//  View.h
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface View : NSView

- (IBAction)clockwise:(id)sender;
- (IBAction)anticlockwise:(id)sender;

- (IBAction)toggleOne:(id)sender;
- (IBAction)toggleTwo:(id)sender;
- (IBAction)toggleThree:(id)sender;

- (IBAction)togglePoints:(id)sender;

@end
