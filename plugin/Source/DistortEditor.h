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

//  DistortEditor.h -- the on-canvas editor: a tool whose handles are the four
//  corners of the selected object's Free Distort, drawn by an annotator over
//  the real artwork.
//
//  A drag rewrites Adobe's dictionary on every mouse event. Illustrator does
//  not repaint the document until the mouse comes up, so while the drag lasts
//  the annotator draws an outline of exactly where Adobe will put the art (see
//  PreviewContour), and on release Adobe's own effect draws it in place, at
//  whatever zoom the user is working at. Each drag is one undo step, the way
//  every Illustrator tool's drag is: Illustrator bundles a tool's mouse-down,
//  drags, and mouse-up into one undo context, and each drag event first
//  discards the previous event's write with UndoChanges, so the history holds
//  only where the corner was released. Escape during a drag discards it.

#ifndef __DISTORTEDITOR_H__
#define __DISTORTEDITOR_H__

#include "IllustratorSDK.h"
#include "AIAnnotator.h"
#include "AITool.h"
#include "AINotifier.h"
#include "FreeDistortEffect.h"
#include "QuadMath.h"

#include <string>
#include <vector>

class DistortEditor
{
public:
    enum class Mode
    {
        kFree,          // the corner goes where it is put
        kPerspective,   // Shift: the edge across the drag widens or narrows about its midpoint
        kSymmetric,     // Alt: the opposite corner moves the other way
        kAffine         // Shift+Alt: the quad stays a parallelogram
    };

    ASErr Startup(SPPluginRef self);

    bool OwnsTool(AIToolHandle tool) const { return tool != nullptr && tool == fTool; }
    bool OwnsAnnotator(AIAnnotatorHandle annotator) const { return annotator != nullptr && annotator == fAnnotator; }
    bool OwnsNotifier(AINotifierHandle notifier) const;

    ASErr SelectTool();
    ASErr DeselectTool();
    ASErr MouseDown(AIToolMessage* message);
    ASErr MouseDrag(AIToolMessage* message);
    ASErr MouseUp(AIToolMessage* message);
    ASErr Draw(AIAnnotatorMessage* message);
    ASErr Notify(AINotifierMessage* message);

    /** Makes this the current tool. */
    ASErr Activate();

    // ---- the test bridge ---------------------------------------------------

    /** What the editor is looking at, one fact per line. */
    std::string Status() const;

    /** Re-reads the target from the host. With `measure`, the input bounds
        are asked of Adobe; without, they are estimated. */
    std::string Refresh(bool measure);

    /** Runs a drag through exactly the code the mouse runs -- begin at the
        corner, one step per point, end -- in the calling context. `cancelAt`
        is the step after which Escape is simulated, or -1. */
    std::string SimulateDrag(int corner, const std::vector<fdmath::Pt>& points, Mode mode, int cancelAt);

    /** The corners' positions in view pixels, for a probe that drives the
        real mouse and needs to know where to press. */
    std::string HandlesInView() const;

    /** Begins a drag and takes one step, and leaves it open, so that the
        live preview is on the canvas for a capture to photograph. */
    std::string PreviewOpen(int corner, fdmath::Pt pointer, Mode mode);

    /** Every point of the open drag's preview, in artwork coordinates, one
        "h,v" per line: what the editor is showing, for comparison with what
        Adobe draws once the drag is released. */
    std::string PreviewPoints() const;

    /** Ends the open drag: commits it, or cancels it as Esc would. */
    std::string PreviewClose(bool commit);

    /** Zeroes the tool-message counters, so a test made by hand starts from
        nothing. */
    void ResetCounters() { fMouseDowns = fMouseDrags = fMouseUps = 0; fLastHitCorner = -2; fLastCursor = { 0, 0 }; }

private:
    struct Target
    {
        bool valid = false;
        std::string why;                    // when not valid
        AIArtHandle art = nullptr;
        ai::int32 postIndex = -1;
        ai::int32 instanceCount = 0;
        bool chosenByFocus = false;
        fd::State stored;
        fdmath::Rect inputBounds;
        bool boundsMeasured = false;        // asked of Adobe, as opposed to estimated
        fd::BoundsSource boundsSource = fd::BoundsSource::kGeometric;
        fdmath::Rect geometryAtMeasure;     // the art's geometric bounds when last measured
        AIArtStyleHandle styleAtMeasure = nullptr;  // and its style, less this editor's own writes
        fdmath::Quad quad;                  // effective destination quad, artwork coordinates
        std::string measureReport;
        bool sourceIsRectangle = true;      // the stored source, as Adobe's dialog writes it
        // When the input bounds were asked of Adobe, its commit also said
        // where it draws; the quad is then Adobe's own answer, and the
        // editor's formula is checked against it.
        bool quadFromAdobe = false;
        double formulaDeviation = -1.0;     // formula against Adobe, in points; -1 when not compared
    };

