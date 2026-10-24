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
#import "SVG.hpp"
#import "libTess.hpp"

constexpr float kScrollLineDelta = 10;


using namespace std;

// Editing handles, in points.
constexpr float kHandleLineWidth = 1, kHandleControlRadius = 3, kHandlePointRadius = 4, kHandlePointInnerRadius = 2.5, kHandleHitRadius = 6;

// A point of the selected shape that can be dragged, in one of its path's cubic contours: an end point, or a curve's
// control point. A contour of -1 is none.
struct EditPoint {
    int contour, index;
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
@property(nonatomic) CGSize svgSize;           // the loaded SVG's size, about whose height the scene flips y
@property(nonatomic) vector<string> shapeIds;   // its shapes' ids, by Instance::source
@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) BOOL showColors;
@property(nonatomic) BOOL showEdgeTriangles;
@property(nonatomic) BOOL showInterior;
@property(nonatomic) BOOL showPolygons;
@property(nonatomic) BOOL showTriangles;
@property(nonatomic) BOOL useStrokeColor;
@property(nonatomic) BOOL inspect;              // debug: the path under the mouse is highlighted and described
@property(nonatomic) BOOL solo;                 // debug: only the inspected path is drawn
@property(nonatomic) BOOL perspective;          // the page tilted back in perspective
@property(nonatomic) BOOL cube;                 // the scene on the faces of a turning cube
@property(nonatomic) BOOL timer;                // the timeline animates the paths
@property(nonatomic) double cubeStart;          // when the cube started turning
@property(nonatomic) CGAffineTransform cubeView;    // the view transform then: gestures since move the cube on screen
@property(nonatomic) NSInteger hoverPath, selectedPath;
@property(nonatomic) Mesh editMesh;             // the selected shape's paths while its points are edited
@property(nonatomic) int editBegin, editEnd;    // those paths' scene indices, which the scene's mesh leaves out
@property(nonatomic) Mesh handleMesh;           // a line and a dot, triangulated once, which every handle draws as an instance
@property(nonatomic) vector<EditHandle> handles;
@property(nonatomic) vector<EditPoint> editPoints;
@property(nonatomic) EditPoint dragPoint;       // the point being dragged
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
    self.dragPoint = { -1, 0 };
    [self makeHandleTemplates];
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

// The display link runs while the timeline or the cube animates.
- (void)updateDisplayLink {
    if (!(_timer || _cube))
        [self stopTimer];
    else if (!_displayLink)
        [self resetTimer];
}

- (IBAction)toggleTimer:(id)sender {
    _timer = !_timer;
    [self updateDisplayLink];
    [self redraw];
}

- (IBAction)toggleCube:(id)sender {
    [self commitEdit];
    _cube = !_cube;
    _cubeStart = [NSDate date].timeIntervalSinceReferenceDate;
    _cubeView = self.transform.affineTransform;
    _hoverPath = _selectedPath = -1;
    [self updateHandles];
    [self updateInspector];
    [self updateDisplayLink];
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
    _dragPoint = e == -1 ? EditPoint { -1, 0 } : _editPoints[e];
    if (_dragPoint.contour != -1 || !_inspect || _solo)
        return;
    NSInteger p = [self pathAt:event.locationInWindow];
    if ([self shapeOf:p] != [self shapeOf:_selectedPath])
        [self commitEdit];
    _selectedPath = p;
    if (_selectedPath != -1)
        printf("%s\n", [self describePath:_selectedPath].UTF8String);
    [self updateHandles];
    [self updateInspector];
}

