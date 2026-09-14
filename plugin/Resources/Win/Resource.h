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

#define IDD_FDP_ABOUT             101

// Deliberately apart from the identifiers CornerDialog.cpp gives its own
// controls, which start at 2000. Those live on a different window, so nothing
// would collide, but overlapping sets are a trap for whoever reads GetDlgItem.
#define IDC_ABOUT_TITLE                 1101
#define IDC_ABOUT_BODY                  1102
#define IDC_ABOUT_ATTRIB                1103
#define IDC_ABOUT_LEGAL                 1104

// The row where the upper panel ends, in dialog units, so it follows the shell
// font and the monitor's scaling like the rest of the layout. The dialog
// procedure paints the two bands and the rule between them at this line.
#define kAboutSplitDlgY                 158