    bool Retarget(bool measure);
    bool BeginDrag(int corner);
    void StepDrag(fdmath::Pt pointer, Mode mode);
    void EndDrag(bool commit);
    void Invalidate();
    int HitCorner(const AIRealPoint& cursor) const;
    static Mode ModeFromEvent(const AIEvent* event);

    AIToolHandle fTool = nullptr;
    AIAnnotatorHandle fAnnotator = nullptr;
    AINotifierHandle fSelectionChanged = nullptr;
    AINotifierHandle fPropertiesChanged = nullptr;
    AINotifierHandle fDocumentChanged = nullptr;
    AINotifierHandle fDocumentClosing = nullptr;
    AINotifierHandle fWindowChanging = nullptr;

    bool fActive = false;
    /** Set while this editor is itself changing the document. Adobe's edit
        path and every style write raise selection and art notifiers, which
        can arrive inside the call that caused them; they describe changes the
        editor already knows about. */
    bool fBusy = false;
    Target fTarget;

    /** The live preview of a drag.

        Illustrator does not repaint the document inside a tool's drag loop:
        a real drag showed the handles following the pointer and the artwork
        changing only on release, with kToolDoesntWantArtStyleExecutionSuspender
        set and with RedrawDocument called on every step, and the SDK has no
        call that updates a view at once. So the editor draws the result
        itself, from Adobe's own.

        At the start of a drag it takes the art's styled result -- what Adobe's
        effect drew for the starting quad -- and stores every anchor and handle
        as its (u, t) in that quad, by inverse bilinear. Free Distort maps each
        point independently by the bilinear map (section B of the
        investigation), so the same (u, t) through the quad under the pointer
        is exactly where Adobe will put that point on release. Only when Free
        Distort is the last post-effect, so that the styled result is its
        output, and only for quads the inverse can be solved on. */
    struct PreviewContour
    {
        bool closed = false;
        std::vector<fdmath::Pt> points;   // per segment: in handle, anchor, out handle, as (u, t)
    };
    bool CapturePreview(const fdmath::Quad& quad, std::vector<PreviewContour>* preview, std::string* note);
    void RefreshPreviewSource();
    std::vector<PreviewContour> fPreview;          // the open drag's
    std::string fPreviewNote;
    // Captured whenever the editor looks at its target outside a drag, since
    // a tool's mouse-down cannot produce the styled result, and valid while
    // the art, its style, and its quad are what they were at capture.
    std::vector<PreviewContour> fPreviewSource;
    AIArtHandle fPreviewSourceArt = nullptr;
    AIArtStyleHandle fPreviewSourceStyle = nullptr;
    fdmath::Quad fPreviewSourceQuad;
    std::string fPreviewSourceNote;

    // The drag in progress. Every step is computed from fDragStart, never
    // from the previous step.
    int fDragCorner = -1;
    bool fDragCanceled = false;
    bool fDragWrote = false;
    fdmath::Quad fDragStart;
    AIArtStyleHandle fDragStartStyle = nullptr;
    std::string fLastWrite;

    // How many tool messages have arrived, for a probe to tell whether the
    // host's input dispatch reached this tool at all.
    int fMouseDowns = 0;
    int fMouseDrags = 0;
    int fMouseUps = 0;
    int fLastHitCorner = -2;
    AIRealPoint fLastCursor = { 0, 0 };

    bool fHaveDrawnRect = false;
    AIRect fDrawnRect = { 0, 0, 0, 0 };
};

#endif // __DISTORTEDITOR_H__
