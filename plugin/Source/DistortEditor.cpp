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

//  DistortEditor.cpp -- see DistortEditor.h.

#include "IllustratorSDK.h"
#include "DistortEditor.h"
#include "FDPSuites.h"
#include "FDPID.h"
#include "Introspect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

#ifdef WIN_ENV
#include <windows.h>
#endif

namespace
{
    /** Half the side of a corner handle, in view pixels. */
    constexpr int kHandleRadius = 4;
    /** How close the pointer has to be to a handle to take it, in view pixels. */
    constexpr double kHitRadius = 7.0;

    AIRealPoint ToAI(fdmath::Pt p)
    {
        AIRealPoint r;
        r.h = static_cast<AIReal>(p.h);
        r.v = static_cast<AIReal>(p.v);
        return r;
    }

    fdmath::Pt FromAI(const AIRealPoint& p) { return fdmath::Make(p.h, p.v); }

    /** Artwork to view coordinates, in the view given, or in the current view when
        none is. Drawing passes the view it was asked to draw into: the
        current view can belong to another document, or to one being closed. */
    bool ToView(fdmath::Pt artwork, AIPoint* out, AIDocumentViewHandle view = nullptr)
    {
        const AIRealPoint a = ToAI(artwork);
        return sAIDocumentView->ArtworkPointToViewPoint(view, &a, out) == kNoErr;
    }

    AIRGBColor Rgb(int r, int g, int b)
    {
        AIRGBColor c;
        c.red = static_cast<ai::uint16>(r * 257);
        c.green = static_cast<ai::uint16>(g * 257);
        c.blue = static_cast<ai::uint16>(b * 257);
        return c;
    }

    std::string Num(double v)
    {
        std::ostringstream o;
        o.precision(12);
        o << v;
        return o.str();
    }

    std::string ModeName(DistortEditor::Mode mode)
    {
        switch (mode)
        {
            case DistortEditor::Mode::kPerspective: return "perspective";
            case DistortEditor::Mode::kSymmetric:   return "symmetric";
            case DistortEditor::Mode::kAffine:      return "affine";
            default:                                return "free";
        }
    }
}

ASErr DistortEditor::Startup(SPPluginRef self)
{
    AIAddToolData data;
    data.title = ai::UnicodeString(kFDPToolTitle);
    data.tooltip = ai::UnicodeString(kFDPToolTooltip);
    data.normalIconResID = kFDPToolIconResID;
    data.darkIconResID = kFDPToolIconDarkResID;
    data.iconType = ai::IconType::kSVG;
    data.sameGroupAs = kNoTool;
    data.sameToolsetAs = kNoTool;

    // Illustrator suspends live effects while a tool drags, by default, so
    // the artwork would not change until the mouse came up. The preview is
    // the whole point of dragging on the canvas, so this tool opts out.
    const ai::int32 options = kToolWantsToTrackCursorOption | kToolDoesntWantArtStyleExecutionSuspender;
    ASErr err = sAITool->AddTool(self, kFDPToolName, data, options, &fTool);
    if (err) return err;

    err = sAIAnnotator->AddAnnotator(self, kFDPAnnotatorName, &fAnnotator);
    if (err) return err;
    sAIAnnotator->SetAnnotatorActive(fAnnotator, false);

    sAINotifier->AddNotifier(self, kFDPNotifierName, kAIArtSelectionChangedNotifier, &fSelectionChanged);
    sAINotifier->AddNotifier(self, kFDPNotifierName, kAIArtPropertiesChangedNotifier, &fPropertiesChanged);
    sAINotifier->AddNotifier(self, kFDPNotifierName, kAIDocumentChangedNotifier, &fDocumentChanged);
    sAINotifier->AddNotifier(self, kFDPNotifierName, kAIDocumentAboutToCloseNotifier, &fDocumentClosing);
    sAINotifier->AddNotifier(self, kFDPNotifierName, kAIActiveDocumentWindowAboutToBeChangedNotifier, &fWindowChanging);
    return kNoErr;
}

bool DistortEditor::OwnsNotifier(AINotifierHandle notifier) const
{
    return notifier != nullptr &&
           (notifier == fSelectionChanged || notifier == fPropertiesChanged || notifier == fDocumentChanged ||
            notifier == fDocumentClosing || notifier == fWindowChanging);
}

ASErr DistortEditor::Activate()
{
    // Selecting a tool that is already selected sends no select message, and
    // the select message is where the target is found and its input bounds
    // measured -- in that message's own undo context, which matters: measuring
    // in the caller's context would undo whatever the caller just did, such as
    // adding the Free Distort. So an already selected editor is deselected
    // first, by way of Illustrator's Selection tool.
    AIToolHandle current = nullptr;
    if (!sAITool->GetSelectedTool(&current) && current == fTool)
        sAITool->SetSelectedToolByName("Adobe Select Tool");
    return sAITool->SetSelectedTool(fTool);
}

