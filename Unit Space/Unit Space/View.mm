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

// Editing handles, in points.
constexpr float kHandleLineWidth = 1, kHandleControlRadius = 3, kHandlePointRadius = 4, kHandlePointInnerRadius = 2.5, kHandleHitRadius = 6;

// A point of the selected shape that can be dragged: an end point, or a curve's control point.
struct EditPoint {
    NSVGpath *path;
    int index;
};

// A dot or a line drawn over the selected shape. Each is a path after the scene's, whose transform is set every frame
// so it keeps its size on screen.
struct EditHandle {
    enum Kind { kLine, kDot };
    Kind kind;
    float x0, y0, x1, y1;   // scene coordinates: the dot's centre, or the line's ends
    float size;             // the dot's radius or the line's width, in points
    RGBA8 color;
};

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
@property(nonatomic) Mesh editMesh;             // the selected shape's paths while its points are edited
@property(nonatomic) int editBegin, editEnd;    // those paths' scene indices, which the scene's mesh leaves out
@property(nonatomic) Mesh handleTemplates;      // a dot and a line, triangulated once, which every handle copies
@property(nonatomic) Mesh handleMesh;
@property(nonatomic) vector<EditHandle> handles;
@property(nonatomic) vector<EditPoint> editPoints;
@property(nonatomic) EditPoint dragPoint;       // the point being dragged: its path is null when none is
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
    self.dragPoint = { nullptr, 0 };
    [self makeHandleTemplates];
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
    NSInteger e = [self editPointAt:event.locationInWindow];
    _dragPoint = e == -1 ? EditPoint { nullptr, 0 } : _editPoints[e];
    if (_dragPoint.path || !_inspect || _solo)
        return;
    NSInteger p = [self pathAt:event.locationInWindow];
    if ([self sourceShape:p] != [self sourceShape:_selectedPath])
        [self commitEdit];
    _selectedPath = p;
    if (_selectedPath != -1)
        printf("%s\n", [self describePath:_selectedPath].UTF8String);
    [self updateHandles];
    [self updateInspector];
}

