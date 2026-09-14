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

//  FDPPlugin.cpp -- see FDPPlugin.h.

#include "IllustratorSDK.h"
#include "FDPPlugin.h"
#include "FDPSuites.h"
#include "FreeDistortEffect.h"
#include "Introspect.h"
#include "SDKDef.h"
#include "SDKAboutPluginsHelper.h"
#include "CornerDialog.h"
#ifdef WIN_ENV
#include "FDPAbout.h"
#include "FDPTheme.h"
#endif

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::vector<std::string> Split(const std::string& text, char sep, size_t maxFields = 0)
    {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos <= text.size())
        {
            if (maxFields && out.size() + 1 == maxFields)
            {
                out.push_back(text.substr(pos));
                return out;
            }
            const size_t next = text.find(sep, pos);
            if (next == std::string::npos)
            {
                out.push_back(text.substr(pos));
                return out;
            }
            out.push_back(text.substr(pos, next - pos));
            pos = next + 1;
        }
        return out;
    }

    /** Numbers from the bridge are always written with a decimal point,
        whatever the machine's locale says. strtod honors the C locale, which
        is what a plugin runs in unless something changed it; this checks. */
    std::vector<double> Numbers(const std::string& text)
    {
        std::vector<double> out;
        for (const std::string& piece : Split(text, ','))
            if (!piece.empty()) out.push_back(std::strtod(piece.c_str(), nullptr));
        return out;
    }

    /** The one object an editing command applies to, or a reason. */
    AIArtHandle SingleTarget(std::string* why)
    {
        const std::vector<AIArtHandle> arts = introspect::SelectedTopLevelArt();
        if (arts.size() == 1) return arts[0];
        *why = arts.empty() ? "No selection.\n" : "Select exactly one object.\n";
        return nullptr;
    }
}

Plugin* AllocatePlugin(SPPluginRef pluginRef)
{
    return new FDPPlugin(pluginRef);
}

void FixupReload(Plugin* plugin)
{
    FDPPlugin::FixupVTable(static_cast<FDPPlugin*>(plugin));
}

FDPPlugin::FDPPlugin(SPPluginRef pluginRef)
    : Plugin(pluginRef)
{
    strncpy(fPluginName, kFDPPluginName, kMaxStringLength);
}

ASErr FDPPlugin::Message(char* caller, char* selector, void* message)
{
    ASErr error = kNoErr;
    try
    {
        if (std::strcmp(caller, kCallerAIScriptMessage) == 0)
            return HandleScriptMessage(selector, static_cast<AIScriptMessage*>(message));

        if (std::strcmp(caller, kCallerAIAnnotation) == 0)
        {
            if (std::strcmp(selector, kSelectorAIDrawAnnotation) == 0)
                return fEditor.Draw(static_cast<AIAnnotatorMessage*>(message));
            return kNoErr;
        }

        error = Plugin::Message(caller, selector, message);
    }
    catch (ai::Error& ex)
    {
        error = ex;
    }
    catch (...)
    {
        error = kCantHappenErr;
    }

    if (error)
    {
        if (error == kUnhandledMsgErr)
            error = kNoErr;
        else
            Plugin::ReportError(error, caller, selector, message);
    }
    return error;
}

ASErr FDPPlugin::StartupPlugin(SPInterfaceMessage* message)
{
    ASErr error = Plugin::StartupPlugin(message);
    if (error) return error;

    SDKAboutPluginsHelper aboutPluginsHelper;
    error = aboutPluginsHelper.AddAboutPluginsMenuItem(message, kFDPAboutGroupName,
        ai::UnicodeString(kFDPAboutGroupTitle), kFDPAboutMenuTitle, &fAboutMenu);
    if (error) return error;

    return fEditor.Startup(message->d.self);
}

