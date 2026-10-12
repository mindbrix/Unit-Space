//
//  Stroke.hpp
//  Unit Space
//
//  Strokes become filled outlines, so they share the fill pipeline: tessellation, hull curves and edge AA.
//
#import "Path.hpp"

// Expands a subpath's quadratics into closed contours whose nonzero union is the stroke.
//  - Each side is one chain of offset quadratics: tangent-preserving, and split until their midpoints are within
//    tolerance of the true offset. An open subpath is one contour (left side, end cap, right side back, start cap) and
//    a closed one two loops (left, and right reversed).
//  - Corners: the outer side gets the miter, round or bevel join; the inner side passes through the corner point, so
//    it overlaps the neighbouring pieces with the same winding instead of cutting across them.
//  - Pieces tighter than the stroke (radius below half its width) fold their inner offset back on itself, so that side
//    follows the curve itself, and triangles meeting at the centre of curvature cover the rest.
// Every contour winds clockwise (the body lies right of the left side's travel), so overlaps union rather than cancel.
struct Stroke {
    enum Join { kMiter, kRound, kBevel };
    enum Cap { kButt, kRoundCap, kSquare };

    Stroke(Path& out, float width, Join join, Cap cap, float miterLimit) : out(out), w(width * 0.5f), miterLimit(miterLimit), join(join), cap(cap) {
        tolerance = std::max(1e-4f, std::min(kSplitCubicPrecision, 0.02f * w));
    }

    // Dash lengths (on, off, ...) and the distance into the pattern at which each subpath starts.
    void setDashes(const float *lengths, int count, float offset) {
        dashes.assign(lengths, lengths + count);
        if (count & 1)
            dashes.insert(dashes.end(), lengths, lengths + count);
        float period = 0;
        for (float d : dashes)
            period += std::max(0.f, d);
        if (period <= 0)
            dashes.resize(0);
        dashOffset = period > 0 ? std::fmod(std::fmod(offset, period) + period, period) : 0;
    }

    // q is a quadratic chain: p0, c0, p1, c1, p2 ... (npoints odd).
    void addSubPath(const float *q, int npoints, bool closed) {
        if (dashes.size() == 0 || npoints < 3)
            addSolid(q, npoints, closed);
        else
            addDashed(q, npoints, closed);
    }

private:
    // Cuts the chain into dashes by arc length, each stroked as an open subpath with caps.
    void addDashed(const float *q, int npoints, bool closed) {
        vector<float> src(q, q + npoints * 2), dash;
        if (closed && (q[0] != q[npoints * 2 - 2] || q[1] != q[npoints * 2 - 1]))
            lineTo(src, q[0], q[1]);
        int k = 0;
        float remaining = dashes[0], skip = dashOffset;
        while (skip > 0) {
            float step = std::min(skip, remaining);
            skip -= step, remaining -= step;
            if (remaining <= 0)
                k = (k + 1) % dashes.size(), remaining = dashes[k];
        }
        bool on = (k & 1) == 0;
        if (on)
            dash.assign({ src[0], src[1] });
        for (size_t i = 0; i + 5 < src.size(); i += 4) {
            const float *b = & src[i];
            // Arc length table along the quadratic.
            constexpr int kSamples = 16;
            float lengths[kSamples + 1] = { 0 }, px = b[0], py = b[1];
            for (int j = 1; j <= kSamples; j++) {
                float t = float(j) / kSamples, u = 1 - t;
                float x = u * u * b[0] + 2 * u * t * b[2] + t * t * b[4], y = u * u * b[1] + 2 * u * t * b[3] + t * t * b[5];
                lengths[j] = lengths[j - 1] + std::sqrt((x - px) * (x - px) + (y - py) * (y - py)), px = x, py = y;
            }
            float t0 = 0, s0 = 0, total = lengths[kSamples];
            auto tAt = [&](float s) {
                int j = 1;
                while (j < kSamples && lengths[j] < s) j++;
                float seg = lengths[j] - lengths[j - 1];
                return ((j - 1) + (seg > 0 ? (s - lengths[j - 1]) / seg : 0)) / kSamples;
            };
            while (s0 + remaining < total) {
                float t1 = tAt(s0 + remaining);
                if (on)
                    subQuadratic(dash, b, t0, t1), addSolid(dash.data(), (int)dash.size() / 2, false);
                s0 += remaining, t0 = t1;
                k = (k + 1) % dashes.size(), remaining = dashes[k], on = !on;
                if (on) {
                    float u = 1 - t0;
                    dash.assign({ u * u * b[0] + 2 * u * t0 * b[2] + t0 * t0 * b[4], u * u * b[1] + 2 * u * t0 * b[3] + t0 * t0 * b[5] });
                }
            }
            if (on)
                subQuadratic(dash, b, t0, 1);
            remaining -= total - s0;
        }
        if (on && dash.size() > 2)
            addSolid(dash.data(), (int)dash.size() / 2, false);
    }

