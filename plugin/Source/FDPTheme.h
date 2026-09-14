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

//  FDPTheme.h -- the colors Illustrator is currently drawing its own dialogs
//  with. The same reading LiveShear's dialog uses.
//
//  A plain Win32 dialog is light gray with black text whatever the host looks
//  like, which is wrong three times out of four: Illustrator's interface has
//  four brightness settings and the darkest is the default. The host will say
//  what it is using, so the dialog asks instead of guessing, and asks about
//  dialogs specifically rather than panels, which are a different shade.

#ifndef __FDPTHEME_H__
#define __FDPTHEME_H__

#ifdef WIN_ENV

#include <windows.h>

namespace fdptheme
{
    struct Theme
    {
        /** False when the host could not be asked, in which case everything
            below came from the system colors instead. */
        bool fromHost = false;
        bool dark = false;

        // Straight from the host.
        COLORREF background = 0;
        COLORREF text = 0;
        COLORREF editText = 0;
        COLORREF editBackground = 0;
        COLORREF border = 0;
        COLORREF focusRing = 0;

        // Derived: the suite has no color for the face of a raised button.
        COLORREF control = 0;
        COLORREF controlHot = 0;
        COLORREF controlPressed = 0;
        COLORREF disabledText = 0;
    };

    /** Reads the theme now. Dialogs are modal, so it cannot change while one
        is open. */
    Theme Read();

    /** Asks the window manager to draw this window's title bar dark, on the
        Windows versions that can. */
    void ApplyTitleBar(HWND hwnd, bool dark);
}

#endif // WIN_ENV

#endif // __FDPTHEME_H__