ASErr FDPPlugin::PostStartupPlugin()
{
    // Where the command lives is being measured, not settled: Adobe's own
    // Effect > Distort & Transform submenu first, which exists only once every
    // plugin has loaded, and Object > Transform if that group cannot be found.
    // The bridge's "menu" selector reports which one took.
    AIPlatformAddMenuItemDataUS data;
    data.itemText = ai::UnicodeString(kFDPMenuTitle);
    const char* const groups[] = { "Live Vector &Distort && Transform", kArrangeTransformMenuGroup };
    for (const char* group : groups)
    {
        data.groupName = group;
        if (!sAIMenu->AddMenuItem(fPluginRef, "VulpesNexus FreeDistort+", &data, 0, &fEditorMenu) && fEditorMenu)
        {
            fEditorMenuPlacement = group;
            sAIMenu->UpdateMenuItemAutomatically(fEditorMenu, kAutoEnableMenuItemAction, 0, 0, kIfAnyArt, 0, 0, 0);
            break;
        }
    }
    return kNoErr;
}

ASErr FDPPlugin::ShutdownPlugin(SPInterfaceMessage* message)
{
    fEditor.Shutdown();
    ShutdownCornerDialog();
    message->d.globals = nullptr;
    return Plugin::ShutdownPlugin(message);
}

ASErr FDPPlugin::UnloadPlugin(SPInterfaceMessage* message)
{
    // The arrow-key hook points into this module; it must never outlive it.
    fEditor.Shutdown();
    return Plugin::UnloadPlugin(message);
}

std::string FDPPlugin::OpenEditor()
{
    std::string why;
    AIArtHandle art = SingleTarget(&why);
    if (art == nullptr) return why;

    std::vector<ai::int32> indices;
    fd::FindAll(art, &indices);
    std::string said;
    if (indices.empty())
    {
        ai::int32 index = -1;
        const ASErr err = fd::AppendIdentity(art, &index);
        if (err) return "Could not add Free Distort (" + std::to_string(err) + ").\n";
        sAIUndo->SetUndoTextUS(ai::UnicodeString("Undo Free Distort"), ai::UnicodeString("Redo Free Distort"));
        said = "Added Free Distort as post-effect " + std::to_string(index) + ".\n";
    }
    const ASErr err = fEditor.Activate();
    return said + "Editor " + (err ? "could not be selected (" + std::to_string(err) + ")" : "selected") + ".\n";
}

#ifdef WIN_ENV
/* Illustrator's own dialog colors, turned into the plain struct the About
   dialog takes, the same way LiveShear does it: the host is asked here, so
   FDPAbout.cpp compiles without a line of Illustrator in it, and the numbers
   come from the same reading the corners dialog uses. */
static FDPAboutTheme AboutThemeFromHost()
{
    const fdptheme::Theme host = fdptheme::Read();
    FDPAboutTheme about;
    if (!host.fromHost) return about;
    about.panel = host.editBackground;
    about.panelText = host.editText;
    about.band = host.background;
    about.bandText = host.text;
    about.rule = host.border;
    about.link = host.focusRing;
    about.ownerDrawButton = true;
    about.button = host.control;
    about.buttonText = host.text;
    about.buttonBorder = host.border;
    about.darkTitleBar = host.dark;
    return about;
}
#endif

ASErr FDPPlugin::GoMenuItem(AIMenuMessage* message)
{
    if (message->menuItem == fEditorMenu)
    {
        OpenEditor();
    }
    else if (message->menuItem == fAboutMenu)
    {
#ifdef WIN_ENV
        // Our own module, where the dialog resource lives, found from the
        // address of a function in it.
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&FDPShowAboutDialog), &self) && self != nullptr)
        {
            if (FDPShowAboutDialog(self, GetActiveWindow(), AboutThemeFromHost())) return kNoErr;
        }
#endif
        // Last resort, reached only if the dialog could not be created.
        SDKAboutPluginsHelper aboutPluginsHelper;
        const std::string about = std::string(kFDPProductName) + " " + kFDPVersionString + "\n" +
                                  kFDPDescription + "\n" + kFDPHomePage + "\n" + kFDPCopyright;
        aboutPluginsHelper.PopAboutBox(message, "About FreeDistort+", about.c_str());
    }
    return kNoErr;
}

ASErr FDPPlugin::Notify(AINotifierMessage* message)
{
    if (fEditor.OwnsNotifier(message->notifier)) return fEditor.Notify(message);
    return kNoErr;
}

ASErr FDPPlugin::SelectTool(AIToolMessage* message)
{
    return fEditor.OwnsTool(message->tool) ? fEditor.SelectTool() : kNoErr;
}