- (void)mouseUp:(NSEvent *)event {
    _dragPoint = { -1, 0 };
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
    if (_dragPoint.contour != -1) {
        CGPoint s = [self scenePointAt:event.locationInWindow];
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
- (IBAction)togglePerspective:(id)sender {
    _perspective = !_perspective;
    [self redraw];
}

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

// The topmost path whose polygon (its quadratics' chords) holds a point in window coordinates. Only paths whose
// triangulated bounds hold it are converted to test.
- (NSInteger)pathAt:(NSPoint)location {
    if (_cube)
        return -1;
    CGPoint u = [self unproject:location];
    float x = u.x, y = u.y;
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    float sx = x * inv.a + y * inv.c + inv.tx, sy = x * inv.b + y * inv.d + inv.ty;
    vector<MeshPath>& base = _mesh._timeline.basePaths();
    for (NSInteger i = std::min((NSInteger)_scene._instances.size(), (NSInteger)base.size()) - 1; i >= 0; i--) {
        AffineTransform m = AffineTransformInvert(_scene._instances[i].ctm);
        float px = sx * m.a + sy * m.c + m.tx, py = sx * m.b + sy * m.d + m.ty;
        if (base[i].bounds.contains(px, py) && libTess::convert(_scene._paths[_scene._instances[i].path]).contains(px, py))
            return i;
    }
    return -1;
}

// The source shape a path came from, or -1.
- (int)shapeOf:(NSInteger)p {
    return p < 0 || p >= (NSInteger)_scene._instances.size() ? -1 : _scene._instances[p].source;
}

- (NSString *)describePath:(NSInteger)p {
    Path path = libTess::convert(_scene._paths[_scene._instances[p].path]);
    size_t quadratics = 0;
    for (Path::Contour& c : path._contours)
        quadratics += c.npoints / 2;
    // An edited path's triangles are in the edit mesh, whose indices start at its first path.
    bool edited = p >= _editBegin && p < _editEnd;
    Mesh& mesh = edited ? _editMesh : _mesh;
    int k = int(edited ? p - _editBegin : p), base = mesh._idxs[k], end = mesh._idxs[k + 1], hulls = 0;
    for (int i = base; i < end; i++)
        hulls += (mesh._tris[mesh._triangleInstances[i].triangle].flags & Triangle::kHull) != 0;
    int shape = [self shapeOf:p];
    NSString *source = shape >= 0 && shape < (int)_shapeIds.size() ? [NSString stringWithFormat:@" · shape %d \"%s\"", shape, _shapeIds[shape].c_str()] : @"";
    return [NSString stringWithFormat:@"Path %ld of %zu%@ · %s · %zu subpaths, %zu quadratics · %d triangles, %d hulls · bounds %g %g %g %g",
            (long)p, _scene._instances.size(), source, path.stroked() ? "stroke" : "fill", path._contours.size(), quadratics, end - base, hulls,
            path._bounds.lx, path._bounds.ly, path._bounds.ux, path._bounds.uy];
}

#pragma mark - Editing

// A selected path's cubic contours show their points as handles, which drag to edit them: an end point carries its
// curves' control points with it, and a closed contour's start and end move together. The points move in place, and its
// shape's other paths (its fill or its stroke) take them too. Only those paths are triangulated again as they change, into
// their own mesh, which draws in their place; deselecting triangulates the whole scene again.

// The selected path, whose points can be edited, or null.
- (Path *)editedPath {
    return _inspect && _selectedPath != -1 ? & _scene._paths[_scene._instances[_selectedPath].path] : nullptr;
}

// A scene point in device pixels.
- (CGPoint)devicePointForScene:(const float *)p {
    AffineTransform t = self.viewCTM;
    float x = p[0], y = p[1];
    simd_float3 h = simd_mul(self.projection, simd_make_float3(x * t.a + y * t.c + t.tx, x * t.b + y * t.d + t.ty, 1));
    return CGPointMake(h.x / h.z, h.y / h.z);
}

- (CGPoint)scenePointAt:(NSPoint)location {
    CGPoint u = [self unproject:location];
    float x = u.x, y = u.y;
    AffineTransform inv = AffineTransformInvert(self.viewCTM);
    return CGPointMake(x * inv.a + y * inv.c + inv.tx, x * inv.b + y * inv.d + inv.ty);
}

static bool closesOnStart(const float *pts, int npoints) {
    return npoints > 1 && pts[0] == pts[npoints * 2 - 2] && pts[1] == pts[npoints * 2 - 1];
}

// The edit point under a location in window coordinates, the nearest within kHandleHitRadius, or -1.
- (NSInteger)editPointAt:(NSPoint)location {
    Path *path = [self editedPath];
    if (_cube || !path)
        return -1;
    NSPoint v = [self convertPoint:location fromView:nil];
    float x = v.x * self.layer.contentsScale, y = v.y * self.layer.contentsScale, r = kHandleHitRadius * self.layer.contentsScale, best = r * r;
    NSInteger found = -1;
    for (size_t i = 0; i < _editPoints.size(); i++) {
        CGPoint d = [self devicePointForScene:path->points(_editPoints[i].contour) + _editPoints[i].index * 2];
        float d2 = (d.x - x) * (d.x - x) + (d.y - y) * (d.y - y);
        if (d2 <= best)
            best = d2, found = i;
    }
    return found;
}

// Moves an edit point to (x, y) in scene coordinates. Lines (straight cubics) keep their controls at thirds, so they stay
// lines; curves' controls move with their end points.
- (void)moveEditPoint:(EditPoint)e x:(float)x y:(float)y {
    Path *path = [self editedPath];
    float *pts = path->points(e.contour);
    int n = path->_contours[e.contour].npoints, i = e.index;
    if (i % 3) {
        pts[i * 2] = x, pts[i * 2 + 1] = y;
        return;
    }
    float dx = x - pts[i * 2], dy = y - pts[i * 2 + 1];
    vector<int> ends = { i };
    if (i == 0 && closesOnStart(pts, n))
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

// Triangulates just the selected shape's paths again, from their edited points.
- (void)rebuildSelectedShape {
    Path *path = [self editedPath];
    if (!path)
        return;
    path->updateBounds();
    int source = [self shapeOf:_selectedPath], begin = (int)_selectedPath, end = begin + 1;
    while (source >= 0 && begin > 0 && _scene._instances[begin - 1].source == source)
        begin--;
    while (source >= 0 && end < (int)_scene._instances.size() && _scene._instances[end].source == source)
        end++;
    // A shape's paths have the same contours: its fill's, and its stroke's.
    for (int p = begin; p < end; p++) {
        Path& other = _scene._paths[_scene._instances[p].path];
        if (& other != path)
            other._points = path->_points, other._bounds = path->_bounds;
    }
    // The paths' bounds as triangulated come from converting them, so the scene's mesh takes them from the edit's.
    libTess::triangulatePaths(_editMesh, _scene, begin, end);
    vector<MeshPath>& base = _mesh._timeline.basePaths();
    for (int p = begin; p < end; p++)
        base[p] = _editMesh._timeline.basePaths()[p];
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
    Path line, dot;
    line.add(Path::kLines, square, 5, true), dot.add(Path::kQuadratics, circle, 17, true);
    templates.addInstance(templates.addPath(line), RGBA8::Black()), templates.addInstance(templates.addPath(dot), RGBA8::Black());
    libTess::triangulatePaths(_handleMesh, templates, 0, 2);
}

// The selected shape's handles: lines from end points to their curves' controls, the controls, then the end points.
- (void)updateHandles {
    _handles.resize(0), _editPoints.resize(0);
    Path *path = [self editedPath];
    RGBA8 blue(0, 122, 255, 255), white(255, 255, 255, 255);
    vector<EditHandle> controls, points;
    for (int c = 0; path && c < (int)path->_contours.size(); c++) {
        const Path::Contour& contour = path->_contours[c];
        if (contour.kind != Path::kCubics)
            continue;
        const float *pts = path->points(c);
        for (int i = 0; i + 3 < contour.npoints; i += 3) {
            if (isStraightCubic(pts + i * 2))
                continue;
            for (int k : { 1, 2 }) {
                const float *q = pts + (i + k) * 2, *e = pts + (k == 1 ? i : i + 3) * 2;
                _handles.push_back({ EditHandle::kLine, e[0], e[1], q[0], q[1], kHandleLineWidth, blue });
                controls.push_back({ EditHandle::kDot, q[0], q[1], 0, 0, kHandleControlRadius, blue });
                _editPoints.push_back({ c, i + k });
            }
        }
        int n = closesOnStart(pts, contour.npoints) ? contour.npoints - 1 : contour.npoints;
        for (int i = 0; i < n; i += 3) {
            const float *p = pts + i * 2;
            points.push_back({ EditHandle::kDot, p[0], p[1], 0, 0, kHandlePointRadius, blue });
            points.push_back({ EditHandle::kDot, p[0], p[1], 0, 0, kHandlePointInnerRadius, white });
            _editPoints.push_back({ c, i });
        }
    }
    _handles.insert(_handles.end(), controls.begin(), controls.end());
    _handles.insert(_handles.end(), points.begin(), points.end());
    
    // Each handle is an instance of its template, after the scene's. Handles draw no interior, so the templates' own
    // interior, which the mesh keeps, is never drawn.
    int N = (int)_scene._instances.size();
    vector<int> templates;
    for (EditHandle& h : _handles)
        templates.push_back(h.kind == EditHandle::kLine ? 0 : 1);
    _handleMesh.writeTriangleInstances(templates.data(), (int)templates.size(), N);
    [self.metalLayer uploadMesh:_handleMesh index:2];
    [self.metalLayer reservePaths:N + _handles.size()];
}

// The handles' paths, after the scene's: each one's transform keeps its size on screen at any view transform.
- (void)writeHandlePaths:(MeshPath *)paths {
    AffineTransform view = self.viewCTM;
    float k = std::sqrt(std::abs(AffineTransformArea(view))), pixels = self.layer.contentsScale;
    vector<MeshPath>& templates = _handleMesh._timeline.basePaths();
    MeshPath *path = paths + _scene._instances.size();
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

// The inspected path as a standalone SVG with the loaded SVG's size: its contours exactly, in SVG coordinates (y flipped
// back), and its fill or stroke in its instance's colour.
- (IBAction)copy:(id)sender {
    NSInteger p = self.inspectedPath;
    if (p == -1) {
        NSBeep();
        return;
    }
    Path& path = _scene._paths[_scene._instances[p].path];
    RGBA8 color = _scene._instances[p].color;
    NSString *hex = [NSString stringWithFormat:@"#%02x%02x%02x", color.r, color.g, color.b];
    float height = _svgSize.height;
    NSMutableString *d = [NSMutableString string];
    for (int c = 0; c < (int)path._contours.size(); c++) {
        const Path::Contour& contour = path._contours[c];
        const float *pts = path.points(c);
        int degree = contour.kind == Path::kCubics ? 3 : contour.kind == Path::kQuadratics ? 2 : 1;
        [d appendFormat:@"M%.9g %.9g", pts[0], height - pts[1]];
        for (int i = 1; i + degree - 1 < contour.npoints; i += degree) {
            [d appendString:degree == 3 ? @" C" : degree == 2 ? @" Q" : @" L"];
            for (int k = 0; k < degree; k++)
                [d appendFormat:@"%@%.9g %.9g", k ? @" " : @"", pts[(i + k) * 2], height - pts[(i + k) * 2 + 1]];
        }
        [d appendString:contour.closed ? @" Z " : @" "];
    }
    const char *joins[] = { "miter", "round", "bevel" }, *caps[] = { "butt", "round", "square" };
    NSString *paint = path.stroked()
        ? [NSString stringWithFormat:@"fill=\"none\" stroke=\"%@\" stroke-width=\"%.9g\" stroke-linejoin=\"%s\" stroke-linecap=\"%s\" stroke-miterlimit=\"%g\"",
           hex, path._strokeWidth, joins[path._join], caps[path._cap], path._miterLimit]
        : [NSString stringWithFormat:@"fill=\"%@\" fill-rule=\"%s\"", hex, path._evenOdd ? "evenodd" : "nonzero"];
    NSMutableString *dashes = [NSMutableString string];
    for (size_t i = 0; path.stroked() && i < path._dashes.size(); i++)
        [dashes appendFormat:@"%@%.9g", i ? @" " : @" stroke-dasharray=\"", path._dashes[i]];
    if (dashes.length)
        [dashes appendFormat:@"\" stroke-dashoffset=\"%.9g\"", path._dashOffset];
    NSString *opacity = color.a == 255 ? @"" : [NSString stringWithFormat:@" opacity=\"%.4g\"", color.a / 255.0];
    NSString *svg = [NSString stringWithFormat:@"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%g\" height=\"%g\" viewBox=\"0 0 %g %g\">\n"
                     "<!-- %@ -->\n<path %@%@%@ d=\"%@\"/>\n</svg>\n",
                     _svgSize.width, _svgSize.height, _svgSize.width, _svgSize.height, [self describePath:p], paint, dashes, opacity, d];
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
    _dragPoint = { -1, 0 };
    [self updateHandles];
}

- (void)writeDrawCalls:(MeshPath *)paths {
    State uniforms;
    uniforms.CTM = self.viewCTM;
    uniforms.perspective = self.projection;
    uniforms.strokeColor = { 0, 0, 0, 1 };
    uniforms.width = 0;
    
    MeshState meshState;
    meshState.showEdgeTriangles = _showEdgeTriangles;
    meshState.showInterior = _showInterior;
    meshState.showPolygons = _showPolygons;
    meshState.showTriangles = _showTriangles;
    meshState.useStrokeColor = _useStrokeColor;
    int N = (int)_scene._instances.size();
    meshState.colorsSize = N + (int)_handles.size();
    
    _mesh._calls.resize(0);
	MeshPath *path = & paths[0];
	for (int i = 0; i < _scene._instances.size(); i++, path++)
		path->color = _showColors ? _scene._instances[i].color : RGBA8::Black();
    // Debug: the inspected path in red, and with solo on, every other path transparent.
    NSInteger inspected = self.inspectedPath;
    if (inspected != -1) {
        for (int i = 0; _solo && i < _scene._instances.size(); i++)
            paths[i].color.a = 0;
        paths[inspected].color = RGBA8::Red();
    }
    
    float pw = std::ceil(self.metalLayer.bounds.size.width * self.metalLayer.contentsScale), ph = std::ceil(self.metalLayer.bounds.size.height * self.metalLayer.contentsScale);
    if (_cube) {
        [self writeCubeCalls:paths count:N width:pw height:ph uniforms:uniforms meshState:meshState];
        return;
    }
    // What the drawable shows of the flat page: its corners projected back, as perspective shows the far side more of it.
    simd_float3x3 inverse = simd_inverse(self.projection);
    Bounds clipBounds = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (simd_float3 corner : { simd_make_float3(0, 0, 1), simd_make_float3(pw, 0, 1), simd_make_float3(0, ph, 1), simd_make_float3(pw, ph, 1) }) {
        simd_float3 h = simd_mul(inverse, corner);
        clipBounds.unite(Bounds(h.x / h.z, h.y / h.z, h.x / h.z, h.y / h.z));
    }
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

// Core Graphics ground truth: the scene's paths as drawn, cubics and all, and strokes with their joins, caps and dashes,
// rather than the quadratics and stroke outlines the mesh triangulates. Like Rasterizer, it renders into a DeviceRGB
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
    const CGLineJoin joins[] = { kCGLineJoinMiter, kCGLineJoinRound, kCGLineJoinBevel };
    const CGLineCap caps[] = { kCGLineCapButt, kCGLineCapRound, kCGLineCapSquare };
    for (Instance& instance : _scene._instances) {
        Path& path = _scene._paths[instance.path];
        CGMutablePathRef cgPath = CGPathCreateMutable();
        for (int c = 0; c < (int)path._contours.size(); c++) {
            const Path::Contour& contour = path._contours[c];
            const float *p = path.points(c);
            CGPathMoveToPoint(cgPath, NULL, p[0], p[1]);
            if (contour.kind == Path::kCubics)
                for (int i = 1; i + 2 < contour.npoints; i += 3)
                    CGPathAddCurveToPoint(cgPath, NULL, p[i * 2], p[i * 2 + 1], p[i * 2 + 2], p[i * 2 + 3], p[i * 2 + 4], p[i * 2 + 5]);
            else if (contour.kind == Path::kQuadratics)
                for (int i = 1; i + 1 < contour.npoints; i += 2)
                    CGPathAddQuadCurveToPoint(cgPath, NULL, p[i * 2], p[i * 2 + 1], p[i * 2 + 2], p[i * 2 + 3]);
            else
                for (int i = 1; i < contour.npoints; i++)
                    CGPathAddLineToPoint(cgPath, NULL, p[i * 2], p[i * 2 + 1]);
            if (contour.closed)
                CGPathCloseSubpath(cgPath);
        }
        RGBA8 color = _showColors ? instance.color : RGBA8::Black();
        CGFloat r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0, a = color.a / 255.0;
        CGContextSaveGState(ctx);
        CGContextConcatCTM(ctx, CGAffineTransformMake(instance.ctm.a, instance.ctm.b, instance.ctm.c, instance.ctm.d, instance.ctm.tx, instance.ctm.ty));
        CGContextAddPath(ctx, cgPath);
        if (!path.stroked()) {
            CGContextSetRGBFillColor(ctx, r, g, b, a);
            path._evenOdd ? CGContextEOFillPath(ctx) : CGContextFillPath(ctx);
        } else {
            CGContextSetRGBStrokeColor(ctx, r, g, b, a);
            CGContextSetLineWidth(ctx, path._strokeWidth);
            CGContextSetLineJoin(ctx, joins[path._join]), CGContextSetLineCap(ctx, caps[path._cap]);
            CGContextSetMiterLimit(ctx, path._miterLimit);
            vector<CGFloat> dashes(path._dashes.begin(), path._dashes.end());
            CGContextSetLineDash(ctx, path._dashOffset, dashes.size() ? dashes.data() : NULL, dashes.size());
            CGContextStrokePath(ctx);
        }
        CGContextRestoreGState(ctx);
        CGPathRelease(cgPath);
    }
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    layer.contents = (__bridge id)image;
    CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(rgb);
}

#pragma mark - LayerDelegate

- (Mesh *)meshForLayer:(CALayer *)layer paths:(MeshPath *)paths {
    double timerTime = _timer ? [NSDate date].timeIntervalSinceReferenceDate : 0;
    _mesh._timeline.writePathsForTime(timerTime, paths);
    [self writeHandlePaths:paths];
    [self writeDrawCalls: paths];
    return & _mesh;
}

#pragma mark - Properties

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    
    // The scene keeps the SVG's shapes as paths; the view keeps only its size and its shapes' ids. nanosvg parses in
    // place, so a copy.
    _scene.empty(), _shapeIds.resize(0), _svgSize = CGSizeZero;
    char *data = _svgData ? (char *)malloc(_svgData.length + 1) : nullptr;
    if (data) {
        memcpy(data, _svgData.bytes, _svgData.length), data[_svgData.length] = 0;
        if (NSVGimage *image = nsvgParse(data, "px", 96)) {
            SVG::addImage(_scene, image);
            _svgSize = CGSizeMake(image->width, image->height);
            for (NSVGshape *shape = image->shapes; shape; shape = shape->next)
                _shapeIds.push_back(shape->id);
            nsvgDelete(image);
        }
        free(data);
    }
    [self triangulate];
    [self redraw];
}

// The perspective view's homography of device pixels: the page tilted back by 45 degrees about the drawable's horizontal
// centre line, seen from the drawable's height away. Relative to the centre, (x, y) goes to (x, y cos) / (1 + y sin / d),
// so the top recedes. The horizon, where the page's far side converges, lies d cos / sin above the centre: beyond the
// top for this tilt, so every point on screen projects back onto the page.
- (simd_float3x3)projection {
    if (!_perspective)
        return matrix_identity_float3x3;
    float w = std::ceil(self.bounds.size.width * self.layer.contentsScale), h = std::ceil(self.bounds.size.height * self.layer.contentsScale);
    float cx = w * 0.5f, cy = h * 0.5f, d = h, c = std::cos(float(M_PI) / 4), s = std::sin(float(M_PI) / 4), k = s / d;
    return simd_matrix_from_rows(simd_make_float3(1, cx * k, -cx * cy * k),
                                 simd_make_float3(0, c + cy * k, cy * (1 - c) - cy * cy * k),
                                 simd_make_float3(0, k, 1 - cy * k));
}

// The scene on each face of a cube a third of the drawable across, turning about two axes and seen from twice its size
// away. A face is the scene fitted to a square of side S, (a, b) in its pixels, at o + a u + b v in 3D, its axes chosen so
// that u x v points inward and no face reads mirrored from outside. Seen from (0, 0, -D) looking along z, a point P is at
// (P.xy / w) about the centre with w = 1 + P.z / D, so the face's projection is a homography of its pixels. Only faces
// turned toward the eye are drawn: they never overlap on screen, so the depth buffer orders each as it does the page.
// Gestures since the cube appeared (zoom, pan, rotate) move it on screen, as view, a similarity of device pixels. Its
// scale s also scales the faces' fit, with each projection taking it out again: dilation and anti-aliasing are in fit
// pixels, so the faces draw at the resolution they are seen at rather than as a magnified image.
- (void)writeCubeCalls:(MeshPath *)paths count:(int)n width:(float)w height:(float)h uniforms:(State)uniforms meshState:(MeshState)meshState {
    float S = std::min(w, h) / 3, D = 2 * S, cx = w * 0.5f, cy = h * 0.5f;
    CGAffineTransform g = CGAffineTransformConcat(CGAffineTransformInvert(_cubeView), self.transform.affineTransform);
    float px = self.layer.contentsScale, sv = std::sqrt(std::abs(float(g.a * g.d - g.b * g.c)));
    simd_float3x3 view = simd_matrix(simd_make_float3(g.a, g.b, 0), simd_make_float3(g.c, g.d, 0), simd_make_float3(g.tx * px, g.ty * px, 1));
    simd_float3x3 unscale = simd_matrix(simd_make_float3(1 / sv, 0, 0), simd_make_float3(0, 1 / sv, 0), simd_make_float3(0, 0, 1));
    Bounds b = _mesh._bounds;
    float k = S / std::max(b.ux - b.lx, b.uy - b.ly);
    uniforms.CTM = { k * sv, 0, 0, k * sv, ((S - (b.ux - b.lx) * k) * 0.5f - b.lx * k) * sv, ((S - (b.uy - b.ly) * k) * 0.5f - b.ly * k) * sv };
    float t = float([NSDate date].timeIntervalSinceReferenceDate - _cubeStart), ay = 0.7f * t, ax = 0.45f * t;
    simd_float3x3 R = simd_mul(simd_matrix(simd_make_float3(std::cos(ay), 0, -std::sin(ay)), simd_make_float3(0, 1, 0), simd_make_float3(std::sin(ay), 0, std::cos(ay))),
                               simd_matrix(simd_make_float3(1, 0, 0), simd_make_float3(0, std::cos(ax), std::sin(ax)), simd_make_float3(0, -std::sin(ax), std::cos(ax))));
    struct Face { simd_float3 n, u, v; };
    const Face faces[6] = {
        { { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } }, { { 0, 0, 1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } }, { { -1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
    };
    auto column = [&](simd_float3 p, float one) { return simd_make_float3(p.x + cx * (one + p.z / D), p.y + cy * (one + p.z / D), one + p.z / D); };
    for (const Face& f : faces) {
        simd_float3 nr = simd_mul(R, f.n), u = simd_mul(R, f.u), v = simd_mul(R, f.v), c = nr * (S * 0.5f);
        if (simd_dot(nr, c + simd_make_float3(0, 0, D)) >= 0)
            continue;
        simd_float3x3 face = simd_matrix(column(u, 0), column(v, 0), column(c - (u + v) * (S * 0.5f), 1));
        uniforms.perspective = simd_mul(view, simd_mul(face, unscale));
        Mesh::writePathCalls(_mesh, paths, n, Bounds(0, 0, S * sv, S * sv), uniforms, meshState);
    }
}

// A window location in the flat page's device pixels, before the perspective view.
- (CGPoint)unproject:(NSPoint)location {
    NSPoint v = [self convertPoint:location fromView:nil];
    simd_float3 h = simd_mul(simd_inverse(self.projection), simd_make_float3(v.x * self.layer.contentsScale, v.y * self.layer.contentsScale, 1));
    return CGPointMake(h.x / h.z, h.y / h.z);
}

- (AffineTransform)viewCTM {
    CGAffineTransform viewCTM = self.transform.affineTransform;
    AffineTransform ctm = { float(viewCTM.a), float(viewCTM.b), float(viewCTM.c), float(viewCTM.d), float(viewCTM.tx), float(viewCTM.ty) };
    AffineTransform retina = { float(self.layer.contentsScale), 0, 0, float(self.layer.contentsScale), 0, 0 };
    AffineTransform skew = { 1, 0, 0, 1, 0, 0 };
    return AffineTransformConcat(skew, AffineTransformConcat(ctm, retina));
}

@end