    // Appends the part of quadratic b between t0 and t1 to the chain, which already ends at b(t0).
    static void subQuadratic(vector<float>& s, const float *b, float t0, float t1) {
        if (t1 <= t0)
            return;
        float u0 = 1 - t0, u1 = 1 - t1, m = u0 * t1 + t0 * u1;
        float cx = u0 * u1 * b[0] + m * b[2] + t0 * t1 * b[4], cy = u0 * u1 * b[1] + m * b[3] + t0 * t1 * b[5];
        float ex = u1 * u1 * b[0] + 2 * u1 * t1 * b[2] + t1 * t1 * b[4], ey = u1 * u1 * b[1] + 2 * u1 * t1 * b[3] + t1 * t1 * b[5];
        if (t1 == 1)
            ex = b[4], ey = b[5];
        quadTo(s, cx, cy, ex, ey);
    }

    void addSolid(const float *q, int npoints, bool closed) {
        pieces.resize(0);
        for (int i = 0; i + 2 < npoints; i += 2)
            addPiece(q + i * 2, 0);
        if (closed && npoints > 2) {
            const float *a = q + (npoints - 1) * 2;
            if (a[0] != q[0] || a[1] != q[1]) {
                const float line[6] = { a[0], a[1], (a[0] + q[0]) * 0.5f, (a[1] + q[1]) * 0.5f, q[0], q[1] };
                addPiece(line, 0);
            }
        }
        int n = (int)pieces.size();
        if (n == 0) {
            if (npoints > 0)
                addDot(q[0], q[1]);
            return;
        }
        // Joins turning so little that sharing one normal moves the offset by less than twice the tolerance (smooth
        // joins, and the kinks of least-error quadratics) need no join, and their offsets meet exactly.
        float smooth = 2.f * tolerance / w;
        sharp.assign(n, true);
        for (int i = 0; i < (closed ? n : n - 1); i++) {
            Piece& a = pieces[i], &b = pieces[(i + 1) % n];
            float cross = a.t2x * b.t0y - a.t2y * b.t0x, dot = a.t2x * b.t0x + a.t2y * b.t0y;
            if (dot > 0 && cross * cross < smooth * smooth) {
                float nx = a.n2x + b.n0x, ny = a.n2y + b.n0y, rl = 1.f / std::sqrt(nx * nx + ny * ny);
                a.n2x = b.n0x = nx * rl, a.n2y = b.n0y = ny * rl;
                sharp[i] = false;
            }
        }
        vector<float> left, right;
        side(left, w, closed), side(right, -w, closed);
        if (closed) {
            chain = left, addContour();
            reverse(right), chain = right, addContour();
        } else {
            Piece& a = pieces[0], &b = pieces[n - 1];
            chain = left;
            addCap(b.x2, b.y2, b.t2x, b.t2y);
            reverse(right);
            for (size_t k = 2; k < right.size(); k++)
                chain.push_back(right[k]);
            addCap(a.x0, a.y0, -a.t0x, -a.t0y);
            addContour();
        }
        for (Piece& p : pieces)
            for (float d : { w, -w })
                if (folds(p, d))
                    addFold(p, d);
    }

    struct Piece {
        float x0, y0, cx, cy, x2, y2;
        float t0x, t0y, t2x, t2y, n0x, n0y, n2x, n2y;   // unit tangents and left normals at the ends
    };

