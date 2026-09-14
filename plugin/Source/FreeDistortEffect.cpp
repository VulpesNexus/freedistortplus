// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vixen420
//
// FreeDistort+ is free software: you may redistribute it and/or
// modify it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. It comes with ABSOLUTELY NO WARRANTY. See the
// file LICENSE, or <https://www.gnu.org/licenses/>, for the full text.
//
// Additional permission under GPL-3.0 section 7: this file may be combined with
// the Adobe Illustrator SDK, whose sample framework sources are compiled into
// every plugin built from it. See LICENSE-EXCEPTION.

//  FreeDistortEffect.cpp -- see FreeDistortEffect.h.

#include "IllustratorSDK.h"
#include "FreeDistortEffect.h"
#include "FDPSuites.h"
#include "FDPID.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace
{
    /** Owns a style parser for the length of a scope. */
    class ScopedParser
    {
    public:
        ScopedParser() { if (sAIArtStyleParser->NewParser(&fParser)) fParser = nullptr; }
        ~ScopedParser() { if (fParser) sAIArtStyleParser->DisposeParser(fParser); }
        ScopedParser(const ScopedParser&) = delete;
        ScopedParser& operator=(const ScopedParser&) = delete;
        AIStyleParser get() const { return fParser; }
        explicit operator bool() const { return fParser != nullptr; }
    private:
        AIStyleParser fParser = nullptr;
    };

    const char* const kSourceKeys[8] = {
        "src0h", "src0v", "src1h", "src1v", "src2h", "src2v", "src3h", "src3v"
    };
    const char* const kDestinationKeys[8] = {
        "dst0h", "dst0v", "dst1h", "dst1v", "dst2h", "dst2v", "dst3h", "dst3v"
    };

    std::string Num(double v)
    {
        std::ostringstream o;
        o << std::setprecision(12) << v;
        return o.str();
    }

    bool IsFreeDistort(AIParserLiveEffect effect)
    {
        const char* name = nullptr;
        ai::int32 major = 0, minor = 0;
        if (sAIArtStyleParser->GetLiveEffectNameAndVersion(effect, &name, &major, &minor) || name == nullptr)
            return false;
        return std::strcmp(name, kAdobeFreeDistortName) == 0;
    }

    /** Parses the art's style and finds the Free Distort at `postIndex`. */
    ASErr Locate(AIArtHandle art, ai::int32 postIndex, const ScopedParser& parser,
                 AIArtStyleHandle* style, AIParserLiveEffect* effect)
    {
        *style = nullptr;
        *effect = nullptr;
        if (!parser) return kCantHappenErr;
        ASErr err = sAIArtStyle->GetArtStyle(art, style);
        if (err) return err;
        if (*style == nullptr) return kBadParameterErr;
        err = sAIArtStyleParser->ParseStyle(parser.get(), *style);
        if (err) return err;
        if (postIndex < 0 || postIndex >= sAIArtStyleParser->CountPostEffects(parser.get()))
            return kBadParameterErr;
        err = sAIArtStyleParser->GetNthPostEffect(parser.get(), postIndex, effect);
        if (err) return err;
        if (*effect == nullptr || !IsFreeDistort(*effect)) return kBadParameterErr;
        return kNoErr;
    }

    bool ReadEight(ConstAIDictionaryRef dict, const char* const keys[8], fdmath::Quad* quad)
    {
        for (int i = 0; i < 8; ++i)
        {
            const AIDictKey key = sAIDictionary->Key(keys[i]);
            AIEntryType type = UnknownType;
            if (sAIDictionary->GetEntryType(dict, key, &type) || type != RealType) return false;
        }
        for (int corner = 0; corner < 4; ++corner)
        {
            AIReal h = 0, v = 0;
            sAIDictionary->GetRealEntry(dict, sAIDictionary->Key(keys[corner * 2]), &h);
            sAIDictionary->GetRealEntry(dict, sAIDictionary->Key(keys[corner * 2 + 1]), &v);
            quad->c[corner] = fdmath::Make(h, v);
        }
        return true;
    }

    ASErr WriteEight(AIDictionaryRef dict, const char* const keys[8], const fdmath::Quad& quad)
    {
        for (int corner = 0; corner < 4; ++corner)
        {
            ASErr err = sAIDictionary->SetRealEntry(dict, sAIDictionary->Key(keys[corner * 2]),
                                                    static_cast<AIReal>(quad.c[corner].h));
            if (err) return err;
            err = sAIDictionary->SetRealEntry(dict, sAIDictionary->Key(keys[corner * 2 + 1]),
                                              static_cast<AIReal>(quad.c[corner].v));
            if (err) return err;
        }
        return kNoErr;
    }
}

