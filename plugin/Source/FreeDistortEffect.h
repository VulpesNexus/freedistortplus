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

//  FreeDistortEffect.h -- reading and writing Adobe's own Free Distort.
//
//  The document state this plugin edits is exactly and only the parameter
//  dictionary of Adobe's "Adobe Free Distort" live effect: sixteen reals,
//  src0h..src3v and dst0h..dst3v. Nothing is added to it. Every write replaces
//  those sixteen values in a fresh copy of the dictionary, carries every other
//  entry across untouched, and puts the rebuilt style back on the art, which
//  is what makes Adobe's effect run again.

#ifndef __FREEDISTORTEFFECT_H__
#define __FREEDISTORTEFFECT_H__

#include "IllustratorSDK.h"
#include "QuadMath.h"

#include <string>
#include <vector>

namespace fd
{
    /** One Free Distort as stored. */
    struct State
    {
        /** All eight src keys were present and real. */
        bool hasSource = false;
        /** All eight dst keys were present and real. */
        bool hasDestination = false;
        fdmath::Quad source;
        fdmath::Quad destination;
        ai::uint32 entryCount = 0;
    };

    /** Post-effect indices of every Adobe Free Distort in the art's
        appearance, in stack order. */
    ASErr FindAll(AIArtHandle art, std::vector<ai::int32>* postIndices);

    /** Reads the Free Distort at `postIndex`. Fails with kBadParameterErr if
        the post-effect there is not Adobe Free Distort. */
    ASErr Read(AIArtHandle art, ai::int32 postIndex, State* state);

    /** Writes a source rectangle and destination quad. `report`, when given,
        receives a line describing what was done. */
    ASErr Write(AIArtHandle art, ai::int32 postIndex, const fdmath::Rect& source,
                const fdmath::Quad& destination, std::string* report = nullptr);

    /** How MeasureInputBounds arrived at its answer. */
    enum class BoundsSource
    {
        /** Adobe's own edit path chose the source rectangle. */
        kAdobe,
        /** Adobe's edit path could not be used, and the art's geometric
            bounds were taken instead. Right whenever nothing ahead of the
            Free Distort in the appearance changes geometry. */
        kGeometric
    };

    /** The geometric bounds of the art this Free Distort receives as input:
        the rectangle the renderer lays the destination quad onto.

        Asked of Adobe rather than computed. With user interaction switched
        off, asking the effect to edit its parameters does not show its dialog;
        it commits the dictionary its dialog would commit on an untouched OK,
        whose source rectangle is the input bounds by construction -- whatever
        sits ahead of the effect, whatever kind of art it is. That commit is
        render-invariant, and it is undone again here before returning, so the
        document is left as it was found. `report` says what happened. */
    ASErr MeasureInputBounds(AIArtHandle art, ai::int32 postIndex, fdmath::Rect* bounds,
                             BoundsSource* how, std::string* report = nullptr);

    /** Appends an identity Free Distort (destination equal to source) to the
        art's appearance, the same entry Effect > Distort & Transform > Free
        Distort would add on an untouched OK. Returns its post-effect index. */
    ASErr AppendIdentity(AIArtHandle art, ai::int32* postIndex);

    /** Geometric bounds of an art object: the Bezier outline, without strokes
        or the glyph extents of area text. */
    ASErr GeometricBounds(AIArtHandle art, fdmath::Rect* bounds);

    std::string Describe(const State& state);
    std::string Describe(const fdmath::Rect& rect);
    std::string Describe(const fdmath::Quad& quad);
}

#endif // __FREEDISTORTEFFECT_H__
