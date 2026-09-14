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
#include "CornerDialog.h"
#include "FDPTheme.h"

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

}

std::string DistortEditor::ModeName(Mode mode)
{
    switch (mode)
    {
        case Mode::kAxis:       return "axis";
        case Mode::kSymmetric:  return "symmetric";
        case Mode::kConverging: return "converging";
        default:                return "free";
    }
}

bool DistortEditor::ModeFromName(const std::string& name, Mode* mode)
{
    for (Mode m : { Mode::kFree, Mode::kAxis, Mode::kSymmetric, Mode::kConverging })
        if (name == ModeName(m)) { *mode = m; return true; }
    return false;
}

// ---- arrow keys ---------------------------------------------------------------
//
// Illustrator sends a tool no key messages (only [ and ]), and an arrow key
// with any art selected nudges the art. So while this tool is active, a
// message hook on Illustrator's own UI thread -- in this process, on this
// thread, nothing injected anywhere -- looks at key presses as Illustrator
// takes them from its queue. With a corner selected and the keyboard in a
// document window, an arrow moves that corner instead, and the key is not
// passed on. With no corner selected, arrows do what they always do: the art
// moves, and its distortion with it.

namespace
{
#ifdef WIN_ENV
    HHOOK gKeyHook = nullptr;
    DistortEditor* gKeyEditor = nullptr;

    bool InDocumentWindow(HWND hwnd)
    {
        for (HWND h = hwnd; h != nullptr; h = GetParent(h))
        {
            wchar_t cls[64] = { 0 };
            GetClassNameW(h, cls, 64);
            if (wcscmp(cls, L"OWL.Document") == 0) return true;
        }
        return false;
    }

    LRESULT CALLBACK KeyMessageHook(int code, WPARAM removal, LPARAM lp)
    {
        MSG* msg = reinterpret_cast<MSG*>(lp);
        if (code == HC_ACTION && removal == PM_REMOVE && msg != nullptr && gKeyEditor != nullptr &&
            msg->message == WM_KEYDOWN &&
            (msg->wParam == VK_LEFT || msg->wParam == VK_RIGHT || msg->wParam == VK_UP || msg->wParam == VK_DOWN) &&
            GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0 && InDocumentWindow(msg->hwnd))
        {
            const int dh = msg->wParam == VK_LEFT ? -1 : (msg->wParam == VK_RIGHT ? 1 : 0);
            const int dv = msg->wParam == VK_DOWN ? -1 : (msg->wParam == VK_UP ? 1 : 0);
            const std::string said = gKeyEditor->Nudge(dh, dv, GetKeyState(VK_SHIFT) < 0);
            // Taken: Illustrator must not also nudge the art.
            if (said.rfind("moved", 0) == 0) msg->message = WM_NULL;
        }
        return CallNextHookEx(gKeyHook, code, removal, lp);
    }
#endif

    /** Illustrator's keyboard increment, from its own preference, in points;
        1 pt, Illustrator's default, when the preference cannot be read. */
    double KeyboardIncrement()
    {
        double value = 0.0;
        if (sAIPreference->GetRealPreference(nullptr, "cursorKeyLength", &value) == kNoErr && value > 0.0) return value;
        return 1.0;
    }
}

void DistortEditor::InstallKeyHook()
{
#ifdef WIN_ENV
    gKeyEditor = this;
    if (gKeyHook == nullptr) gKeyHook = SetWindowsHookExW(WH_GETMESSAGE, KeyMessageHook, nullptr, GetCurrentThreadId());
#endif
}

void DistortEditor::RemoveKeyHook()
{
#ifdef WIN_ENV
    if (gKeyHook != nullptr) UnhookWindowsHookEx(gKeyHook);
    gKeyHook = nullptr;
    gKeyEditor = nullptr;
#endif
}

void DistortEditor::Shutdown()
{
    RemoveKeyHook();
}