ASErr DistortEditor::SelectTool()
{
    fActive = true;
    sAIAnnotator->SetAnnotatorActive(fAnnotator, true);
    Retarget(true);
    Invalidate();
    return kNoErr;
}

ASErr DistortEditor::DeselectTool()
{
    if (fDragCorner >= 0) EndDrag(false);
    Invalidate();
    fActive = false;
    sAIAnnotator->SetAnnotatorActive(fAnnotator, false);
    fTarget = Target();
    return kNoErr;
}

bool DistortEditor::Retarget(bool measure)
{
    struct BusyScope
    {
        bool& flag;
        const bool was;
        explicit BusyScope(bool& f) : flag(f), was(f) { flag = true; }
        ~BusyScope() { flag = was; }
    } busy(fBusy);

    const Target previous = fTarget;
    Target t;

    const std::vector<AIArtHandle> arts = introspect::SelectedTopLevelArt();
    if (arts.size() != 1)
    {
        t.why = arts.empty() ? "nothing is selected" : "more than one object is selected";
        fTarget = t;
        return false;
    }
    t.art = arts[0];

    // Adobe's effect accepts an embedded image and leaves it exactly as it
    // was, reporting a meaningless 100 pt input box at the origin
    // (docs/evidence/support.txt). Handles over it would move nothing.
    short artType = kUnknownArt;
    sAIArt->GetArtType(t.art, &artType);
    if (artType == kRasterArt)
    {
        t.why = "Free Distort does not change raster images";
        fTarget = t;
        return false;
    }

    std::vector<ai::int32> indices;
    if (fd::FindAll(t.art, &indices) || indices.empty())
    {
        t.why = "the selected object has no Free Distort";
        fTarget = t;
        return false;
    }
    t.instanceCount = static_cast<ai::int32>(indices.size());

    // Which Free Distort. The same one as before if it is still there; else
    // the one the Appearance panel has in focus, if that is a Free Distort;
    // else, when there is exactly one, that one. With several and no focus
    // there is no right answer to guess, so the last is taken and the status
    // says so.
    ai::int32 chosen = -1;
    if (previous.valid && previous.art == t.art &&
        std::find(indices.begin(), indices.end(), previous.postIndex) != indices.end())
    {
        chosen = previous.postIndex;
        t.chosenByFocus = previous.chosenByFocus;
    }
    if (chosen < 0)
    {
        AIArtStyleHandle style = nullptr;
        AIStyleParser parser = nullptr;
        if (!sAIArtStyle->GetArtStyle(t.art, &style) && style &&
            !sAIArtStyleParser->NewParser(&parser) && parser)
        {
            AIParserLiveEffect focus = nullptr;
            if (!sAIArtStyleParser->ParseStyle(parser, style) &&
                !sAIArtStyleParser->GetFocusEffect(parser, &focus) && focus)
            {
                for (ai::int32 index : indices)
                {
                    AIParserLiveEffect candidate = nullptr;
                    if (!sAIArtStyleParser->GetNthPostEffect(parser, index, &candidate) && candidate == focus)
                    {
                        chosen = index;
                        t.chosenByFocus = true;
                    }
                }
            }
            sAIArtStyleParser->DisposeParser(parser);
        }
    }
    if (chosen < 0) chosen = indices.back();
    t.postIndex = chosen;

    if (fd::Read(t.art, t.postIndex, &t.stored))
    {
        t.why = "the Free Distort could not be read";
        fTarget = t;
        return false;
    }

    // A source frame with no width or height leaves the renderer dividing by
    // zero. Checked before Adobe's edit path is asked anything about it.
    if (t.stored.hasSource && t.stored.hasDestination && !fdmath::HasUsableFrame(t.stored.source, 1e-6))
    {
        t.why = "this Free Distort's source has no width or no height, so where Adobe draws it is not defined";
        fTarget = t;
        return false;
    }

    fdmath::Rect geometry;
    fd::GeometricBounds(t.art, &geometry);
    AIArtStyleHandle style = nullptr;
    sAIArtStyle->GetArtStyle(t.art, &style);
    t.styleAtMeasure = style;

    const bool sameArt = previous.valid && previous.art == t.art && previous.postIndex == t.postIndex;
    const bool sameGeometry = sameArt &&
        geometry.left == previous.geometryAtMeasure.left && geometry.top == previous.geometryAtMeasure.top &&
        geometry.right == previous.geometryAtMeasure.right && geometry.bottom == previous.geometryAtMeasure.bottom;

    fdmath::Quad drawnByAdobe;
    bool haveDrawnByAdobe = false;
    if (!measure && previous.boundsMeasured && sameGeometry && previous.styleAtMeasure == style)
    {
        // Nothing the input bounds depend on has changed since Adobe was
        // asked: not the art, and not its appearance. The answer stands.
        t.inputBounds = previous.inputBounds;
        t.boundsMeasured = true;
        t.boundsSource = previous.boundsSource;
        t.geometryAtMeasure = previous.geometryAtMeasure;
        t.measureReport = previous.measureReport;
        if (previous.quadFromAdobe)
        {
            drawnByAdobe = previous.quad;
            haveDrawnByAdobe = true;
        }
    }
    else if (measure)
    {
        std::string report;
        if (fd::MeasureInputBounds(t.art, t.postIndex, &t.inputBounds, &t.boundsSource, &report,
                                   &drawnByAdobe, &haveDrawnByAdobe))
        {
            t.why = "the input bounds could not be measured";
            fTarget = t;
            return false;
        }
        t.boundsMeasured = true;
        t.geometryAtMeasure = geometry;
        t.measureReport = report;
    }
    else if (sameArt && fdmath::HasArea(previous.geometryAtMeasure))
    {
        // Carry the last measurement along with the art. Exact when nothing
        // ahead of the effect changes geometry, and close when something
        // does; the next mouse-down measures again before anything is written.
        const fdmath::Quad moved = fdmath::Renormalize(fdmath::RectQuad(previous.inputBounds),
                                                       previous.geometryAtMeasure, geometry);
        t.inputBounds = fdmath::BoundingRect(moved);
        t.geometryAtMeasure = geometry;
        t.boundsSource = previous.boundsSource;
        t.measureReport = "estimated from the previous measurement";
    }
    else
    {
        t.inputBounds = geometry;
        t.geometryAtMeasure = geometry;
        t.measureReport = "estimated from geometric bounds";
    }

    if (!fdmath::HasArea(t.inputBounds))
    {
        t.why = "the input art has no area to distort";
        fTarget = t;
        return false;
    }

    if (t.stored.hasSource && t.stored.hasDestination)
    {
        // Adobe's dialog only ever stores a rectangle here. Anything else
        // came from another writer, and the renderer reads it through a frame
        // and per-corner offsets (section D of the investigation), which the
        // formula follows. The first drag writes the source as the input
        // bounds, exactly as Adobe's own OK converts it, without a jump.
        t.sourceIsRectangle = fdmath::IsAxisAlignedRect(t.stored.source, 1e-6);
        t.quad =fdmath::EffectiveQuad(t.stored.source, t.stored.destination, t.inputBounds);
        if (haveDrawnByAdobe)
        {
            // Adobe's own answer is where the handles go. The formula has to
            // agree with it, or the formula is not describing this host, and
            // nothing the editor would draw during a drag could be trusted.
            double worst = 0.0;
            for (int i = 0; i < 4; ++i) worst = (std::max)(worst, fdmath::Distance(t.quad.c[i], drawnByAdobe.c[i]));
            t.formulaDeviation = worst;
            if (worst > 1e-3)
            {
                t.why = "Adobe draws this Free Distort at " + fd::Describe(drawnByAdobe) +
                        ", not where the editor's reading of its source puts it, " + fd::Describe(t.quad);
                fTarget = t;
                return false;
            }
            t.quad = drawnByAdobe;
            t.quadFromAdobe = true;
        }
    }
    else
    {
        // An empty dictionary, or one missing keys, draws the art unchanged.
        t.quad = fdmath::RectQuad(t.inputBounds);
    }

    t.valid = true;
    fTarget = t;
    RefreshPreviewSource();
    return true;
}

