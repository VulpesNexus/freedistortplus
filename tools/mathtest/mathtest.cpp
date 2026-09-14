// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vixen420

//  mathtest.cpp -- the quad geometry, checked without Illustrator.
//
//  The host is the authority on what Free Distort does, and the probes
//  measure it there. This test pins plugin/Source/QuadMath.h, compiled
//  unmodified, to those measurements, and checks the invariants every editing
//  mode promises: that a drag computed from its start cannot drift, that the
//  affine mode really keeps a parallelogram, and so on.
//
//  The two sample tables are taken verbatim from host runs recorded in
//  docs/evidence/mapping.tsv: source points of a path, and where Adobe Free
//  Distort put them once the appearance was expanded.

#include "QuadMath.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>

using namespace fdmath;

namespace
{
    int gFailures = 0;
    int gChecks = 0;

    void Check(bool ok, const std::string& what, const std::string& detail = "")
    {
        ++gChecks;
        if (ok) return;
        ++gFailures;
        std::printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  --  ", detail.c_str());
    }

    struct Sample
    {
        double sh, sv, rh, rv;
    };

    // A 6 x 5 grid of anchors, source rectangle equal to the bounds
    // (100..400 by 100..300), destination 130,350 / 450,290 / 70,80 / 370,130.
    const Sample kGridSamples[] = {
        { 100, 100, 70, 80 },
        { 150, 100, 120, 88.3333333333 },
        { 210, 100, 180, 98.3333333333 },
        { 260, 100, 230, 106.666666667 },
        { 330, 100, 300, 118.333333333 },
        { 400, 100, 370, 130 },
        { 400, 140, 386, 162 },
        { 330, 140, 315.066666667, 155.466666667 },
        { 260, 140, 244.133333333, 148.933333333 },
        { 210, 140, 193.466666667, 144.266666667 },
        { 150, 140, 132.666666667, 138.666666667 },
        { 100, 140, 82, 134 },
        { 100, 190, 97, 201.5 },
        { 150, 190, 148.5, 201.583333333 },
        { 210, 190, 210.3, 201.683333333 },
        { 260, 190, 261.8, 201.766666667 },
        { 330, 190, 333.9, 201.883333333 },
        { 400, 190, 406, 202 },
        { 400, 250, 430, 250 },
        { 330, 250, 356.5, 257.583333333 },
        { 260, 250, 283, 265.166666667 },
        { 210, 250, 230.5, 270.583333333 },
        { 150, 250, 167.5, 277.083333333 },
        { 100, 250, 115, 282.5 },
        { 100, 300, 130, 350 },
        { 150, 300, 183.333333333, 340 },
        { 210, 300, 247.333333333, 328 },
        { 260, 300, 300.666666667, 318 },
        { 330, 300, 375.333333333, 304 },
        { 400, 300, 450, 290 },
    };

    // A five-point curved path, anchors and both direction handles of each.
    // The source rectangle (120..380 by 110..280) is deliberately NOT the
    // art's bounds (98.06..406.17 by 94.02..300), which is what shows that the
    // destination is renormalized onto the input bounds.
    const Sample kCurveSamples[] = {
        { 100, 200, 77.706466117, 225.941797467 },
        { 90, 150, 48.6633809103, 145.942926378 },
        { 110, 250, 107.122999194, 303.840568681 },
        { 220, 300, 259.989903218, 356.043957881 },
        { 150, 300, 173.836057064, 372.56065538 },
        { 290, 300, 346.143749372, 339.527260382 },
        { 400, 260, 463.21053855, 275.407048923 },
        { 380, 300, 456.912980141, 318.291506455 },
        { 420, 220, 468.910580367, 235.882751189 },
        { 330, 100, 309.013657754, 109.860743769 },
        { 390, 110, 382.920548921, 130.656133237 },
        { 270, 90, 235.55490403, 86.5452344513 },
        { 160, 120, 119.846479437, 107.732507636 },
        { 230, 90, 189.461096909, 78.3425083651 },
        { 120, 140, 80.3721399023, 132.893161245 },
    };

    Quad Q(double a, double b, double c, double d, double e, double f, double g, double h)
    {
        Quad q;
        q.c[0] = Make(a, b);
        q.c[1] = Make(c, d);
        q.c[2] = Make(e, f);
        q.c[3] = Make(g, h);
        return q;
    }

