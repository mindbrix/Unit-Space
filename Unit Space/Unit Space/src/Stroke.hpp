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
//    follows the curve itself, and two regions meeting where the piece's end normals cross cover the rest: one back to
//    the curve, one out to the folded offset. Pieces split where their radius crosses half the width, so each folds
//    throughout or not at all.
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
            addPiece(q + i * 2);
        if (closed && npoints > 2) {
            const float *a = q + (npoints - 1) * 2;
            if (a[0] != q[0] || a[1] != q[1]) {
                const float line[6] = { a[0], a[1], (a[0] + q[0]) * 0.5f, (a[1] + q[1]) * 0.5f, q[0], q[1] };
                addPiece(line);
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

    // A quadratic's curvature is |B' x B''| / |B'|^3, with B'(t) = 2(A + tE) and B'' = 2E (A = c - p0, E = p2 - 2c + p0),
    // so |A x E| / (2 |A + tE|^3): it peaks once. Its radius therefore crosses the half-width w at most twice, where
    // |A + tE| = r with r^3 = w |A x E| / 2. Splitting there leaves pieces whose radius is wholly above or below w, so
    // each piece's offset either folds back on itself throughout or not at all (see folds).
    void addPiece(const float *q) {
        float ax = q[2] - q[0], ay = q[3] - q[1], ex = q[4] - 2.f * q[2] + q[0], ey = q[5] - 2.f * q[3] + q[1];
        float cross = ax * ey - ay * ex, ts[2];
        int n = 0;
        if (cross != 0) {
            float r = std::cbrt(0.5f * w * std::abs(cross)), a = ex * ex + ey * ey, b = ax * ex + ay * ey, c = ax * ax + ay * ay - r * r;
            float disc = b * b - a * c;
            if (disc > 0) {
                float root = std::sqrt(disc);
                for (float t : { (-b - root) / a, (-b + root) / a })
                    if (t > 0 && t < 1)
                        ts[n++] = t;
            }
        }
        float rest[6] = { q[0], q[1], q[2], q[3], q[4], q[5] }, head[6], done = 0;
        for (int k = 0; k < n; done = ts[k++]) {
            splitQuadratic(rest, (ts[k] - done) / (1 - done), head, rest);
            pushPiece(head);
        }
        pushPiece(rest);
    }

    // Splits quadratic q at t: a covers [0, t] and b [t, 1]. b may be q.
    static void splitQuadratic(const float *q, float t, float *a, float *b) {
        for (int k = 0; k < 2; k++) {
            float p01 = q[k] + t * (q[2 + k] - q[k]), p12 = q[2 + k] + t * (q[4 + k] - q[2 + k]), p = p01 + t * (p12 - p01);
            a[k] = q[k], a[2 + k] = p01, a[4 + k] = p;
            b[4 + k] = q[4 + k], b[2 + k] = p12, b[k] = p;
        }
    }

    void pushPiece(const float *q) {
        float ax = q[2] - q[0], ay = q[3] - q[1], bx = q[4] - q[2], by = q[5] - q[3], wx = q[4] - q[0], wy = q[5] - q[1];
        if (wx * wx + wy * wy == 0 && ax * ax + ay * ay == 0)
            return;
        if (ax == 0 && ay == 0) ax = wx, ay = wy;
        if (bx == 0 && by == 0) bx = wx, by = wy;
        float la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
        Piece p;
        p.x0 = q[0], p.y0 = q[1], p.cx = q[2], p.cy = q[3], p.x2 = q[4], p.y2 = q[5];
        p.t0x = ax / la, p.t0y = ay / la, p.t2x = bx / lb, p.t2y = by / lb;
        p.n0x = -p.t0y, p.n0y = p.t0x, p.n2x = -p.t2y, p.n2y = p.t2x;
        pieces.push_back(p);
    }

    // Whether offsetting by d folds the piece back on itself: it turns toward that side, with a radius of curvature below
    // |d|, 2 |A + E/2|^3 / |A x E| at its middle, and so throughout (see addPiece).
    static bool folds(const Piece& p, float d) {
        float ax = p.cx - p.x0, ay = p.cy - p.y0, ex = p.x2 - 2.f * p.cx + p.x0, ey = p.y2 - 2.f * p.cy + p.y0, cross = ax * ey - ay * ex;
        float mx = ax + 0.5f * ex, my = ay + 0.5f * ey, m2 = mx * mx + my * my;
        return cross != 0 && (cross > 0) == (d > 0) && 2.f * m2 * std::sqrt(m2) < std::abs(d * cross);
    }

    // The chain along one side, offset by d (positive is left), through every piece and join. Folded pieces follow the
    // curve itself, and inner corners pass through the corner point, so the chain only steps out to the offset where
    // the next part needs it: stepping out and straight back would be a zero-width spike.
    void side(vector<float>& s, float d, bool closed) {
        int n = (int)pieces.size();
        Piece& first = pieces[0];
        bool firstFolds = folds(first, d);
        if (closed && firstFolds)
            s.push_back(first.x0), s.push_back(first.y0);
        else
            s.push_back(first.x0 + d * first.n0x), s.push_back(first.y0 + d * first.n0y);
        for (int i = 0; i < n; i++) {
            Piece& p = pieces[i], &next = pieces[(i + 1) % n];
            bool joined = sharp[i] && (closed || i < n - 1), inner = joined && isInner(p, next, d);
            bool nextFolds = (closed || i < n - 1) && folds(next, d);
            const float q[6] = { p.x0, p.y0, p.cx, p.cy, p.x2, p.y2 };
            if (folds(p, d)) {
                // Follow the curve itself; addFold covers the region beyond it.
                lineTo(s, p.x0, p.y0), quadTo(s, p.cx, p.cy, p.x2, p.y2);
                if (!inner && !(nextFolds && !joined))
                    lineTo(s, p.x2 + d * p.n2x, p.y2 + d * p.n2y);
            } else
                appendOffset(s, q, p.n0x, p.n0y, p.n2x, p.n2y, d, 0);
            if (inner) {
                lineTo(s, p.x2, p.y2);
                if (!nextFolds)
                    lineTo(s, p.x2 + d * next.n0x, p.y2 + d * next.n0y);
            } else if (joined)
                addJoin(s, p, next, d);
        }
        if (closed && !firstFolds)
            s[s.size() - 2] = s[0], s[s.size() - 1] = s[1];
    }

    // Whether the corner from a to b turns toward the side offset by d.
    static bool isInner(const Piece& a, const Piece& b, float d) { return (a.t2x * b.t0y - a.t2y * b.t0x > 0) == (d > 0); }

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
        // Measure how far the curve's true offset lies from the offset curve at its quarter points and midpoint (whose
        // tangent is parallel to its chord), from the offset curve's nearest point rather than at the same parameter: the
        // two differ along the curve by parameterisation (a nearly straight piece whose control sits near one end), which
        // no split removes. Also keep the curve within half the stroke width of its chord (it bows by half its control's
        // distance from the chord's midpoint): the tessellator only sees chords, and on thin strokes those of opposite
        // sides would cross, inventing vertices that flatten the curves.
        float mx = (q[0] + 2.f * q[2] + q[4]) * 0.25f, my = (q[1] + 2.f * q[3] + q[5]) * 0.25f;
        float tx = q[4] - q[0], ty = q[5] - q[1], tl = std::sqrt(tx * tx + ty * ty);
        if (depth < 10 && tl > 0) {
            float nx = -ty / tl, ny = tx / tl, sx = cx - (ax + bx) * 0.5f, sy = cy - (ay + by) * 0.5f;
            const float o[6] = { ax, ay, cx, cy, bx, by };
            float e = 0;
            for (float t : { 0.25f, 0.5f, 0.75f }) {
                float u = 1 - t, px = u * u * q[0] + 2 * u * t * q[2] + t * t * q[4], py = u * u * q[1] + 2 * u * t * q[3] + t * t * q[5];
                float dx = u * (q[2] - q[0]) + t * (q[4] - q[2]), dy = u * (q[3] - q[1]) + t * (q[5] - q[3]), dl = std::sqrt(dx * dx + dy * dy);
                if (dl > 0)
                    e = std::max(e, distanceToQuadratic(o, px - d * dy / dl, py + d * dx / dl));
            }
            if ((depth < 6 && e > tolerance) || sx * sx + sy * sy > w * w) {
                float x01 = (q[0] + q[2]) * 0.5f, y01 = (q[1] + q[3]) * 0.5f, x12 = (q[2] + q[4]) * 0.5f, y12 = (q[3] + q[5]) * 0.5f;
                const float a[6] = { q[0], q[1], x01, y01, mx, my }, b[6] = { mx, my, x12, y12, q[4], q[5] };
                appendOffset(s, a, n0x, n0y, nx, ny, d, depth + 1), appendOffset(s, b, nx, ny, n2x, n2y, d, depth + 1);
                return;
            }
        }
        // Part of a piece can fold even when the whole doesn't, collapsing its offset to a point.
        if (bx != s[s.size() - 2] || by != s[s.size() - 1])
            quadTo(s, cx, cy, bx, by);
    }

    // The distance from (px, py) to quadratic q, from its nearest point: Newton steps from the middle, held to the curve.
    static float distanceToQuadratic(const float *q, float px, float py) {
        float t = 0.5f, ex = 0, ey = 0;
        for (int k = 0; k < 4; k++) {
            float u = 1 - t, x = u * u * q[0] + 2 * u * t * q[2] + t * t * q[4], y = u * u * q[1] + 2 * u * t * q[3] + t * t * q[5];
            float dx = 2 * (u * (q[2] - q[0]) + t * (q[4] - q[2])), dy = 2 * (u * (q[3] - q[1]) + t * (q[5] - q[3]));
            float ddx = 2 * (q[4] - 2 * q[2] + q[0]), ddy = 2 * (q[5] - 2 * q[3] + q[1]);
            ex = x - px, ey = y - py;
            float f1 = ex * dx + ey * dy, f2 = dx * dx + dy * dy + ex * ddx + ey * ddy;
            if (f2 <= 0)
                break;
            t = std::max(0.f, std::min(1.f, t - f1 / f2));
        }
        float u = 1 - t, x = u * u * q[0] + 2 * u * t * q[2] + t * t * q[4], y = u * u * q[1] + 2 * u * t * q[3] + t * t * q[5];
        return std::sqrt((x - px) * (x - px) + (y - py) * (y - py));
    }

    // The outer corner from a's end to b's start on the side offset by d: the miter, round or bevel join.
    void addJoin(vector<float>& s, Piece& a, Piece& b, float d) {
        float cross = a.t2x * b.t0y - a.t2y * b.t0x, dot = a.t2x * b.t0x + a.t2y * b.t0y;
        float px = a.x2, py = a.y2, bx = px + d * b.n0x, by = py + d * b.n0y;
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

    // A zero-length subpath: a circle for round caps, an axis-aligned square for square ones (it has no direction), and
    // nothing for butt caps.
    void addDot(float x, float y) {
        if (cap == kRoundCap) {
            chain.assign({ x + w, y });
            arcTo(chain, x, y, x - w, y, -1.f, true), arcTo(chain, x, y, x + w, y, -1.f, true);
            addContour();
        } else if (cap == kSquare) {
            chain.assign({ x - w, y - w });
            lineTo(chain, x - w, y + w), lineTo(chain, x + w, y + w), lineTo(chain, x + w, y - w);
            addContour();
        }
    }

    // Covers the far side of a piece folding at offset d: the curve to where its end normals meet, and from there out to
    // the folded offset itself, which runs back from b to a. A piece only just folding puts that point on an offset
    // end, collapsing the region to a line that retraces itself, so leave that out.
    void addFold(Piece& p, float d) {
        float den = p.n0x * p.n2y - p.n0y * p.n2x;
        if (den == 0)
            return;
        float u = ((p.x2 - p.x0) * p.n2y - (p.y2 - p.y0) * p.n2x) / den, xx = p.x0 + u * p.n0x, xy = p.y0 + u * p.n0y;
        float ax = p.x0 + d * p.n0x, ay = p.y0 + d * p.n0y, bx = p.x2 + d * p.n2x, by = p.y2 + d * p.n2y;
        chain.assign({ p.x0, p.y0 }), quadTo(chain, p.cx, p.cy, p.x2, p.y2), lineTo(chain, xx, xy), addContour(true);
        if ((xx != ax || xy != ay) && (xx != bx || xy != by) && (ax != bx || ay != by)) {
            const float q[6] = { p.x0, p.y0, p.cx, p.cy, p.x2, p.y2 };
            chain.assign({ ax, ay }), appendOffset(chain, q, p.n0x, p.n0y, p.n2x, p.n2y, d, 0), lineTo(chain, xx, xy), addContour(true);
        }
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

    // Closes the chain back to its start and adds it. Stroke bodies already wind clockwise by construction; separate
    // pieces (orient) are made to.
    void addContour(bool orient = false) {
        lineTo(chain, chain[0], chain[1]);
        int npoints = (int)chain.size() / 2;
        if (npoints < 5)
            return;
        if (orient && pointsSignedArea(chain.data(), chain.data() + chain.size(), kPointStride) > 0)
            reverse(chain);
        SubPath s(chain.data(), npoints, SubPath::kQuadratic);
        out.addSubPath(s);
    }

    Path& out;
    float w, tolerance, miterLimit;
    Join join;
    Cap cap;
    vector<Piece> pieces;
    vector<bool> sharp;
    vector<float> chain, dashes;
    float dashOffset = 0;
};
