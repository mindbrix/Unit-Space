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
#import "Scene.hpp"
#import "libTess.hpp"

constexpr float kScrollLineDelta = 10;


using namespace std;

@interface View () <LayerDelegate>

@property(nonatomic) MetalLayer *metalLayer;
@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) BOOL showColors;
@property(nonatomic) BOOL showEdgeTriangles;
@property(nonatomic) BOOL showInterior;
@property(nonatomic) BOOL showPolygons;
@property(nonatomic) BOOL showTriangles;
@property(nonatomic) BOOL useStrokeColor;
@property(nonatomic) size_t shapeIndex;
@property(nonatomic) vector<vector<Vector>> shapes;
@property(nonatomic) vector<SubPath::Type> shapeTypes;
@property(nonatomic) Mesh mesh;
@property(nonatomic) Scene scene;
@property(nonatomic) CADisplayLink *displayLink;

- (void)timerFired;

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
    self.showInterior = YES;
    self.showColors = YES;
    [self resetShapes];
    [self redraw];
    return self;
}

// The display link retains its target, so invalidate it when leaving or closing the window to break the cycle.
- (void)viewWillMoveToWindow:(NSWindow *)newWindow {
	NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
	if (self.window)
		[center removeObserver:self name:NSWindowWillCloseNotification object:self.window];
	if (newWindow)
		[center addObserver:self selector:@selector(windowWillClose:) name:NSWindowWillCloseNotification object:newWindow];
	else
		[self stopTimer];
    [super viewWillMoveToWindow:newWindow];
}

- (void)windowWillClose:(NSNotification *)notification {
	[self stopTimer];
}

#pragma mark - CADisplayLink

- (void)stopTimer {
	[_displayLink invalidate];
	_displayLink = nil;
}
	
- (void)resetTimer {
	[self stopTimer];
	_displayLink = [self displayLinkWithTarget:self selector:@selector(timerFired)];
	[_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (IBAction)toggleTimer:(id)sender {
	if (_displayLink)
		[self stopTimer];
	else
		[self resetTimer];
    [self redraw];
}

- (void)timerFired {
	[self redraw];
}

#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    return YES;
}

- (void)magnifyWithEvent:(NSEvent *)event {
    [self.transform scaleBy:1.0f + event.magnification bounds:self.bounds];
    [self redraw];
}

- (void)mouseDown:(NSEvent *)event {}

- (void)mouseDragged:(NSEvent *)event {
    [self.transform translateByX:event.deltaX andY:-event.deltaY];
    [self redraw];
}

- (void)rotateWithEvent:(NSEvent *)event {
    [self.transform rotateBy:event.rotation / 10.f bounds:self.bounds];
    [self redraw];
}

- (void)scrollWheel:(NSEvent *)event {
    BOOL isInverted = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]);
    CGFloat inversion = isInverted ? 1.0f : -1.0f;
    // Trackpads and Magic Mice report precise point deltas; wheel mice report lines.
    CGFloat scale = event.hasPreciseScrollingDeltas ? 1.0f : kScrollLineDelta;
    [self.transform translateByX:event.scrollingDeltaX * scale * inversion andY:-event.scrollingDeltaY * scale * inversion];
    [self redraw];
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

- (IBAction)resetModel:(id)sender {
    [self resetShapes];
    [self triangulate];
    [self redraw];
}

- (IBAction)cycleShapes:(id)sender {
    _shapeIndex++;
    if (_shapeIndex == _shapes.size())
        _shapeIndex = 0;
    
    [self resetShapes];
    [self triangulate];
    [self redraw];
}
- (IBAction)resetViewCTM:(id)sender {
    [self.transform reset];
    [self redraw];
}





- (IBAction)toggleEdges:(id)sender {
    _showEdgeTriangles = !_showEdgeTriangles;
    [self redraw];
}
- (IBAction)toggleInterior:(id)sender {
    _showInterior = !_showInterior;
    [self redraw];
}
- (IBAction)toggleShowPolygons:(id)sender{
    _showPolygons = !_showPolygons;
    _showTriangles = NO;
    [self redraw];
}
- (IBAction)toggleShowColors:(id)sender {
    _showColors = !_showColors;
    [self updateFromState];
    [self redraw];
}



- (IBAction)toggleShowTriangles:(id)sender {
    _showPolygons = NO;
    _showTriangles = !_showTriangles;
    [self updateFromState];
    [self redraw];
}

