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

//  FDPID.h -- names and version numbers of the FreeDistort+ plugin.
//
//  This plugin writes nothing of its own into documents. Every identifier
//  below is a runtime name -- a tool, an annotator, a menu item, a notifier --
//  and none of them is ever saved. The one persistent name that matters is
//  Adobe's, kAdobeFreeDistortName, and this plugin only reads it.

#ifndef __FDPID_H__
#define __FDPID_H__

#define kFDPPluginName              "FreeDistortPlus"

#define kFDPProductName             "FreeDistort+"
#define kFDPCompanyName             "VulpesNexus"
#define kFDPCopyright               "Copyright (C) 2026 Vixen420. GPL-3.0-or-later, with an Adobe Illustrator SDK linking exception."
#define kFDPDescription             "An on-canvas editor for Adobe Illustrator's built-in Free Distort effect"
#define kFDPHomePage                "https://github.com/VulpesNexus"

#define kFDPVersionMajor            0
#define kFDPVersionMinor            1
#define kFDPVersionPatch            0
#define kFDPVersionString           "0.1.0-dev"

/** Adobe's effect. Its unique name, as the live effect registry reports it,
    and the keys of its parameter dictionary. Read from the host, never
    remembered: see docs/evidence/dictionary.txt. */
#define kAdobeFreeDistortName       "Adobe Free Distort"

/** Runtime identifiers. Illustrator keeps tool, annotator, and notifier names
    in namespaces shared with every other plugin and accepts duplicates
    silently, so each carries the publisher prefix. */
#define kFDPToolName                "VulpesNexus FreeDistort+ Tool"
#define kFDPToolTitle               "FreeDistort+"
#define kFDPToolTooltip             "FreeDistort+: drag the corners of the selected object's Free Distort effect"
#define kFDPAnnotatorName           "VulpesNexus FreeDistort+ Handles"
#define kFDPNotifierName            "VulpesNexus FreeDistort+"

#define kFDPMenuTitle               "FreeDistort+..."

#define kFDPAboutGroupName          "VulpesNexusAboutPluginsGroupName"
#define kFDPAboutGroupTitle         "About VulpesNexus Plug-ins"
#define kFDPAboutMenuTitle          "FreeDistort+..."

/** Resource identifiers of the tool's SVG icon; see Resources/raw/IDToFile.txt. */
#define kFDPToolIconResID           19500
#define kFDPToolIconDarkResID       19501

#endif // __FDPID_H__
