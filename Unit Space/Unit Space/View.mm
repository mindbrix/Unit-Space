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

@interface View () <LayerDelegate, CALayerDelegate>

@property(nonatomic) MetalLayer *metalLayer;
@property(nonatomic) BOOL useCG;
@property(nonatomic) NSVGimage *svgImage;
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
    self.metalLayer = [MetalLayer layer];
    [self setLayer:self.metalLayer];
    self.metalLayer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    self.metalLayer.layerDelegate = self;
    self.transform = [VGAffineTransform new];
    self.showInterior = YES;
    self.showColors = YES;
    [self resetShapes];
    [self redraw];
    return self;
}

- (void)dealloc {
    if (_svgImage)
        nsvgDelete(_svgImage);
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
    CGFloat native = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    self.layer.contentsScale = self.metalLayer.contentsScale = self.layer.contentsScale == native ? 1 : native;
    [self redraw];
}

// Swaps the Metal layer for a CALayer that Core Graphics renders from the source paths, as Rasterizer does: ground truth for
// the edge functions, at the same view transform and scale.
- (IBAction)toggleCoreGraphics:(id)sender {
    _useCG = !_useCG;
    CGFloat scale = self.layer.contentsScale;
    if (_useCG) {
        CALayer *layer = [CALayer layer];
        layer.delegate = self;
        layer.magnificationFilter = kCAFilterNearest;
        layer.needsDisplayOnBoundsChange = YES;
        layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
        self.layer = layer;
    } else
        self.layer = self.metalLayer;
    self.layer.contentsScale = self.metalLayer.contentsScale = scale;
    self.layer.bounds = self.bounds;
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
        {   { 0, 0 }, { 10, 100 }, { 0, 100 }                              },
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
    [self.layer setNeedsDisplay];
}


#pragma mark - CALayerDelegate

// A paint's colour as Scene takes it: gradients are the average of their stops, and the shape's opacity scales alpha.
static void setPaintColor(CGContextRef ctx, NSVGpaint& paint, float opacity, bool fill, bool showColors) {
    float r = 0, g = 0, b = 0, a = 0;
    int nstops = paint.type == NSVG_PAINT_COLOR ? 1 : paint.gradient->nstops;
    for (int i = 0; i < nstops; i++) {
        unsigned int rgba = paint.type == NSVG_PAINT_COLOR ? paint.color : paint.gradient->stops[i].color;
        r += rgba & 0xFF, g += (rgba >> 8) & 0xFF, b += (rgba >> 16) & 0xFF, a += rgba >> 24;
    }
    r /= nstops * 255.f, g /= nstops * 255.f, b /= nstops * 255.f, a = a / (nstops * 255.f) * opacity;
    if (!showColors)
        r = g = b = 0, a = 1;
    fill ? CGContextSetRGBFillColor(ctx, r, g, b, a) : CGContextSetRGBStrokeColor(ctx, r, g, b, a);
}