std::string DistortEditor::Nudge(int dh, int dv, bool large)
{
    if (!fActive || fActiveCorner < 0 || fDragCorner >= 0 || fNumericOpen || fBusy) return "not taken\n";
    // The hook runs between Illustrator's own messages, where no plugin
    // context is set up. Pushing one makes everything below one undo step.
    AIAppContextHandle context = nullptr;
    const bool pushed = fPlugin != nullptr && sAIAppContext->PushAppContext(fPlugin, &context) == kNoErr;
    std::ostringstream o;
    if (!Retarget(true))
    {
        o << "not taken: " << fTarget.why;
    }
    else
    {
        const double step = KeyboardIncrement() * (large ? 10.0 : 1.0);
        fdmath::Quad next = fTarget.quad;
        next.c[fActiveCorner] = fdmath::Add(next.c[fActiveCorner], fdmath::Make(dh * step, dv * step));
        std::string report;
        const ASErr err = fd::Write(fTarget.art, fTarget.postIndex, fTarget.inputBounds, next, &report);
        if (err)
        {
            o << "not taken: the write failed (" << err << ")";
        }
        else
        {
            sAIUndo->SetUndoTextUS(ai::UnicodeString("Undo Free Distort"), ai::UnicodeString("Redo Free Distort"));
            fTarget.quad = next;
            AIArtStyleHandle written = nullptr;
            sAIArtStyle->GetArtStyle(fTarget.art, &written);
            fTarget.styleAtMeasure = written;
            fd::Read(fTarget.art, fTarget.postIndex, &fTarget.stored);
            ++fNudges;
            o << "moved corner " << fActiveCorner << " by " << Num(dh * step) << "," << Num(dv * step);
            Invalidate();
            sAIDocument->RedrawDocument();
        }
    }
    if (pushed) sAIAppContext->PopAppContext(context);
    fLastNudge = o.str();
    return fLastNudge + "\n";
}