    template <size_t N>
    void CheckSamples(const char* name, const Sample samples[N], const Rect& source,
                      const Quad& destination, const Rect& inputBounds)
    {
        const Quad effective = EffectiveQuad(source, destination, inputBounds);
        double worst = 0.0;
        for (size_t i = 0; i < N; ++i)
        {
            const Pt got = MapPoint(effective, inputBounds, Make(samples[i].sh, samples[i].sv));
            worst = std::fmax(worst, Distance(got, Make(samples[i].rh, samples[i].rv)));
        }
        char detail[160];
        std::snprintf(detail, sizeof(detail), "%zu points, worst deviation %.3g pt", N, worst);
        // The host printed its coordinates to 12 significant digits.
        Check(worst < 1e-7, std::string("the model reproduces the host: ") + name, detail);
        std::printf("      %s: %s\n", name, detail);
    }

    Quad RandomQuad(std::mt19937& rng, double spread)
    {
        std::uniform_real_distribution<double> d(-spread, spread);
        return Q(0 + d(rng), 100 + d(rng), 100 + d(rng), 100 + d(rng),
                 0 + d(rng), 0 + d(rng), 100 + d(rng), 0 + d(rng));
    }
}

int main()
{
    std::printf("FreeDistort+ -- quad arithmetic\n\n");

    // ---- the renderer model against the host -----------------------------

    CheckSamples<sizeof(kGridSamples) / sizeof(kGridSamples[0])>(
        "grid, source = bounds", kGridSamples,
        MakeRect(100, 300, 400, 100),
        Q(130, 350, 450, 290, 70, 80, 370, 130),
        MakeRect(100, 300, 400, 100));

    CheckSamples<sizeof(kCurveSamples) / sizeof(kCurveSamples[0])>(
        "curves and handles, source != bounds", kCurveSamples,
        MakeRect(120, 280, 380, 110),
        Q(130, 350, 450, 290, 70, 80, 370, 130),
        MakeRect(98.0618520421503, 300, 406.170447970364, 94.0192378864667));

    // The same curve data read as though the source rectangle were the frame
    // must NOT fit: otherwise the test above would not be showing anything.
    {
        const Rect source = MakeRect(120, 280, 380, 110);
        const Quad dst = Q(130, 350, 450, 290, 70, 80, 370, 130);
        double worst = 0.0;
        for (const Sample& s : kCurveSamples)
        {
            const Pt got = MapPoint(dst, source, Make(s.sh, s.sv));
            worst = std::fmax(worst, Distance(got, Make(s.rh, s.rv)));
        }
        Check(worst > 1.0, "control: ignoring the input bounds does not fit the host");
    }

    // ---- sources that are not rectangles -----------------------------------
    //
    // Destinations Adobe's own edit path committed from a non-rectangular
    // source, verbatim from docs/evidence/source-quads.tsv. Adobe writes the
    // source as the input bounds and the destination as the quad it draws
    // into, without changing the drawing.

    {
        struct HostCommit
        {
            const char* name;
            Quad source, destination;
            Rect bounds;
            Quad committed;
        };
        const Rect grid = MakeRect(100, 300, 400, 100);
        const Quad general = Q(130, 350, 450, 290, 70, 80, 370, 130);
        const Quad identity = RectQuad(grid);
        const HostCommit commits[] = {
            { "convex source", Q(90, 320, 420, 280, 130, 90, 380, 120), identity, grid,
              Q(105.82345191, 283.636363636, 384.87483531, 314.466403162, 73.1488801054, 107.878787879, 415.441370224, 83.372859025) },
            { "trapezoid source, distorted destination", Q(160, 300, 340, 300, 100, 100, 400, 100), general, grid,
              Q(30, 350, 576.666666667, 290, 82, 80, 382, 130) },
            { "concave source", Q(100, 300, 400, 300, 100, 100, 250, 220), identity, grid,
              Q(100, 300, 400, 300, 100, 100, 550, -20) },
            { "bow-tie source", Q(400, 300, 100, 300, 100, 100, 400, 100), identity, grid,
              Q(400, 300, 100, 300, 100, 100, 400, 100) },
            { "convex source 1000 pt away", Q(1090, 1320, 1420, 1280, 1130, 1090, 1380, 1120), general, grid,
              Q(-1922.92490119, 198.972332016, -1355.75757576, -65.2700922266, -2376.04743083, 264.584980237, -1664.58498024, -31.8577075099) },
            { "convex offsets over other input bounds", Q(240, 290, 720, 250, 280, 140, 680, 170), Q(280, 320, 750, 260, 220, 130, 670, 180),
              MakeRect(250, 270, 700, 150),
              Q(294.166666667, 298.5, 737.125, 273.625, 185.027777778, 143.444444444, 676.347222222, 171.847222222) },
        };
        for (const HostCommit& c : commits)
        {
            const Quad got = EffectiveQuad(c.source, c.destination, c.bounds);
            double worst = 0.0;
            for (int i = 0; i < 4; ++i) worst = std::fmax(worst, Distance(got.c[i], c.committed.c[i]));
            char detail[120];
            std::snprintf(detail, sizeof(detail), "worst deviation %.3g pt", worst);
            Check(worst < 1e-7, std::string("a non-rectangular source is read as the host commits it: ") + c.name, detail);
            std::printf("      %s: %s\n", c.name, detail);
        }

        // Control: the source's bounding box, the obvious reading, misses.
        const Rect box = BoundingRect(commits[0].source);
        const Quad byBox = EffectiveQuad(box, commits[0].destination, commits[0].bounds);
        Check(!Near(byBox, commits[0].committed, 1.0), "control: the source's bounding box does not fit the host");

        // For a rectangular source, both readings are the same function.
        std::mt19937 rng(11);
        std::uniform_real_distribution<double> d(-150.0, 150.0);
        bool same = true;
        for (int i = 0; i < 500; ++i)
        {
            const Rect s = MakeRect(d(rng), 200 + d(rng), 400 + d(rng), d(rng) - 200);
            const Rect b = MakeRect(d(rng), 200 + d(rng), 400 + d(rng), d(rng) - 200);
            const Quad dst = RandomQuad(rng, 150.0);
            same = same && Near(EffectiveQuad(RectQuad(s), dst, b), EffectiveQuad(s, dst, b), 1e-9);
        }
        Check(same, "for a rectangular source the general reading is the renormalization");
        Check(HasUsableFrame(Q(0, 10, 20, 30, 40, 0, 50, 60), 1e-9) && !HasUsableFrame(Q(5, 10, 5, 30, 40, 0, 50, 60), 1e-9) &&
              !HasUsableFrame(Q(0, 10, 20, 30, 40, 10, 50, 60), 1e-9),
              "a frame with no width or no height is not usable");
    }

    // ---- renormalization ---------------------------------------------------

    {
        const Rect a = MakeRect(90, 330, 340, 100);
        const Rect b = MakeRect(77.5, 407.5, 452.5, 62.5);
        // Measured: a dictionary with dst1h = 400 over source 90..340, once the
        // art was scaled 150% about its center and Adobe's own editor
        // committed it, read dst1h = 542.5 over source 77.5..452.5.
        const Pt moved = Renormalize(Make(400, 330), a, b);
        Check(std::fabs(moved.h - 542.5) < 1e-9 && std::fabs(moved.v - 407.5) < 1e-9,
              "renormalization matches Adobe's own re-commit after a 150% scale");
        const Quad q = Q(1, 2, 3, 4, 5, 6, 7, 8);
        Check(Near(Renormalize(Renormalize(q, a, b), b, a), q, 1e-9), "renormalization round-trips");
    }

    // ---- inverse bilinear --------------------------------------------------

    {
        std::mt19937 rng(20260914);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        int solved = 0, trials = 0;
        double worst = 0.0;
        for (int i = 0; i < 2000; ++i)
        {
            const Quad q = RandomQuad(rng, 20.0);   // mild distortion stays convex
            const double u = unit(rng), t = unit(rng);
            const Pt p = Bilinear(q, u, t);
            double ru = 0, rt = 0;
            ++trials;
            if (InverseBilinear(q, p, &ru, &rt))
            {
                ++solved;
                worst = std::fmax(worst, std::fabs(ru - u) + std::fabs(rt - t));
            }
        }
        Check(solved == trials, "inverse bilinear solves every mildly distorted quad");
        Check(worst < 1e-9, "inverse bilinear recovers (u, t)");
    }

    // ---- editing modes -----------------------------------------------------

    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> d(-80.0, 80.0);
        bool driftFree = true, affineParallel = true, affineHitsPointer = true;
        bool perspectiveKeepsMid = true, symmetricKeepsCentroid = true, freeTouchesOne = true;
        for (int i = 0; i < 500; ++i)
        {
            const Quad start = RandomQuad(rng, 30.0);
            const int corner = i % 4;
            const Pt to = Add(start.c[corner], Make(d(rng), d(rng)));

            // Returning to the start point restores the start exactly, in every mode.
            driftFree = driftFree &&
                Near(MoveCorner(start, corner, start.c[corner]), start, 0.0) &&
                Near(MoveCornerSymmetric(start, corner, start.c[corner]), start, 0.0) &&
                Near(MoveCornerPerspective(start, corner, start.c[corner]), start, 0.0);

            const Quad free = MoveCorner(start, corner, to);
            for (int k = 0; k < 4; ++k)
                if (k != corner && Distance(free.c[k], start.c[k]) != 0.0) freeTouchesOne = false;

            const Quad sym = MoveCornerSymmetric(start, corner, to);
            symmetricKeepsCentroid = symmetricKeepsCentroid &&
                Distance(Centroid(sym), Centroid(start)) < 1e-9;

            const Quad persp = MoveCornerPerspective(start, corner, to);
            const Pt delta = Sub(to, start.c[corner]);
            const int partner = std::fabs(delta.h) >= std::fabs(delta.v)
                ? HorizontalNeighbor(corner) : VerticalNeighbor(corner);
            const Pt midStart = Scale(Add(start.c[corner], start.c[partner]), 0.5);
            const Pt midNow = Scale(Add(persp.c[corner], persp.c[partner]), 0.5);
            perspectiveKeepsMid = perspectiveKeepsMid && Distance(midStart, midNow) < 1e-9;

            const Quad rect = RectQuad(MakeRect(0, 100, 150, 0));
            const Quad aff = MoveCornerAffine(rect, corner, Add(rect.c[corner], Make(d(rng), d(rng))));
            affineParallel = affineParallel && IsParallelogram(aff, 1e-9);
            const Pt target = Add(start.c[corner], Make(d(rng) * 0.2, d(rng) * 0.2));
            const Quad aff2 = MoveCornerAffine(start, corner, target);
            affineHitsPointer = affineHitsPointer && Distance(aff2.c[corner], target) < 1e-9;
        }
        Check(driftFree, "free, symmetric and perspective edits return exactly to the start");
        Check(freeTouchesOne, "free distortion moves only the dragged corner");
        Check(symmetricKeepsCentroid, "symmetric distortion keeps the centroid");
        Check(perspectiveKeepsMid, "perspective keeps the midpoint of the edge it changes");
        Check(affineParallel, "affine distortion of a parallelogram stays a parallelogram");
        Check(affineHitsPointer, "affine distortion puts the dragged corner under the pointer");
    }

    // ---- shape predicates --------------------------------------------------

    Check(IsAxisAlignedRect(RectQuad(MakeRect(0, 10, 20, 0)), 0.0), "a rectangle is a rectangle");
    Check(!IsAxisAlignedRect(Q(0, 10, 20, 10, 0, 0, 21, 0), 0.5), "a skewed quad is not a rectangle");
    Check(!IsAxisAlignedRect(Q(20, 10, 0, 10, 20, 0, 0, 0), 0.0), "a mirrored rectangle is not in Adobe's order");
    Check(!IsSelfIntersecting(RectQuad(MakeRect(0, 10, 20, 0))), "a rectangle does not cross itself");
    Check(IsSelfIntersecting(Q(0, 10, 20, 10, 20, 0, 0, 0)), "a bow-tie crosses itself");
    Check(IsParallelogram(Q(0, 10, 20, 10, 5, 0, 25, 0), 1e-12), "a sheared rectangle is a parallelogram");
    Check(!IsParallelogram(Q(0, 10, 20, 10, 5, 0, 20, 0), 1e-6), "a trapezoid is not a parallelogram");
    Check(HorizontalNeighbor(kTopLeft) == kTopRight && VerticalNeighbor(kTopLeft) == kBottomLeft &&
          Opposite(kTopLeft) == kBottomRight && Opposite(kTopRight) == kBottomLeft,
          "corner neighbors follow Adobe's numbering");

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
