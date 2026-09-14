// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vixen420
//
// Enhanced Free Distort is free software: you may redistribute it and/or
// modify it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. It comes with ABSOLUTELY NO WARRANTY. See the
// file LICENSE, or <https://www.gnu.org/licenses/>, for the full text.
//
// Additional permission under GPL-3.0 section 7: this file may be combined with
// the Adobe Illustrator SDK, whose sample framework sources are compiled into
// every plugin built from it. See LICENSE-EXCEPTION.

//  QuadMath.h -- the geometry of Adobe's Free Distort, and of editing it.
//
//  Nothing in here knows about Illustrator. The plugin converts to and from
//  SDK types at the edges, and tools/mathtest compiles this header unmodified
//  with no SDK at all.
//
//  What the renderer does was measured, not assumed (see
//  docs/ENHANCED_FREE_DISTORT_INVESTIGATION.md, section B):
//
//    * The map is BILINEAR in the input art's geometric bounds, applied to
//      every anchor point and every direction handle. It is not a homography
//      and not affine: fitted over 90 anchors and handles, bilinear matches to
//      1e-12 pt while the best affine misses by 30 pt and the corner
//      homography by 45 pt. Segments are not subdivided, so a straight segment
//      stays straight; only its end points move.
//
//    * The stored source rectangle is a frame of reference, not a position.
//      The destination corners are re-expressed relative to it and then laid
//      onto the input art's current bounds. So moving or scaling the art moves
//      or scales the distortion with it, and two dictionaries that differ only
//      by that renormalization render identically.
//
//    * A source quad that is not a rectangle, which only a writer other than
//      Adobe's dialog produces, is read through a frame and per-corner
//      offsets (EffectiveQuad for a Quad source, section D). Adobe's own edit
//      path converts such a dictionary to a rectangular source without
//      changing the drawing, and the effective quad here is what it writes.
//
//  Corner numbering is Adobe's key numbering (src0h, dst3v, ...), in
//  Illustrator's y-up artwork coordinates:
//
//      0 --- 1        0 = top-left      1 = top-right
//      |     |        2 = bottom-left   3 = bottom-right
//      2 --- 3

#ifndef __QUADMATH_H__
#define __QUADMATH_H__

#include <cmath>

namespace fdmath
{
    struct Pt
    {
        double h = 0.0;
        double v = 0.0;
    };

    /** An axis-aligned rectangle, y-up: top >= bottom. */
    struct Rect
    {
        double left = 0.0;
        double top = 0.0;
        double right = 0.0;
        double bottom = 0.0;
    };

    enum Corner
    {
        kTopLeft = 0,
        kTopRight = 1,
        kBottomLeft = 2,
        kBottomRight = 3
    };

    struct Quad
    {
        Pt c[4];
    };

    inline Pt Make(double h, double v)
    {
        Pt p;
        p.h = h;
        p.v = v;
        return p;
    }

    inline Pt Add(Pt a, Pt b) { return Make(a.h + b.h, a.v + b.v); }
    inline Pt Sub(Pt a, Pt b) { return Make(a.h - b.h, a.v - b.v); }
    inline Pt Scale(Pt a, double k) { return Make(a.h * k, a.v * k); }
    inline double Dot(Pt a, Pt b) { return a.h * b.h + a.v * b.v; }
    inline double Cross(Pt a, Pt b) { return a.h * b.v - a.v * b.h; }
    inline double Length(Pt a) { return std::sqrt(Dot(a, a)); }
    inline double Distance(Pt a, Pt b) { return Length(Sub(a, b)); }

    inline double Width(const Rect& r) { return r.right - r.left; }
    inline double Height(const Rect& r) { return r.top - r.bottom; }

    inline Rect MakeRect(double left, double top, double right, double bottom)
    {
        Rect r;
        r.left = left;
        r.top = top;
        r.right = right;
        r.bottom = bottom;
        return r;
    }

    /** The rectangle as a quad, in Adobe's corner order. */
    inline Quad RectQuad(const Rect& r)
    {
        Quad q;
        q.c[kTopLeft] = Make(r.left, r.top);
        q.c[kTopRight] = Make(r.right, r.top);
        q.c[kBottomLeft] = Make(r.left, r.bottom);
        q.c[kBottomRight] = Make(r.right, r.bottom);
        return q;
    }