namespace fd
{

ASErr FindAll(AIArtHandle art, std::vector<ai::int32>* postIndices)
{
    postIndices->clear();
    AIArtStyleHandle style = nullptr;
    ASErr err = sAIArtStyle->GetArtStyle(art, &style);
    if (err) return err;
    if (style == nullptr) return kNoErr;

    ScopedParser parser;
    if (!parser) return kCantHappenErr;
    err = sAIArtStyleParser->ParseStyle(parser.get(), style);
    if (err) return err;

    const ai::int32 n = sAIArtStyleParser->CountPostEffects(parser.get());
    for (ai::int32 i = 0; i < n; ++i)
    {
        AIParserLiveEffect effect = nullptr;
        if (!sAIArtStyleParser->GetNthPostEffect(parser.get(), i, &effect) && effect && IsFreeDistort(effect))
            postIndices->push_back(i);
    }
    return kNoErr;
}

ASErr Read(AIArtHandle art, ai::int32 postIndex, State* state)
{
    *state = State();
    ScopedParser parser;
    AIArtStyleHandle style = nullptr;
    AIParserLiveEffect effect = nullptr;
    ASErr err = Locate(art, postIndex, parser, &style, &effect);
    if (err) return err;

    AILiveEffectParameters params = nullptr;
    err = sAIArtStyleParser->GetLiveEffectParams(effect, &params);
    if (err) return err;
    if (params == nullptr) return kNoErr;   // an empty effect: the identity

    state->entryCount = sAIDictionary->Size(params);
    state->hasSource = ReadEight(params, kSourceKeys, &state->source);
    state->hasDestination = ReadEight(params, kDestinationKeys, &state->destination);
    return kNoErr;
}

ASErr Write(AIArtHandle art, ai::int32 postIndex, const fdmath::Rect& source,
            const fdmath::Quad& destination, std::string* report)
{
    ScopedParser parser;
    AIArtStyleHandle style = nullptr;
    AIParserLiveEffect effect = nullptr;
    ASErr err = Locate(art, postIndex, parser, &style, &effect);
    if (err) return err;

    AILiveEffectParameters current = nullptr;
    err = sAIArtStyleParser->GetLiveEffectParams(effect, &current);
    if (err) return err;

    // A fresh dictionary, never the one the parser handed back: that one
    // belongs to the style, and a style is shared by every object wearing it
    // -- an object and its duplicate, for a start -- so writing into it in
    // place would move all of them.
    AILiveEffectParameters fresh = nullptr;
    err = sAILiveEffect->CreateLiveEffectParameters(&fresh);
    if (err) return err;
    if (current != nullptr) err = sAIDictionary->Copy(fresh, current);
    if (!err) err = WriteEight(fresh, kSourceKeys, fdmath::RectQuad(source));
    if (!err) err = WriteEight(fresh, kDestinationKeys, destination);
    if (err)
    {
        sAIDictionary->Release(fresh);
        return err;
    }

    err = sAIArtStyleParser->SetLiveEffectParams(effect, fresh);
    // Whether the parser keeps its own reference is not documented. Ask the
    // dictionary: AddRef returns the new count. With the parser holding one,
    // ours is released; without, it is left for the style to own, which at
    // worst leaks one small dictionary rather than freeing one in use.
    const ai::int32 countWithProbe = sAIDictionary->AddRef(fresh);
    sAIDictionary->Release(fresh);
    const bool parserHoldsReference = countWithProbe >= 3;

    AIArtStyleHandle newStyle = nullptr;
    if (!err) err = sAIArtStyleParser->CreateNewStyle(parser.get(), &newStyle);
    if (!err && newStyle) err = sAIArtStyle->SetArtStyle(art, newStyle);
    if (parserHoldsReference) sAIDictionary->Release(fresh);

    if (report)
    {
        std::ostringstream o;
        o << "write post-effect " << postIndex << ": source " << Describe(source)
          << " destination " << Describe(destination)
          << " (dictionary refs with probe " << countWithProbe
          << (parserHoldsReference ? ", released ours" : ", kept ours")
          << ", style " << (newStyle == style ? "unchanged" : "replaced")
          << ", result " << err << ")\n";
        *report += o.str();
    }
    return err;
}

ASErr GeometricBounds(AIArtHandle art, fdmath::Rect* bounds)
{
    // kControlBounds cannot be combined with the no-stroke flags at all, and
    // kNoExtendedBounds does not imply kNoStrokeBounds whatever the header
    // says; these three together are what the DOM calls geometricBounds.
    AIRealRect r = { 0, 0, 0, 0 };
    const ASErr err = sAIArt->GetArtTransformBounds(art, nullptr,
        kVisibleBounds | kNoStrokeBounds | kNoExtendedBounds, &r);
    if (err) return err;
    *bounds = fdmath::MakeRect(r.left, r.top, r.right, r.bottom);
    return kNoErr;
}

namespace
{
    /** One question to Adobe's edit path; see MeasureInputBounds. Returns
        true when it answered with a rectangle, which is left in `bounds`,
        with the destination it committed in `drawn` when it wrote one, and
        always leaves the art's style as it found it. */
    bool AskAdobe(AIArtHandle art, ai::int32 postIndex, fdmath::Rect* bounds,
                  fdmath::Quad* drawn, bool* haveDrawn, std::ostringstream& o)
    {
    *haveDrawn = false;
    AIArtStyleHandle originalStyle = nullptr;
    sAIArtStyle->GetArtStyle(art, &originalStyle);

    ASErr editErr = kCantHappenErr;
    if (sASUserInteraction != nullptr)
    {
        ScopedParser parser;
        AIArtStyleHandle style = nullptr;
        AIParserLiveEffect effect = nullptr;
        editErr = Locate(art, postIndex, parser, &style, &effect);
        if (!editErr)
        {
            const ASInteractionAllowed previous = sASUserInteraction->GetInteractionAllowed();
            sASUserInteraction->SetInteractionAllowed(kASInteractWithNone);
            editErr = sAIArtStyleParser->EditEffectParameters(style, effect);
            sASUserInteraction->SetInteractionAllowed(previous);
        }
        o << "silent edit returned " << editErr << "; ";
    }
    else
    {
        o << "no user interaction suite; ";
    }

    State committed;
    bool adobeAnswered = false;
    if (!editErr && !fd::Read(art, postIndex, &committed) && committed.hasSource &&
        fdmath::IsAxisAlignedRect(committed.source, 1e-6))
    {
        *bounds = fdmath::BoundingRect(committed.source);
        adobeAnswered = true;
        o << "Adobe chose source " << fd::Describe(*bounds) << "; ";
        if (committed.hasDestination)
        {
            *drawn = committed.destination;
            *haveDrawn = true;
        }
    }

    // Put the document back. UndoChanges discards what this context changed,
    // which is exactly the silent commit when nothing else was written first.
    AIArtStyleHandle afterStyle = nullptr;
    sAIArtStyle->GetArtStyle(art, &afterStyle);
    if (afterStyle != originalStyle)
    {
        const ASErr undoErr = sAIUndo->UndoChanges();
        AIArtStyleHandle restored = nullptr;
        sAIArtStyle->GetArtStyle(art, &restored);
        o << "UndoChanges returned " << undoErr << " and "
          << (restored == originalStyle ? "restored the original style" : "did not restore it");
        if (restored != originalStyle && originalStyle != nullptr)
        {
            const ASErr setErr = sAIArtStyle->SetArtStyle(art, originalStyle);
            o << ", so the style was set back directly (result " << setErr << ")";
        }
        o << "; ";
    }
    else
    {
        o << "style unchanged; ";
    }
    return adobeAnswered;
    }

