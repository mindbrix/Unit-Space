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
@property(nonatomic) BOOL inspect;              // debug: the path under the mouse is highlighted and described
@property(nonatomic) BOOL solo;                 // debug: only the inspected path is drawn
@property(nonatomic) NSInteger hoverPath, selectedPath;
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
    self.hoverPath = self.selectedPath = -1;
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

- (void)mouseDown:(NSEvent *)event {
    if (!_inspect || _solo)
        return;
    _selectedPath = [self pathAt:event.locationInWindow];
    if (_selectedPath != -1)
        printf("%s\n", [self describePath:_selectedPath].UTF8String);
    [self updateInspector];
}

- (void)mouseMoved:(NSEvent *)event {
    if (!_inspect || _solo)
        return;
    NSInteger p = [self pathAt:event.locationInWindow];
    if (p != _hoverPath) {
        _hoverPath = p;
        [self updateInspector];
    }
}

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

// Rebuilds the SVG with the other stroker: Stroke's outlines, or Core Graphics'.
- (IBAction)toggleCGStroker:(id)sender {
    _scene._cgStroker = !_scene._cgStroker;
    NSLog(@"Strokes by %@", _scene._cgStroker ? @"Core Graphics" : @"Stroke");
    if (_svgData)
        [self setSvgData:_svgData];
}

#pragma mark - Inspector

// Hovering highlights the topmost path under the mouse and describes it in the window's subtitle; clicking selects it (or
// clears the selection), so the view can be zoomed and panned around it. Solo draws only that path, so the triangle and
// polygon views show its triangulation alone, and holds it: the mouse no longer changes the inspected path. Copy puts its source shape on the pasteboard as a standalone SVG with the
// same coordinates, to reproduce it.
- (IBAction)toggleInspector:(id)sender {
    _inspect = !_inspect;
    self.window.acceptsMouseMovedEvents = _inspect;
    _hoverPath = _selectedPath = -1;
    [self updateInspector];
}

- (IBAction)toggleSolo:(id)sender {
    _solo = !_solo;
    _selectedPath = self.inspectedPath;
    [self updateInspector];
}

- (NSInteger)inspectedPath {
    return !_inspect ? -1 : _selectedPath != -1 ? _selectedPath : _hoverPath;
}

- (void)updateInspector {
    NSInteger p = self.inspectedPath;
    self.window.subtitle = p == -1 ? (_inspect ? @"Inspector: hover over a path" : @"") : [self describePath:p];
    [self redraw];
}

// The topmost path whose polygon (its quadratics' chords) holds a point in window coordinates.
- (NSInteger)pathAt:(NSPoint)location {
    NSPoint v = [self convertPoint:location fromView:nil];
    float x = v.x * self.layer.contentsScale, y = v.y * self.layer.contentsScale;
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    float sx = x * inv.a + y * inv.c + inv.tx, sy = x * inv.b + y * inv.d + inv.ty;
    for (NSInteger i = (NSInteger)_scene._paths.size() - 1; i >= 0; i--) {
        AffineTransform m = AffineTransformInvert(_scene._CTMs[i]);
        if (_scene._paths[i].contains(sx * m.a + sy * m.c + m.tx, sx * m.b + sy * m.d + m.ty))
            return i;
    }
    return -1;
}

- (NSVGshape *)sourceShape:(NSInteger)p {
    int source = _scene._paths[p]._source;
    NSVGshape *shape = _svgImage && source >= 0 ? _svgImage->shapes : nullptr;
    for (int i = 0; shape && i < source; i++)
        shape = shape->next;
    return shape;
}