    inline Rect BoundingRect(const Quad& q)
    {
        Rect r = MakeRect(q.c[0].h, q.c[0].v, q.c[0].h, q.c[0].v);
        for (int i = 1; i < 4; ++i)
        {
            if (q.c[i].h < r.left) r.left = q.c[i].h;
            if (q.c[i].h > r.right) r.right = q.c[i].h;
            if (q.c[i].v > r.top) r.top = q.c[i].v;
            if (q.c[i].v < r.bottom) r.bottom = q.c[i].v;
        }
        return r;
    }

    /** True when the quad is the rectangle it claims to be, in Adobe's corner
        order. Adobe's own dialog only ever writes a source quad of this shape;
        a source quad of any other shape can only come from another writer
        (section D). */
    inline bool IsAxisAlignedRect(const Quad& q, double tolerance)
    {
        return std::fabs(q.c[kTopLeft].v - q.c[kTopRight].v) <= tolerance &&
               std::fabs(q.c[kBottomLeft].v - q.c[kBottomRight].v) <= tolerance &&
               std::fabs(q.c[kTopLeft].h - q.c[kBottomLeft].h) <= tolerance &&
               std::fabs(q.c[kTopRight].h - q.c[kBottomRight].h) <= tolerance &&
               q.c[kTopRight].h > q.c[kTopLeft].h &&
               q.c[kTopLeft].v > q.c[kBottomLeft].v;
    }

    inline bool HasArea(const Rect& r)
    {
        return Width(r) > 0.0 && Height(r) > 0.0;
    }

    /** Re-expresses a point given relative to one rectangle as the same
        relative position in another. */
    inline Pt Renormalize(Pt p, const Rect& from, const Rect& to)
    {
        const double u = (p.h - from.left) / Width(from);
        const double t = (p.v - from.bottom) / Height(from);
        return Make(to.left + u * Width(to), to.bottom + t * Height(to));
    }

    inline Quad Renormalize(const Quad& q, const Rect& from, const Rect& to)
    {
        Quad out;
        for (int i = 0; i < 4; ++i) out.c[i] = Renormalize(q.c[i], from, to);
        return out;
    }

    /** The point at (u, t) of a bilinear patch: u runs left to right, t
        bottom to top, both 0..1 over the patch. */
    inline Pt Bilinear(const Quad& q, double u, double t)
    {
        const Pt bottom = Add(Scale(q.c[kBottomLeft], 1.0 - u), Scale(q.c[kBottomRight], u));
        const Pt top = Add(Scale(q.c[kTopLeft], 1.0 - u), Scale(q.c[kTopRight], u));
        return Add(Scale(bottom, 1.0 - t), Scale(top, t));
    }

    /** The quad the renderer actually draws into, for input art whose
        geometric bounds are `inputBounds`, given the dictionary's source
        rectangle and destination quad. */
    inline Quad EffectiveQuad(const Rect& source, const Quad& destination, const Rect& inputBounds)
    {
        return Renormalize(destination, source, inputBounds);
    }

    /** The rectangle the renderer takes as a source quad's frame of
        reference: through the top-left corner, the top-right corner's h, and
        the bottom-left corner's v. The other three numbers do not shape it.
        For a rectangular source it is that rectangle. Its width or height is
        negative when the source is mirrored, which the renderer follows. */
    inline Rect SourceFrame(const Quad& source)
    {
        return MakeRect(source.c[kTopLeft].h, source.c[kTopLeft].v, source.c[kTopRight].h, source.c[kBottomLeft].v);
    }

    /** False when the frame has no width or no height, which leaves the
        renderer dividing by zero. */
    inline bool HasUsableFrame(const Quad& source, double tolerance)
    {
        const Rect frame = SourceFrame(source);
        return std::fabs(Width(frame)) > tolerance && std::fabs(Height(frame)) > tolerance;
    }