    /** The box Adobe's edit path reports when its effect has not rendered
        the art it is asked about: 100 pt square at the origin. Seen for
        embedded images, which the effect never processes, and now and then
        for art whose Free Distort was added a moment before. */
    bool IsUnrenderedAnswer(const fdmath::Rect& r)
    {
        return r.left == 0.0 && r.top == 100.0 && r.right == 100.0 && r.bottom == 0.0;
    }
}

ASErr MeasureInputBounds(AIArtHandle art, ai::int32 postIndex, fdmath::Rect* bounds,
                         BoundsSource* how, std::string* report,
                         fdmath::Quad* drawnQuad, bool* haveDrawnQuad)
{
    std::ostringstream o;
    State before;
    ASErr err = Read(art, postIndex, &before);
    if (err) return err;
    if (haveDrawnQuad) *haveDrawnQuad = false;

    fdmath::Rect geometry;
    const bool haveGeometry = GeometricBounds(art, &geometry) == kNoErr;

    // Adobe's edit path answers from what its effect last rendered, so make
    // sure the effect has run on this art before asking.
    AIArtHandle styled = nullptr;
    sAIArtStyle->GetStyledArt(art, &styled);

    fdmath::Quad drawn;
    bool haveDrawn = false;
    bool adobeAnswered = AskAdobe(art, postIndex, bounds, &drawn, &haveDrawn, o);
    if (adobeAnswered && IsUnrenderedAnswer(*bounds) && !(haveGeometry && IsUnrenderedAnswer(geometry)))
    {
        o << "that is the box Adobe reports before its effect has rendered; redrawing and asking again; ";
        sAIDocument->RedrawDocument();
        adobeAnswered = AskAdobe(art, postIndex, bounds, &drawn, &haveDrawn, o);
        if (adobeAnswered && IsUnrenderedAnswer(*bounds))
        {
            o << "the same box again; ";
            adobeAnswered = false;
        }
    }

    if (adobeAnswered)
    {
        *how = BoundsSource::kAdobe;
        if (haveDrawn && drawnQuad && haveDrawnQuad)
        {
            *drawnQuad = drawn;
            *haveDrawnQuad = true;
        }
    }
    else
    {
        err = GeometricBounds(art, bounds);
        if (err) return err;
        *how = BoundsSource::kGeometric;
        o << "fell back to geometric bounds " << Describe(*bounds) << "; ";
    }

    if (report) *report += o.str() + "\n";
    return kNoErr;
}

ASErr AppendIdentity(AIArtHandle art, ai::int32* postIndex)
{
    AILiveEffectHandle effect = nullptr;
    ASErr err = sAILiveEffect->GetLiveEffectHandleByName(kAdobeFreeDistortName, &effect);
    if (err) return err;
    if (effect == nullptr) return kBadParameterErr;

    AIArtStyleHandle style = nullptr;
    err = sAIArtStyle->GetArtStyle(art, &style);
    if (err) return err;
    if (style == nullptr)
    {
        // Groups and other containers often have no style at all until one is
        // given to them.
        ScopedParser empty;
        if (!empty) return kCantHappenErr;
        err = sAIArtStyleParser->CreateNewStyle(empty.get(), &style);
        if (err) return err;
    }

    // An empty dictionary renders as the identity, which is also what the
    // effect holds between being added from the menu and its dialog closing.
    AILiveEffectParameters params = nullptr;
    err = sAILiveEffect->CreateLiveEffectParameters(&params);
    if (err) return err;
    AIArtStyleHandle merged = nullptr;
    err = sAILiveEffect->NewArtStyleByMergingLiveEffect(style, effect, params, kAppendLiveEffectToStyle, &merged);
    sAIDictionary->Release(params);
    if (err) return err;
    err = sAIArtStyle->SetArtStyle(art, merged);
    if (err) return err;

    std::vector<ai::int32> all;
    err = FindAll(art, &all);
    if (err) return err;
    if (all.empty()) return kCantHappenErr;
    *postIndex = all.back();
    return kNoErr;
}

std::string Describe(const fdmath::Rect& r)
{
    return "[" + Num(r.left) + " " + Num(r.top) + " " + Num(r.right) + " " + Num(r.bottom) + "]";
}

std::string Describe(const fdmath::Quad& q)
{
    std::string s = "(";
    for (int i = 0; i < 4; ++i)
        s += (i ? " " : "") + Num(q.c[i].h) + "," + Num(q.c[i].v);
    return s + ")";
}

std::string Describe(const State& s)
{
    std::string out = "entries " + std::to_string(s.entryCount);
    out += s.hasSource ? " src " + Describe(s.source) : " src (absent)";
    out += s.hasDestination ? " dst " + Describe(s.destination) : " dst (absent)";
    return out;
}

} // namespace fd
