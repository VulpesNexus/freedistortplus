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

/* Shows the plugin's About dialog on its own, outside Illustrator.
 *
 * A modal dialog inside a host application is close to untestable: you cannot
 * drive it, screenshot it, or measure it without a person sitting there. This
 * builds the very same FDPAbout.cpp and the very same dialog resource into a
 * three-hundred-line executable, so the layout can be looked at, and looked at
 * again after every edit.
 *
 * The prose has a hard limit of about ten lines -- the body control does not
 * scroll and says nothing when the text runs past the bottom -- so this is
 * also how an edit to the wording gets checked.
 *
 * It can also stand in for Illustrator. /owner puts a window of a given size
 * at a given place on the desktop and hands it to the dialog as its parent,
 * which is what lets the placement be measured: where the About box opens is
 * decided entirely by that rectangle and the monitor it falls on, and none of
 * it needs a copy of Illustrator. /report writes down where the window
 * actually landed.
 *
 * Build it with tools/AboutHarness/build.cmd. It is a development tool and is
 * not part of the plugin.
 */

#include <windows.h>
#include <cstdio>
#include <cstring>
#include "FDPAbout.h"

namespace {

const wchar_t* const kDialogCaption = L"About FreeDistort+";

/* Reads /name:<value> out of the command line. Returns false when the switch
   is absent; an empty value counts as present. */
bool SwitchValue(LPCWSTR cmdLine, const wchar_t* name, wchar_t* out, size_t cch)
{
    out[0] = L'\0';
    if (cmdLine == nullptr) return false;
    const wchar_t* at = wcsstr(cmdLine, name);
    if (at == nullptr) return false;
    at += wcslen(name);
    size_t i = 0;
    // A value runs to the next space, so a path with spaces has to be quoted.
    if (*at == L'"') {
        ++at;
        while (*at != L'\0' && *at != L'"' && i + 1 < cch) out[i++] = *at++;
    } else {
        while (*at != L'\0' && *at != L' ' && i + 1 < cch) out[i++] = *at++;
    }
    out[i] = L'\0';
    return true;
}

LRESULT CALLBACK OwnerProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(w, m, wp, lp);
}

/* A stand-in for Illustrator's main window: the About box is centered on
   whatever it is given as an owner, so the only thing that matters about this
   window is its rectangle. */
HWND MakeOwner(HINSTANCE instance, int x, int y, int w, int h)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OwnerProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = L"FDPAboutHarnessOwner";
    RegisterClassExW(&wc);

    HWND owner = CreateWindowExW(0, L"FDPAboutHarnessOwner",
                                 L"Stand-in for Illustrator",
                                 WS_OVERLAPPEDWINDOW,
                                 x, y, w, h, nullptr, nullptr, instance, nullptr);
    if (owner != nullptr) {
        // SW_SHOWNA leaves the focus alone, so running this does not fight
        // whatever the person at the keyboard is doing.
        ShowWindow(owner, SW_SHOWNA);
        UpdateWindow(owner);
    }
    return owner;
}

struct ReportJob {
    wchar_t path[MAX_PATH];
    HWND    owner;
    DWORD   delayMs;
};

/* Writes one tab-separated line: where the dialog opened, where its owner is,
   the work area of the monitor it landed on, and whether it is wholly inside
   that work area. The last column is the whole question -- a dialog that is
   not entirely inside the work area is one that opened off the desktop or
   under the taskbar. */