    /** The quad the renderer draws into for a source of any shape, as
        measured (section D): each source corner's offset from its frame's
        corner is subtracted, unscaled, from the input bounds' corner, and
        each destination corner is carried by the bilinear map from the frame
        onto that quad. With a rectangular source every offset is zero and
        this is the renormalization above. */
    inline Quad EffectiveQuad(const Quad& source, const Quad& destination, const Rect& inputBounds)
    {
        const Rect frame = SourceFrame(source);
        const Quad frameCorners = RectQuad(frame);
        const Quad boundsCorners = RectQuad(inputBounds);
        Quad target;
        for (int i = 0; i < 4; ++i)
            target.c[i] = Sub(boundsCorners.c[i], Sub(source.c[i], frameCorners.c[i]));
        Quad out;
        for (int i = 0; i < 4; ++i)
        {
            const double u = (destination.c[i].h - frame.left) / Width(frame);
            const double t = (destination.c[i].v - frame.bottom) / Height(frame);
            out.c[i] = Bilinear(target, u, t);
        }
        return out;
    }

    /** Where Free Distort puts one point of the input art -- an anchor or a
        direction handle alike. `effective` is EffectiveQuad(). */
    inline Pt MapPoint(const Quad& effective, const Rect& inputBounds, Pt p)
    {
        const double u = (p.h - inputBounds.left) / Width(inputBounds);
        const double t = (p.v - inputBounds.bottom) / Height(inputBounds);
        return Bilinear(effective, u, t);
    }

    /** Solves Bilinear(q, u, t) == p by Newton's method. Returns false when
        the patch is degenerate at the solution or the iteration does not
        settle, which happens for folded quads and for points far outside a
        strongly non-convex one. */
    inline bool InverseBilinear(const Quad& q, Pt p, double* u, double* t)
    {
        double uu = 0.5;
        double tt = 0.5;
        for (int iteration = 0; iteration < 50; ++iteration)
        {
            const Pt f = Sub(Bilinear(q, uu, tt), p);
            const Pt du = Add(Scale(Sub(q.c[kBottomRight], q.c[kBottomLeft]), 1.0 - tt),
                              Scale(Sub(q.c[kTopRight], q.c[kTopLeft]), tt));
            const Pt dt = Add(Scale(Sub(q.c[kTopLeft], q.c[kBottomLeft]), 1.0 - uu),
                              Scale(Sub(q.c[kTopRight], q.c[kBottomRight]), uu));
            const double det = Cross(du, dt);
            if (std::fabs(det) < 1e-12) return false;
            const double stepU = (f.h * dt.v - f.v * dt.h) / det;
            const double stepT = (du.h * f.v - du.v * f.h) / det;
            uu -= stepU;
            tt -= stepT;
            if (std::fabs(stepU) < 1e-13 && std::fabs(stepT) < 1e-13)
            {
                *u = uu;
                *t = tt;
                return true;
            }
        }
        return false;
    }

    inline Pt Centroid(const Quad& q)
    {
        return Scale(Add(Add(q.c[0], q.c[1]), Add(q.c[2], q.c[3])), 0.25);
    }

    inline Quad Translate(const Quad& q, Pt delta)
    {
        Quad out;
        for (int i = 0; i < 4; ++i) out.c[i] = Add(q.c[i], delta);
        return out;
    }

    /** The corners in drawing order around the outline: 0, 1, 3, 2. Adobe's
        numbering goes row by row, which is not a polygon. */
    inline int OutlineOrder(int step)
    {
        static const int kOrder[4] = { kTopLeft, kTopRight, kBottomRight, kBottomLeft };
        return kOrder[((step % 4) + 4) % 4];
    }

