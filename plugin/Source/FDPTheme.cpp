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

//  FDPTheme.cpp -- reading Illustrator's own dialog colors.

#include "IllustratorSDK.h"
#include "FDPTheme.h"
#include "FDPSuites.h"

#ifdef WIN_ENV

namespace
{
    /** The suite hands back components as reals from zero to one. */
    BYTE Channel(AIReal v)
    {
        const double scaled = static_cast<double>(v) * 255.0 + 0.5;
        if (scaled <= 0.0) return 0;
        if (scaled >= 255.0) return 255;
        return static_cast<BYTE>(scaled);
    }

    COLORREF ToColorRef(const AIUIThemeColor& c)
    {
        return RGB(Channel(c.red), Channel(c.green), Channel(c.blue));
    }

    /** Moves a color towards white or towards black by a fixed number of
        levels, in the direction the theme says, so a button face stays
        distinguishable at every brightness. */
    COLORREF Shift(COLORREF base, int amount, bool lighter)
    {
        auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
        const int delta = lighter ? amount : -amount;
        return RGB(clamp(GetRValue(base) + delta), clamp(GetGValue(base) + delta), clamp(GetBValue(base) + delta));
    }

    COLORREF Blend(COLORREF a, COLORREF b, double t)
    {
        const double s = 1.0 - t;
        return RGB(
            static_cast<BYTE>(GetRValue(a) * s + GetRValue(b) * t + 0.5),
            static_cast<BYTE>(GetGValue(a) * s + GetGValue(b) * t + 0.5),
            static_cast<BYTE>(GetBValue(a) * s + GetBValue(b) * t + 0.5));
    }

    fdptheme::Theme FromSystem()
    {
        fdptheme::Theme t;
        t.background = GetSysColor(COLOR_BTNFACE);
        t.text = GetSysColor(COLOR_BTNTEXT);
        t.editText = GetSysColor(COLOR_WINDOWTEXT);
        t.editBackground = GetSysColor(COLOR_WINDOW);
        t.border = GetSysColor(COLOR_3DSHADOW);
        t.focusRing = GetSysColor(COLOR_HOTLIGHT);
        t.control = GetSysColor(COLOR_BTNFACE);
        t.controlHot = Shift(t.control, 12, true);
        t.controlPressed = Shift(t.control, 12, false);
        t.disabledText = GetSysColor(COLOR_GRAYTEXT);
        return t;
    }
}

namespace fdptheme
{
    Theme Read()
    {
        AIUIThemeSuite* const suite = sAIUITheme;
        if (suite == nullptr || suite->GetUIThemeColor == nullptr) return FromSystem();

        Theme t;
        struct Wanted { AIUIComponentColor which; COLORREF* into; };
        const Wanted wanted[] = {
            { kAIUIComponentColorBackground,         &t.background },
            { kAIUIComponentColorText,               &t.text },
            { kAIUIComponentColorEditText,           &t.editText },
            { kAIUIComponentColorEditTextBackground, &t.editBackground },
            { kAIUIComponentColorBorder,             &t.border },
            { kAIUIComponentColorFocusRing,          &t.focusRing },
        };
        for (const Wanted& w : wanted)
        {
            AIUIThemeColor c;
            if (suite->GetUIThemeColor(kAIUIThemeSelectorDialog, w.which, c) != kNoErr) return FromSystem();
            *w.into = ToColorRef(c);
        }
        if (suite->IsUIThemeDark != nullptr) t.dark = suite->IsUIThemeDark() ? true : false;

        t.fromHost = true;
        t.control = Shift(t.background, 14, t.dark);
        t.controlHot = Shift(t.background, 26, t.dark);
        t.controlPressed = Shift(t.background, 8, !t.dark);
        t.disabledText = Blend(t.text, t.background, 0.55);
        return t;
    }

    void ApplyTitleBar(HWND hwnd, bool dark)
    {
        typedef HRESULT (WINAPI *SetAttrProc)(HWND, DWORD, LPCVOID, DWORD);
        // Loaded by name rather than linked, so the plugin depends on nothing a
        // machine that can start Illustrator does not already have.
        const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm == nullptr) return;
        const SetAttrProc setAttr = reinterpret_cast<SetAttrProc>(
            reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
        if (setAttr != nullptr)
        {
            const BOOL on = dark ? TRUE : FALSE;
            // 20 is the documented attribute; Windows 10 builds 17763 to 18985
            // used 19 for the same thing.
            setAttr(hwnd, 20, &on, sizeof(on));
            setAttr(hwnd, 19, &on, sizeof(on));
        }
        FreeLibrary(dwm);
    }
}

#endif // WIN_ENV
