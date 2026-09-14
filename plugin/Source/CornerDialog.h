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

//  CornerDialog.h -- numeric entry for the four corners of a Free Distort.
//
//  A modal dialog laid out the way the corners sit: top left and top right
//  above bottom left and bottom right, each with a horizontal and a vertical
//  field. The fields show either the corner's position, as Illustrator's
//  Transform panel shows positions, or its offset from the undistorted corner.
//
//  Nothing is written while a field is being typed in. A field takes effect
//  when it is left, or on Enter, and the preview then shows it on the artwork.
//  A field whose text was not edited keeps the corner's exact value, however
//  few decimals it displays, so opening the dialog and pressing OK changes
//  nothing. The host does every conversion and every write; this file only
//  runs the window.

#ifndef __CORNERDIALOG_H__
#define __CORNERDIALOG_H__

#include "QuadMath.h"

#include <string>

class CornerDialogHost
{
public:
    virtual ~CornerDialogHost() {}
    /** An artwork point as Illustrator's own panels show it: from the ruler
        origin, in the ruler's direction. */
    virtual fdmath::Pt ToDisplay(fdmath::Pt artwork) = 0;
    virtual fdmath::Pt FromDisplay(fdmath::Pt display) = 0;
    /** A length in points, as Illustrator formats one in the document's units. */
    virtual std::wstring FormatLength(double points) = 0;
    /** What the user typed, evaluated the way Illustrator's own fields
        evaluate it, in points. False when it is not a number. */
    virtual bool ParseLength(const std::wstring& text, double* points) = 0;
    /** Shows `quad` on the artwork; with nullptr, puts back what was there
        when the dialog opened. */
    virtual void Preview(const fdmath::Quad* quad) = 0;
};

/** Colors to draw in, as 0x00BBGGRR values. The host fills them from
    Illustrator's own dialog colors; left unset, the dialog uses the system's.
    A plain struct, so this file takes no Illustrator type and a harness can
    build it. */
struct CornerDialogColors
{
    bool set = false;
    bool dark = false;
    unsigned long background = 0, text = 0, editText = 0, editBackground = 0, border = 0, focusRing = 0;
    unsigned long control = 0, controlHot = 0, controlPressed = 0;
};

struct CornerDialogState
{
    /** The owner window (an HWND on Windows), or null. */
    void* owner = nullptr;
    CornerDialogColors colors;
    /** The quad Adobe draws into when the dialog opens, artwork coordinates. */
    fdmath::Quad start;
    /** The input bounds: where each corner is with no distortion. */
    fdmath::Rect bounds;
    /** On OK, the quad to commit. */
    fdmath::Quad result;
    /** The corner whose first field has the focus when the dialog opens. */
    int focusCorner = 0;
    /** Offsets rather than positions; kept by the caller between sessions. */
    bool offsets = false;
    bool preview = true;
    /** False only for a test, so the window does not take the foreground
        from whoever is using the machine. */
    bool activate = true;
};

/** Runs the dialog. True when it was closed with OK. */
bool RunCornerDialog(CornerDialogHost& host, CornerDialogState& state);

/** Unregisters the window class; call at plugin shutdown. */
void ShutdownCornerDialog();

#endif // __CORNERDIALOG_H__