void WriteReport(const ReportJob& job, HWND dlg)
{
    RECT d = { 0, 0, 0, 0 };
    RECT o = { 0, 0, 0, 0 };
    if (dlg != nullptr) GetWindowRect(dlg, &d);
    if (job.owner != nullptr) GetWindowRect(job.owner, &o);

    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromRect(&d, MONITOR_DEFAULTTONEAREST);
    if (mon == nullptr || GetMonitorInfoW(mon, &mi) == FALSE) {
        mi.rcWork.left = 0;
        mi.rcWork.top = 0;
        mi.rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        mi.rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    const bool inside = dlg != nullptr &&
                        d.left   >= mi.rcWork.left  &&
                        d.top    >= mi.rcWork.top   &&
                        d.right  <= mi.rcWork.right &&
                        d.bottom <= mi.rcWork.bottom;

    char line[512];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%s\n",
                d.left, d.top, d.right, d.bottom,
                o.left, o.top, o.right, o.bottom,
                mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom,
                dlg == nullptr ? "NODIALOG" : (inside ? "INSIDE" : "OUTSIDE"));

    HANDLE f = CreateFileW(job.path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        SetFilePointer(f, 0, nullptr, FILE_END);
        WriteFile(f, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(f);
    }
}

ReportJob gJob;

/* Waits for the dialog to exist, measures it, and closes it. Measuring has to
   happen from another thread: the dialog is modal, so the thread that opened
   it is inside DialogBoxParamW until the dialog goes away. */
DWORD WINAPI ReportAndClose(LPVOID)
{
    HWND dlg = nullptr;
    // Poll rather than sleep a fixed time: the dialog is up within a few
    // milliseconds, and waiting a fixed second for it is a second per case.
    for (int waited = 0; waited < 10000 && dlg == nullptr; waited += 25) {
        dlg = FindWindowW(nullptr, kDialogCaption);
        if (dlg == nullptr) Sleep(25);
    }
    // Let WM_INITDIALOG finish placing it before reading the rectangle back.
    Sleep(gJob.delayMs);
    if (gJob.path[0] != L'\0') WriteReport(gJob, dlg);
    if (dlg != nullptr) PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
    return 0;
}

} /* anonymous namespace */

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmdLine, int)
{
    /* /exit<ms> closes the dialog by itself, so a screenshot can be taken
       without anything hanging around waiting for a click. */
    int autoCloseMs = 0;
    if (cmdLine != nullptr && wcsstr(cmdLine, L"/exit") != nullptr)
        autoCloseMs = _wtoi(wcsstr(cmdLine, L"/exit") + 5);

    /* /dark shows it in Illustrator's darkest interface colours rather than the
       system's. The plugin reads the real numbers from the host; these are the
       ones Illustrator 30.7 reported at its darkest setting, hard-coded here so
       the dialog can be looked at in that theme without a copy of Illustrator
       to ask. That is the whole point of the harness. */
    FDPAboutTheme theme;
    if (cmdLine != nullptr && wcsstr(cmdLine, L"/dark") != nullptr) {
        theme.panel     = RGB(0x32, 0x32, 0x32);
        theme.panelText = RGB(0xCD, 0xCD, 0xCD);
        theme.band      = RGB(0x32, 0x32, 0x32);
        theme.bandText  = RGB(0xCD, 0xCD, 0xCD);
        theme.rule      = RGB(0x1E, 0x1E, 0x1E);
        theme.link      = RGB(0x5A, 0xA9, 0xE6);
        theme.ownerDrawButton = true;
        theme.button       = RGB(0x46, 0x46, 0x46);
        theme.buttonText   = RGB(0xCD, 0xCD, 0xCD);
        theme.buttonBorder = RGB(0x1E, 0x1E, 0x1E);
        theme.darkTitleBar = true;
    }

    /* /owner:x,y,w,h stands a window of that size at that place and hands it
       to the dialog as its parent. Negative coordinates are allowed and are
       the interesting ones: they put the stand-in partly off the desktop,
       which is the case the clamp exists for. */
    HWND owner = nullptr;
    wchar_t spec[128];
    if (SwitchValue(cmdLine, L"/owner:", spec, 128) && spec[0] != L'\0') {
        int x = 0, y = 0, w = 0, h = 0;
        if (swscanf_s(spec, L"%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0)
            owner = MakeOwner(instance, x, y, w, h);
    }

    /* /report:<path> appends where the dialog landed to a file, then closes
       it. It implies /exit, because a report nobody closed is a hang. */
    ZeroMemory(&gJob, sizeof(gJob));
    gJob.owner = owner;
    gJob.delayMs = autoCloseMs > 0 ? static_cast<DWORD>(autoCloseMs) : 400;
    const bool reporting = SwitchValue(cmdLine, L"/report:", gJob.path, MAX_PATH) &&
                           gJob.path[0] != L'\0';

    if (reporting) {
        CloseHandle(CreateThread(nullptr, 0, ReportAndClose, nullptr, 0, nullptr));
    } else if (autoCloseMs > 0) {
        struct Closer {
            static DWORD WINAPI Run(LPVOID ms) {
                Sleep(static_cast<DWORD>(reinterpret_cast<UINT_PTR>(ms)));
                HWND dlg = FindWindowW(nullptr, kDialogCaption);
                if (dlg != nullptr) PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
                return 0;
            }
        };
        CloseHandle(CreateThread(nullptr, 0, Closer::Run,
                    reinterpret_cast<LPVOID>(static_cast<UINT_PTR>(autoCloseMs)),
                    0, nullptr));
    }

    const bool shown = FDPShowAboutDialog(instance, owner, theme);

    if (owner != nullptr) DestroyWindow(owner);

    if (!shown) {
        if (reporting) return 1;
        MessageBoxW(nullptr, L"FDPShowAboutDialog returned false: the "
                             L"dialog could not be created.",
                    L"About harness", MB_ICONERROR | MB_OK);
        return 1;
    }
    return 0;
}