int DistortEditor::HitCorner(const AIRealPoint& cursor) const
{
    if (!fTarget.valid) return -1;
    AIPoint at;
    if (!ToView(FromAI(cursor), &at)) return -1;
    int best = -1;
    double bestDistance = kHitRadius;
    for (int i = 0; i < 4; ++i)
    {
        AIPoint corner;
        if (!ToView(fTarget.quad.c[i], &corner)) continue;
        const double d = std::hypot(static_cast<double>(corner.h - at.h), static_cast<double>(corner.v - at.v));
        if (d <= bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

DistortEditor::Mode DistortEditor::ModeFromEvent(const AIEvent* event)
{
    if (event == nullptr) return Mode::kFree;
    const bool shift = (event->modifiers & aiEventModifiers_shiftKey) != 0;
    const bool alt = (event->modifiers & aiEventModifiers_optionKey) != 0;
    if (shift && alt) return Mode::kAffine;
    if (shift) return Mode::kPerspective;
    if (alt) return Mode::kSymmetric;
    return Mode::kFree;
}

bool DistortEditor::BeginDrag(int corner)
{
    // Measure before anything is written in this context: the measurement
    // undoes its own change with UndoChanges, which would otherwise take the
    // drag's writes with it.
    if (!Retarget(true) || corner < 0 || corner > 3) return false;
    fDragCorner = corner;
    fDragCanceled = false;
    fDragWrote = false;
    fDragStart = fTarget.quad;
    fDragStartStyle = fTarget.styleAtMeasure;
    fLastWrite.clear();

    // Illustrator will not produce the styled result inside a tool's
    // mouse-down (GetStyledArt fails there, measured with a real drag), so
    // the preview is normally the one captured beforehand, if it still
    // describes this art as it now is.
    AIArtStyleHandle style = nullptr;
    sAIArtStyle->GetArtStyle(fTarget.art, &style);
    if (!fPreviewSource.empty() && fPreviewSourceArt == fTarget.art && fPreviewSourceStyle == style &&
        fdmath::Near(fPreviewSourceQuad, fDragStart, 1e-9))
    {
        fPreview = fPreviewSource;
        fPreviewNote = fPreviewSourceNote + " (captured before the drag)";
    }
    else
    {
        std::string note;
        CapturePreview(fDragStart, &fPreview, &note);
        fPreviewNote = note + " (captured at the start of the drag)";
    }
    return true;
}

void DistortEditor::RefreshPreviewSource()
{
    if (!fTarget.valid || fDragCorner >= 0) return;
    AIArtStyleHandle style = nullptr;
    sAIArtStyle->GetArtStyle(fTarget.art, &style);
    if (!fPreviewSource.empty() && fPreviewSourceArt == fTarget.art && fPreviewSourceStyle == style &&
        fdmath::Near(fPreviewSourceQuad, fTarget.quad, 1e-9))
        return;
    std::vector<PreviewContour> captured;
    std::string note;
    const bool ok = CapturePreview(fTarget.quad, &captured, &note);
    // A failure in a context that cannot produce the styled result must not
    // throw away a capture that still describes the art.
    if (!ok && fPreviewSourceArt == fTarget.art && fPreviewSourceStyle == style) return;
    fPreviewSource = std::move(captured);
    fPreviewSourceArt = fTarget.art;
    fPreviewSourceStyle = style;
    fPreviewSourceQuad = fTarget.quad;
    fPreviewSourceNote = note;
}

bool DistortEditor::CapturePreview(const fdmath::Quad& quad, std::vector<PreviewContour>* preview, std::string* note)
{
    preview->clear();
    note->clear();

    // The styled result is Free Distort's output only when nothing comes
    // after it in the appearance.
    AIArtStyleHandle style = nullptr;
    AIStyleParser parser = nullptr;
    ai::int32 postCount = -1;
    if (!sAIArtStyle->GetArtStyle(fTarget.art, &style) && style &&
        !sAIArtStyleParser->NewParser(&parser) && parser)
    {
        if (!sAIArtStyleParser->ParseStyle(parser, style))
            postCount = sAIArtStyleParser->CountPostEffects(parser);
        sAIArtStyleParser->DisposeParser(parser);
    }
    if (postCount != fTarget.postIndex + 1)
    {
        *note = "no preview: another effect follows this Free Distort";
        return false;
    }

    AIArtHandle styled = nullptr;
    if (sAIArtStyle->GetStyledArt(fTarget.art, &styled) || styled == nullptr)
    {
        *note = "no preview: the styled result could not be read";
        return false;
    }

    // Walk the result, keeping every path's segments as (u, t) in the
    // starting quad. A limit keeps a drag over heavy artwork responsive.
    const size_t kMaxSegments = 20000;
    size_t segments = 0;
    bool unsolved = false;
    std::vector<AIArtHandle> stack(1, styled);
    while (!stack.empty() && segments < kMaxSegments && !unsolved)
    {
        const AIArtHandle art = stack.back();
        stack.pop_back();
        short type = kUnknownArt;
        sAIArt->GetArtType(art, &type);
        if (type == kPathArt)
        {
            ai::int16 count = 0;
            AIBoolean closed = false;
            sAIPath->GetPathSegmentCount(art, &count);
            sAIPath->GetPathClosed(art, &closed);
            if (count < 1) continue;
            std::vector<AIPathSegment> segs(static_cast<size_t>(count));
            if (sAIPath->GetPathSegments(art, 0, count, segs.data())) continue;
            PreviewContour contour;
            contour.closed = closed != 0;
            for (const AIPathSegment& s : segs)
            {
                for (const AIRealPoint* p : { &s.in, &s.p, &s.out })
                {
                    double u = 0, t = 0;
                    if (!fdmath::InverseBilinear(quad, FromAI(*p), &u, &t)) { unsolved = true; break; }
                    contour.points.push_back(fdmath::Make(u, t));
                }
                if (unsolved) break;
            }
            segments += segs.size();
            if (!unsolved) preview->push_back(std::move(contour));
            continue;
        }
        // Containers: groups, compound paths, and anything else with children.
        AIArtHandle child = nullptr;
        if (!sAIArt->GetArtFirstChild(art, &child))
        {
            std::vector<AIArtHandle> children;
            while (child != nullptr)
            {
                children.push_back(child);
                AIArtHandle next = nullptr;
                if (sAIArt->GetArtSibling(child, &next)) break;
                child = next;
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) stack.push_back(*it);
        }
    }

    if (unsolved)
    {
        preview->clear();
        *note = "no preview: the starting quad is folded";
        return false;
    }
    std::ostringstream o;
    o << "preview of " << preview->size() << " paths, " << segments << " segments";
    if (segments >= kMaxSegments) o << " (limited)";
    *note = o.str();
    return !preview->empty();
}

void DistortEditor::StepDrag(fdmath::Pt pointer, Mode mode)
{
    if (fDragCorner < 0 || fDragCanceled || !fTarget.valid) return;

    fdmath::Quad next;
    switch (mode)
    {
        case Mode::kPerspective: next = fdmath::MoveCornerPerspective(fDragStart, fDragCorner, pointer); break;
        case Mode::kSymmetric:   next = fdmath::MoveCornerSymmetric(fDragStart, fDragCorner, pointer); break;
        case Mode::kAffine:      next = fdmath::MoveCornerAffine(fDragStart, fDragCorner, pointer); break;
        default:                 next = fdmath::MoveCorner(fDragStart, fDragCorner, pointer); break;
    }

    // Replace, do not accumulate: the previous step's write is discarded
    // first, so the undo history ends up holding one change, and the
    // dictionary is always written from the gesture's start.
    if (fDragWrote) sAIUndo->UndoChanges();
    fLastWrite.clear();
    const ASErr err = fd::Write(fTarget.art, fTarget.postIndex, fTarget.inputBounds, next, &fLastWrite);
    if (!err)
    {
        fDragWrote = true;
        fTarget.quad = next;
        // This write replaced the style, but not anything the input bounds
        // depend on: it changed only this effect's own destination.
        AIArtStyleHandle written = nullptr;
        sAIArtStyle->GetArtStyle(fTarget.art, &written);
        fTarget.styleAtMeasure = written;
        fd::Read(fTarget.art, fTarget.postIndex, &fTarget.stored);
    }
    // No redraw of the document is asked for: Illustrator does not repaint it
    // inside a tool's drag loop even when told to (see PreviewContour), and
    // asking only makes every step slower. The annotator's preview is what
    // moves during the drag; Adobe's own rendering lands on release.
    Invalidate();
}

void DistortEditor::EndDrag(bool commit)
{
    if (fDragCorner < 0) return;
    if (!commit && fDragWrote)
    {
        sAIUndo->UndoChanges();
        fTarget.quad = fDragStart;
        fTarget.styleAtMeasure = fDragStartStyle;
        fDragWrote = false;
    }
    if (commit && fDragWrote)
    {
        sAIUndo->SetUndoTextUS(ai::UnicodeString("Undo Free Distort"), ai::UnicodeString("Redo Free Distort"));
    }
    Invalidate();
    fDragCorner = -1;
    fPreview.clear();
    Invalidate();
}

ASErr DistortEditor::MouseDown(AIToolMessage* message)
{
    ++fMouseDowns;
    fLastCursor = message->cursor;
    if (!fTarget.valid) Retarget(false);
    const int corner = HitCorner(message->cursor);
    fLastHitCorner = corner;
    if (corner < 0) return kNoErr;
    BeginDrag(corner);
    return kNoErr;
}

ASErr DistortEditor::MouseDrag(AIToolMessage* message)
{
    ++fMouseDrags;
    fLastCursor = message->cursor;
    if (fDragCorner < 0) return kNoErr;
#ifdef WIN_ENV
    if (!fDragCanceled && (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
    {
        if (fDragWrote) sAIUndo->UndoChanges();
        fDragWrote = false;
        fTarget.quad = fDragStart;
        fTarget.styleAtMeasure = fDragStartStyle;
        fDragCanceled = true;
        Invalidate();
        return kNoErr;
    }
#endif
    StepDrag(FromAI(message->cursor), ModeFromEvent(message->event));
    return kNoErr;
}

ASErr DistortEditor::MouseUp(AIToolMessage* message)
{
    ++fMouseUps;
    fLastCursor = message->cursor;
    EndDrag(!fDragCanceled);
    return kNoErr;
}

ASErr DistortEditor::Notify(AINotifierMessage* message)
{
    // A document closing, or another window coming forward: whatever the
    // editor held belongs to a document that is going away or going to the
    // back. Forget it without touching it -- no invalidation, no undo -- and
    // look again when something happens in the document that is left.
    if (message->notifier == fDocumentClosing || message->notifier == fWindowChanging)
    {
        fTarget = Target();
        fDragCorner = -1;
        fDragWrote = false;
        fHaveDrawnRect = false;
        return kNoErr;
    }
    // Our own writes raise these too, during a drag and while measuring; the
    // target is already current then.
    if (!fActive || fBusy || fDragCorner >= 0) return kNoErr;
    Invalidate();
    Retarget(false);
    Invalidate();
    return kNoErr;
}

void DistortEditor::Invalidate()
{
    AIRect rect = { 0, 0, 0, 0 };
    bool have = false;
    if (fTarget.valid)
    {
        fdmath::Quad everything = fTarget.quad;
        fdmath::Rect r = fdmath::BoundingRect(everything);
        // The preview's handles can reach outside the quad.
        for (const PreviewContour& contour : fPreview)
            for (const fdmath::Pt& uv : contour.points)
            {
                const fdmath::Pt p = fdmath::Bilinear(fTarget.quad, uv.h, uv.v);
                r.left = (std::min)(r.left, p.h); r.right = (std::max)(r.right, p.h);
                r.top = (std::max)(r.top, p.v); r.bottom = (std::min)(r.bottom, p.v);
            }
        r.left = (std::min)(r.left, fTarget.inputBounds.left);
        r.right = (std::max)(r.right, fTarget.inputBounds.right);
        r.top = (std::max)(r.top, fTarget.inputBounds.top);
        r.bottom = (std::min)(r.bottom, fTarget.inputBounds.bottom);
        AIRealRect artwork;
        artwork.left = static_cast<AIReal>(r.left);
        artwork.top = static_cast<AIReal>(r.top);
        artwork.right = static_cast<AIReal>(r.right);
        artwork.bottom = static_cast<AIReal>(r.bottom);
        if (!sAIDocumentView->ArtworkRectToViewRect(nullptr, &artwork, &rect))
        {
            const int pad = kHandleRadius + 3;
            rect.left -= pad; rect.top -= pad; rect.right += pad; rect.bottom += pad;
            have = true;
        }
    }
    if (fHaveDrawnRect) sAIAnnotator->InvalAnnotationRect(nullptr, &fDrawnRect);
    if (have) sAIAnnotator->InvalAnnotationRect(nullptr, &rect);
    fHaveDrawnRect = have;
    fDrawnRect = rect;
}

ASErr DistortEditor::Draw(AIAnnotatorMessage* message)
{
    if (!fActive || !fTarget.valid || message == nullptr || message->drawer == nullptr) return kNoErr;
    AIAnnotatorDrawer* drawer = message->drawer;

    AIPoint c[4];
    for (int i = 0; i < 4; ++i)
        if (!ToView(fTarget.quad.c[i], &c[i], message->view)) return kNoErr;

    const AIRGBColor accent = Rgb(0, 153, 255);
    const AIRGBColor quiet = Rgb(140, 140, 140);
    const AIRGBColor white = Rgb(255, 255, 255);

    // The input bounds: where the artwork would be with no distortion.
    AIPoint b0, b3;
    if (ToView(fdmath::Make(fTarget.inputBounds.left, fTarget.inputBounds.top), &b0, message->view) &&
        ToView(fdmath::Make(fTarget.inputBounds.right, fTarget.inputBounds.bottom), &b3, message->view))
    {
        AIRect box;
        box.left = (std::min)(b0.h, b3.h); box.right = (std::max)(b0.h, b3.h);
        box.top = (std::min)(b0.v, b3.v); box.bottom = (std::max)(b0.v, b3.v);
        sAIAnnotatorDrawer->SetColor(drawer, quiet);
        sAIAnnotatorDrawer->SetLineWidth(drawer, 1.0);
        sAIAnnotatorDrawer->SetLineDashed(drawer, true);
        sAIAnnotatorDrawer->DrawRect(drawer, box, false);
        sAIAnnotatorDrawer->SetLineDashed(drawer, false);
    }

    // The bilinear grid at thirds. In a bilinear patch every line of
    // constant u or t is straight, so each is one segment.
    sAIAnnotatorDrawer->SetColor(drawer, accent);
    sAIAnnotatorDrawer->SetOpacity(drawer, 0.35);
    for (int k = 1; k <= 2; ++k)
    {
        const double s = k / 3.0;
        AIPoint p, q;
        if (ToView(fdmath::Bilinear(fTarget.quad, s, 0.0), &p, message->view) && ToView(fdmath::Bilinear(fTarget.quad, s, 1.0), &q, message->view))
            sAIAnnotatorDrawer->DrawLine(drawer, p, q);
        if (ToView(fdmath::Bilinear(fTarget.quad, 0.0, s), &p, message->view) && ToView(fdmath::Bilinear(fTarget.quad, 1.0, s), &q, message->view))
            sAIAnnotatorDrawer->DrawLine(drawer, p, q);
    }
    sAIAnnotatorDrawer->SetOpacity(drawer, 1.0);

    // During a drag, where Adobe's effect is about to put the artwork.
    if (fDragCorner >= 0 && !fDragCanceled && !fPreview.empty())
    {
        // Not the handles' blue: Illustrator outlines the selected source art
        // in its layer color, blue by default, and during a drag that outline
        // stays where the source is. The preview has to read as different.
        sAIAnnotatorDrawer->SetColor(drawer, Rgb(230, 0, 160));
        sAIAnnotatorDrawer->SetLineWidth(drawer, 1.0);
        auto toView = [&](fdmath::Pt uv, AIPoint* out) {
            return ToView(fdmath::Bilinear(fTarget.quad, uv.h, uv.v), out, message->view);
        };
        std::vector<std::array<AIPoint, 3>> sets;
        for (const PreviewContour& contour : fPreview)
        {
            const size_t n = contour.points.size() / 3;
            if (n == 0) continue;
            AIPoint start;
            if (!toView(contour.points[1], &start)) continue;
            sets.clear();
            const size_t last = contour.closed ? n : n - 1;
            for (size_t k = 0; k < last; ++k)
            {
                const size_t from = k, to = (k + 1) % n;
                std::array<AIPoint, 3> set;
                if (!toView(contour.points[from * 3 + 2], &set[0]) ||    // exit handle of the previous anchor
                    !toView(contour.points[to * 3 + 0], &set[1]) ||      // entry handle of the next
                    !toView(contour.points[to * 3 + 1], &set[2]))        // the next anchor
                    break;
                sets.push_back(set);
            }
            if (!sets.empty())
            {
                std::vector<AIPoint> flat;
                flat.reserve(sets.size() * 3);
                for (const auto& s : sets) flat.insert(flat.end(), s.begin(), s.end());
                sAIAnnotatorDrawer->DrawBezier(drawer, start,
                    reinterpret_cast<const AIPoint (*)[3]>(flat.data()),
                    static_cast<ai::uint32>(sets.size()), false);
            }
        }
    }

    // The destination outline, in drawing order rather than Adobe's numbering.
    for (int step = 0; step < 4; ++step)
        sAIAnnotatorDrawer->DrawLine(drawer, c[fdmath::OutlineOrder(step)], c[fdmath::OutlineOrder(step + 1)]);

    for (int i = 0; i < 4; ++i)
    {
        AIRect handle;
        handle.left = c[i].h - kHandleRadius; handle.right = c[i].h + kHandleRadius;
        handle.top = c[i].v - kHandleRadius; handle.bottom = c[i].v + kHandleRadius;
        sAIAnnotatorDrawer->SetColor(drawer, i == fDragCorner ? accent : white);
        sAIAnnotatorDrawer->DrawRect(drawer, handle, true);
        sAIAnnotatorDrawer->SetColor(drawer, accent);
        sAIAnnotatorDrawer->DrawRect(drawer, handle, false);
    }
    return kNoErr;
}

std::string DistortEditor::Status() const
{
    std::ostringstream o;
    o << "active\t" << (fActive ? "yes" : "no") << "\n";
    o << "tool messages\tdown " << fMouseDowns << " drag " << fMouseDrags << " up " << fMouseUps
      << " last hit " << fLastHitCorner << " at " << Num(fLastCursor.h) << "," << Num(fLastCursor.v) << "\n";
    o << "target\t" << (fTarget.valid ? "valid" : "none") << "\n";
    if (!fTarget.valid)
    {
        o << "why\t" << fTarget.why << "\n";
        return o.str();
    }
    o << "post-effect\t" << fTarget.postIndex << "\n";
    o << "instances\t" << fTarget.instanceCount << "\n";
    o << "chosen by focus\t" << (fTarget.chosenByFocus ? "yes" : "no") << "\n";
    o << "stored\t" << fd::Describe(fTarget.stored) << "\n";
    o << "input bounds\t" << fd::Describe(fTarget.inputBounds) << "\n";
    o << "bounds from\t" << (fTarget.boundsMeasured
        ? (fTarget.boundsSource == fd::BoundsSource::kAdobe ? "Adobe" : "geometric bounds (Adobe declined)")
        : "estimate") << "\n";
    o << "measure report\t" << fTarget.measureReport;
    if (fTarget.measureReport.empty() || fTarget.measureReport.back() != '\n') o << "\n";
    o << "quad\t" << fd::Describe(fTarget.quad) << "\n";
    o << "source\t" << (fTarget.sourceIsRectangle ? "rectangle" : "not a rectangle") << "\n";
    o << "quad from\t" << (fTarget.quadFromAdobe ? "Adobe's commit" : "the formula") << "\n";
    if (fTarget.formulaDeviation >= 0.0) o << "formula against Adobe\t" << Num(fTarget.formulaDeviation) << "\n";
    o << "drag\t" << (fDragCorner >= 0 ? "open, corner " + std::to_string(fDragCorner) : std::string("none")) << "\n";
    if (!fPreviewNote.empty()) o << "last preview\t" << fPreviewNote << "\n";
    o << "preview source\t" << (fPreviewSourceArt == fTarget.art && !fPreviewSource.empty()
        ? fPreviewSourceNote : std::string("none for this target")) << "\n";
    return o.str();
}

std::string DistortEditor::Refresh(bool measure)
{
    Invalidate();
    Retarget(measure);
    Invalidate();
    return Status();
}

std::string DistortEditor::SimulateDrag(int corner, const std::vector<fdmath::Pt>& points, Mode mode, int cancelAt)
{
    std::ostringstream o;
    if (!BeginDrag(corner))
    {
        o << "could not begin: " << (fTarget.valid ? "bad corner" : fTarget.why) << "\n";
        return o.str();
    }
    o << "begin corner " << corner << " mode " << ModeName(mode) << " start " << fd::Describe(fDragStart) << "\n";
    for (size_t i = 0; i < points.size(); ++i)
    {
        StepDrag(points[i], mode);
        if (static_cast<int>(i) == cancelAt)
        {
            if (fDragWrote) sAIUndo->UndoChanges();
            fDragWrote = false;
            fTarget.quad = fDragStart;
            fTarget.styleAtMeasure = fDragStartStyle;
            fDragCanceled = true;
            o << "escape after step " << i << "\n";
            break;
        }
    }
    if (!fLastWrite.empty()) o << fLastWrite;
    EndDrag(!fDragCanceled);
    o << "end quad " << fd::Describe(fTarget.quad) << "\n";
    return o.str();
}

std::string DistortEditor::PreviewOpen(int corner, fdmath::Pt pointer, Mode mode)
{
    if (fDragCorner >= 0) return "a drag is already open\n";
    if (!BeginDrag(corner)) return "could not begin: " + (fTarget.valid ? std::string("bad corner") : fTarget.why) + "\n";
    StepDrag(pointer, mode);
    return fPreviewNote + "\nquad " + fd::Describe(fTarget.quad) + "\n";
}

std::string DistortEditor::PreviewPoints() const
{
    std::ostringstream o;
    o.precision(12);
    for (const PreviewContour& contour : fPreview)
        for (const fdmath::Pt& uv : contour.points)
        {
            const fdmath::Pt p = fdmath::Bilinear(fTarget.quad, uv.h, uv.v);
            o << p.h << "," << p.v << "\n";
        }
    return o.str();
}

std::string DistortEditor::PreviewClose(bool commit)
{
    if (fDragCorner < 0) return "no drag is open\n";
    // A drag opened by PreviewOpen wrote in an earlier message's undo context,
    // which UndoChanges in this one cannot reach; canceling it is the
    // caller's Undo.
    EndDrag(commit || !fDragWrote);
    return "closed; quad " + fd::Describe(fTarget.quad) + "\n";
}

std::string DistortEditor::HandlesInView() const
{
    std::ostringstream o;
    if (!fTarget.valid) return "none\n";
    for (int i = 0; i < 4; ++i)
    {
        AIPoint v;
        if (ToView(fTarget.quad.c[i], &v))
            o << i << "\t" << v.h << "\t" << v.v << "\t" << Num(fTarget.quad.c[i].h) << "\t" << Num(fTarget.quad.c[i].v) << "\n";
    }
    return o.str();
}
