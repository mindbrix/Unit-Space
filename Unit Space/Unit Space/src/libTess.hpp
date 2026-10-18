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
#import <functional>
#import "tesselator.h"
#import "Algorithms.hpp"

constexpr TESSreal kTESSNormal[4] = { 0.0, 0.0, 1.0 };
constexpr int kPolySize = 3;
#ifndef LINE_TURN_SINE
#define LINE_TURN_SINE 0.0174524064 // sin(1 degree)
#endif
// Curves whose tangents turn by less than the angle with this sine are drawn as lines. Each curve costs a hull
// triangle, so nearly straight ones are cheaper as lines. It is an angle rather than a distance so it is independent
// of scale: a distance in path units that suits a map leaves small text faceted. Turns are measured by the sine, from
// cross products, so it is kept as one.
constexpr float kLineTurnSine = LINE_TURN_SINE;

struct libTess {
    // The tessellators' memory for one dispatch_apply() slice. libTess builds and frees a mesh, a dictionary and queues
    // for every tessellation, which as malloc and free calls from every thread cost more than many tessellations. So
    // each path's tessellators take their memory in order from blocks the slice keeps, all of it reclaimed at once by
    // reset() before the next path. Every allocation records its size, which memrealloc() needs to copy it. A failed
    // allocation returns null, which libTess handles: tessNewTess() returns null and tessTesselate() fails.
    struct Arena {
        ~Arena() { for (Block& b : blocks) free(b.base); }
        void reset() { block = 0, used = 0; }
        void *alloc(size_t size) {
            const size_t align = alignof(std::max_align_t), n = align + (size + align - 1) / align * align;
            while (block < blocks.size() && used + n > blocks[block].size)
                block++, used = 0;
            if (block == blocks.size()) {
                // Each new block at least doubles the arena, so a slice needs only a few, whatever its paths' sizes.
                size_t total = n;
                for (Block& b : blocks)
                    total += b.size;
                char *base = (char *)malloc(total);
                if (base == nullptr)
                    return nullptr;
                blocks.push_back({ base, total });
            }
            char *p = blocks[block].base + used;
            used += n;
            *(size_t *)p = size;
            return p + align;
        }
        static void *memalloc(void *arena, unsigned int size) { return ((Arena *)arena)->alloc(size); }
        static void *memrealloc(void *arena, void *ptr, unsigned int size) {
            void *p = ((Arena *)arena)->alloc(size);
            if (p && ptr)
                memcpy(p, ptr, std::min(size_t(size), *(size_t *)((char *)ptr - alignof(std::max_align_t))));
            return p;
        }
        static void memfree(void *arena, void *ptr) {}
        TESSalloc tessAlloc() { TESSalloc a = {}; a.memalloc = memalloc, a.memrealloc = memrealloc, a.memfree = memfree, a.userData = this; return a; }
        
        struct Block { char *base; size_t size; };
        vector<Block> blocks;
        size_t block = 0, used = 0;
    };
    