    // Splits quadratics until they turn by at most kPieceTurnLimit, so offsets are accurate and folds are detectable.
    void addPiece(const float *q, int depth) {
        float ax = q[2] - q[0], ay = q[3] - q[1], bx = q[4] - q[2], by = q[5] - q[3], wx = q[4] - q[0], wy = q[5] - q[1];
        if (wx * wx + wy * wy == 0 && ax * ax + ay * ay == 0)
            return;
        if (ax == 0 && ay == 0) ax = wx, ay = wy;
        if (bx == 0 && by == 0) bx = wx, by = wy;
        float la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
        if (depth < 8 && ax * bx + ay * by < std::cos(kPieceTurnLimit) * la * lb) {
            float x01 = (q[0] + q[2]) * 0.5f, y01 = (q[1] + q[3]) * 0.5f, x12 = (q[2] + q[4]) * 0.5f, y12 = (q[3] + q[5]) * 0.5f;
            float x = (x01 + x12) * 0.5f, y = (y01 + y12) * 0.5f;
            const float a[6] = { q[0], q[1], x01, y01, x, y }, b[6] = { x, y, x12, y12, q[4], q[5] };
            addPiece(a, depth + 1), addPiece(b, depth + 1);
            return;
        }
        Piece p;
        p.x0 = q[0], p.y0 = q[1], p.cx = q[2], p.cy = q[3], p.x2 = q[4], p.y2 = q[5];
        p.t0x = ax / la, p.t0y = ay / la, p.t2x = bx / lb, p.t2y = by / lb;
        p.n0x = -p.t0y, p.n0y = p.t0x, p.n2x = -p.t2y, p.n2y = p.t2x;
        pieces.push_back(p);
    }

    // Whether offsetting by d folds the piece back on itself: its radius of curvature is below |d| on that side.
    static bool folds(const Piece& p, float d) {
        float ax = p.x0 + d * p.n0x, ay = p.y0 + d * p.n0y, bx = p.x2 + d * p.n2x, by = p.y2 + d * p.n2y;
        return (bx - ax) * (p.x2 - p.x0) + (by - ay) * (p.y2 - p.y0) <= 0;
    }

    // The chain along one side, offset by d (positive is left), through every piece and join.
    void side(vector<float>& s, float d, bool closed) {
        int n = (int)pieces.size();
        Piece& first = pieces[0];
        s.push_back(first.x0 + d * first.n0x), s.push_back(first.y0 + d * first.n0y);
        for (int i = 0; i < n; i++) {
            Piece& p = pieces[i];
            const float q[6] = { p.x0, p.y0, p.cx, p.cy, p.x2, p.y2 };
            if (folds(p, d)) {
                // Follow the curve itself; addFold covers the region beyond it.
                lineTo(s, p.x0, p.y0), quadTo(s, p.cx, p.cy, p.x2, p.y2), lineTo(s, p.x2 + d * p.n2x, p.y2 + d * p.n2y);
            } else
                appendOffset(s, q, p.n0x, p.n0y, p.n2x, p.n2y, d, 0);
            if (sharp[i] && (closed || i < n - 1))
                addJoin(s, p, pieces[(i + 1) % n], d);
        }
        if (closed)
            s[s.size() - 2] = s[0], s[s.size() - 1] = s[1];
    }

    // Appends the offset of the quadratic q by d, given its end normals, to the chain (its start point is already there).
    void appendOffset(vector<float>& s, const float *q, float n0x, float n0y, float n2x, float n2y, float d, int depth) {
        float ax = q[0] + d * n0x, ay = q[1] + d * n0y, bx = q[4] + d * n2x, by = q[5] + d * n2y;
        // Tangents follow the normals, so shared normals keep neighbouring offsets tangent to each other.
        float t0x = n0y, t0y = -n0x, t2x = n2y, t2y = -n2x, den = t0x * t2y - t0y * t2x;
        float cx = (ax + bx) * 0.5f, cy = (ay + by) * 0.5f;
        if (den * den > 1e-12f) {
            float u = ((bx - ax) * t2y - (by - ay) * t2x) / den;
            float px = ax + u * t0x, py = ay + u * t0y;
            if ((px - ax) * (bx - ax) + (py - ay) * (by - ay) > 0 && (bx - px) * (bx - ax) + (by - py) * (by - ay) > 0)
                cx = px, cy = py;
        }
        // Compare midpoints with the true offset of the curve's midpoint, whose tangent is parallel to its chord.
        float mx = (q[0] + 2.f * q[2] + q[4]) * 0.25f, my = (q[1] + 2.f * q[3] + q[5]) * 0.25f;
        float tx = q[4] - q[0], ty = q[5] - q[1], tl = std::sqrt(tx * tx + ty * ty);
        if (depth < 6 && tl > 0) {
            float nx = -ty / tl, ny = tx / tl, ex = mx + d * nx - (ax + 2.f * cx + bx) * 0.25f, ey = my + d * ny - (ay + 2.f * cy + by) * 0.25f;
            if (ex * ex + ey * ey > tolerance * tolerance) {
                float x01 = (q[0] + q[2]) * 0.5f, y01 = (q[1] + q[3]) * 0.5f, x12 = (q[2] + q[4]) * 0.5f, y12 = (q[3] + q[5]) * 0.5f;
                const float a[6] = { q[0], q[1], x01, y01, mx, my }, b[6] = { mx, my, x12, y12, q[4], q[5] };
                appendOffset(s, a, n0x, n0y, nx, ny, d, depth + 1), appendOffset(s, b, nx, ny, n2x, n2y, d, depth + 1);
                return;
            }
        }
        quadTo(s, cx, cy, bx, by);
    }