- (IBAction)toggleRetina:(id)sender {
    if (self.metalLayer.contentsScale == [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width)
        self.metalLayer.contentsScale = 1;
    else
        self.metalLayer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    [self redraw];
}

- (IBAction)toggleUseStrokeColor:(id)sender {
    _useStrokeColor = !_useStrokeColor;
    [self redraw];
}
- (void)updateFromState {
    self.metalLayer.backgroundColor = CGColorGetConstantColor(_showColors ? kCGColorClear : kCGColorWhite);
}

#pragma mark -

- (void)resetShapes {
    _shapes =
    {
        {   { 0, 0 }, { 100, 100 }, { 0, 100 }                              },
        {   { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }, { 0, 0 }        },
        {   { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 }                  },
        {   { 0, 0 }, { 50, 50 }, { 100, 100 }, { 50, 100 }, { 0, 100 }     },
        {   { 0, 0 }, { 100, 0 }, { 0, 100 }, { 100, 100 }                  },
        {   { 0, 0 }, { 100, 100 }, { 0, 100 }                              },
    };
    _shapeTypes =
    {
        SubPath::kLine,
        SubPath::kQuadratic,
        SubPath::kLine,
        SubPath::kQuadratic,
        SubPath::kLine,
        SubPath::kQuadratic,
    };
}

- (void)triangulate {
    if (_svgData == nil && _fontData == nil) {
        Path p(true, false, 0, RGBA8::Red(), { SubPath((float *)& _shapes[_shapeIndex][0], (int)_shapes[_shapeIndex].size(), _shapeTypes[_shapeIndex]) });
        _scene.empty();
        _scene.addPath(p);
    }
    libTess::triangulate(_mesh, _scene);
    [self.metalLayer uploadMesh:_mesh];
}

- (void)writeDrawCalls:(MeshPath *)paths {
    State uniforms;
    uniforms.CTM = self.viewCTM;
    uniforms.strokeColor = { 0, 0, 0, 1 };
    uniforms.width = 0;
    
    MeshState meshState;
    meshState.showEdgeTriangles = _showEdgeTriangles;
    meshState.showInterior = _showInterior;
    meshState.showPolygons = _showPolygons;
    meshState.showTriangles = _showTriangles;
    meshState.useStrokeColor = _useStrokeColor;
    meshState.colorsSize = (int)_scene._paths.size();
    
    _mesh._calls.resize(0);
	MeshPath *path = & paths[0];
	for (int i = 0; i < _scene._paths.size(); i++, path++)
		path->color = _showColors ? _scene._paths[i]._color : RGBA8::Black();
    
    Bounds clipBounds = { 0, 0, float(std::ceil(self.metalLayer.bounds.size.width * self.metalLayer.contentsScale)), float(std::ceil(self.metalLayer.bounds.size.height * self.metalLayer.contentsScale)) };
    Mesh::writePathCalls(_mesh, paths, _scene._paths.size(), clipBounds, uniforms, meshState);
}

- (void)redraw {
    [self.metalLayer setNeedsDisplay];
}


#pragma mark - LayerDelegate

- (Mesh *)meshForLayer:(CALayer *)layer paths:(MeshPath *)paths {
    double timerTime = _displayLink ? [NSDate date].timeIntervalSinceReferenceDate : 0;
    _mesh._timeline.writePathsForTime(timerTime, paths);
    [self writeDrawCalls: paths];
    return & _mesh;
}

#pragma mark - Properties

-(MetalLayer *)metalLayer {
    return (MetalLayer *)self.layer;
}

- (void)setFontData:(NSData *)fontData {
    _fontData = fontData;
    
    if (_fontData) {
        _scene.empty();
        _scene.addFont(_fontData.bytes, _fontData.length);
    }
    [self triangulate];
    [self redraw];
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    
    if (_svgData) {
        _scene.empty();
        _scene.addSVG(_svgData.bytes, _svgData.length);
    }
    [self triangulate];
    [self redraw];
}

- (AffineTransform)viewCTM {
    CGAffineTransform viewCTM = self.transform.affineTransform;
    AffineTransform ctm = { float(viewCTM.a), float(viewCTM.b), float(viewCTM.c), float(viewCTM.d), float(viewCTM.tx), float(viewCTM.ty) };
    AffineTransform retina = { float(self.layer.contentsScale), 0, 0, float(self.layer.contentsScale), 0, 0 };
    AffineTransform skew = { 1, 0, 0, 1, 0, 0 };
    return AffineTransformConcat(skew, AffineTransformConcat(ctm, retina));
}

@end
