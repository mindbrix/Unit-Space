//
//  libTess.hpp
//  Unit Space
//
//  Created by Nigel Barber on 06/05/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//

#import <dispatch/dispatch.h>

#import "Scene.hpp"
#import <vector>
#import <unordered_map>
#import <atomic>
#import "tesselator.h"
#import "Algorithms.hpp"

constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
constexpr int kPolySize = 3;
#ifndef LINE_TURN_LIMIT
#define LINE_TURN_LIMIT 0.0174532925 // 1 degree
#endif
// Curves whose tangents turn by less than this are drawn as lines (radians). Each curve costs a hull triangle, so
// nearly straight ones are cheaper as lines. It is an angle rather than a distance so it is independent of scale:
// a distance in path units that suits a map leaves small text faceted.
constexpr float kLineTurnLimit = LINE_TURN_LIMIT;

struct libTess {
    static void addPathContours(TESStesselator *dst, Path& path) {
        for (SubPath& s : path._subPaths)
            if (path._strokeWidth) {
                float w, x0, y0, x1, y1, x2, y2, *q, *qend, dx, dy, l, nx, ny, pnx, pny, det;
                w = path._strokeWidth * 0.5, pnx = pny = 0;
                q = s._quadratics.get(), qend = q + s._quadratics.count();
                if (path._closed) {
                    x0 = *(qend - 6), y0 = *(qend - 5), x2 = *(qend - 2), y2 = *(qend - 1);
                    dx = x2 - x0, dy = y2 - y0, l = std::sqrt(dx * dx + dy * dy), pnx = -dy / l, pny = dx / l;
    
                    x0 = *(qend - 2), y0 = *(qend - 1), x2 = *q++, y2 = *q++, x1 = (x0 + x2) * 0.5, y1 = (y0 + y2) * 0.5;
                } else
                    x0 = *q++, y0 = *q++, x1 = *q++, y1 = *q++, x2 = *q++, y2 = *q++;
                while (q <= qend) {
                    if (x0 != x2 || y0 != y2) {
                        dx = x2 - x0, dy = y2 - y0, l = std::sqrt(dx * dx + dy * dy), nx = -dy / l, ny = dx / l;
                        det = pnx * ny - pny * nx;
                        float polygon[10] = {
                            x0 + nx * w, y0 + ny * w,
                            det == 0 ? x0 : det < 0 ? x0 + pnx * w : x0 - pnx * w, det == 0 ? y0 : det < 0 ? y0 + pny * w : y0 - pny * w,
                            x0 - nx * w, y0 - ny * w,
                            x2 - nx * w, y2 - ny * w,
                            x2 + nx * w, y2 + ny * w,
                        };
                        tessAddContour(dst, 2, polygon, kPointStride, 5);
                        
                        pnx = nx, pny = ny;
                    }
                    x0 = x2, y0 = y2, x1 = *q++, y1 = *q++, x2 = *q++, y2 = *q++;
                }
            } else
                tessAddContour(dst, 2, s._quadratics.get(), kPointStride * 2, (int)s._quadratics.count() / 4 + 1);
    }
        
    // The path quadratic joining two contour vertices, if there is one.
    struct Segment {
        Segment() : found(false), cx(0), cy(0) {}
        bool found;
        float cx, cy;
    };
    