ASErr DistortEditor::Startup(SPPluginRef self)
{
    fPlugin = self;
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

// ---- numeric entry -----------------------------------------------------------

/** What the corners dialog asks of Illustrator: the ruler's coordinates, the
    document's units, and writes to the document that a later write or a
    cancel replaces. Every write goes through fd::Write, so the dialog stores
    exactly what a drag would, and every preview write is undone before the
    next, so the undo history ends up with at most one change. */
class EditorDialogHost : public CornerDialogHost
{
public:
    EditorDialogHost(DistortEditor& editor, const fdmath::Quad& start, AIArtStyleHandle startStyle)
        : fEditor(editor), fStart(start), fStartStyle(startStyle) {}

    fdmath::Pt ToDisplay(fdmath::Pt artwork) override { return DistortEditor::RulerFromArtwork(artwork); }
    fdmath::Pt FromDisplay(fdmath::Pt display) override { return DistortEditor::ArtworkFromRuler(display); }

    std::wstring FormatLength(double points) override { return DistortEditor::FormatLength(points); }
    bool ParseLength(const std::wstring& text, double* points) override { return DistortEditor::ParseLength(text, points); }

    void Preview(const fdmath::Quad* quad) override
    {
        Discard();
        const fdmath::Quad show = quad ? *quad : fStart;
        if (quad != nullptr && !fdmath::Near(*quad, fStart, 0.0))
        {
            std::string report;
            if (fd::Write(fEditor.fTarget.art, fEditor.fTarget.postIndex, fEditor.fTarget.inputBounds, *quad, &report) == kNoErr)
            {
                fWrote = true;
                fWritten = *quad;
                AIArtStyleHandle written = nullptr;
                sAIArtStyle->GetArtStyle(fEditor.fTarget.art, &written);
                fEditor.fTarget.styleAtMeasure = written;
            }
        }
        fEditor.fTarget.quad = show;
        fEditor.Invalidate();
        sAIDocument->RedrawDocument();
    }

    /** Takes back the last preview write, if there is one. */
    void Discard()
    {
        if (!fWrote) return;
        sAIUndo->UndoChanges();
        fWrote = false;
        fEditor.fTarget.styleAtMeasure = fStartStyle;
        fEditor.fTarget.quad = fStart;
    }

    bool Wrote() const { return fWrote; }
    const fdmath::Quad& Written() const { return fWritten; }

private:
    DistortEditor& fEditor;
    const fdmath::Quad fStart;
    const AIArtStyleHandle fStartStyle;
    bool fWrote = false;
    fdmath::Quad fWritten;
};

namespace
{
    /** Illustrator's ruler as an affine map from artwork coordinates: origin
        and the two axis scales, read from the host's forward conversion of
        three points. The ruler neither rotates nor shears, so this is exact. */
    struct Ruler { double oh, ov, sh, sv; };

    Ruler ReadRuler()
    {
        auto forward = [](double h, double v) {
            AIRealPoint p = { static_cast<AIReal>(h), static_cast<AIReal>(v) };
            if (sAIHardSoft != nullptr)
                sAIHardSoft->ConvertCoordinates(p, kAIDocumentCoordinateSystem, kAICurrentCoordinateSystem, true);
            return fdmath::Make(p.h, p.v);
        };
        const fdmath::Pt o = forward(0.0, 0.0), x = forward(1.0, 0.0), y = forward(0.0, 1.0);
        Ruler r = { o.h, o.v, x.h - o.h, y.v - o.v };
        if (r.sh == 0.0) r.sh = 1.0;
        if (r.sv == 0.0) r.sv = 1.0;
        return r;
    }
}

fdmath::Pt DistortEditor::RulerFromArtwork(fdmath::Pt artwork)
{
    const Ruler r = ReadRuler();
    return fdmath::Make(r.oh + r.sh * artwork.h, r.ov + r.sv * artwork.v);
}

fdmath::Pt DistortEditor::ArtworkFromRuler(fdmath::Pt ruler)
{
    // Not AIHardSoftSuite::ConvertCoordinates in the other direction: with
    // convertForDisplay set it is not the inverse of the forward conversion
    // (measured on 30.7.0: a point shown at y 270 on a 600 pt artboard came
    // back as -870, not 330). Inverting the forward map is exact.
    const Ruler r = ReadRuler();
    return fdmath::Make((ruler.h - r.oh) / r.sh, (ruler.v - r.ov) / r.sv);
}

std::wstring DistortEditor::FormatLength(double points)
{
    ai::UnicodeString s;
    // Four decimals, the most Illustrator's formatter offers. The dialog keeps
    // a field's exact value for as long as its text is not edited.
    if (sAIUser->IUAIRealToStringUnits(static_cast<AIReal>(points), 4, s) != kNoErr) return std::wstring();
    const std::basic_string<ASUnicode> u = s.as_ASUnicode();
    return std::wstring(u.begin(), u.end());
}

bool DistortEditor::ParseLength(const std::wstring& text, double* points, std::wstring* evaluatedText)
{
    const std::basic_string<ASUnicode> u(text.begin(), text.end());
    AIExpressionOptions options;
    options.unit = kAIPointUnits;
    options.minValue = -1.0e7;
    options.maxValue = 1.0e7;
    options.oldValue = 0.0;
    options.precision = 12;
    ai::UnicodeString evaluated;
    AIBoolean changed = false;
    AIDouble value = 0.0;
    const AIErr err = sAIUser->EvaluateExpression(ai::UnicodeString(u), options, evaluated, changed, value);
    if (evaluatedText != nullptr)
    {
        const std::basic_string<ASUnicode> e = evaluated.as_ASUnicode();
        *evaluatedText = std::wstring(e.begin(), e.end());
    }
    // "Changed" is how the evaluator says the text was not a number it could
    // take as it stood: it fell back to the old value, or clipped.
    if (err != kNoErr || changed) return false;
    *points = value;
    return true;
}

namespace
{
    const char* const kPreferencePrefix = kFDPPluginName;

    bool ReadBoolPreference(const char* name, bool fallback)
    {
        AIBoolean value = fallback;
        if (sAIPreference->GetBooleanPreference(kPreferencePrefix, name, &value) != kNoErr) return fallback;
        return value != 0;
    }
}

std::string DistortEditor::OpenNumeric(int corner, bool activate)
{
    if (fNumericOpen) return "the corners dialog is already open\n";
    if (fDragCorner >= 0) return "a drag is open\n";
    // Measured in this message's own context, before anything is written.
    if (!Retarget(true)) return "no target: " + fTarget.why + "\n";

    CornerDialogState state;
    state.start = fTarget.quad;
    state.bounds = fTarget.inputBounds;
    state.focusCorner = corner >= 0 && corner <= 3 ? corner : (fActiveCorner >= 0 ? fActiveCorner : 0);
    state.offsets = ReadBoolPreference("NumericOffsets", false);
    state.preview = ReadBoolPreference("NumericPreview", true);
    state.activate = activate;
    AIWindowRef appWindow = nullptr;
    if (!sAIAppContext->GetPlatformAppWindow(&appWindow)) state.owner = appWindow;
#ifdef WIN_ENV
    const fdptheme::Theme theme = fdptheme::Read();
    if (theme.fromHost)
    {
        state.colors.set = true;
        state.colors.dark = theme.dark;
        state.colors.background = theme.background;
        state.colors.text = theme.text;
        state.colors.editText = theme.editText;
        state.colors.editBackground = theme.editBackground;
        state.colors.border = theme.border;
        state.colors.focusRing = theme.focusRing;
        state.colors.control = theme.control;
        state.colors.controlHot = theme.controlHot;
        state.colors.controlPressed = theme.controlPressed;
    }
#endif

    // The outline shows where Adobe will draw while the fields change, from
    // the same capture a drag uses.
    AIArtStyleHandle style = nullptr;
    sAIArtStyle->GetArtStyle(fTarget.art, &style);
    if (!fPreviewSource.empty() && fPreviewSourceArt == fTarget.art && fPreviewSourceStyle == style &&
        fdmath::Near(fPreviewSourceQuad, fTarget.quad, 1e-9))
        fPreview = fPreviewSource;
    else
        CapturePreview(fTarget.quad, &fPreview, &fPreviewNote);

    EditorDialogHost host(*this, state.start, fTarget.styleAtMeasure);
    fNumericOpen = true;
    Invalidate();
    const bool ok = RunCornerDialog(host, state);
    fNumericOpen = false;

    std::ostringstream o;
    if (ok && !fdmath::Near(state.result, state.start, 0.0))
    {
        if (!host.Wrote() || !fdmath::Near(host.Written(), state.result, 0.0)) host.Preview(&state.result);
        sAIUndo->SetUndoTextUS(ai::UnicodeString("Undo Free Distort"), ai::UnicodeString("Redo Free Distort"));
        fd::Read(fTarget.art, fTarget.postIndex, &fTarget.stored);
        o << "committed " << fd::Describe(state.result);
    }
    else
    {
        host.Discard();
        fTarget.quad = state.start;
        o << (ok ? "OK with nothing changed" : "canceled");
    }
    sAIPreference->PutBooleanPreference(kPreferencePrefix, "NumericOffsets", state.offsets);
    sAIPreference->PutBooleanPreference(kPreferencePrefix, "NumericPreview", state.preview);
    fPreview.clear();
    Invalidate();
    sAIDocument->RedrawDocument();
    fLastNumeric = o.str();
    return fLastNumeric + "\n";
}

ASErr DistortEditor::EditTool()
{
    OpenNumeric(fActiveCorner, true);
    return kNoErr;
}

ASErr DistortEditor::SelectTool()
{
    fActive = true;
    sAIAnnotator->SetAnnotatorActive(fAnnotator, true);
    Retarget(true);
    Invalidate();
    InstallKeyHook();
    return kNoErr;
}

ASErr DistortEditor::DeselectTool()
{
    if (fDragCorner >= 0) EndDrag(false);
    RemoveKeyHook();
    Invalidate();
    fActive = false;
    fActiveCorner = -1;
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
    if (shift && alt) return Mode::kConverging;
    if (shift) return Mode::kAxis;
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
        case Mode::kAxis:       next = fdmath::MoveCornerAxis(fDragStart, fDragCorner, pointer); break;
        case Mode::kSymmetric:  next = fdmath::MoveCornerSymmetric(fDragStart, fDragCorner, pointer); break;
        case Mode::kConverging: next = fdmath::MoveCornerConverging(fDragStart, fDragCorner, pointer); break;
        default:                next = fdmath::MoveCorner(fDragStart, fDragCorner, pointer); break;
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
    fDownCursor = message->cursor;
    fDownAlt = message->event != nullptr && (message->event->modifiers & aiEventModifiers_optionKey) != 0;
    if (!fTarget.valid) Retarget(false);
    const int corner = HitCorner(message->cursor);
    fLastHitCorner = corner;
    if (corner < 0)
    {
        if (fActiveCorner >= 0) { fActiveCorner = -1; Invalidate(); }
        return kNoErr;
    }
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
    // A press and release within a couple of pixels is a click, not a drag:
    // it selects the corner, and with Alt it opens the corners dialog, the
    // way Alt-clicking with Illustrator's transform tools opens theirs.
    AIPoint down, up;
    const bool click = fDragCorner >= 0 && ToView(FromAI(fDownCursor), &down) && ToView(FromAI(message->cursor), &up) &&
                       std::abs(down.h - up.h) <= 2 && std::abs(down.v - up.v) <= 2;
    if (click)
    {
        const int corner = fDragCorner;
        EndDrag(false);
        fActiveCorner = corner;
        Invalidate();
        if (fDownAlt) OpenNumeric(corner, true);
        return kNoErr;
    }
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
    if (((fDragCorner >= 0 && !fDragCanceled) || fNumericOpen) && !fPreview.empty())
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
        sAIAnnotatorDrawer->SetColor(drawer, (i == fDragCorner || i == fActiveCorner) ? accent : white);
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
    o << "active corner\t" << fActiveCorner << "\n";
    o << "keyboard increment\t" << Num(KeyboardIncrement()) << "\n";
    o << "nudges\t" << fNudges << (fLastNudge.empty() ? "" : ", last: " + fLastNudge) << "\n";
    if (!fLastNumeric.empty()) o << "last numeric\t" << fLastNumeric << "\n";
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
