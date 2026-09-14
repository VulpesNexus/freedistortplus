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

/* The corners dialog on its own, outside Illustrator.
 *
 * Builds plugin/Source/CornerDialog.cpp unmodified against a host that stands
 * in for Illustrator: a ruler with its origin at (10, 500) and y increasing
 * downward, as Illustrator's rulers are by default; lengths shown as points;
 * and a parser that takes a number with an optional "pt" or "in" and either
 * decimal separator. Illustrator's own conversions and parser are measured in
 * the host by tools/probe-numeric.ps1; this checks the dialog's own logic.
 *
 *   CornerHarness.exe               show it
 *   CornerHarness.exe /dark         in Illustrator's darkest colors
 *   CornerHarness.exe /exit3000     close after three seconds, for a capture
 *   CornerHarness.exe /test:<file>  run the scripted checks, write one line
 *                                   per check to <file>, and exit 1 on a
 *                                   failure
 */

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>
#include "CornerDialog.h"

namespace {

const fdmath::Pt kRulerOrigin = fdmath::Make(10.0, 500.0);

class FakeHost : public CornerDialogHost
{
public:
    fdmath::Pt ToDisplay(fdmath::Pt a) override { return fdmath::Make(a.h - kRulerOrigin.h, kRulerOrigin.v - a.v); }
    fdmath::Pt FromDisplay(fdmath::Pt d) override { return fdmath::Make(d.h + kRulerOrigin.h, kRulerOrigin.v - d.v); }
    std::wstring FormatLength(double points) override
    {
        wchar_t buffer[64];
        swprintf_s(buffer, L"%.4f pt", points);
        std::wstring s(buffer);
        // Trailing zeros off, the way Illustrator shows numbers.
        size_t unit = s.find(L" pt");
        std::wstring number = s.substr(0, unit);
        while (!number.empty() && number.back() == L'0') number.pop_back();
        if (!number.empty() && number.back() == L'.') number.pop_back();
        return number + L" pt";
    }
    bool ParseLength(const std::wstring& text, double* points) override
    {
        std::wstring t;
        for (wchar_t c : text) t.push_back(c == L',' ? L'.' : c);
        wchar_t* end = nullptr;
        const double v = wcstod(t.c_str(), &end);
        if (end == t.c_str()) return false;
        std::wstring rest(end);
        while (!rest.empty() && rest.front() == L' ') rest.erase(rest.begin());
        if (rest.empty() || rest == L"pt") { *points = v; return true; }
        if (rest == L"in") { *points = v * 72.0; return true; }
        return false;
    }
    void Preview(const fdmath::Quad* quad) override { ++previews; lastPreviewWasStart = quad == nullptr; }
    int previews = 0;
    bool lastPreviewWasStart = false;
};

CornerDialogState StartState()
{
    CornerDialogState s;
    s.bounds = fdmath::MakeRect(100.25, 330.5, 340.75, 100.125);
    s.start = fdmath::RectQuad(s.bounds);
    s.start.c[1] = fdmath::Make(410.123456789, 360.987654321);   // more digits than a field shows
    s.activate = false;
    return s;
}

// ---- scripted checks --------------------------------------------------------

struct Step { int kind; int id; std::wstring text; };   // kinds below
enum { kSetText, kCommitField, kClick, kKeyUp, kKeyDown, kEnterIn };

struct Script
{
    std::vector<Step> steps;
    HWND dialog = nullptr;
};

Script gScript;

HWND FindDialog()
{
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND h, LPARAM out) -> BOOL {
        wchar_t cls[64] = { 0 };
        GetClassNameW(h, cls, 64);
        if (wcscmp(cls, L"VulpesNexusFreeDistortCorners") == 0) { *reinterpret_cast<HWND*>(out) = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

/* Runs on a timer inside the dialog's own message loop, so every message is
   sent from the thread that owns the window, the way a user's input arrives. */
void CALLBACK RunScript(HWND, UINT, UINT_PTR timer, DWORD)
{
    KillTimer(nullptr, timer);
    HWND dlg = FindDialog();
    if (dlg == nullptr) return;
    for (const Step& s : gScript.steps)
    {
        HWND control = GetDlgItem(dlg, s.id);
        switch (s.kind)
        {
            case kSetText: SetWindowTextW(control, s.text.c_str()); break;
            case kCommitField: SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(s.id, EN_KILLFOCUS), reinterpret_cast<LPARAM>(control)); break;
            case kClick: SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(s.id, BN_CLICKED), reinterpret_cast<LPARAM>(control)); break;
            case kKeyUp: SendMessageW(control, WM_KEYDOWN, VK_UP, 0); break;
            case kKeyDown: SendMessageW(control, WM_KEYDOWN, VK_DOWN, 0); break;
            case kEnterIn: PostMessageW(control, WM_KEYDOWN, VK_RETURN, 0); break;
        }
    }
}

bool Run(FakeHost& host, CornerDialogState& state, const std::vector<Step>& steps)
{
    gScript.steps = steps;
    SetTimer(nullptr, 0, 150, RunScript);
    return RunCornerDialog(host, state);
}

const int kEdit = 2000;   // + corner * 2 + axis
const int kOffset = 2102;
const int kPreview = 2103;
const int kUndistort = 2104;

int gFailures = 0;
FILE* gOut = nullptr;

void Check(bool ok, const char* what, const std::string& detail)
{
    if (!ok) ++gFailures;
    std::fprintf(gOut, "%s\t%s\t%s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
}

std::string Describe(const fdmath::Quad& q)
{
    char b[256];
    std::snprintf(b, sizeof(b), "(%.9f,%.9f %.9f,%.9f %.9f,%.9f %.9f,%.9f)",
                  q.c[0].h, q.c[0].v, q.c[1].h, q.c[1].v, q.c[2].h, q.c[2].v, q.c[3].h, q.c[3].v);
    return b;
}

bool Same(const fdmath::Quad& a, const fdmath::Quad& b) { return fdmath::Near(a, b, 0.0); }

int RunTests(const wchar_t* path)
{
    if (_wfopen_s(&gOut, path, L"w") != 0 || gOut == nullptr) return 2;

    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kClick, IDOK, L"" } });
        Check(ok && Same(s.result, s.start), "OK with nothing typed returns every corner exactly, digits the fields do not show included", Describe(s.result));
        Check(host.previews == 0, "and writes no preview", std::to_string(host.previews) + " previews");
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kSetText, kEdit + 0, L"100 pt" }, { kCommitField, kEdit + 0, L"" }, { kClick, IDOK, L"" } });
        fdmath::Quad want = s.start; want.c[0].h = 110.0;
        Check(ok && Same(s.result, want), "a typed position moves that one number, through the ruler", Describe(s.result));
        Check(host.previews == 1, "one preview for one committed field", std::to_string(host.previews) + " previews");
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kSetText, kEdit + 7, L"12,5" }, { kCommitField, kEdit + 7, L"" }, { kClick, IDOK, L"" } });
        fdmath::Quad want = s.start; want.c[3].v = 500.0 - 12.5;
        Check(ok && Same(s.result, want), "a decimal comma is a decimal separator (in this host's parser)", Describe(s.result));
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kClick, kOffset, L"" }, { kSetText, kEdit + 7, L"-5.5" }, { kCommitField, kEdit + 7, L"" }, { kClick, IDOK, L"" } });
        fdmath::Quad want = s.start; want.c[3].v = s.bounds.bottom + 5.5;
        Check(ok && Same(s.result, want) && s.offsets, "an offset moves from the undistorted corner, in the ruler's direction", Describe(s.result));
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kSetText, kEdit + 2, L"abc" }, { kCommitField, kEdit + 2, L"" }, { kClick, IDOK, L"" } });
        Check(ok && Same(s.result, s.start), "text that is not a number changes nothing", Describe(s.result));
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kKeyUp, kEdit + 1, L"" }, { kKeyUp, kEdit + 1, L"" }, { kKeyDown, kEdit + 3, L"" }, { kClick, IDOK, L"" } });
        fdmath::Quad want = s.start; want.c[0].v -= 2.0; want.c[1].v += 1.0;
        Check(ok && Same(s.result, want), "the up and down arrows step a field by one unit of the ruler", Describe(s.result));
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kSetText, kEdit + 0, L"1 in" }, { kCommitField, kEdit + 0, L"" }, { kClick, IDCANCEL, L"" } });
        Check(!ok && host.lastPreviewWasStart == false, "Cancel returns false, whatever was typed", ok ? "OK" : "canceled");
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kSetText, kEdit + 4, L"1 in" }, { kEnterIn, kEdit + 4, L"" } });
        fdmath::Quad want = s.start; want.c[2].h = 72.0 + 10.0;
        Check(ok && Same(s.result, want), "Enter in a field takes what was typed there and closes with OK", Describe(s.result));
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kClick, kUndistort, L"" }, { kClick, IDOK, L"" } });
        Check(ok && Same(s.result, fdmath::RectQuad(s.bounds)), "Undistort puts every corner where it is with no distortion", Describe(s.result));
    }
    {
        FakeHost host; CornerDialogState s = StartState();
        const bool ok = Run(host, s, { { kClick, kPreview, L"" }, { kSetText, kEdit + 0, L"50" }, { kCommitField, kEdit + 0, L"" }, { kClick, kPreview, L"" }, { kClick, IDOK, L"" } });
        fdmath::Quad want = s.start; want.c[0].h = 60.0;
        Check(ok && Same(s.result, want) && s.preview, "with the preview off, a field still counts; turning it back on previews it", Describe(s.result) + ", " + std::to_string(host.previews) + " previews");
    }

    std::fprintf(gOut, "%d failed\n", gFailures);
    std::fclose(gOut);
    return gFailures == 0 ? 0 : 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR cmdLine, int)
{
    const wchar_t* test = cmdLine ? wcsstr(cmdLine, L"/test:") : nullptr;
    if (test != nullptr) return RunTests(test + 6);

    FakeHost host;
    CornerDialogState state = StartState();
    state.activate = true;
    if (cmdLine != nullptr && wcsstr(cmdLine, L"/dark") != nullptr)
    {
        // Illustrator 30.7's darkest dialog colors, as the About harness uses.
        state.colors.set = true;
        state.colors.dark = true;
        state.colors.background = RGB(0x32, 0x32, 0x32);
        state.colors.text = RGB(0xCD, 0xCD, 0xCD);
        state.colors.editText = RGB(0xE6, 0xE6, 0xE6);
        state.colors.editBackground = RGB(0x1E, 0x1E, 0x1E);
        state.colors.border = RGB(0x1E, 0x1E, 0x1E);
        state.colors.focusRing = RGB(0x5A, 0xA9, 0xE6);
        state.colors.control = RGB(0x40, 0x40, 0x40);
        state.colors.controlHot = RGB(0x4C, 0x4C, 0x4C);
        state.colors.controlPressed = RGB(0x2A, 0x2A, 0x2A);
    }
    const wchar_t* exitAt = cmdLine ? wcsstr(cmdLine, L"/exit") : nullptr;
    if (exitAt != nullptr)
    {
        const UINT ms = static_cast<UINT>(_wtoi(exitAt + 5));
        SetTimer(nullptr, 0, ms, [](HWND, UINT, UINT_PTR t, DWORD) {
            KillTimer(nullptr, t);
            HWND dlg = FindDialog();
            if (dlg != nullptr) SendMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
        });
    }
    RunCornerDialog(host, state);
    return 0;
}