    /** Reports whether the outline 0-1-3-2 crosses itself. A folded quad is
        still something the renderer will draw, so this is a warning for the
        editor to show, never a refusal. */
    inline bool IsSelfIntersecting(const Quad& q)
    {
        auto segmentsCross = [](Pt a, Pt b, Pt c, Pt d) {
            const double d1 = Cross(Sub(b, a), Sub(c, a));
            const double d2 = Cross(Sub(b, a), Sub(d, a));
            const double d3 = Cross(Sub(d, c), Sub(a, c));
            const double d4 = Cross(Sub(d, c), Sub(b, c));
            return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) &&
                   d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
        };
        // Opposite edges of the outline: top against bottom, left against right.
        const Pt tl = q.c[kTopLeft], tr = q.c[kTopRight], br = q.c[kBottomRight], bl = q.c[kBottomLeft];
        return segmentsCross(tl, tr, br, bl) || segmentsCross(tr, br, bl, tl);
    }

    inline bool Near(const Quad& a, const Quad& b, double tolerance)
    {
        for (int i = 0; i < 4; ++i)
            if (Distance(a.c[i], b.c[i]) > tolerance) return false;
        return true;
    }

    // ---- editing ----------------------------------------------------------
    //
    //  Every edit is computed from the quad as it was when the gesture began
    //  plus where the pointer is now -- never from the previous preview -- so a
    //  drag that wanders and comes back lands exactly where it started.

    /** Free distortion: one corner goes where it is put. */
    inline Quad MoveCorner(const Quad& start, int corner, Pt to)
    {
        Quad out = start;
        out.c[corner] = to;
        return out;
    }

    /** The corner that shares an edge with `corner` along the horizontal
        (top or bottom) edge, and along the vertical (left or right) edge. */
    inline int HorizontalNeighbor(int corner) { return corner ^ 1; }
    inline int VerticalNeighbor(int corner) { return corner ^ 2; }
    inline int Opposite(int corner) { return 3 - corner; }

    /** Symmetric distortion: the diagonally opposite corner moves by the
        same amount the other way, so the quad keeps its centroid. */
    inline Quad MoveCornerSymmetric(const Quad& start, int corner, Pt to)
    {
        const Pt delta = Sub(to, start.c[corner]);
        Quad out = start;
        out.c[corner] = to;
        out.c[Opposite(corner)] = Sub(start.c[Opposite(corner)], delta);
        return out;
    }

    /** Perspective, as a trapezoid edit: the corner moves along one axis and
        its neighbor on the edge running across that axis moves the mirror
        amount, so that edge grows or shrinks about its own midpoint. Which
        axis is decided by the larger component of the drag. */
    inline Quad MoveCornerPerspective(const Quad& start, int corner, Pt to)
    {
        const Pt delta = Sub(to, start.c[corner]);
        Quad out = start;
        if (std::fabs(delta.h) >= std::fabs(delta.v))
        {
            // Horizontal drag: the top or bottom edge widens or narrows.
            const int partner = HorizontalNeighbor(corner);
            out.c[corner].h = start.c[corner].h + delta.h;
            out.c[partner].h = start.c[partner].h - delta.h;
        }
        else
        {
            // Vertical drag: the left or right edge lengthens or shortens.
            const int partner = VerticalNeighbor(corner);
            out.c[corner].v = start.c[corner].v + delta.v;
            out.c[partner].v = start.c[partner].v - delta.v;
        }
        return out;
    }

    /** Affine: the quad stays a parallelogram. The dragged corner goes where
        it is put, the diagonally opposite corner stays, and the remaining two
        are solved so that opposite sides stay parallel while keeping the
        edge directions of the start as far as that allows: the quad is the
        start's parallelogram, shared-vertex at the opposite corner, sheared
        and scaled so that its far corner lands on the pointer. */
    inline Quad MoveCornerAffine(const Quad& start, int corner, Pt to)
    {
        const int opposite = Opposite(corner);
        const int hn = HorizontalNeighbor(corner);   // shares the horizontal edge with `corner`
        const int vn = VerticalNeighbor(corner);     // shares the vertical edge with `corner`
        const Pt o = start.c[opposite];
        // Edge directions leaving the fixed corner.
        Pt a = Sub(start.c[vn], o);   // vn shares the horizontal edge with `opposite`
        Pt b = Sub(start.c[hn], o);   // hn shares the vertical edge with `opposite`
        const double det = Cross(a, b);
        Quad out = start;
        if (std::fabs(det) < 1e-12)
        {
            out.c[corner] = to;
            return out;
        }
        // Express the new diagonal in the start's edge basis: d = x*a + y*b.
        const Pt d = Sub(to, o);
        const double x = Cross(d, b) / det;
        const double y = Cross(a, d) / det;
        out.c[opposite] = o;
        out.c[vn] = Add(o, Scale(a, x));
        out.c[hn] = Add(o, Scale(b, y));
        out.c[corner] = to;
        return out;
    }

    /** True when the quad is a parallelogram, which is exactly when the
        bilinear map degenerates to an affine one. */
    inline bool IsParallelogram(const Quad& q, double tolerance)
    {
        const Pt s1 = Add(q.c[kTopLeft], q.c[kBottomRight]);
        const Pt s2 = Add(q.c[kTopRight], q.c[kBottomLeft]);
        return Distance(s1, s2) <= tolerance;
    }
}

#endif // __QUADMATH_H__