// Core Graphics ground truth. With an SVG, its own paths (cubics, and strokes with their joins, caps and dashes) rather
// than the Scene's quadratics and stroke outlines; otherwise the Scene's paths. Like Rasterizer, it renders into a DeviceRGB
// bitmap, as the Metal layer's colorspace is DeviceRGB, so both blend the same device values. Animation is not drawn.
- (void)displayLayer:(CALayer *)layer {
    double scale = layer.contentsScale;
    size_t pw = ceil(self.bounds.size.width * scale), ph = ceil(self.bounds.size.height * scale);
    if (pw == 0 || ph == 0)
        return;
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(nullptr, pw, ph, 8, pw * 4, rgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    if (!_showColors)
        CGContextSetRGBFillColor(ctx, 1, 1, 1, 1), CGContextFillRect(ctx, CGRectMake(0, 0, pw, ph));
    // The view transform in device pixels, as the Metal layer draws: the bitmap's origin is its bottom left, as is Metal's.
    AffineTransform t = self.viewCTM;
    CGContextConcatCTM(ctx, CGAffineTransformMake(t.a, t.b, t.c, t.d, t.tx, t.ty));
    if (_svgImage) {
        // Scene flips SVG y about the image height.
        CGContextTranslateCTM(ctx, 0, _svgImage->height), CGContextScaleCTM(ctx, 1, -1);
        for (NSVGshape *shape = _svgImage->shapes; shape != NULL; shape = shape->next) {
            bool filled = shape->fill.type != NSVG_PAINT_NONE, stroked = shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth > 0;
            if (!(filled || stroked))
                continue;
            CGMutablePathRef path = CGPathCreateMutable();
            for (NSVGpath *p = shape->paths; p != NULL; p = p->next) {
                bool finite = true;
                for (int i = 0; i < p->npts * 2; i++)
                    finite &= std::isfinite(p->pts[i]);
                if (!finite || p->npts == 0)
                    continue;
                CGPathMoveToPoint(path, NULL, p->pts[0], p->pts[1]);
                for (int i = 1; i + 2 < p->npts; i += 3)
                    CGPathAddCurveToPoint(path, NULL, p->pts[i * 2], p->pts[i * 2 + 1], p->pts[i * 2 + 2], p->pts[i * 2 + 3], p->pts[i * 2 + 4], p->pts[i * 2 + 5]);
                if (p->closed)
                    CGPathCloseSubpath(path);
            }
            if (filled) {
                setPaintColor(ctx, shape->fill, shape->opacity, true, _showColors);
                CGContextAddPath(ctx, path);
                shape->fillRule == NSVG_FILLRULE_EVENODD ? CGContextEOFillPath(ctx) : CGContextFillPath(ctx);
            }
            if (stroked) {
                setPaintColor(ctx, shape->stroke, shape->opacity, false, _showColors);
                CGContextSetLineWidth(ctx, shape->strokeWidth);
                CGContextSetLineJoin(ctx, shape->strokeLineJoin == NSVG_JOIN_ROUND ? kCGLineJoinRound : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? kCGLineJoinBevel : kCGLineJoinMiter);
                CGContextSetLineCap(ctx, shape->strokeLineCap == NSVG_CAP_ROUND ? kCGLineCapRound : shape->strokeLineCap == NSVG_CAP_SQUARE ? kCGLineCapSquare : kCGLineCapButt);
                CGContextSetMiterLimit(ctx, shape->miterLimit);
                CGFloat dashes[8];
                for (int i = 0; i < shape->strokeDashCount; i++)
                    dashes[i] = shape->strokeDashArray[i];
                CGContextSetLineDash(ctx, shape->strokeDashOffset, shape->strokeDashCount ? dashes : NULL, shape->strokeDashCount);
                CGContextAddPath(ctx, path);
                CGContextStrokePath(ctx);
            }
            CGPathRelease(path);
        }
    } else {
        for (size_t i = 0; i < _scene._paths.size(); i++) {
            Path& p = _scene._paths[i];
            AffineTransform m = _scene._CTMs[i];
            CGAffineTransform ctm = CGAffineTransformMake(m.a, m.b, m.c, m.d, m.tx, m.ty);
            CGMutablePathRef path = CGPathCreateMutable();
            for (SubPath& sub : p._subPaths) {
                const float *q = sub._quadratics.get();
                int n = (int)sub._quadratics.count() / 4;
                CGPathMoveToPoint(path, & ctm, q[0], q[1]);
                for (int k = 0; k < n; k++)
                    CGPathAddQuadCurveToPoint(path, & ctm, q[k * 4 + 2], q[k * 4 + 3], q[k * 4 + 4], q[k * 4 + 5]);
                CGPathCloseSubpath(path);
            }
            RGBA8 c = _showColors ? p._color : RGBA8::Black();
            CGContextSetRGBFillColor(ctx, c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
            CGContextAddPath(ctx, path);
            p._evenOdd ? CGContextEOFillPath(ctx) : CGContextFillPath(ctx);
            CGPathRelease(path);
        }
    }
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    layer.contents = (__bridge id)image;
    CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(rgb);
}

#pragma mark - LayerDelegate

- (Mesh *)meshForLayer:(CALayer *)layer paths:(MeshPath *)paths {
    double timerTime = _displayLink ? [NSDate date].timeIntervalSinceReferenceDate : 0;
    _mesh._timeline.writePathsForTime(timerTime, paths);
    [self writeDrawCalls: paths];
    return & _mesh;
}

#pragma mark - Properties

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
    
    // The source paths Core Graphics draws: nanosvg's own, before Scene converts them. It parses in place, so a copy.
    if (_svgImage)
        nsvgDelete(_svgImage), _svgImage = nullptr;
    if (_svgData) {
        char *data = (char *)malloc(_svgData.length + 1);
        memcpy(data, _svgData.bytes, _svgData.length), data[_svgData.length] = 0;
        _svgImage = nsvgParse(data, "px", 96);
        free(data);
    }
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