    // The corner from a's end to b's start on the side offset by d: the join if it is the outer side, and through the
    // corner point if it is the inner one.
    void addJoin(vector<float>& s, Piece& a, Piece& b, float d) {
        float cross = a.t2x * b.t0y - a.t2y * b.t0x, dot = a.t2x * b.t0x + a.t2y * b.t0y;
        float px = a.x2, py = a.y2, bx = px + d * b.n0x, by = py + d * b.n0y;
        if ((cross > 0) == (d > 0)) {   // turning toward this side: the inner side
            lineTo(s, px, py), lineTo(s, bx, by);
            return;
        }
        if (join == kRound)
            arcTo(s, px, py, bx, by, cross > 0 ? 1.f : -1.f);   // the normal turns with the tangent
        else if (join == kMiter && dot > -0.9999f && 1.f / std::sqrt((1.f + dot) * 0.5f) <= miterLimit) {
            // Miter length / width is 1 / cos(turn / 2), and its tip lies along the bisector of the two normals.
            float nx = a.n2x + b.n0x, ny = a.n2y + b.n0y, rl = 1.f / std::sqrt(nx * nx + ny * ny), m = d / std::sqrt((1.f + dot) * 0.5f);
            lineTo(s, px + nx * rl * m, py + ny * rl * m), lineTo(s, bx, by);
        } else
            lineTo(s, bx, by);
    }

    // A cap at (x, y) facing outward along (tx, ty), continuing the chain from the left of the outward direction to its right.
    void addCap(float x, float y, float tx, float ty) {
        float nx = -ty * w, ny = tx * w;
        if (cap == kSquare)
            lineTo(chain, x + nx + tx * w, y + ny + ty * w), lineTo(chain, x - nx + tx * w, y - ny + ty * w), lineTo(chain, x - nx, y - ny);
        else if (cap == kRoundCap)
            arcTo(chain, x, y, x - nx, y - ny, -1.f, true);
        else
            lineTo(chain, x - nx, y - ny);
    }

    void addDot(float x, float y) {
        if (cap != kRoundCap)
            return;
        chain.assign({ x + w, y });
        arcTo(chain, x, y, x - w, y, -1.f, true), arcTo(chain, x, y, x + w, y, -1.f, true);
        addContour();
    }

    // Covers the far side of a piece folding at offset d: the curve to the centre of curvature, and from there to the
    // folded offset's ends.
    void addFold(Piece& p, float d) {
        float den = p.n0x * p.n2y - p.n0y * p.n2x;
        if (den == 0)
            return;
        float u = ((p.x2 - p.x0) * p.n2y - (p.y2 - p.y0) * p.n2x) / den, xx = p.x0 + u * p.n0x, xy = p.y0 + u * p.n0y;
        chain.assign({ p.x0, p.y0 }), quadTo(chain, p.cx, p.cy, p.x2, p.y2), lineTo(chain, xx, xy), addContour(true);
        chain.assign({ xx, xy }), lineTo(chain, p.x2 + d * p.n2x, p.y2 + d * p.n2y), lineTo(chain, p.x0 + d * p.n0x, p.y0 + d * p.n0y), addContour(true);
    }