    // Maps tessellator input vertices back to a path's quadratics. Each subpath adds its on-curve points in order.
    struct PathVertices {
        PathVertices(Path& path) : stroked(path._strokeWidth != 0) {
            int base = 0;
            for (SubPath& s : path._subPaths) {
                bases.push_back(base), subPaths.push_back(& s);
                int n = (int)s._quadratics.count() / 4 + 1;
                for (int i = 0; i < n; i++)
                    positions[key(s._quadratics[i * 4], s._quadratics[i * 4 + 1])].push_back(base + i);
                base += n;
            }
            bases.push_back(base);
        }
        // The segment joining input vertices a and b. The tessellator merges coincident vertices (stroke joins meet
        // their runs exactly), returning just one of their indices, so try every input vertex at each position.
        Segment segment(TESSindex a, TESSindex b, const float *pa, const float *pb) {
            Segment seg = segment(a, b);
            if (seg.found || stroked || a == TESS_UNDEF || b == TESS_UNDEF)
                return seg;
            auto ia = positions.find(key(pa[0], pa[1])), ib = positions.find(key(pb[0], pb[1]));
            if (ia == positions.end() || ib == positions.end() || (ia->second.size() == 1 && ib->second.size() == 1))
                return seg;
            for (int va : ia->second)
                for (int vb : ib->second)
                    if ((seg = segment(va, vb)).found)
                        return seg;
            return seg;
        }
        // The segment joining input vertices a and b, if they are neighbours on the same subpath.
        Segment segment(TESSindex a, TESSindex b) {
            Segment seg;
            if (stroked || a == TESS_UNDEF || b == TESS_UNDEF)
                return seg;
            int sp = int(upper_bound(bases.begin(), bases.end(), (int)a) - bases.begin()) - 1, la, lb, first;
            if (sp < 0 || (int)b < bases[sp] || (int)b >= bases[sp + 1])
                return seg;
            la = (int)a - bases[sp], lb = (int)b - bases[sp];
            int n = bases[sp + 1] - bases[sp];
            const float *q = subPaths[sp]->_quadratics.get();
            // A closed subpath repeats its first point last, and the tessellator may merge them to either index,
            // so count its vertices modulo the n - 1 distinct ones.
            int m = n > 2 && q[0] == q[(n - 1) * 4] && q[1] == q[(n - 1) * 4 + 1] ? n - 1 : 0;
            if (m)
                la %= m, lb %= m;
            if (lb == la + 1 || (m && lb == 0 && la == m - 1)) first = la;
            else if (la == lb + 1 || (m && la == 0 && lb == m - 1)) first = lb;
            else return seg;
            q += first * 4;
            seg.found = true, seg.cx = q[2], seg.cy = q[3];
            return seg;
        }
        static uint64_t key(float x, float y) {
            uint32_t ux, uy;
            memcpy(& ux, & x, 4), memcpy(& uy, & y, 4);
            return (uint64_t)ux << 32 | uy;
        }
        bool stroked;
        vector<int> bases;
        vector<SubPath *> subPaths;
        unordered_map<uint64_t, vector<int>> positions;
    };
    
    // A boundary contour segment from (x, y) to the next segment's start, and its control point when it is a curve.
    struct ContourSegment {
        float x, y, cx, cy;
        bool curved, concave;   // concave: the control point lies inside the fill, so the curve bows inward
    };
    
    // Lines carry their midpoint as control, and curves turning less than kLineTurnLimit are drawn as lines too.
    // Boundary contours keep the fill on their left, so a control point left of its chord is concave.
    static void classify(ContourSegment& s, float ex, float ey) {
        float ax = s.cx - s.x, ay = s.cy - s.y, bx = ex - s.cx, by = ey - s.cy;
        float turn = std::atan2(std::abs(ax * by - ay * bx), ax * bx + ay * by);
        s.curved = (ax != 0 || ay != 0) && (bx != 0 || by != 0) && turn > kLineTurnLimit;
        s.concave = s.curved && (ex - s.x) * (s.cy - s.y) - (ey - s.y) * (s.cx - s.x) > 0;
    }
    
