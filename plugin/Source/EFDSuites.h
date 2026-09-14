// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vixen420
//
// Enhanced Free Distort is free software: you may redistribute it and/or
// modify it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. It comes with ABSOLUTELY NO WARRANTY. See the
// file LICENSE, or <https://www.gnu.org/licenses/>, for the full text.
//
// Additional permission under GPL-3.0 section 7: this file may be combined with
// the Adobe Illustrator SDK, whose sample framework sources are compiled into
// every plugin built from it. See LICENSE-EXCEPTION.

//  EFDSuites.h -- the host suites this plugin uses.

#ifndef __EFDSUITES_H__
#define __EFDSUITES_H__

#include "IllustratorSDK.h"
#include "Suites.hpp"

#include "AILiveEffect.h"
#include "AIArtStyle.h"
#include "AIArtStyleParser.h"
#include "AIEntry.h"
#include "AIArray.h"
#include "AIDocument.h"
#include "AIDocumentView.h"
#include "AIArtSet.h"
#include "AIAnnotator.h"
#include "AIAnnotatorDrawer.h"
#include "AITool.h"
#include "AICursorSnap.h"
#include "AIUndo.h"
#include "AIUITheme.h"
#include "ASUserInteraction.h"

extern "C" AIMenuSuite*                 sAIMenu;
extern "C" AIUnicodeStringSuite*        sAIUnicodeString;
extern "C" SPBlocksSuite*               sSPBlocks;
extern "C" AILiveEffectSuite*           sAILiveEffect;
extern "C" AIDictionarySuite*           sAIDictionary;
extern "C" AIDictionaryIteratorSuite*   sAIDictionaryIterator;
extern "C" AIEntrySuite*                sAIEntry;
extern "C" AIArraySuite*                sAIArray;
extern "C" AIArtSuite*                  sAIArt;
extern "C" AIPathSuite*                 sAIPath;
extern "C" AIMatchingArtSuite*          sAIMatchingArt;
extern "C" AIMdMemorySuite*             sAIMdMemory;
extern "C" AIArtStyleSuite*             sAIArtStyle;
extern "C" AIArtStyleParserSuite*       sAIArtStyleParser;
extern "C" AIDocumentSuite*             sAIDocument;
extern "C" AIDocumentViewSuite*         sAIDocumentView;
extern "C" AIPreferenceSuite*           sAIPreference;
extern "C" AIUndoSuite*                 sAIUndo;
extern "C" AIAnnotatorSuite*            sAIAnnotator;
extern "C" AIAnnotatorDrawerSuite*      sAIAnnotatorDrawer;
extern "C" AIToolSuite*                 sAITool;
extern "C" AICursorSnapSuite*           sAICursorSnap;

// Optional: a host without them still loads the plugin. Without the user
// interaction suite, input bounds fall back to the art's geometric bounds;
// without the theme suite, colors fall back to fixed ones.
extern "C" ASUserInteractionSuite*      sASUserInteraction;
extern "C" AIUIThemeSuite*              sAIUITheme;

#endif // __EFDSUITES_H__
