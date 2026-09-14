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

#ifndef __FDPABOUT_H__
#define __FDPABOUT_H__

#ifdef WIN_ENV

#include <windows.h>

/** The colors to draw the About dialog in.

    Passed in rather than read here, which is the whole point: Illustrator
    knows what it is painting its own dialogs with, and this file must not know
    what Illustrator is. A plain struct of COLORREFs crosses that line without
    dragging the SDK across it, so the dialog can be built into a small harness
    and looked at directly -- in either theme -- instead of being inspected
    only through the host, which for a modal dialog means not at all.

    Defaults are the system colors, which is what a plugin looks like before it
    can be told otherwise, and what it falls back to on a host that declines to
    say. */
struct FDPAboutTheme
{
    COLORREF panel;      /**< The upper band, behind the title and the prose. */
    COLORREF panelText;
    COLORREF band;       /**< The lower band, behind the button and the notices. */
    COLORREF bandText;
    COLORREF rule;       /**< The line dividing them. */
    COLORREF link;       /**< Link text, which a SysLink otherwise picks itself. */

    /** The OK button. A stock button is painted by the visual styles and no
        message recolors it, so on a dark panel it stays a white slab. When
        this is true the dialog paints it in the colors below instead. */
    bool     ownerDrawButton;
    COLORREF button;
    COLORREF buttonText;
    COLORREF buttonBorder;

    /** Ask the window manager for a dark caption. Without it a dark dialog
        wears a white title bar, which looks worse than not theming it. */
    bool     darkTitleBar;

    FDPAboutTheme()
        : panel(GetSysColor(COLOR_WINDOW)),
          panelText(GetSysColor(COLOR_WINDOWTEXT)),
          band(GetSysColor(COLOR_BTNFACE)),
          bandText(GetSysColor(COLOR_BTNTEXT)),
          rule(GetSysColor(COLOR_3DLIGHT)),
          link(GetSysColor(COLOR_HOTLIGHT)),
          ownerDrawButton(false),
          button(GetSysColor(COLOR_BTNFACE)),
          buttonText(GetSysColor(COLOR_BTNTEXT)),
          buttonBorder(GetSysColor(COLOR_3DSHADOW)),
          darkTitleBar(false) {}
};

/** Shows the About dialog.

    @param  instance  the module holding the dialog resource.
    @param  parent    owner window, may be null.
    @param  theme     colors to draw in; omit for the system's own.
    @return false if the dialog could not be shown, so the caller can fall back
            to something plainer. */
bool FDPShowAboutDialog(HINSTANCE instance, HWND parent,
                          const FDPAboutTheme& theme = FDPAboutTheme());

#endif /* WIN_ENV */

#endif /* __FDPABOUT_H__ */