ASErr FDPPlugin::DeselectTool(AIToolMessage* message)
{
    return fEditor.OwnsTool(message->tool) ? fEditor.DeselectTool() : kNoErr;
}

ASErr FDPPlugin::ToolMouseDown(AIToolMessage* message)
{
    return fEditor.OwnsTool(message->tool) ? fEditor.MouseDown(message) : kNoErr;
}

ASErr FDPPlugin::ToolMouseDrag(AIToolMessage* message)
{
    return fEditor.OwnsTool(message->tool) ? fEditor.MouseDrag(message) : kNoErr;
}

ASErr FDPPlugin::ToolMouseUp(AIToolMessage* message)
{
    return fEditor.OwnsTool(message->tool) ? fEditor.MouseUp(message) : kNoErr;
}

ASErr FDPPlugin::EditTool(AIToolMessage* message)
{
    return fEditor.OwnsTool(message->tool) ? fEditor.EditTool() : kNoErr;
}

//  The test bridge:
//      app.sendScriptMessage("FreeDistortPlus", "<selector>", "<arguments>")
//  A test interface, not an API. See docs/BUILDING.md.
ASErr FDPPlugin::HandleScriptMessage(const char* selector, AIScriptMessage* message)
{
    if (message == nullptr) return kNoErr;
    const std::string sel(selector ? selector : "");
    const std::string in = message->inParam.as_UTF8();
    std::ostringstream out;
    std::string why;

    if (sel == "version")
    {
        char probe[32];
        std::snprintf(probe, sizeof(probe), "%.2f", 0.25);
        out << kFDPProductName << " " << kFDPVersionString << "\n"
            << "plugin\t" << kFDPPluginName << "\n"
            << "decimal point\t" << probe << "\n";
    }
    else if (sel == "registry") out << introspect::DumpLiveEffectRegistry();
    else if (sel == "appearance") out << introspect::DumpSelectionAppearance();
    else if (sel == "geometry") out << introspect::DumpSelectionGeometry();
    else if (sel == "menu groups") out << introspect::DumpMenuGroups();
    else if (sel == "menu")
    {
        out << "placement\t" << (fEditorMenuPlacement.empty() ? "(none)" : fEditorMenuPlacement) << "\n";
    }
    else if (sel == "apply effect")
    {
        const std::vector<std::string> f = Split(in, '|', 2);
        out << introspect::ApplyEffectByName(f.size() > 0 ? f[0] : "", f.size() > 1 ? f[1] : "");
    }
    else if (sel == "set param" || sel == "delete param")
    {
        // index|key|type|value   or   index|key
        const std::vector<std::string> f = Split(in, '|', 4);
        const bool del = sel == "delete param";
        if (f.size() < (del ? 2u : 4u)) out << "Expected index|key" << (del ? "" : "|type|value") << "\n";
        else out << introspect::EditEffectParameter(std::atoi(f[0].c_str()), f[1], del ? "" : f[2], del ? "" : f[3], del);
    }
    else if (sel == "edit effect") out << introspect::EditEffect(std::atoi(in.c_str()));
    else if (sel == "remove effect") out << introspect::RemoveEffect(std::atoi(in.c_str()));
    else if (sel == "move effect")
    {
        const std::vector<double> n = Numbers(in);
        out << (n.size() == 2 ? introspect::MoveEffect(static_cast<ai::int32>(n[0]), static_cast<ai::int32>(n[1])) : "Expected from,to\n");
    }
    else if (sel == "undo count")
    {
        ai::int32 past = 0, future = 0;
        const ASErr err = sAIUndo->CountTransactions(&past, &future);
        out << "past\t" << past << "\nfuture\t" << future << "\nresult\t" << err << "\n";
    }
    else if (sel == "undo clear")
    {
        // Only for probes, in their own document: forgets this document's
        // undo history so a count of transactions starts from zero.
        AIDocumentHandle document = nullptr;
        ASErr err = sAIDocument->GetDocument(&document);
        if (!err) err = sAIUndo->ClearHistory(document);
        out << "result\t" << err << "\n";
    }
    else if (sel == "fd list")
    {
        for (AIArtHandle art : introspect::SelectedTopLevelArt())
        {
            std::vector<ai::int32> indices;
            fd::FindAll(art, &indices);
            out << "object";
            for (ai::int32 i : indices) out << "\t" << i;
            out << "\n";
        }
    }
    else if (sel == "fd read")
    {
        AIArtHandle art = SingleTarget(&why);
        fd::State state;
        if (art == nullptr) out << why;
        else if (const ASErr err = fd::Read(art, std::atoi(in.c_str()), &state)) out << "Read failed (" << err << ")\n";
        else out << fd::Describe(state) << "\n";
    }
    else if (sel == "fd bounds")
    {
        AIArtHandle art = SingleTarget(&why);
        if (art == nullptr) out << why;
        else
        {
            fdmath::Rect bounds, geometry;
            fd::BoundsSource how = fd::BoundsSource::kGeometric;
            std::string report;
            const ASErr err = fd::MeasureInputBounds(art, std::atoi(in.c_str()), &bounds, &how, &report);
            fd::GeometricBounds(art, &geometry);
            out << "result\t" << err << "\n"
                << "input bounds\t" << fd::Describe(bounds) << "\n"
                << "from\t" << (how == fd::BoundsSource::kAdobe ? "Adobe" : "geometric") << "\n"
                << "geometric bounds\t" << fd::Describe(geometry) << "\n"
                << "report\t" << report;
        }
    }
    else if (sel == "fd write")
    {
        // index|left,top,right,bottom|d0h,d0v,d1h,d1v,d2h,d2v,d3h,d3v
        const std::vector<std::string> f = Split(in, '|', 3);
        AIArtHandle art = SingleTarget(&why);
        const std::vector<double> s = f.size() > 1 ? Numbers(f[1]) : std::vector<double>();
        const std::vector<double> d = f.size() > 2 ? Numbers(f[2]) : std::vector<double>();
        if (art == nullptr) out << why;
        else if (s.size() != 4 || d.size() != 8) out << "Expected index|l,t,r,b|8 destination numbers\n";
        else
        {
            fdmath::Quad q;
            for (int i = 0; i < 4; ++i) q.c[i] = fdmath::Make(d[i * 2], d[i * 2 + 1]);
            std::string report;
            const ASErr err = fd::Write(art, std::atoi(f[0].c_str()), fdmath::MakeRect(s[0], s[1], s[2], s[3]), q, &report);
            out << report << "result\t" << err << "\n";
        }
    }
    else if (sel == "fd corner")
    {
        // index|corner|h,v -- in effective (on-canvas) coordinates
        const std::vector<std::string> f = Split(in, '|', 3);
        AIArtHandle art = SingleTarget(&why);
        const std::vector<double> p = f.size() > 2 ? Numbers(f[2]) : std::vector<double>();
        if (art == nullptr) out << why;
        else if (p.size() != 2) out << "Expected index|corner|h,v\n";
        else
        {
            const ai::int32 index = std::atoi(f[0].c_str());
            const int corner = std::atoi(f[1].c_str());
            fd::State state;
            fdmath::Rect bounds;
            fd::BoundsSource how = fd::BoundsSource::kGeometric;
            std::string report;
            ASErr err = fd::Read(art, index, &state);
            if (!err) err = fd::MeasureInputBounds(art, index, &bounds, &how, &report);
            if (!err && (corner < 0 || corner > 3)) err = kBadParameterErr;
            if (!err)
            {
                fdmath::Quad quad = (state.hasSource && state.hasDestination)
                    ? fdmath::EffectiveQuad(state.source, state.destination, bounds)
                    : fdmath::RectQuad(bounds);
                quad.c[corner] = fdmath::Make(p[0], p[1]);
                err = fd::Write(art, index, bounds, quad, &report);
            }
            out << report << "result\t" << err << "\n";
        }
    }
    else if (sel == "fd append")
    {
        AIArtHandle art = SingleTarget(&why);
        ai::int32 index = -1;
        if (art == nullptr) out << why;
        else out << "result\t" << fd::AppendIdentity(art, &index) << "\nindex\t" << index << "\n";
    }
    else if (sel == "tools")
    {
        ai::int32 count = 0;
        sAITool->CountTools(&count);
        AIToolHandle current = nullptr;
        sAITool->GetSelectedTool(&current);
        for (ai::int32 i = 0; i < count; ++i)
        {
            AIToolHandle tool = nullptr;
            char* name = nullptr;
            if (sAITool->GetNthTool(i, &tool) || tool == nullptr || sAITool->GetToolName(tool, &name) || name == nullptr) continue;
            out << name << (tool == current ? "\t(selected)" : "") << "\n";
        }
    }
    else if (sel == "tool select") out << "result\t" << sAITool->SetSelectedToolByName(in.c_str()) << "\n";
    else if (sel == "view")
    {
        AIRealRect bounds = { 0, 0, 0, 0 };
        AIReal zoom = 0;
        const ASErr err = sAIDocumentView->GetDocumentViewBounds(nullptr, &bounds);
        sAIDocumentView->GetDocumentViewZoom(nullptr, &zoom);
        AIRealPoint topLeft = { bounds.left, bounds.top };
        AIPoint viewTopLeft = { 0, 0 };
        sAIDocumentView->ArtworkPointToViewPoint(nullptr, &topLeft, &viewTopLeft);
        out << "result\t" << err << "\nbounds\t" << fd::Describe(fdmath::MakeRect(bounds.left, bounds.top, bounds.right, bounds.bottom))
            << "\nzoom\t" << zoom << "\ntop-left in view\t" << viewTopLeft.h << "," << viewTopLeft.v << "\n";
    }
    else if (sel == "editor open") out << OpenEditor();
    else if (sel == "editor status") out << fEditor.Status();
    else if (sel == "editor refresh") out << fEditor.Refresh(in == "measure");
    else if (sel == "editor handles") out << fEditor.HandlesInView();
    else if (sel == "editor preview open")
    {
        // corner|mode|h,v
        const std::vector<std::string> f = Split(in, '|', 3);
        const std::vector<double> p = f.size() > 2 ? Numbers(f[2]) : std::vector<double>();
        if (f.size() < 3 || p.size() != 2) out << "Expected corner|mode|h,v\n";
        else
        {
            DistortEditor::Mode mode = DistortEditor::Mode::kFree;
            DistortEditor::ModeFromName(f[1], &mode);
            out << fEditor.PreviewOpen(std::atoi(f[0].c_str()), fdmath::Make(p[0], p[1]), mode);
        }
    }
    else if (sel == "editor preview points") out << fEditor.PreviewPoints();
    else if (sel == "editor preview close") out << fEditor.PreviewClose(true);
    else if (sel == "editor numeric")
    {
        // corner|activate: activate 0 keeps the window from taking the
        // foreground, for a probe driving it with window messages.
        const std::vector<std::string> f = Split(in, '|', 2);
        out << fEditor.OpenNumeric(f.size() > 0 ? std::atoi(f[0].c_str()) : 0, !(f.size() > 1 && f[1] == "0"));
    }
    else if (sel == "units format")
    {
        // A length in points, formatted the way the corners dialog shows it.
        const std::wstring w = DistortEditor::FormatLength(std::strtod(in.c_str(), nullptr));
        out << "formatted\t" << ai::UnicodeString(std::basic_string<ASUnicode>(w.begin(), w.end())).as_UTF8() << "\n";
    }
    else if (sel == "units parse")
    {
        // Text taken the way the corners dialog takes a field.
        const ai::UnicodeString text = ai::UnicodeString::FromUTF8(in);
        const std::basic_string<ASUnicode> u = text.as_ASUnicode();
        double points = 0.0;
        std::wstring evaluated;
        const bool ok = DistortEditor::ParseLength(std::wstring(u.begin(), u.end()), &points, &evaluated);
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.12g", points);
        out << "accepted\t" << (ok ? "yes" : "no") << "\npoints\t" << (ok ? buffer : "") << "\nevaluated\t"
            << ai::UnicodeString(std::basic_string<ASUnicode>(evaluated.begin(), evaluated.end())).as_UTF8() << "\n";
    }
    else if (sel == "coords")
    {
        // h,v in artwork coordinates, as the ruler shows them, and back.
        const std::vector<double> n = Numbers(in);
        if (n.size() == 2)
        {
            char buffer[160];
            const fdmath::Pt display = DistortEditor::RulerFromArtwork(fdmath::Make(n[0], n[1]));
            const fdmath::Pt back = DistortEditor::ArtworkFromRuler(display);
            AIRealPoint raw = { static_cast<AIReal>(display.h), static_cast<AIReal>(display.v) };
            sAIHardSoft->ConvertCoordinates(raw, kAICurrentCoordinateSystem, kAIDocumentCoordinateSystem, true);
            std::snprintf(buffer, sizeof(buffer), "ruler\t%.12g,%.12g\nback\t%.12g,%.12g\nhost reverse\t%.12g,%.12g\n",
                          display.h, display.v, back.h, back.v, raw.h, raw.v);
            out << buffer;
        }
        else out << "Expected h,v\n";
    }
    else if (sel == "editor corner")
    {
        // Selects a corner, as a click on its handle does; -1 for none.
        out << fEditor.SelectCorner(std::atoi(in.c_str()));
    }
    else if (sel == "snap info")
    {
        // Whether Illustrator's snapping answers for the null view and for
        // the document's first view, and what Track does with a point.
        AIDocumentViewHandle first = nullptr;
        const ASErr ve = sAIDocumentView->GetNthDocumentView(0, &first);
        out << "use smart guides (null view)\t" << (sAICursorSnap->UseSmartGuides(nullptr) ? "yes" : "no") << "\n"
            << "first view\t" << (first ? "found" : "none") << " (" << ve << ")\n"
            << "use smart guides (first view)\t" << (first && sAICursorSnap->UseSmartGuides(first) ? "yes" : "no") << "\n";
        const std::vector<double> n = Numbers(in);
        if (n.size() == 2)
        {
            AIEvent plain;
            std::memset(&plain, 0, sizeof(plain));
            AIRealPoint p = { static_cast<AIReal>(n[0]), static_cast<AIReal>(n[1]) }, q = p;
            const ASErr te = sAICursorSnap->Track(first, p, &plain, "ATFPLMG v i o", &q);
            char buffer[120];
            std::snprintf(buffer, sizeof(buffer), "track\t%.12g,%.12g -> %.12g,%.12g (%d)\n", p.h, p.v, q.h, q.v, static_cast<int>(te));
            out << buffer;
        }
    }
    else if (sel == "pref")
    {
        // A preference read the way the plugin reads it: name, application prefix.
        double r = 0.0;
        ai::int32 i = 0;
        AIBoolean b = false;
        const ASErr er = sAIPreference->GetRealPreference(nullptr, in.c_str(), &r);
        const ASErr ei = sAIPreference->GetIntegerPreference(nullptr, in.c_str(), &i);
        const ASErr eb = sAIPreference->GetBooleanPreference(nullptr, in.c_str(), &b);
        out << "real\t" << r << " (" << er << ")\ninteger\t" << i << " (" << ei << ")\nboolean\t" << (b ? 1 : 0) << " (" << eb << ")\n";
    }
    else if (sel == "editor reset")
    {
        fEditor.ResetCounters();
        out << "counters reset\n";
    }
    else if (sel == "editor drag")
    {
        // corner|mode|cancelAt|h,v;h,v;...
        const std::vector<std::string> f = Split(in, '|', 4);
        if (f.size() < 4) out << "Expected corner|mode|cancelAt|h,v;h,v;...\n";
        else
        {
            // A mode name, optionally with "+snap": the points then go through
            // Illustrator's snapping as a mouse drag's do.
            DistortEditor::Mode mode = DistortEditor::Mode::kFree;
            const bool snap = f[1].size() > 5 && f[1].compare(f[1].size() - 5, 5, "+snap") == 0;
            DistortEditor::ModeFromName(snap ? f[1].substr(0, f[1].size() - 5) : f[1], &mode);
            std::vector<fdmath::Pt> points;
            for (const std::string& pair : Split(f[3], ';'))
            {
                const std::vector<double> n = Numbers(pair);
                if (n.size() == 2) points.push_back(fdmath::Make(n[0], n[1]));
            }
            out << fEditor.SimulateDrag(std::atoi(f[0].c_str()), points, mode, std::atoi(f[2].c_str()), snap);
        }
    }
    else
    {
        out << "Unknown selector \"" << sel << "\".\n";
    }

    message->outParam = ai::UnicodeString::FromUTF8(out.str().c_str());
    return kNoErr;
}