- (void)mouseUp:(NSEvent *)event {
    _dragPoint = { nullptr, 0 };
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
    if (_dragPoint.path) {
        CGPoint s = [self sourcePointAt:event.locationInWindow];
        [self moveEditPoint:_dragPoint x:s.x y:s.y];
        [self rebuildSelectedShape];
        return;
    }
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

#pragma mark - Inspector

// Hovering highlights the topmost path under the mouse and describes it in the window's subtitle; clicking selects it (or
// clears the selection), so the view can be zoomed and panned around it. Solo draws only that path, so the triangle and
// polygon views show its triangulation alone, and holds it: the mouse no longer changes the inspected path. Copy puts its source shape on the pasteboard as a standalone SVG with the
// same coordinates, to reproduce it.
- (IBAction)toggleInspector:(id)sender {
    [self commitEdit];
    _inspect = !_inspect;
    self.window.acceptsMouseMovedEvents = _inspect;
    _hoverPath = _selectedPath = -1;
    [self updateHandles];
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
    if (p < 0 || p >= (NSInteger)_scene._paths.size())
        return nullptr;
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
    // An edited path's triangles are in the edit mesh, whose indices start at its first path.
    bool edited = p >= _editBegin && p < _editEnd;
    Mesh& mesh = edited ? _editMesh : _mesh;
    int k = int(edited ? p - _editBegin : p), base = mesh._idxs[k], end = mesh._idxs[k + 1], hulls = 0;
    for (int i = base; i < end; i++)
        hulls += (mesh._tris[i].curves & Triangle::kHull) != 0;
    NSVGshape *shape = [self sourceShape:p];
    NSString *source = shape ? [NSString stringWithFormat:@" · shape %d \"%s\"", path._source, shape->id] : @"";
    return [NSString stringWithFormat:@"Path %ld of %zu%@ · %s · %zu subpaths, %zu quadratics · %d triangles, %d hulls · bounds %g %g %g %g",
            (long)p, _scene._paths.size(), source, path._outline ? "stroke" : "fill", path._subPaths.size(), quadratics, end - base, hulls,
            path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy];
}

#pragma mark - Editing

// A selected path's shape shows its points as handles, which drag to edit it: an end point carries its curves' control
// points with it, and a closed subpath's start and end move together. Only its paths are triangulated again as they
// change, into their own mesh, which draws in their place; deselecting triangulates the whole scene again.

// The selected path's shape, whose points can be edited.
- (NSVGshape *)editedShape {
    return _inspect && _selectedPath != -1 ? [self sourceShape:_selectedPath] : nullptr;
}

// A source (SVG) point in device pixels: Scene flips y about the image height.
- (CGPoint)devicePointForSource:(const float *)p {
    AffineTransform t = self.viewCTM;
    float x = p[0], y = _svgImage->height - p[1];
    return CGPointMake(x * t.a + y * t.c + t.tx, x * t.b + y * t.d + t.ty);
}

- (CGPoint)sourcePointAt:(NSPoint)location {
    NSPoint v = [self convertPoint:location fromView:nil];
    float x = v.x * self.layer.contentsScale, y = v.y * self.layer.contentsScale;
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    return CGPointMake(x * inv.a + y * inv.c + inv.tx, _svgImage->height - (x * inv.b + y * inv.d + inv.ty));
}

static bool closesOnStart(NSVGpath *path) {
    return path->npts > 1 && path->pts[0] == path->pts[path->npts * 2 - 2] && path->pts[1] == path->pts[path->npts * 2 - 1];
}

// The edit point under a location in window coordinates, the nearest within kHandleHitRadius, or -1.
- (NSInteger)editPointAt:(NSPoint)location {
    if (![self editedShape])
        return -1;
    NSPoint v = [self convertPoint:location fromView:nil];
    float x = v.x * self.layer.contentsScale, y = v.y * self.layer.contentsScale, r = kHandleHitRadius * self.layer.contentsScale, best = r * r;
    NSInteger found = -1;
    for (size_t i = 0; i < _editPoints.size(); i++) {
        CGPoint d = [self devicePointForSource:_editPoints[i].path->pts + _editPoints[i].index * 2];
        float d2 = (d.x - x) * (d.x - x) + (d.y - y) * (d.y - y);
        if (d2 <= best)
            best = d2, found = i;
    }
    return found;
}

// Moves an edit point to (x, y) in source coordinates. Lines (nanosvg's straight cubics) keep their controls at thirds,
// so they stay lines; curves' controls move with their end points.
- (void)moveEditPoint:(EditPoint)e x:(float)x y:(float)y {
    float *pts = e.path->pts;
    int n = e.path->npts, i = e.index;
    if (i % 3) {
        pts[i * 2] = x, pts[i * 2 + 1] = y;
        return;
    }
    float dx = x - pts[i * 2], dy = y - pts[i * 2 + 1];
    vector<int> ends = { i };
    if (i == 0 && closesOnStart(e.path))
        ends.push_back(n - 1);
    // Each cubic at a moved end, the control next to that end, and whether the cubic is a line, before anything moves.
    struct Adjacent { int cubic, control; bool straight; };
    vector<Adjacent> adjacent;
    for (int end : ends) {
        if (end > 0)
            adjacent.push_back({ end - 3, end - 1, isStraightCubic(pts + (end - 3) * 2) });
        if (end < n - 1)
            adjacent.push_back({ end, end + 1, isStraightCubic(pts + end * 2) });
    }
    for (int end : ends)
        pts[end * 2] += dx, pts[end * 2 + 1] += dy;
    for (Adjacent& a : adjacent) {
        float *q = pts + a.cubic * 2;
        if (a.straight)
            for (int k = 0; k < 2; k++)
                q[2 + k] = q[k] + (q[6 + k] - q[k]) / 3, q[4 + k] = q[k] + (q[6 + k] - q[k]) * 2 / 3;
        else
            pts[a.control * 2] += dx, pts[a.control * 2 + 1] += dy;
    }
}

// Converts the selected shape again from its edited points, and triangulates just its paths.
- (void)rebuildSelectedShape {
    NSVGshape *shape = [self editedShape];
    if (!shape)
        return;
    int source = _scene._paths[_selectedPath]._source, begin = (int)_selectedPath, end = begin + 1;
    while (begin > 0 && _scene._paths[begin - 1]._source == source)
        begin--;
    while (end < (int)_scene._paths.size() && _scene._paths[end]._source == source)
        end++;
    Scene edited;
    edited.addShape(shape, _svgImage->height, source);
    if (edited._paths.size() != size_t(end - begin)) {
        // A path came or went (it lost all its subpaths, or regained them): the scene's indices change.
        NSInteger selected = _selectedPath - begin;
        _scene.empty();
        _scene.addImage(_svgImage);
        [self triangulate];
        for (int p = 0; p < (int)_scene._paths.size(); p++)
            if (_scene._paths[p]._source == source) {
                _selectedPath = std::min(p + selected, (NSInteger)_scene._paths.size() - 1);
                break;
            }
        [self updateHandles];
        [self updateInspector];
        return;
    }
    vector<MeshPath>& base = _mesh._timeline.basePaths();
    for (int p = begin; p < end; p++) {
        _scene._paths[p] = edited._paths[p - begin];
        base[p] = MeshPath(_scene._paths[p]._color, _scene._CTMs[p], _scene._paths[p]._bounds);
    }
    libTess::triangulatePaths(_editMesh, _scene, begin, end);
    [self.metalLayer uploadMesh:_editMesh index:1];
    _editBegin = begin, _editEnd = end;
    [self updateHandles];
    [self updateInspector];
}

// Folds an edit back into the scene's mesh.
- (void)commitEdit {
    if (_editEnd == _editBegin)
        return;
    NSInteger selected = _selectedPath;
    [self triangulate];
    _selectedPath = selected;
    [self updateHandles];
}

- (void)makeHandleTemplates {
    // A unit circle in eight quadratics, and a unit square: a line's own bounds set its length and width.
    float circle[34], r = 1 / std::cos(M_PI / 8);
    for (int k = 0; k <= 16; k++) {
        float a = k * M_PI / 8, d = k & 1 ? r : 1;
        circle[k * 2] = d * std::cos(a), circle[k * 2 + 1] = d * std::sin(a);
    }
    float square[10] = { 0, -0.5, 1, -0.5, 1, 0.5, 0, 0.5, 0, -0.5 };
    Scene templates;
    Path line(true, false, RGBA8::Black(), { SubPath(square, 5, SubPath::kLine) });
    Path dot(true, false, RGBA8::Black(), { SubPath(circle, 17, SubPath::kQuadratic) });
    templates.addPath(line), templates.addPath(dot);
    libTess::triangulatePaths(_handleTemplates, templates, 0, 2);
}

// The selected shape's handles: lines from end points to their curves' controls, the controls, then the end points.
- (void)updateHandles {
    _handles.resize(0), _editPoints.resize(0);
    NSVGshape *shape = [self editedShape];
    RGBA8 blue(0, 122, 255, 255), white(255, 255, 255, 255);
    vector<EditHandle> controls, points;
    auto source = [&](const float *p) { return CGPointMake(p[0], _svgImage->height - p[1]); };
    for (NSVGpath *path = shape ? shape->paths : nullptr; path; path = path->next) {
        float *pts = path->pts;
        for (int i = 0; i + 3 < path->npts; i += 3) {
            if (isStraightCubic(pts + i * 2))
                continue;
            for (int k : { 1, 2 }) {
                CGPoint c = source(pts + (i + k) * 2), e = source(pts + (k == 1 ? i : i + 3) * 2);
                _handles.push_back({ EditHandle::kLine, float(e.x), float(e.y), float(c.x), float(c.y), kHandleLineWidth, blue });
                controls.push_back({ EditHandle::kDot, float(c.x), float(c.y), 0, 0, kHandleControlRadius, blue });
                _editPoints.push_back({ path, i + k });
            }
        }
        int n = closesOnStart(path) && path->npts > 1 ? path->npts - 1 : path->npts;
        for (int i = 0; i < n; i += 3) {
            CGPoint p = source(pts + i * 2);
            points.push_back({ EditHandle::kDot, float(p.x), float(p.y), 0, 0, kHandlePointRadius, blue });
            points.push_back({ EditHandle::kDot, float(p.x), float(p.y), 0, 0, kHandlePointInnerRadius, white });
            _editPoints.push_back({ path, i });
        }
    }
    _handles.insert(_handles.end(), controls.begin(), controls.end());
    _handles.insert(_handles.end(), points.begin(), points.end());
    
    // Each handle copies its template's triangles as its own path, after the scene's.
    _handleMesh.empty();
    int N = (int)_scene._paths.size();
    for (size_t k = 0; k < _handles.size(); k++) {
        int t = _handles[k].kind == EditHandle::kLine ? 0 : 1, begin = _handleTemplates._idxs[t], end = _handleTemplates._idxs[t + 1];
        _handleMesh._idxs.emplace_back((int)_handleMesh._tris.count());
        Triangle *tris = _handleMesh._tris.new_ptr(end - begin);
        for (int i = begin; i < end; i++) {
            tris[i - begin] = _handleTemplates._tris[i];
            tris[i - begin].p = (tris[i - begin].p & ~Triangle::kFlagsMask) | unsigned(N + k);
        }
    }
    _handleMesh._idxs.emplace_back((int)_handleMesh._tris.count());
    [self.metalLayer uploadMesh:_handleMesh index:2];
    [self.metalLayer reservePaths:N + _handles.size()];
}

// The handles' paths, after the scene's: each one's transform keeps its size on screen at any view transform.
- (void)writeHandlePaths:(MeshPath *)paths {
    AffineTransform view = self.viewCTM;
    float k = std::sqrt(std::abs(AffineTransformArea(view))), pixels = self.layer.contentsScale;
    vector<MeshPath>& templates = _handleTemplates._timeline.basePaths();
    MeshPath *path = paths + _scene._paths.size();
    for (EditHandle& h : _handles) {
        if (h.kind == EditHandle::kDot) {
            float r = h.size * pixels / k;
            *path++ = MeshPath(h.color, { r, 0, 0, r, h.x0, h.y0 }, templates[1].bounds);
        } else {
            // In its own space a line runs from (0, 0) along x, in pixels, so its triangles and miters stay the
            // template's at any length: the transform is a similarity.
            float dx = h.x1 - h.x0, dy = h.y1 - h.y0, length = std::sqrt(dx * dx + dy * dy);
            float ex = length ? dx / length : 1, ey = length ? dy / length : 0;
            *path++ = MeshPath(h.color, { ex / k, ey / k, -ey / k, ex / k, h.x0, h.y0 }, Bounds(0, -h.size * pixels * 0.5f, length * k, h.size * pixels * 0.5f));
        }
    }
    // While animating, only transforms are written, but the edited paths' bounds have changed.
    for (int p = _editBegin; p < _editEnd; p++)
        paths[p].bounds = _mesh._timeline.basePaths()[p].bounds, paths[p].color = _mesh._timeline.basePaths()[p].color;
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

- (void)triangulate {
    libTess::triangulate(_mesh, _scene);
    [self.metalLayer uploadMesh:_mesh];
    _editMesh.empty();
    [self.metalLayer uploadMesh:_editMesh index:1];
    _editBegin = _editEnd = 0;
    _hoverPath = _selectedPath = -1;
    _dragPoint = { nullptr, 0 };
    [self updateHandles];
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
    int N = (int)_scene._paths.size();
    meshState.colorsSize = N + (int)_handles.size();
    
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
    Mesh::writePathCalls(_mesh, paths, N, clipBounds, uniforms, meshState, 0, 0, _editBegin, _editEnd);
    if (_editEnd == _editBegin && _handles.size() == 0)
        return;
    // The edited paths' edges draw in their place among the scene's, which blend in order, and the handles' last. The
    // interiors are opaque and ordered by depth.
    _editMesh._calls.resize(0), _handleMesh._calls.resize(0);
    if (_editEnd > _editBegin)
        Mesh::writePathCalls(_editMesh, paths + _editBegin, _editEnd - _editBegin, clipBounds, uniforms, meshState, 1, _editBegin);
    MeshState handleState = meshState;
    handleState.showInterior = false;
    if (_handles.size())
        Mesh::writePathCalls(_handleMesh, paths + N, _handles.size(), clipBounds, uniforms, handleState, 2, N);
    vector<Call> calls;
    for (vector<Call> *v : { & _mesh._calls, & _editMesh._calls })
        for (Call& c : *v)
            if (c.type == kCallTypeMeshInteriorIndexed)
                calls.push_back(c);
    bool edited = false;
    for (Call& c : _mesh._calls)
        if (c.type == kCallTypeMeshTriangles) {
            if (!edited && c.firstPath >= _editEnd) {
                for (Call& e : _editMesh._calls)
                    if (e.type == kCallTypeMeshTriangles)
                        calls.push_back(e);
                edited = true;
            }
            calls.push_back(c);
        }
    for (Call& e : _editMesh._calls)
        if (!edited && e.type == kCallTypeMeshTriangles)
            calls.push_back(e);
    calls.insert(calls.end(), _handleMesh._calls.begin(), _handleMesh._calls.end());
    _mesh._calls = calls;
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
    }
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    layer.contents = (__bridge id)image;
    CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(rgb);
}

#pragma mark - LayerDelegate

- (Mesh *)meshForLayer:(CALayer *)layer paths:(MeshPath *)paths {
    double timerTime = _displayLink ? [NSDate date].timeIntervalSinceReferenceDate : 0;
    _mesh._timeline.writePathsForTime(timerTime, paths);
    [self writeHandlePaths:paths];
    [self writeDrawCalls: paths];
    return & _mesh;
}

#pragma mark - Properties

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    
    // The source paths Core Graphics draws: nanosvg's own, before Scene converts them. It parses in place, so a copy.
    if (_svgImage)
        nsvgDelete(_svgImage), _svgImage = nullptr;
    if (_svgData) {
        char *data = (char *)malloc(_svgData.length + 1);
        if (data) {
            memcpy(data, _svgData.bytes, _svgData.length), data[_svgData.length] = 0;
            _svgImage = nsvgParse(data, "px", 96);
            free(data);
        }
    }
    if (_svgImage) {
        _scene.empty();
        _scene.addImage(_svgImage);
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