    // Inner polygon edges must not cross, or the interior pass paints outside the fill: concave curves in thin features
    // can reach across them. Split the curves involved at t = 1/2 until nothing crosses, then fall back to lines.
    static void resolveCrossings(vector<vector<ContourSegment>>& contours) {
        struct InnerEdge { float x0, y0, x1, y1, lx, ux; int contour, seg; };
        auto orient = [](float ax, float ay, float bx, float by, float cx, float cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); };
        auto crosses = [&](const InnerEdge& a, const InnerEdge& b) {
            if ((a.x0 == b.x0 && a.y0 == b.y0) || (a.x0 == b.x1 && a.y0 == b.y1) || (a.x1 == b.x0 && a.y1 == b.y0) || (a.x1 == b.x1 && a.y1 == b.y1))
                return false;
            return orient(a.x0, a.y0, a.x1, a.y1, b.x0, b.y0) * orient(a.x0, a.y0, a.x1, a.y1, b.x1, b.y1) < 0
                && orient(b.x0, b.y0, b.x1, b.y1, a.x0, a.y0) * orient(b.x0, b.y0, b.x1, b.y1, a.x1, a.y1) < 0;
        };
        const int kRounds = 6;
        vector<InnerEdge> edges;
        vector<vector<char>> marked(contours.size());
        for (int round = 0; round < kRounds; round++) {
            edges.resize(0);
            for (int ci = 0; ci < (int)contours.size(); ci++) {
                vector<ContourSegment>& c = contours[ci];
                for (int i = 0, n = (int)c.size(); i < n; i++) {
                    const ContourSegment &sg = c[i], &e = c[(i + 1) % n];
                    if (sg.concave) {
                        edges.push_back({ sg.x, sg.y, sg.cx, sg.cy, std::min(sg.x, sg.cx), std::max(sg.x, sg.cx), ci, i });
                        edges.push_back({ sg.cx, sg.cy, e.x, e.y, std::min(sg.cx, e.x), std::max(sg.cx, e.x), ci, i });
                    } else
                        edges.push_back({ sg.x, sg.y, e.x, e.y, std::min(sg.x, e.x), std::max(sg.x, e.x), ci, i });
                }
                marked[ci].assign(c.size(), 0);
            }
            std::sort(edges.begin(), edges.end(), [](const InnerEdge& a, const InnerEdge& b) { return a.lx < b.lx; });
            bool any = false;
            for (size_t i = 0; i < edges.size(); i++)
                for (size_t j = i + 1; j < edges.size() && edges[j].lx <= edges[i].ux; j++) {
                    const InnerEdge &a = edges[i], &b = edges[j];
                    if (std::max(a.y0, a.y1) < std::min(b.y0, b.y1) || std::max(b.y0, b.y1) < std::min(a.y0, a.y1) || !crosses(a, b))
                        continue;
                    if (contours[a.contour][a.seg].curved) marked[a.contour][a.seg] = 1, any = true;
                    if (contours[b.contour][b.seg].curved) marked[b.contour][b.seg] = 1, any = true;
                }
            if (!any)
                return;
            for (int ci = 0; ci < (int)contours.size(); ci++) {
                vector<ContourSegment> &c = contours[ci], split;
                for (int i = 0, n = (int)c.size(); i < n; i++) {
                    ContourSegment sg = c[i];
                    const ContourSegment& e = c[(i + 1) % n];
                    if (!marked[ci][i])
                        split.push_back(sg);
                    else if (round == kRounds - 1)
                        sg.curved = sg.concave = false, split.push_back(sg);
                    else {
                        float x01 = (sg.x + sg.cx) * 0.5f, y01 = (sg.y + sg.cy) * 0.5f, x12 = (sg.cx + e.x) * 0.5f, y12 = (sg.cy + e.y) * 0.5f;
                        ContourSegment a = { sg.x, sg.y, x01, y01, false, false }, b = { (x01 + x12) * 0.5f, (y01 + y12) * 0.5f, x12, y12, false, false };
                        classify(a, b.x, b.y), classify(b, e.x, e.y);
                        split.push_back(a), split.push_back(b);
                    }
                }
                c.swap(split);
            }
        }
    }
    
    // The tessellator only sees on-curve points, so where curves come close (glyph hairlines) their chords can cross
    // although the curves do not. It then invents vertices that match no quadratic, and those curves draw as chords.
    // Split curves whose chords cross until they separate; genuinely overlapping contours keep their crossings.
    static void refineChords(Path& path) {
        struct Chord { float x0, y0, x1, y1, lx, ux; int sub, seg; };
        auto orient = [](float ax, float ay, float bx, float by, float cx, float cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); };
        vector<Chord> chords;
        vector<vector<char>> marked(path._subPaths.size());
        for (int round = 0; round < 6; round++) {
            chords.resize(0);
            for (int si = 0; si < (int)path._subPaths.size(); si++) {
                const float *q = path._subPaths[si]._quadratics.get();
                int n = (int)(path._subPaths[si]._quadratics.count() - 2) / 4;
                for (int k = 0; k < n; k++) {
                    const float *a = q + k * 4, *b = a + 4;
                    chords.push_back({ a[0], a[1], b[0], b[1], std::min(a[0], b[0]), std::max(a[0], b[0]), si, k });
                }
                const float *a = q + n * 4;  // the tessellator closes each contour back to its first point
                chords.push_back({ a[0], a[1], q[0], q[1], std::min(a[0], q[0]), std::max(a[0], q[0]), si, -1 });
                marked[si].assign(n, 0);
            }
            std::sort(chords.begin(), chords.end(), [](const Chord& a, const Chord& b) { return a.lx < b.lx; });
            bool any = false;
            for (size_t i = 0; i < chords.size(); i++)
                for (size_t j = i + 1; j < chords.size() && chords[j].lx <= chords[i].ux; j++) {
                    const Chord &a = chords[i], &b = chords[j];
                    if (std::max(a.y0, a.y1) < std::min(b.y0, b.y1) || std::max(b.y0, b.y1) < std::min(a.y0, a.y1))
                        continue;
                    if ((a.x0 == b.x0 && a.y0 == b.y0) || (a.x0 == b.x1 && a.y0 == b.y1) || (a.x1 == b.x0 && a.y1 == b.y0) || (a.x1 == b.x1 && a.y1 == b.y1))
                        continue;
                    if (orient(a.x0, a.y0, a.x1, a.y1, b.x0, b.y0) * orient(a.x0, a.y0, a.x1, a.y1, b.x1, b.y1) >= 0
                        || orient(b.x0, b.y0, b.x1, b.y1, a.x0, a.y0) * orient(b.x0, b.y0, b.x1, b.y1, a.x1, a.y1) >= 0)
                        continue;
                    if (a.seg >= 0) marked[a.sub][a.seg] = 1, any = true;
                    if (b.seg >= 0) marked[b.sub][b.seg] = 1, any = true;
                }
            if (!any)
                return;
            for (int si = 0; si < (int)path._subPaths.size(); si++) {
                SubPath& sp = path._subPaths[si];
                const float *q = sp._quadratics.get();
                int n = (int)(sp._quadratics.count() - 2) / 4;
                bool changed = false;
                for (char m : marked[si]) changed |= m;
                if (!changed)
                    continue;
                Memory<float> refined;
                refined.push_back(q[0]), refined.push_back(q[1]);
                for (int k = 0; k < n; k++) {
                    const float *a = q + k * 4;
                    if (marked[si][k]) {
                        float x01 = (a[0] + a[2]) * 0.5f, y01 = (a[1] + a[3]) * 0.5f, x12 = (a[2] + a[4]) * 0.5f, y12 = (a[3] + a[5]) * 0.5f;
                        refined.push_back(x01), refined.push_back(y01), refined.push_back((x01 + x12) * 0.5f), refined.push_back((y01 + y12) * 0.5f);
                        refined.push_back(x12), refined.push_back(y12), refined.push_back(a[4]), refined.push_back(a[5]);
                    } else
                        refined.push_back(a[2]), refined.push_back(a[3]), refined.push_back(a[4]), refined.push_back(a[5]);
                }
                sp._quadratics = refined;
            }
        }
    }
    
    // A vertex of the triangulation. Corners sit on the outer polygon and anti-alias its two edges there, which head
    // toward (px, py) and (nx, ny). Concave curves' control points are interior and have no corner.
    struct Point {
        Point() {}
        Point(float x, float y) : x(x), y(y), px(0), py(0), nx(0), ny(0), corner(false) {}
        Point(float x, float y, float px, float py, float nx, float ny) : x(x), y(y), px(px), py(py), nx(nx), ny(ny), corner(true) {}
        float x, y, px, py, nx, ny;
        bool corner;
    };
    
    // A curve's hull triangle p0 -> c -> p2. It is the only triangle that evaluates the curve.
    struct Hull {
        Point p0, c, p2;
        float tpx, tpy, tnx, tny;   // where the segments before p0 and after p2 head from their joins
        bool concave;
    };
    
    // Counts vertices the tessellator invented where inner contours cross: thin features whose hulls overlap.
    static std::atomic<size_t>& innerIntersections() { static std::atomic<size_t> count(0); return count; }
    
    // The miter at a corner, and whether the corner is convex. (x, y) is any point inside the fill near the corner.
    static Vector miter(const Point& v, float x, float y, bool *convex) {
        float x0 = v.x, y0 = v.y, px = v.px, py = v.py, nx = v.nx, ny = v.ny;
        float det = (x0 - px) * (ny - y0) - (y0 - py) * (nx - x0);
        bool edge = det > 0;
        bool left = (x0 - px) * (y - py) - (y0 - py) * (x - px) > 0 || (nx - x0) * (y - y0) - (ny - y0) * (x - x0) > 0;
        *convex = edge == left;
        return miterVector(x0, y0, px, py, nx, ny, left, 1e-12);
    }
    
	static float solveTriangle(float x0, float y0, Vector m0, float x1, float y1, Vector m1, float x2, float y2, Vector m2) {
		float t, t1, a0, a1, ta, tx0, ty0, tx1, ty1, tx2, ty2;
		::solveTriangle(Vector(x0, y0), m0, Vector(x1, y1), m1, Vector(x2, y2), m2, & t, & t1);
		tx0 = x0 + m0.x * t, ty0 = y0 + m0.y * t;
		tx1 = x1 + m1.x * t, ty1 = y1 + m1.y * t;
		tx2 = x2 + m2.x * t, ty2 = y2 + m2.y * t;
		a0 = std::abs((tx1 - tx0) * (ty2 - ty1) - (ty1 - ty0) * (tx2 - tx1)), a1 = 0;
		if (t1 != -1) {
			tx0 = x0 + m0.x * t1, ty0 = y0 + m0.y * t1;
			tx1 = x1 + m1.x * t1, ty1 = y1 + m1.y * t1;
			tx2 = x2 + m2.x * t1, ty2 = y2 + m2.y * t1;
			a1 = std::abs((tx1 - tx0) * (ty2 - ty1) - (ty1 - ty0) * (tx2 - tx1));
		}
		t = 1.0 / std::abs(t1 > 1e-2 && (a1 < 1 || a1 < a0) ? t1 : t);
		ta = 1.0 / std::sqrt(0.5 * std::abs((x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1)));
		return ta > t ? ta : t;
	}
	
    static void writeTriangle(vector<Triangle>& tris, int p, const Point *v, const Vector *m, const bool *convex,
                              bool ef0, bool ef1, bool ef2, unsigned int curves, float lx, float ly, float rw, float rh) {
        float t = solveTriangle(v[0].x, v[0].y, m[0], v[1].x, v[1].y, m[1], v[2].x, v[2].y, m[2]);
        tris.emplace_back(p, ef0, ef1, ef2, convex[0], convex[1], convex[2], t,
                          (v[0].x - lx) * rw, (v[0].y - ly) * rh, (v[1].x - lx) * rw, (v[1].y - ly) * rh, (v[2].x - lx) * rw, (v[2].y - ly) * rh,
                          m[0].x, m[0].y, m[1].x, m[1].y, m[2].x, m[2].y);
        tris.back().curves = curves;
    }
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        int p, i, base, divisions = 64, size = (int)scene._paths.size();
        
        mesh.empty();
        if (size == 0)
            return;
        
        for (p = 0; p < size; p++)
            mesh._timeline.basePaths().emplace_back(scene._paths[p]._color, scene._CTMs[p], scene._paths[p]._bounds);
        
        int counts[size], count, total;
        // Balance the divisions by cost: stroke outlines cross themselves, so their segments cost about three times as much.
        for (total = count = p = 0; p < size; counts[p] = count, total += count, count = 0, p++)
            for (SubPath& s : scene._paths[p]._subPaths)
                count += s._quadratics.count() * (scene._paths[p]._outline ? 3 : 1);
        int begins[divisions + 1], target, *b = begins;
        begins[0] = 0, begins[divisions] = size;
        for (count = p = 0, i = 1; i < divisions; i++) {
            target = total * i / divisions;
            while (count < target)
                count += counts[p++];
            begins[i] = p;
        }
        vector<Triangle> tris[divisions], *triss = tris;
        vector<int> idxs[divisions], *idxss = idxs;
        if (divisions == 1)
            triangulateRange(tris[0], idxss[0], scene, 0, size);
        else {
            dispatch_apply(divisions, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                triangulateRange(triss[idx], idxss[idx], scene, b[idx], b[idx + 1]);
            });
        }
        for (base = i = 0; i < divisions; base += (int)tris[i].size(), i++) {
            if (tris[i].size())
                memcpy(mesh._tris.new_ptr(tris[i].size()), & tris[i][0], tris[i].size() * sizeof(tris[i][0]));
            for (int idx : idxs[i])
                mesh._idxs.emplace_back(base + idx);
        }
        mesh._idxs.emplace_back((int)mesh._tris.count());
        writeIndexedInterior(mesh);
    }
    
    // Shares vertices between triangles so the interior pass transforms each vertex once instead of three times.
    // Triangles are written in reverse so opaque paths draw front to back, and wound counter-clockwise so
    // back-face culling drops triangles that the miter inset has inverted. Hull triangles extend outside the fill,
    // so they are written degenerate: the interior pass must never paint them.
    static void writeIndexedInterior(Mesh& mesh) {
        struct Hash {
            size_t operator()(const MeshVertex& v) const {
                uint64_t a, b;
                memcpy(& a, & v, sizeof(a)), memcpy(& b, (const char *)& v + sizeof(a), sizeof(b));
                return std::hash<uint64_t>()(a * 0x9E3779B97F4A7C15ULL ^ b);
            }
        };
        struct Equal {
            bool operator()(const MeshVertex& a, const MeshVertex& b) const { return memcmp(& a, & b, sizeof(MeshVertex)) == 0; }
        };
        size_t ntris = mesh._tris.count(), i;
        if (ntris == 0)
            return;
        std::unordered_map<MeshVertex, unsigned int, Hash, Equal> lookup;
        lookup.reserve(ntris * 3 / 2);
        unsigned int *idx = mesh._interiorIdxs.new_ptr(ntris * 3);
        for (i = 0; i < ntris; i++) {
            Triangle& t = mesh._tris[ntris - 1 - i];
            unsigned int p = t.p & Triangle::kFlagsMask;
            MeshVertex v[3] = { { p, t.x0, t.y0, t.ix0, t.iy0 }, { p, t.x1, t.y1, t.ix1, t.iy1 }, { p, t.x2, t.y2, t.ix2, t.iy2 } };
            if (t.curves & Triangle::kHull)
                v[1] = v[2] = v[0];
            else if (int64_t(t.x1 - t.x0) * (t.y2 - t.y0) - int64_t(t.y1 - t.y0) * (t.x2 - t.x0) < 0)
                std::swap(v[1], v[2]);
            for (int k = 0; k < 3; k++) {
                auto it = lookup.emplace(v[k], (unsigned int)lookup.size());
                if (it.second)
                    *mesh._interiorVerts.new_ptr(1) = v[k];
                *idx++ = it.first->second;
            }
        }
    }
    
    // Triangulates each path's inner polygon, and adds one hull triangle per curve. On the outer polygon a convex curve
    // runs p0 -> c -> p2 and a concave curve along its chord, so every curve lies inside its own hull triangle. The inner
    // polygon cuts those off: chords for convex curves, p0 -> c -> p2 for concave ones.
    static void triangulateRange(vector<Triangle>& _tris, vector<int>& _idxs, Scene& scene, int begin, int end) {
        TESStesselator *tess = tessNewTess(NULL), *dst = tessNewTess(NULL);
        tessSetOption(dst, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
        vector<Point> points;
        vector<float> xy;
        vector<Hull> hulls;
        vector<vector<ContourSegment>> contours;
        for (int p = begin; p < end; p++) {
            Path& path = scene._paths[p];
            float lx, ly, rw, rh;
            lx = path._bounds.lx, rw = kUnsignedShortMax / (path._bounds.ux - lx);
            ly = path._bounds.ly, rh = kUnsignedShortMax / (path._bounds.uy - ly);
            
            _idxs.emplace_back((int)_tris.size());
            
            // Stroke outlines cross themselves by construction (inner corners, overlapping pieces), so refining their
            // crossing chords only multiplies segments.
            if (!path._strokeWidth && !path._outline)
                refineChords(path);
            PathVertices pv(path);
            addPathContours(tess, path);
            if (!tessTesselate(tess, path._evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
                continue;
            const float *verts = tessGetVertices(tess);
            const TESSindex *indices = tessGetVertexIndices(tess);
            const int *elems = tessGetElements(tess), nelems = tessGetElementCount(tess);
            contours.resize(0);
            for (int e = 0; e < nelems; e++) {
                int base = elems[e * 2], n = elems[e * 2 + 1], i;
                if (n < 3)
                    continue;
                contours.emplace_back(n);
                vector<ContourSegment>& c = contours.back();
                for (i = 0; i < n; i++) {
                    const float *a = verts + (base + i) * 2, *b = verts + (base + (i + 1) % n) * 2;
                    Segment q = pv.segment(indices[base + i], indices[base + (i + 1) % n], a, b);
                    c[i] = { a[0], a[1], q.found ? q.cx : a[0], q.found ? q.cy : a[1], false, false };
                    if (q.found)
                        classify(c[i], b[0], b[1]);
                }
            }
            resolveCrossings(contours);
            
            points.resize(0), hulls.resize(0);
            for (vector<ContourSegment>& c : contours) {
                int n = (int)c.size(), i;
                // An on-curve vertex's outer edges head to a convex curve's control point, else along the chord.
                auto corner = [&](int i) {
                    const ContourSegment &sp = c[(i + n - 1) % n], &sn = c[i], &next = c[(i + 1) % n];
                    bool cp = sp.curved && !sp.concave, cn = sn.curved && !sn.concave;
                    return Point(sn.x, sn.y, cp ? sp.cx : sp.x, cp ? sp.cy : sp.y, cn ? sn.cx : next.x, cn ? sn.cy : next.y);
                };
                size_t first = points.size();
                for (i = 0; i < n; i++) {
                    Point v = corner(i);
                    points.push_back(v);
                    const ContourSegment& sg = c[i];
                    if (sg.curved) {
                        Hull h;
                        h.p0 = v, h.p2 = corner((i + 1) % n), h.concave = sg.concave;
                        // A neighbour's tangent at the join heads to its control point if it is a curve, else along it.
                        const ContourSegment &before = c[(i + n - 1) % n], &after = c[(i + 1) % n], &afterEnd = c[(i + 2) % n];
                        bool bc = before.curved && (before.cx != sg.x || before.cy != sg.y), ac = after.curved && (after.cx != after.x || after.cy != after.y);
                        h.tpx = bc ? before.cx : before.x, h.tpy = bc ? before.cy : before.y;
                        h.tnx = ac ? after.cx : afterEnd.x, h.tny = ac ? after.cy : afterEnd.y;
                        h.c = sg.concave ? Point(sg.cx, sg.cy) : Point(sg.cx, sg.cy, v.x, v.y, h.p2.x, h.p2.y);
                        hulls.push_back(h);
                        if (sg.concave)
                            points.push_back(h.c);
                    }
                }
                xy.resize(0);
                for (size_t k = first; k < points.size(); k++)
                    xy.push_back(points[k].x), xy.push_back(points[k].y);
                tessAddContour(dst, 2, xy.data(), kPointStride, int(points.size() - first));
            }
            
            if (tessTesselate(dst, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                const float *dverts = tessGetVertices(dst);
                const TESSindex *dindices = tessGetVertexIndices(dst), *polygon = tessGetElements(dst);
                const int ndverts = tessGetVertexCount(dst), ntris = tessGetElementCount(dst);
                for (int k = 0; k < ndverts; k++)
                    if (dindices[k] == TESS_UNDEF)
                        innerIntersections()++;
                for (int i = 0; i < ntris; i++, polygon += kPolySize) {
                    Point v[3];
                    Vector m[3];
                    bool convex[3], ef[3];
                    float cx = 0, cy = 0;
                    for (int k = 0; k < 3; k++) {
                        TESSindex di = dindices[polygon[k]];
                        v[k] = di == TESS_UNDEF ? Point(dverts[polygon[k] * 2], dverts[polygon[k] * 2 + 1]) : points[di];
                        cx += v[k].x / 3, cy += v[k].y / 3;
                    }
                    for (int k = 0; k < 3; k++) {
                        m[k] = v[k].corner ? miter(v[k], cx, cy, & convex[k]) : Vector(0, 0);
                        if (!v[k].corner)
                            convex[k] = false;
                        TESSindex a = dindices[polygon[k]], b = dindices[polygon[(k + 1) % 3]];
                        ef[k] = a != TESS_UNDEF && b != TESS_UNDEF && (b == a + 1 || a == b + 1);
                    }
                    writeTriangle(_tris, p, v, m, convex, ef[0], ef[1], ef[2], 0, lx, ly, rw, rh);
                }
            }
            
            // Hull triangles, p0 -> c -> p2. Their insides are on the chord's side for convex curves, and toward the
            // control point for concave ones.
            for (Hull& h : hulls) {
                // Triangles must wind counter-clockwise. A concave control is left of its chord, so reverse the curve.
                Point v[3] = { h.concave ? h.p2 : h.p0, h.c, h.concave ? h.p0 : h.p2 };
                Vector m[3];
                bool convex[3];
                float ix = h.concave ? h.c.x : (h.p0.x + h.p2.x) * 0.5f, iy = h.concave ? h.c.y : (h.p0.y + h.p2.y) * 0.5f;
                m[0] = miter(v[0], ix, iy, & convex[0]);
                m[2] = miter(v[2], ix, iy, & convex[2]);
                m[1] = h.concave ? Vector(0, 0) : miter(v[1], (h.p0.x + h.p2.x) * 0.5f, (h.p0.y + h.p2.y) * 0.5f, & convex[1]);
                if (h.concave)
                    convex[1] = false;
                // The joins' convexity follows the tangents: the fill is on the left, so a left turn is convex. Smooth joins
                // (tangent-preserving quadratics) have no turn to measure, so they take the curve's own convexity.
                float cx = h.c.x, cy = h.c.y, sine = std::sin(kLineTurnLimit);
                auto turn = [&](float ax, float ay, float bx, float by) {
                    float cross = ax * by - ay * bx;
                    return cross * cross <= sine * sine * (ax * ax + ay * ay) * (bx * bx + by * by) ? !h.concave : cross > 0;
                };
                bool start = turn(h.p0.x - h.tpx, h.p0.y - h.tpy, cx - h.p0.x, cy - h.p0.y);
                bool end = turn(h.p2.x - cx, h.p2.y - cy, h.tnx - h.p2.x, h.tny - h.p2.y);
                convex[0] = h.concave ? end : start, convex[2] = h.concave ? start : end;
                writeTriangle(_tris, p, v, m, convex, !h.concave, !h.concave, h.concave,
                              Triangle::kHull | (h.concave ? Triangle::kConcave : 0), lx, ly, rw, rh);
                Triangle& t = _tris.back();
                float t0x = h.concave ? h.tnx : h.tpx, t0y = h.concave ? h.tny : h.tpy, t2x = h.concave ? h.tpx : h.tnx, t2y = h.concave ? h.tpy : h.tny;
                auto q = [](float v) { return (unsigned short)(v < 0 ? 0 : v > kUnsignedShortMax ? kUnsignedShortMax : v + 0.5f); };
                t.tx0 = q((t0x - lx) * rw), t.ty0 = q((t0y - ly) * rh), t.tx2 = q((t2x - lx) * rw), t.ty2 = q((t2y - ly) * rh);
            }
        }
        tessDeleteTess(tess), tessDeleteTess(dst);
    }
};