- (NSString *)describePath:(NSInteger)p {
    Path& path = _scene._paths[p];
    size_t quadratics = 0;
    for (SubPath& s : path._subPaths)
        quadratics += s._quadratics.count() / 4;
    int base = _mesh._idxs[p], end = _mesh._idxs[p + 1], hulls = 0;
    for (int i = base; i < end; i++)
        hulls += (_mesh._tris[i].curves & Triangle::kHull) != 0;
    NSVGshape *shape = [self sourceShape:p];
    NSString *source = shape ? [NSString stringWithFormat:@" · shape %d \"%s\"", path._source, shape->id] : @"";
    return [NSString stringWithFormat:@"Path %ld of %zu%@ · %s · %zu subpaths, %zu quadratics · %d triangles, %d hulls · bounds %g %g %g %g",
            (long)p, _scene._paths.size(), source, path._outline ? "stroke" : "fill", path._subPaths.size(), quadratics, end - base, hulls,
            path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy];
}

// The inspected path's source shape as a standalone SVG: its cubics exactly as parsed, and only the fill or the stroke.
- (IBAction)copy:(id)sender {
    NSInteger p = self.inspectedPath;
    NSVGshape *shape = p == -1 ? nullptr : [self sourceShape:p];
    if (!shape) {
        NSBeep();
        return;
    }
    auto hex = [](NSVGpaint& paint) {
        if (paint.type == NSVG_PAINT_NONE)
            return [NSString stringWithFormat:@"none"];
        unsigned int rgba = paint.type == NSVG_PAINT_COLOR ? paint.color : paint.gradient->stops[0].color;
        return [NSString stringWithFormat:@"#%02x%02x%02x", rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF];
    };
    NSMutableString *d = [NSMutableString string];
    for (NSVGpath *path = shape->paths; path; path = path->next) {
        [d appendFormat:@"M%.9g %.9g", path->pts[0], path->pts[1]];
        for (int i = 1; i + 2 < path->npts; i += 3) {
            float *q = path->pts + i * 2;
            [d appendFormat:@" C%.9g %.9g %.9g %.9g %.9g %.9g", q[0], q[1], q[2], q[3], q[4], q[5]];
        }
        [d appendString:path->closed ? @" Z " : @" "];
    }
    NSString *paint = _scene._paths[p]._outline
        ? [NSString stringWithFormat:@"fill=\"none\" stroke=\"%@\" stroke-width=\"%.9g\" stroke-linejoin=\"%s\" stroke-linecap=\"%s\" stroke-miterlimit=\"%g\"",
           hex(shape->stroke), shape->strokeWidth, shape->strokeLineJoin == NSVG_JOIN_ROUND ? "round" : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? "bevel" : "miter",
           shape->strokeLineCap == NSVG_CAP_ROUND ? "round" : shape->strokeLineCap == NSVG_CAP_SQUARE ? "square" : "butt", shape->miterLimit]
        : [NSString stringWithFormat:@"fill=\"%@\" fill-rule=\"%s\"", hex(shape->fill), shape->fillRule == NSVG_FILLRULE_EVENODD ? "evenodd" : "nonzero"];
    NSMutableString *dashes = [NSMutableString string];
    for (int i = 0; _scene._paths[p]._outline && i < shape->strokeDashCount; i++)
        [dashes appendFormat:@"%@%.9g", i ? @" " : @" stroke-dasharray=\"", shape->strokeDashArray[i]];
    if (dashes.length)
        [dashes appendFormat:@"\" stroke-dashoffset=\"%.9g\"", shape->strokeDashOffset];
    NSString *svg = [NSString stringWithFormat:@"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%g\" height=\"%g\" viewBox=\"0 0 %g %g\">\n"
                     "<!-- %@ -->\n<path %@%@ d=\"%@\"/>\n</svg>\n",
                     _svgImage->width, _svgImage->height, _svgImage->width, _svgImage->height, [self describePath:p], paint, dashes, d];
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:svg forType:NSPasteboardTypeString];
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
    _hoverPath = _selectedPath = -1;
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
    // Debug: the inspected path in red, and with solo on, every other path transparent.
    NSInteger inspected = self.inspectedPath;
    if (inspected != -1) {
        for (int i = 0; _solo && i < _scene._paths.size(); i++)
            paths[i].color.a = 0;
        paths[inspected].color = RGBA8::Red();
    }
    
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