    static void addPathContours(TESStesselator *dst, Path& path) {
        for (SubPath& s : path._subPaths)
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
        PathVertices(Path& path) {
            int base = 0;
            for (SubPath& s : path._subPaths) {
                bases.push_back(base), subPaths.push_back(& s);
                base += (int)s._quadratics.count() / 4 + 1;
            }
            bases.push_back(base);
        }
        // The segment joining input vertices a and b. The tessellator merges coincident vertices (stroke joins meet
        // their runs exactly), returning just one of their indices, so try every input vertex at each position.
        Segment segment(TESSindex a, TESSindex b, const float *pa, const float *pb) {
            Segment seg = segment(a, b);
            if (seg.found || a == TESS_UNDEF || b == TESS_UNDEF)
                return seg;
            // Most paths have no merged vertices, so only index positions once a lookup misses.
            if (!indexed) {
                for (int sp = 0; sp < (int)subPaths.size(); sp++)
                    for (int i = 0; i < bases[sp + 1] - bases[sp]; i++)
                        positions[key(subPaths[sp]->_quadratics[i * 4], subPaths[sp]->_quadratics[i * 4 + 1])].push_back(bases[sp] + i);
                indexed = true;
            }
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
            if (a == TESS_UNDEF || b == TESS_UNDEF)
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
        bool indexed = false;
        vector<int> bases;
        vector<SubPath *> subPaths;
        unordered_map<uint64_t, vector<int>> positions;
    };
    
    // A boundary contour segment from (x, y) to the next segment's start, and its control point when it is a curve.
    struct ContourSegment {
        float x, y, cx, cy;
        bool curved, concave;   // concave: the control point lies inside the fill, so the curve bows inward
    };
    
    // Lines carry their midpoint as control, and curves turning less than kLineTurnSine are drawn as lines too.
    // Boundary contours keep the fill on their left, so a control point left of its chord is concave.
    // A line is what mergeVertices() makes one: it straightens every quadratic within the merge limit of its chord to
    // the chord's midpoint, as lines are written. Measuring a line's turn instead would overrule it, as short legs
    // cannot resolve kLineTurnSine: rounding the midpoint alone turns a chord under a hundred float steps by more.
    // The legs turn by more than the limit if they turn back, or if the sine of their turn, |a x b| / (|a| |b|), exceeds it.
    static bool isCurved(float x0, float y0, float cx, float cy, float x2, float y2) {
        if (cx == (x0 + x2) * 0.5f && cy == (y0 + y2) * 0.5f)
            return false;
        float ax = cx - x0, ay = cy - y0, bx = x2 - cx, by = y2 - cy, cross = ax * by - ay * bx;
        return (ax != 0 || ay != 0) && (bx != 0 || by != 0)
            && (ax * bx + ay * by <= 0 || cross * cross > kLineTurnSine * kLineTurnSine * (ax * ax + ay * ay) * (bx * bx + by * by));
    }
    static void classify(ContourSegment& s, float ex, float ey) {
        s.curved = isCurved(s.x, s.y, s.cx, s.cy, ex, ey);
        s.concave = s.curved && (ex - s.x) * (s.cy - s.y) - (ey - s.y) * (s.cx - s.x) > 0;
    }
    
    // Calls f(a, b) once for each pair of segments (x0, y0, x1, y1, with x extent lx, ux) whose bounds overlap or touch.
    // Few segments sweep along x (sorting them), testing each against those starting before it ends. Many use a uniform
    // grid sized to them instead: on big paths most segments overlap in x but not y, so the sweep tests hundreds of
    // times as many pairs. Grid pairs come from the cell holding the lower left corner of their overlap, so each is
    // reported once.
    template<typename T, typename F>
    static void forEachOverlap(vector<T>& segs, F&& f) {
        constexpr int kGridMin = 1024;
        auto ly = [](const T& s) { return std::min(s.y0, s.y1); };
        auto uy = [](const T& s) { return std::max(s.y0, s.y1); };
        auto overlap = [&](const T& a, const T& b) { return !(a.ux < b.lx || b.ux < a.lx || uy(a) < ly(b) || uy(b) < ly(a)); };
        int n = (int)segs.size();
        float cell = 0, lx = 0, by = 0, w = 0, h = 0;
        if (n >= kGridMin) {
            float ux = segs[0].ux, ty = uy(segs[0]), size = 0;
            lx = segs[0].lx, by = ly(segs[0]);
            for (const T& s : segs) {
                lx = std::min(lx, s.lx), ux = std::max(ux, s.ux), by = std::min(by, ly(s)), ty = std::max(ty, uy(s));
                size += std::max(s.ux - s.lx, uy(s) - ly(s));
            }
            // Cells about twice the mean segment size, and no more cells than segments.
            w = ux - lx, h = ty - by, cell = std::max(std::max(2.f * size / n, std::sqrt(w * h / n)), std::max(w, h) * 1e-6f);
        }
        if (!(cell > 0)) {
            std::sort(segs.begin(), segs.end(), [](const T& a, const T& b) { return a.lx < b.lx; });
            for (int i = 0; i < n; i++)
                for (int j = i + 1; j < n && segs[j].lx <= segs[i].ux; j++)
                    if (!(uy(segs[i]) < ly(segs[j]) || uy(segs[j]) < ly(segs[i])))
                        f(segs[i], segs[j]);
            return;
        }
        int gw = std::min(int(w / cell) + 1, n), gh = std::min(int(h / cell) + 1, n);
        auto cx = [&](float x) { return std::min(gw - 1, std::max(0, int((x - lx) / cell))); };
        auto cy = [&](float y) { return std::min(gh - 1, std::max(0, int((y - by) / cell))); };
        static thread_local vector<int> starts, cells, fill;
        starts.assign(gw * gh + 1, 0);
        for (const T& s : segs)
            for (int y = cy(ly(s)), y1 = cy(uy(s)); y <= y1; y++)
                for (int c = y * gw + cx(s.lx), c1 = y * gw + cx(s.ux); c <= c1; c++)
                    starts[c + 1]++;
        for (int c = 0; c < gw * gh; c++)
            starts[c + 1] += starts[c];
        cells.resize(starts[gw * gh]);
        fill.assign(starts.begin(), starts.end() - 1);
        for (int i = 0; i < n; i++) {
            const T& s = segs[i];
            for (int y = cy(ly(s)), y1 = cy(uy(s)); y <= y1; y++)
                for (int c = y * gw + cx(s.lx), c1 = y * gw + cx(s.ux); c <= c1; c++)
                    cells[fill[c]++] = i;
        }
        for (int c = 0; c < gw * gh; c++)
            for (int k = starts[c]; k < starts[c + 1]; k++)
                for (int l = k + 1; l < starts[c + 1]; l++) {
                    T &p = segs[cells[k]], &q = segs[cells[l]];
                    if (overlap(p, q) && cy(std::max(ly(p), ly(q))) * gw + cx(std::max(p.lx, q.lx)) == c)
                        f(p, q);
                }
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
            bool any = false;
            forEachOverlap(edges, [&](const InnerEdge& a, const InnerEdge& b) {
                if (!crosses(a, b))
                    return;
                if (contours[a.contour][a.seg].curved) marked[a.contour][a.seg] = 1, any = true;
                if (contours[b.contour][b.seg].curved) marked[b.contour][b.seg] = 1, any = true;
            });
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
    // although the curves do not, and curves can cross although their chords do not. It then invents vertices that
    // match no quadratic, or misses the crossing, and those curves draw wrongly. Two segments can only cross where their
    // hulls overlap, the triangles of their ends and control points (a line's is its chord), and halves lie inside their
    // parent's hull. So one sweep pairs the curves with each segment whose hull overlaps theirs, and most paths have
    // none. Each round splits the paired curves and keeps the halves' pairs whose hulls still overlap: those that no
    // longer do never will. Curves that separate drop out, and genuinely overlapping contours keep their crossings.
    static void refineChords(Path& path) {
        // Points that coincide are tested as such: fused multiply-adds leave a point's orientation against an edge
        // through it a rounding error rather than zero.
        auto orient = [](const float *a, const float *b, const float *c) { return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); };
        auto same = [](const float *a, const float *b) { return a[0] == b[0] && a[1] == b[1]; };
        // Whether segments a0 a1 and b0 b1 cross at a point inside both.
        auto crosses = [&](const float *a0, const float *a1, const float *b0, const float *b1) {
            return !same(a0, b0) && !same(a0, b1) && !same(a1, b0) && !same(a1, b1)
                && orient(a0, a1, b0) * orient(a0, a1, b1) < 0 && orient(b0, b1, a0) * orient(b0, b1, a1) < 0;
        };
        // Whether r is strictly inside triangle t, wound either way. A flat one has no inside.
        auto inside = [&](const float *const *t, const float *r) {
            float s = orient(t[0], t[1], t[2]);
            return !same(r, t[0]) && !same(r, t[1]) && !same(r, t[2])
                && orient(t[0], t[1], r) * s > 0 && orient(t[1], t[2], r) * s > 0 && orient(t[2], t[0], r) * s > 0;
        };
        // Pieces start as the path's segments in a flat numbering, each subpath's closing chord last; a split one
        // points to its two halves.
        struct Piece { float q[6]; float lx, ly, ux, uy; int child; bool curved; };
        // Whether two pieces' hulls share inside points. Hulls touching at a shared end or along an edge do not.
        auto overlaps = [&](const Piece& a, const Piece& b) {
            float am[2] = { (a.q[0] + a.q[4]) * 0.5f, (a.q[1] + a.q[5]) * 0.5f }, bm[2] = { (b.q[0] + b.q[4]) * 0.5f, (b.q[1] + b.q[5]) * 0.5f };
            const float *ta[3] = { a.q, a.curved ? a.q + 2 : am, a.q + 4 }, *tb[3] = { b.q, b.curved ? b.q + 2 : bm, b.q + 4 };
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    if (crosses(ta[i], ta[(i + 1) % 3], tb[j], tb[(j + 1) % 3]))
                        return true;
            for (int k = 0; k < 3; k++)
                if (inside(tb, ta[k]) || inside(ta, tb[k]))
                    return true;
            return false;
        };
        auto setBounds = [](Piece& p) {
            p.lx = std::min(std::min(p.q[0], p.q[2]), p.q[4]), p.ux = std::max(std::max(p.q[0], p.q[2]), p.q[4]);
            p.ly = std::min(std::min(p.q[1], p.q[3]), p.q[5]), p.uy = std::max(std::max(p.q[1], p.q[3]), p.q[5]);
        };
        vector<Piece> pieces;
        vector<int> bases;      // each subpath's first piece
        for (int si = 0; si < (int)path._subPaths.size(); si++) {
            const float *q = path._subPaths[si]._quadratics.get();
            int n = (int)(path._subPaths[si]._quadratics.count() - 2) / 4;
            bases.push_back((int)pieces.size());
            for (int k = 0; k <= n; k++) {
                // The tessellator closes each contour back to its first point, a line.
                const float *a = q + k * 4, *b = k < n ? a + 4 : q;
                Piece p = {{ a[0], a[1], k < n ? a[2] : (a[0] + b[0]) * 0.5f, k < n ? a[3] : (a[1] + b[1]) * 0.5f, b[0], b[1] }};
                setBounds(p), p.child = -1, p.curved = k < n && isCurved(p.q[0], p.q[1], p.q[2], p.q[3], p.q[4], p.q[5]);
                pieces.push_back(p);
            }
        }
        auto boxesOverlap = [&](int a, int b) {
            const Piece &p = pieces[a], &q = pieces[b];
            return !(p.ux < q.lx || q.ux < p.lx || p.uy < q.ly || q.uy < p.ly);
        };
        // Only pairs with a curve: a line is its own chord, so splitting it only multiplies segments.
        struct Box { float x0, y0, x1, y1, lx, ux; int piece; };
        vector<Box> boxes(pieces.size());
        for (int i = 0; i < (int)pieces.size(); i++)
            boxes[i] = { pieces[i].lx, pieces[i].ly, pieces[i].ux, pieces[i].uy, pieces[i].lx, pieces[i].ux, i };
        vector<std::pair<int, int>> pairs, next;
        forEachOverlap(boxes, [&](const Box& a, const Box& b) {
            if ((pieces[a.piece].curved || pieces[b.piece].curved) && overlaps(pieces[a.piece], pieces[b.piece]))
                pairs.emplace_back(a.piece, b.piece);
        });
        if (pairs.empty())
            return;
        vector<char> marked;
        for (int round = 0; round < 6 && pairs.size(); round++) {
            marked.assign(pieces.size(), 0);
            for (auto [a, b] : pairs)
                marked[a] |= pieces[a].curved, marked[b] |= pieces[b].curved;
            for (int i = 0, n = (int)pieces.size(); i < n; i++)
                if (marked[i]) {
                    float *a = pieces[i].q, x01 = (a[0] + a[2]) * 0.5f, y01 = (a[1] + a[3]) * 0.5f, x12 = (a[2] + a[4]) * 0.5f, y12 = (a[3] + a[5]) * 0.5f;
                    float x = (x01 + x12) * 0.5f, y = (y01 + y12) * 0.5f;
                    Piece h0 = {{ a[0], a[1], x01, y01, x, y }}, h1 = {{ x, y, x12, y12, a[4], a[5] }};
                    for (Piece *h : { &h0, &h1 })
                        setBounds(*h), h->child = -1, h->curved = isCurved(h->q[0], h->q[1], h->q[2], h->q[3], h->q[4], h->q[5]);
                    pieces[i].child = (int)pieces.size();
                    pieces.push_back(h0), pieces.push_back(h1);
                }
            if (round == 5)
                break;
            // Each pair's split pieces give way to their halves, kept while their hulls overlap.
            next.resize(0);
            for (auto [a, b] : pairs) {
                int as[2] = { a, a }, bs[2] = { b, b }, na = 1, nb = 1;
                if (marked[a]) as[0] = pieces[a].child, as[1] = as[0] + 1, na = 2;
                if (marked[b]) bs[0] = pieces[b].child, bs[1] = bs[0] + 1, nb = 2;
                for (int i = 0; i < na; i++)
                    for (int j = 0; j < nb; j++)
                        if ((pieces[as[i]].curved || pieces[bs[j]].curved) && boxesOverlap(as[i], bs[j]) && overlaps(pieces[as[i]], pieces[bs[j]]))
                            next.emplace_back(as[i], bs[j]);
            }
            pairs.swap(next);
        }
        // Each changed subpath is written once, its segments' pieces in order.
        std::function<void(Memory<float>&, int)> write = [&](Memory<float>& out, int i) {
            if (pieces[i].child >= 0)
                write(out, pieces[i].child), write(out, pieces[i].child + 1);
            else
                out.push_back(pieces[i].q[2]), out.push_back(pieces[i].q[3]), out.push_back(pieces[i].q[4]), out.push_back(pieces[i].q[5]);
        };
        for (int si = 0; si < (int)path._subPaths.size(); si++) {
            SubPath& sp = path._subPaths[si];
            int n = (int)(sp._quadratics.count() - 2) / 4;
            bool changed = false;
            for (int k = 0; k < n && !changed; k++)
                changed = pieces[bases[si] + k].child >= 0;
            if (!changed)
                continue;
            Memory<float> refined;
            refined.push_back(sp._quadratics[0]), refined.push_back(sp._quadratics[1]);
            for (int k = 0; k < n; k++)
                write(refined, bases[si] + k);
            sp._quadratics = refined;
        }
    }
    
    // A vertex of the triangulation. Corners sit on the outer polygon and anti-alias its two edges there, which head
    // toward (px, py) and (nx, ny). Concave curves' control points are interior: their p0 -> c -> p2 miter only insets
    // the interior pass, and the edge pass gives them no corner.
    // The interior insets along the curves' tangents instead, (tpx, tpy) and (tnx, tny): a concave curve leaves its end
    // inside its chord, so insetting from the chord would let the interior reach the curve.
    struct Point {
        Point() {}
        Point(float x, float y) : x(x), y(y), px(0), py(0), nx(0), ny(0), tpx(0), tpy(0), tnx(0), tny(0), corner(false), control(false) {}
        Point(float x, float y, float px, float py, float nx, float ny, bool control = false) : x(x), y(y), px(px), py(py), nx(nx), ny(ny), tpx(px), tpy(py), tnx(nx), tny(ny), corner(true), control(control) {}
        Point tangents() const { Point t = *this; t.px = tpx, t.py = tpy, t.nx = tnx, t.ny = tny; return t; }
        float x, y, px, py, nx, ny, tpx, tpy, tnx, tny;
        bool corner, control;   // control: a concave curve's control point, whose p0 -> c -> p2 miter only insets the interior
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
    // The scale a triangle needs before its inset, its vertices moving along their miters, leaves it whole: the inverse
    // of where it collapses, and no less than the inverse of its size.
	static float solveTriangle(float x0, float y0, Vector m0, float x1, float y1, Vector m1, float x2, float y2, Vector m2) {
		float t, t1, a0, a1, ta, tx0, ty0, tx1, ty1, tx2, ty2;
		// Solved relative to the first vertex: the area terms cancel, so in absolute coordinates far from the origin
		// (maps) their rounding swamps small triangles.
		x1 -= x0, y1 -= y0, x2 -= x0, y2 -= y0, x0 = y0 = 0;
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
		// The nearer root is the collapse if it is not noise and does shrink the triangle. Both tests are relative to the
		// triangle's size, so they hold in any units: a glyph a few units across as much as a map. Roots are in miters, so
		// noise is a distance the fastest vertex moves: a sharp corner's long miter collapses a triangle at a small root.
		float area2 = std::abs((x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1)), size = std::sqrt(0.5f * area2);
		float fastest = std::sqrt(std::max(m0.x * m0.x + m0.y * m0.y, std::max(m1.x * m1.x + m1.y * m1.y, m2.x * m2.x + m2.y * m2.y)));
		t = 1.0 / std::abs(t1 * fastest > 1e-2 * size && (a1 < area2 || a1 < a0) ? t1 : t);
		ta = 1.0 / size;
		return ta > t ? ta : t;
	}
	
    // A sharp corner's interior inset in a triangle its miter heads outside of (a: the triangle's next vertex, b: the one
    // after). Insets only shrink a triangle until it collapses, but the miter would stretch this one, carrying it far
    // across the picture. So here the vertex slides along one of the triangle's own edges instead, just far enough to stay
    // as far from the corner's edges as the miter keeps it: the triangle then only shrinks, until its collapse.
    // A sharp reflex corner (a slit's end) slides in every triangle. Its miter keeps the vertex as far from its edges'
    // lines, but its edges end at the vertex, so beyond it the vertex itself is the nearest boundary. The miter would
    // collapse the triangles there almost at once, leaving them to the edge pass, which bounds them by those lines.
    static Vector sharpInset(const Point& v, Vector m, bool convex, float ax, float ay, float bx, float by) {
        float e1x = ax - v.x, e1y = ay - v.y, e2x = bx - v.x, e2y = by - v.y, turn = e1x * e2y - e1y * e2x;
        bool inside = (e1x * m.y - e1y * m.x) * turn >= 0 && (m.x * e2y - m.y * e2x) * turn >= 0;
        float dax = v.tpx - v.x, day = v.tpy - v.y, dbx = v.tnx - v.x, dby = v.tny - v.y;
        float la = std::sqrt(dax * dax + day * day), lb = std::sqrt(dbx * dbx + dby * dby);
        if (m.x * m.x + m.y * m.y <= kTriangleDilationMax * kTriangleDilationMax || (inside && convex) || la == 0 || lb == 0)
            return m;
        dax /= la, day /= la, dbx /= lb, dby /= lb;
        // The distance from v + e, e a unit edge, to the nearer of the corner's edges, which run from v to its tangent points.
        auto clearance = [&](float ex, float ey) {
            auto ray = [&](float dx, float dy) { return ex * dx + ey * dy > 0 ? std::abs(dx * ey - dy * ex) : 1.f; };
            return std::min(ray(dax, day), ray(dbx, dby));
        };
        float l1 = std::sqrt(e1x * e1x + e1y * e1y), l2 = std::sqrt(e2x * e2x + e2y * e2y);
        e1x /= l1, e1y /= l1, e2x /= l2, e2y /= l2;
        float c1 = clearance(e1x, e1y), c2 = clearance(e2x, e2y);
        if (c1 < c2)
            std::swap(c1, c2), std::swap(e1x, e2x), std::swap(e1y, e2y);
        return c1 > 0 ? Vector(e1x / c1, e1y / c1) : m;
    }
    
    static Vector miter(const Point& v, float x, float y, bool *convex) {
        float x0 = v.x, y0 = v.y, px = v.px, py = v.py, nx = v.nx, ny = v.ny;
        float det = (x0 - px) * (ny - y0) - (y0 - py) * (nx - x0);
        bool edge = det > 0;
        bool left = (x0 - px) * (y - py) - (y0 - py) * (x - px) > 0 || (nx - x0) * (y - y0) - (ny - y0) * (x - x0) > 0;
        *convex = edge == left;
        return miterVector(x0, y0, px, py, nx, ny, left, 1e-12);
    }
    
    // Whether direction (dx, dy) from a corner heads into the fill: left of both edges if convex, of either if not.
    static bool inside(const Point& v, float dx, float dy) {
        float ax = v.x - v.px, ay = v.y - v.py, bx = v.nx - v.x, by = v.ny - v.y;
        bool a = ax * dy - ay * dx > 0, b = bx * dy - by * dx > 0;
        return ax * by - ay * bx > 0 ? a && b : a || b;
    }
    
    // Quantises a coordinate as Triangle's constructor quantises vertices, so points the edge pass compares or measures
    // from vertices (curves' ends, neighbours' tangents) are on the same grid.
    static unsigned short quantise(float v) { return (unsigned short)(v < 0 ? 0 : v > kUnsignedShortMax ? kUnsignedShortMax : v); }
    
    static void writeTriangle(vector<Triangle>& tris, int p, const Point *v, const Vector *m, const bool *convex,
                              bool ef0, bool ef1, bool ef2, unsigned int curves, float lx, float ly, float rw, float rh) {
        tris.emplace_back(p, ef0, ef1, ef2, convex[0], convex[1], convex[2],
                          (v[0].x - lx) * rw, (v[0].y - ly) * rh, (v[1].x - lx) * rw, (v[1].y - ly) * rh, (v[2].x - lx) * rw, (v[2].y - ly) * rh,
                          m[0].x, m[0].y, m[1].x, m[1].y, m[2].x, m[2].y);
        tris.back().curves = curves;
    }
    
    // Splits paths into contiguous divisions of about equal cost: begins[i] is division i's first path, begins[divisions]
    // the path count. 64-bit, as large scenes' total cost times the division index overflows an int.
    static void balanceDivisions(const vector<int64_t>& costs, int divisions, int *begins) {
        int size = (int)costs.size(), p = 0;
        int64_t total = 0, count = 0;
        for (int64_t c : costs)
            total += c;
        begins[0] = 0, begins[divisions] = size;
        for (int i = 1; i < divisions; i++) {
            int64_t target = total * i / divisions;
            while (p < size && count < target)
                count += costs[p++];
            begins[i] = p;
        }
    }
    
    static void triangulate(Mesh& mesh, Scene& scene) {
        int p, i, base, divisions = 64, size = (int)scene._paths.size();
        
        mesh.empty();
        if (size == 0)
            return;
        
        for (p = 0; p < size; p++)
            mesh._timeline.basePaths().emplace_back(scene._paths[p]._color, scene._CTMs[p], scene._paths[p]._bounds);
        
        // Balance the divisions by cost: stroke outlines cross themselves, so their segments cost about three times as much.
        vector<int64_t> costs(size, 0);
        for (p = 0; p < size; p++)
            for (SubPath& s : scene._paths[p]._subPaths)
                costs[p] += (int64_t)s._quadratics.count() * (scene._paths[p]._outline ? 3 : 1);
        int begins[divisions + 1], *b = begins;
        balanceDivisions(costs, divisions, begins);
        vector<Triangle> tris[divisions], *triss = tris;
        vector<float> imiters[divisions], *imiterss = imiters;
        vector<int> idxs[divisions], *idxss = idxs;
        if (divisions == 1)
            triangulateRange(tris[0], imiters[0], idxss[0], scene, 0, size);
        else {
            dispatch_apply(divisions, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                triangulateRange(triss[idx], imiterss[idx], idxss[idx], scene, b[idx], b[idx + 1]);
            });
        }
        for (base = i = 0; i < divisions; base += (int)tris[i].size(), i++) {
            if (tris[i].size())
                memcpy(mesh._tris.new_ptr(tris[i].size()), & tris[i][0], tris[i].size() * sizeof(tris[i][0]));
            mesh._interiorMiters.insert(mesh._interiorMiters.end(), imiters[i].begin(), imiters[i].end());
            for (int idx : idxs[i])
                mesh._idxs.emplace_back(base + idx);
        }
        mesh._idxs.emplace_back((int)mesh._tris.count());
        writeIndexedInterior(mesh);
    }
    
    // Triangulates scene paths [begin, end) alone, as when one is being edited. The triangles keep their scene path
    // indices, so the mesh's paths cover the whole scene, but its _idxs start at begin.
    static void triangulatePaths(Mesh& mesh, Scene& scene, int begin, int end) {
        mesh.empty();
        for (int p = 0; p < (int)scene._paths.size(); p++)
            mesh._timeline.basePaths().emplace_back(scene._paths[p]._color, scene._CTMs[p], scene._paths[p]._bounds);
        vector<Triangle> tris;
        triangulateRange(tris, mesh._interiorMiters, mesh._idxs, scene, begin, end);
        if (tris.size())
            memcpy(mesh._tris.new_ptr(tris.size()), & tris[0], tris.size() * sizeof(tris[0]));
        mesh._idxs.emplace_back((int)mesh._tris.count());
        writeIndexedInterior(mesh);
    }
    
    // Each triangle writes its own vertices: a vertex carries its triangle's threshold, below, which neighbouring
    // triangles rarely share, so sharing vertices saved only 1-3% of them for far more time than it saved. Triangles
    // are written in reverse so opaque paths draw front to back, and wound counter-clockwise so
    // back-face culling drops triangles that the miter inset has inverted. Hull triangles extend outside the fill,
    // so they are written degenerate: the interior pass must never paint them. The debug outlines need each triangle's
    // polygon edges, which shared vertices cannot carry, so they go in a byte per triangle in index buffer order.
    // Triangles the inset would collapse at the current scale are left to the edge pass, as solveTriangle() decides.
    // Each vertex carries its triangle's threshold, so the pass clips a triangle away whole.
    static void writeIndexedInterior(Mesh& mesh) {
        size_t ntris = mesh._tris.count(), i;
        if (ntris == 0)
            return;
        unsigned int *idx = mesh._interiorIdxs.new_ptr(ntris * 3);
        unsigned char *edges = mesh._interiorEdges.new_ptr(ntris);
        vector<MeshPath>& paths = mesh._timeline.basePaths();
        for (i = 0; i < ntris; i++) {
            Triangle& t = mesh._tris[ntris - 1 - i];
            unsigned int p = t.p & Triangle::kFlagsMask;
            const float *im = & mesh._interiorMiters[(ntris - 1 - i) * 6];
            MeshVertex v[3] = { { p, t.x0, t.y0, im[0], im[1] }, { p, t.x1, t.y1, im[2], im[3] }, { p, t.x2, t.y2, im[4], im[5] } };
            bool e0 = t.p & Triangle::kFlagsEdge0, e1 = t.p & Triangle::kFlagsEdge1, e2 = t.p & Triangle::kFlagsEdge2;
            if (t.curves & Triangle::kHull)
                v[1] = v[2] = v[0], e0 = e1 = e2 = false;
            else if (int64_t(t.x1 - t.x0) * (t.y2 - t.y0) - int64_t(t.y1 - t.y0) * (t.x2 - t.x0) < 0)
                std::swap(v[1], v[2]), std::swap(e0, e2);   // edges v0 v2, v2 v1, v1 v0
            *edges++ = e0 | e1 << 1 | e2 << 2;
            float tt = 0;
            if (!(t.curves & Triangle::kHull)) {
                const Bounds& b = paths[p].bounds;
                float sx = (b.ux - b.lx) / kUnsignedShortMax, sy = (b.uy - b.ly) / kUnsignedShortMax;
                tt = solveTriangle(b.lx + v[0].x * sx, b.ly + v[0].y * sy, Vector(v[0].ix, v[0].iy),
                                   b.lx + v[1].x * sx, b.ly + v[1].y * sy, Vector(v[1].ix, v[1].iy),
                                   b.lx + v[2].x * sx, b.ly + v[2].y * sy, Vector(v[2].ix, v[2].iy));
            }
            // A hull is one vertex, three times over.
            int count = t.curves & Triangle::kHull ? 1 : 3;
            unsigned int base = (unsigned int)mesh._interiorVerts.count();
            MeshVertex *dst = mesh._interiorVerts.new_ptr(count);
            for (int k = 0; k < count; k++)
                dst[k] = v[k], dst[k].t = tt;
            for (int k = 0; k < 3; k++)
                *idx++ = base + (count == 3 ? k : 0);
        }
    }
    
    // Triangulates each path's inner polygon, and adds one hull triangle per curve. On the outer polygon a convex curve
    // runs p0 -> c -> p2 and a concave curve along its chord, so every curve lies inside its own hull triangle. The inner
    // polygon cuts those off: chords for convex curves, p0 -> c -> p2 for concave ones.
    // _imiters: each triangle's interior miters, x and y per vertex. Hulls, which the interior never paints, get zeros.
    static void triangulateRange(vector<Triangle>& _tris, vector<float>& _imiters, vector<int>& _idxs, Scene& scene, int begin, int end) {
        Arena arena;
        TESSalloc alloc = arena.tessAlloc();
        vector<Point> points;
        vector<char> curveEdge;     // whether the polygon edge from each point to the next is a curve's chord or control legs
        vector<int> nextPoint;      // the next point in each point's contour
        vector<float> xy;
        vector<Hull> hulls;
        vector<vector<ContourSegment>> contours;
        vector<int> same;
        unordered_map<uint64_t, int> heads;
        for (int p = begin; p < end; p++) {
            // A copy, which refineChords() changes: the scene keeps its paths as built, so triangulating it again (after
            // every edit) gives the same mesh. Copies share their quadratics until refineChords() replaces them.
            Path path = scene._paths[p];
            float lx, ly, rw, rh;
            lx = path._bounds.lx, rw = kUnsignedShortMax / (path._bounds.ux - lx);
            ly = path._bounds.ly, rh = kUnsignedShortMax / (path._bounds.uy - ly);
            
            _idxs.emplace_back((int)_tris.size());
            
            // A curve split by a crossing would otherwise draw as its chord. Stroke outlines cross themselves throughout.
            refineChords(path);
            PathVertices pv(path);
            // The path's own tessellators, which reset() frees with everything they allocated. Their buckets hold as many
            // items as the path has vertices, as libTess advises, not its defaults of hundreds, which most paths leave
            // largely unused: the arena does not recycle them as malloc did, so they would only spread its memory out.
            arena.reset();
            alloc.meshEdgeBucketSize = alloc.meshVertexBucketSize = alloc.meshFaceBucketSize = std::max(1, pv.bases.back());
            alloc.dictNodeBucketSize = alloc.regionBucketSize = alloc.meshVertexBucketSize;
            TESStesselator *tess = tessNewTess(& alloc), *dst = tessNewTess(& alloc);
            if (tess == nullptr || dst == nullptr)
                continue;
            tessSetOption(dst, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
            addPathContours(tess, path);
            if (!tessTesselate(tess, path._evenOdd ? TESS_WINDING_ODD : TESS_WINDING_NONZERO, TESS_BOUNDARY_CONTOURS, 0, 0, kTESSNormal))
                continue;
            const float *verts = tessGetVertices(tess);
            const TESSindex *indices = tessGetVertexIndices(tess);
            const int *elems = tessGetElements(tess), nelems = tessGetElementCount(tess);
            // Boundary contours can carry microgeometry (vertices a few float ULPs apart, loops smaller than that) where
            // tight pieces of stroke outlines cross. The Delaunay pass asserts on it, so merge vertices closer than eps
            // and drop contours that collapse.
            float eps = vertexMergeLimit(path._bounds);
            contours.resize(0);
            vector<int> kept;
            for (int e = 0; e < nelems; e++) {
                int base = elems[e * 2], n = elems[e * 2 + 1], i;
                if (n < 3)
                    continue;
                kept.resize(0);
                for (i = 0; i < n; i++) {
                    const float *a = verts + (base + i) * 2;
                    if (kept.size()) {
                        const float *b = verts + (base + kept.back()) * 2;
                        if (std::abs(a[0] - b[0]) < eps && std::abs(a[1] - b[1]) < eps)
                            continue;
                    }
                    kept.push_back(i);
                }
                while (kept.size() > 1) {
                    const float *a = verts + (base + kept.back()) * 2, *b = verts + (base + kept[0]) * 2;
                    if (std::abs(a[0] - b[0]) >= eps || std::abs(a[1] - b[1]) >= eps)
                        break;
                    kept.pop_back();
                }
                // Where stroke pieces nearly coincide the union also leaves needles: the contour reverses into a spike
                // far narrower than the pen, whose tip has a near-infinite miter. Real stroke corners are wider (a miter
                // join at the default limit is still about half the pen), so remove such tips until none are left. A tip
                // must be sharp (its miter longer than kTriangleDilationMax) as well as narrow: next to a short edge, any
                // corner is narrow at that edge's end, as where a stroke's inner offsets meet.
                if (path._outline && path._penWidth > 0) {
                    const float minWidth = 0.1f * path._penWidth;
                    for (bool removed = true; removed && kept.size() > 3; ) {
                        removed = false;
                        for (size_t k = 0; k < kept.size() && kept.size() > 3; ) {
                            const float *a = verts + (base + kept[(k + kept.size() - 1) % kept.size()]) * 2, *b = verts + (base + kept[k]) * 2, *c = verts + (base + kept[(k + 1) % kept.size()]) * 2;
                            float abx = b[0] - a[0], aby = b[1] - a[1], bcx = c[0] - b[0], bcy = c[1] - b[1];
                            float lab = std::hypot(abx, aby), lbc = std::hypot(bcx, bcy), dot = abx * bcx + aby * bcy;
                            // The tip's miter is 1 / sin(half its angle), and sin^2(half) = (1 + dot / (lab lbc)) / 2.
                            bool sharp = (lab * lbc + dot) * kTriangleDilationMax * kTriangleDilationMax < 2 * lab * lbc;
                            if (sharp && std::abs(abx * bcy - aby * bcx) < minWidth * std::max(lab, lbc))
                                kept.erase(kept.begin() + k), removed = true;
                            else
                                k++;
                        }
                    }
                }
                n = (int)kept.size();
                float area = 0;
                for (i = 0; i < n; i++) {
                    const float *a = verts + (base + kept[i]) * 2, *b = verts + (base + kept[(i + 1) % n]) * 2;
                    area += a[0] * b[1] - a[1] * b[0];
                }
                // Overlapping stroke pieces also leave islands and holes far smaller than the pen, which no stroke can
                // have. Their spiky vertices become constraints inside the fill, and their near-reversing miters wreck
                // the triangles around them, so they go too.
                float pen = path._outline ? 0.25f * path._penWidth : 0;
                if (n < 3 || std::abs(area) * 0.5f < std::max(eps * eps, pen * pen))
                    continue;
                contours.emplace_back(n);
                vector<ContourSegment>& c = contours.back();
                for (i = 0; i < n; i++) {
                    int ia = base + kept[i], ib = base + kept[(i + 1) % n];
                    const float *a = verts + ia * 2, *b = verts + ib * 2;
                    Segment q = pv.segment(indices[ia], indices[ib], a, b);
                    c[i] = { a[0], a[1], q.found ? q.cx : a[0], q.found ? q.cy : a[1], false, false };
                    if (q.found)
                        classify(c[i], b[0], b[1]);
                }
            }
            resolveCrossings(contours);
            
            points.resize(0), curveEdge.resize(0), nextPoint.resize(0), hulls.resize(0);
            for (vector<ContourSegment>& c : contours) {
                int n = (int)c.size(), i;
                // An on-curve vertex's outer edges head to a convex curve's control point, else along the chord: the hulls'
                // outer edges, which the edge pass dilates away from. Its interior inset follows every curve's tangent.
                auto corner = [&](int i) {
                    const ContourSegment &sp = c[(i + n - 1) % n], &sn = c[i], &next = c[(i + 1) % n];
                    bool cp = sp.curved && !sp.concave, cn = sn.curved && !sn.concave;
                    Point v(sn.x, sn.y, cp ? sp.cx : sp.x, cp ? sp.cy : sp.y, cn ? sn.cx : next.x, cn ? sn.cy : next.y);
                    v.tpx = sp.curved ? sp.cx : sp.x, v.tpy = sp.curved ? sp.cy : sp.y, v.tnx = sn.curved ? sn.cx : next.x, v.tny = sn.curved ? sn.cy : next.y;
                    return v;
                };
                size_t first = points.size();
                for (i = 0; i < n; i++) {
                    Point v = corner(i);
                    points.push_back(v);
                    const ContourSegment& sg = c[i];
                    curveEdge.push_back(sg.curved);
                    if (sg.curved) {
                        Hull h;
                        h.p0 = v, h.p2 = corner((i + 1) % n), h.concave = sg.concave;
                        // A neighbour's tangent at the join heads to its control point if it is a curve, else along it.
                        const ContourSegment &before = c[(i + n - 1) % n], &after = c[(i + 1) % n], &afterEnd = c[(i + 2) % n];
                        bool bc = before.curved && (before.cx != sg.x || before.cy != sg.y), ac = after.curved && (after.cx != after.x || after.cy != after.y);
                        h.tpx = bc ? before.cx : before.x, h.tpy = bc ? before.cy : before.y;
                        h.tnx = ac ? after.cx : afterEnd.x, h.tny = ac ? after.cy : afterEnd.y;
                        h.c = Point(sg.cx, sg.cy, v.x, v.y, h.p2.x, h.p2.y, sg.concave);
                        hulls.push_back(h);
                        if (sg.concave)
                            points.push_back(h.c), curveEdge.push_back(true);
                    }
                }
                xy.resize(0);
                for (size_t k = first; k < points.size(); k++)
                    xy.push_back(points[k].x), xy.push_back(points[k].y), nextPoint.push_back(k + 1 == points.size() ? (int)first : (int)k + 1);
                tessAddContour(dst, 2, xy.data(), kPointStride, int(points.size() - first));
            }
            
            if (tessTesselate(dst, TESS_WINDING_NONZERO, TESS_POLYGONS, kPolySize, 2, kTESSNormal)) {
                const float *dverts = tessGetVertices(dst);
                const TESSindex *dindices = tessGetVertexIndices(dst), *polygon = tessGetElements(dst);
                const int ndverts = tessGetVertexCount(dst), ntris = tessGetElementCount(dst);
                int found = 0;
                for (int k = 0; k < ndverts; k++)
                    if (dindices[k] == TESS_UNDEF)
                        innerIntersections()++;
                    else
                        found++;
                // The tessellator merges coincident points (where contours touch themselves or each other) and returns
                // one of their indices, whichever its sweep kept. Each occurrence has its own edges, so ring them and
                // give each triangle the one whose wedge holds it, else the miters depend on the contours' order.
                same.resize(0);
                if (found < (int)points.size()) {
                    same.resize(points.size());
                    heads.clear();
                    for (int k = 0; k < (int)points.size(); k++) {
                        auto it = heads.emplace(PathVertices::key(points[k].x, points[k].y), k);
                        same[k] = it.second ? k : same[it.first->second];
                        same[it.first->second] = k;
                    }
                }
                for (int i = 0; i < ntris; i++, polygon += kPolySize) {
                    Point v[3];
                    Vector m[3];
                    bool convex[3], ef[3];
                    unsigned int curves = 0;
                    float cx = 0, cy = 0;
                    TESSindex di[3];
                    for (int k = 0; k < 3; k++) {
                        di[k] = dindices[polygon[k]];
                        v[k] = di[k] == TESS_UNDEF ? Point(dverts[polygon[k] * 2], dverts[polygon[k] * 2 + 1]) : points[di[k]];
                    }
                    for (int k = 0; k < 3; k++) {
                        // The triangle's bisector at this vertex.
                        const Point &a = v[(k + 1) % 3], &b = v[(k + 2) % 3];
                        float ax = a.x - v[k].x, ay = a.y - v[k].y, bx = b.x - v[k].x, by = b.y - v[k].y;
                        float ra = 1 / std::sqrt(ax * ax + ay * ay), rb = 1 / std::sqrt(bx * bx + by * by);
                        float dx = ax * ra + bx * rb, dy = ay * ra + by * rb;
                        if (di[k] != TESS_UNDEF && same.size() && same[di[k]] != (int)di[k]) {
                            for (int j = di[k]; ; ) {
                                if (points[j].corner && !points[j].control && inside(points[j], dx, dy)) {
                                    v[k] = points[j], di[k] = j;
                                    break;
                                }
                                if ((j = same[j]) == (int)di[k])
                                    break;
                            }
                        }
                        // A triangle outside its vertex's wedge is not bounded by that corner: the vertex is the tip of a
                        // piece nested inside the fill (stroke outlines' overlapping pieces leave these). Its lines would cut
                        // the triangle away, so here it has no corner.
                        if (v[k].corner && !v[k].control && !inside(v[k], dx, dy))
                            v[k].corner = false;
                        cx += v[k].x / 3, cy += v[k].y / 3;
                    }
                    for (int k = 0; k < 3; k++) {
                        bool insetConvex = true;
                        Vector mi = v[k].corner ? miter(v[k].tangents(), cx, cy, & insetConvex) : Vector(0, 0);
                        if (v[k].corner && !v[k].control)
                            mi = sharpInset(v[k].tangents(), mi, insetConvex, v[(k + 1) % 3].x, v[(k + 1) % 3].y, v[(k + 2) % 3].x, v[(k + 2) % 3].y);
                        _imiters.push_back(mi.x), _imiters.push_back(mi.y);
                        m[k] = v[k].corner ? miter(v[k], cx, cy, & convex[k]) : Vector(0, 0);
                        if (!v[k].corner || v[k].control)
                            convex[k] = false;
                        if (v[k].control)
                            curves |= Triangle::kControl0 << k;
                        // The polygon's straight edges, contours' closing edges included, between the occurrences chosen
                        // above: they set the edge pass's bevels and the debug outlines. Curves' chords and control legs are
                        // left to the hulls, which draw the curves themselves.
                        TESSindex a = di[k], b = di[(k + 1) % 3];
                        ef[k] = a != TESS_UNDEF && b != TESS_UNDEF && (nextPoint[a] == (int)b ? !curveEdge[a] : nextPoint[b] == (int)a && !curveEdge[b]);
                    }
                    writeTriangle(_tris, p, v, m, convex, ef[0], ef[1], ef[2], curves, lx, ly, rw, rh);
                    // Its first control point's curve, so the edge pass can bound the polygon along that curve's hull edges
                    // with the curve itself: its control's p0 and p2.
                    if (curves) {
                        const Point& cp = v[curves & Triangle::kControl0 ? 0 : curves & Triangle::kControl1 ? 1 : 2];
                        Triangle& t = _tris.back();
                        t.tx0 = quantise((cp.px - lx) * rw), t.ty0 = quantise((cp.py - ly) * rh), t.tx2 = quantise((cp.nx - lx) * rw), t.ty2 = quantise((cp.ny - ly) * rh);
                    }
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
                // (tangent-preserving quadratics) have no turn to measure, so they take the curve's own convexity. As in
                // isCurved(), tangents that turn back are not smooth, however small the sine of their turn: a slit's end.
                float cx = h.c.x, cy = h.c.y;
                auto turn = [&](float ax, float ay, float bx, float by) {
                    float cross = ax * by - ay * bx;
                    return ax * bx + ay * by > 0 && cross * cross <= kLineTurnSine * kLineTurnSine * (ax * ax + ay * ay) * (bx * bx + by * by) ? !h.concave : cross > 0;
                };
                bool start = turn(h.p0.x - h.tpx, h.p0.y - h.tpy, cx - h.p0.x, cy - h.p0.y);
                bool end = turn(h.p2.x - cx, h.p2.y - cy, h.tnx - h.p2.x, h.tny - h.p2.y);
                convex[0] = h.concave ? end : start, convex[2] = h.concave ? start : end;
                _imiters.insert(_imiters.end(), 6, 0.f);
                writeTriangle(_tris, p, v, m, convex, !h.concave, !h.concave, h.concave,
                              Triangle::kHull | (h.concave ? Triangle::kConcave : 0), lx, ly, rw, rh);
                Triangle& t = _tris.back();
                float t0x = h.concave ? h.tnx : h.tpx, t0y = h.concave ? h.tny : h.tpy, t2x = h.concave ? h.tpx : h.tnx, t2y = h.concave ? h.tpy : h.tny;
                t.tx0 = quantise((t0x - lx) * rw), t.ty0 = quantise((t0y - ly) * rh), t.tx2 = quantise((t2x - lx) * rw), t.ty2 = quantise((t2y - ly) * rh);
            }
        }
    }
};
