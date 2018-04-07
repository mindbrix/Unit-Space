//
//  Document.h
//  Unit Space
//
//  Created by Nigel Barber on 14/01/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "View.h"

@interface Document : NSDocument

@property(nonatomic, strong) IBOutlet View *view;

@end