    // Arcs around (cx, cy) from the chain's end to (bx, by), turning by sign (+1 counter-clockwise), in quadratics of at
    // most 45 degrees. A half turn (the ends opposite) needs half to say which way round it goes.
    void arcTo(vector<float>& s, float cx, float cy, float bx, float by, float sign, bool half = false) {
        float ax = s[s.size() - 2], ay = s[s.size() - 1];
        float a0 = std::atan2(ay - cy, ax - cx), a1 = std::atan2(by - cy, bx - cx), sweep = a1 - a0;
        if (half)
            sweep = sign * float(M_PI);
        else {
            while (sweep * sign < 0) sweep += sign * 2.f * float(M_PI);
            while (sweep * sign > 2.f * float(M_PI)) sweep -= sign * 2.f * float(M_PI);
        }
        int segments = std::max(1, (int)std::ceil(std::abs(sweep) / (float(M_PI) * 0.25f)));
        float step = sweep / segments, r = std::sqrt((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy)), rc = r / std::cos(step * 0.5f);
        for (int k = 1; k <= segments; k++) {
            float mid = a0 + step * (k - 0.5f), end = a0 + step * k;
            float ex = k == segments ? bx : cx + r * std::cos(end), ey = k == segments ? by : cy + r * std::sin(end);
            quadTo(s, cx + rc * std::cos(mid), cy + rc * std::sin(mid), ex, ey);
        }
    }

    static void lineTo(vector<float>& s, float x, float y) {
        float x0 = s[s.size() - 2], y0 = s[s.size() - 1];
        if (x != x0 || y != y0)
            quadTo(s, (x0 + x) * 0.5f, (y0 + y) * 0.5f, x, y);
    }
    static void quadTo(vector<float>& s, float cx, float cy, float x, float y) { s.push_back(cx), s.push_back(cy), s.push_back(x), s.push_back(y); }

    // Reverses a chain's points, keeping its controls between them.
    static void reverse(vector<float>& s) {
        for (int i = 0, j = (int)s.size() / 2 - 1; i < j; i++, j--)
            std::swap(s[i * 2], s[j * 2]), std::swap(s[i * 2 + 1], s[j * 2 + 1]);
    }

    // Drops zero-length segments, and lines that retrace the previous line exactly (A -> B -> A). Inner corners of tight,
    // folding pieces produce these zero-width spikes, which break the tessellator's Delaunay pass.
    void removeSpikes() {
        auto isLine = [](const float *a, const float *c, const float *b) { return c[0] == (a[0] + b[0]) * 0.5f && c[1] == (a[1] + b[1]) * 0.5f; };
        vector<float> s(chain.begin(), chain.begin() + 2);
        for (size_t i = 2; i + 3 < chain.size(); i += 4) {
            const float *c = & chain[i], *p = & chain[i + 2], *last = & s[s.size() - 2];
            if (p[0] == last[0] && p[1] == last[1])
                continue;
            if (s.size() >= 6) {
                const float *prev = & s[s.size() - 6], *pc = & s[s.size() - 4];
                if (p[0] == prev[0] && p[1] == prev[1] && isLine(prev, pc, last) && isLine(last, c, p)) {
                    s.resize(s.size() - 4);
                    continue;
                }
            }
            s.push_back(c[0]), s.push_back(c[1]), s.push_back(p[0]), s.push_back(p[1]);
        }
        chain.swap(s);
    }

    // Closes the chain back to its start and adds it. Stroke bodies already wind clockwise by construction; separate
    // pieces (orient) are made to.
    void addContour(bool orient = false) {
        lineTo(chain, chain[0], chain[1]);
        removeSpikes();
        int npoints = (int)chain.size() / 2;
        if (npoints < 5)
            return;
        if (orient && pointsSignedArea(chain.data(), chain.data() + chain.size(), kPointStride) > 0)
            reverse(chain);
        SubPath s(chain.data(), npoints, SubPath::kQuadratic);
        out.addSubPath(s);
    }

    static constexpr float kPieceTurnLimit = 0.5235987756; // 30 degrees
    Path& out;
    float w, tolerance, miterLimit;
    Join join;
    Cap cap;
    vector<Piece> pieces;
    vector<bool> sharp;
    vector<float> chain, dashes;
    float dashOffset = 0;
};
