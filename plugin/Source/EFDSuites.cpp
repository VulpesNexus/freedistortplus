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

//  EFDSuites.cpp -- suite import table.

#include "IllustratorSDK.h"
#include "EFDSuites.h"

extern "C"
{
    AIMenuSuite*                sAIMenu = nullptr;
    AIUnicodeStringSuite*       sAIUnicodeString = nullptr;
    SPBlocksSuite*              sSPBlocks = nullptr;
    AILiveEffectSuite*          sAILiveEffect = nullptr;
    AIDictionarySuite*          sAIDictionary = nullptr;
    AIDictionaryIteratorSuite*  sAIDictionaryIterator = nullptr;
    AIEntrySuite*               sAIEntry = nullptr;
    AIArraySuite*               sAIArray = nullptr;
    AIArtSuite*                 sAIArt = nullptr;
    AIPathSuite*                sAIPath = nullptr;
    AIMatchingArtSuite*         sAIMatchingArt = nullptr;
    AIMdMemorySuite*            sAIMdMemory = nullptr;
    AIArtStyleSuite*            sAIArtStyle = nullptr;
    AIArtStyleParserSuite*      sAIArtStyleParser = nullptr;
    AIDocumentSuite*            sAIDocument = nullptr;
    AIDocumentViewSuite*        sAIDocumentView = nullptr;
    AIPreferenceSuite*          sAIPreference = nullptr;
    AIUndoSuite*                sAIUndo = nullptr;
    AIAnnotatorSuite*           sAIAnnotator = nullptr;
    AIAnnotatorDrawerSuite*     sAIAnnotatorDrawer = nullptr;
    AIToolSuite*                sAITool = nullptr;
    AICursorSnapSuite*          sAICursorSnap = nullptr;
    ASUserInteractionSuite*     sASUserInteraction = nullptr;
    AIUIThemeSuite*             sAIUITheme = nullptr;
};

ImportSuite gImportSuites[] =
{
    kAIMenuSuite,               kAIMenuSuiteVersion,            &sAIMenu,
    kAIUnicodeStringSuite,      kAIUnicodeStringSuiteVersion,   &sAIUnicodeString,
    kSPBlocksSuite,             kSPBlocksSuiteVersion,          &sSPBlocks,
    kAILiveEffectSuite,         kAILiveEffectVersion,           &sAILiveEffect,
    kAIDictionarySuite,         kAIDictionaryVersion,           &sAIDictionary,
    kAIDictionaryIteratorSuite, kAIDictionaryIteratorVersion,   &sAIDictionaryIterator,
    kAIEntrySuite,              kAIEntryVersion,                &sAIEntry,
    kAIArraySuite,              kAIArrayVersion,                &sAIArray,
    kAIArtSuite,                kAIArtVersion,                  &sAIArt,
    kAIPathSuite,               kAIPathSuiteVersion,            &sAIPath,
    kAIMatchingArtSuite,        kAIMatchingArtSuiteVersion,     &sAIMatchingArt,
    kAIMdMemorySuite,           kAIMdMemorySuiteVersion,        &sAIMdMemory,
    kAIArtStyleSuite,           kAIArtStyleVersion,             &sAIArtStyle,
    kAIArtStyleParserSuite,     kAIArtStyleParserVersion,       &sAIArtStyleParser,
    kAIDocumentSuite,           kAIDocumentVersion,             &sAIDocument,
    kAIDocumentViewSuite,       kAIDocumentViewVersion,         &sAIDocumentView,
    kAIPreferenceSuite,         kAIPreferenceVersion,           &sAIPreference,
    kAIUndoSuite,               kAIUndoVersion,                 &sAIUndo,
    kAIAnnotatorSuite,          kAIAnnotatorVersion,            &sAIAnnotator,
    kAIAnnotatorDrawerSuite,    kAIAnnotatorDrawerVersion,      &sAIAnnotatorDrawer,
    kAIToolSuite,               kAIToolVersion,                 &sAITool,
    kAICursorSnapSuite,         kAICursorSnapVersion,           &sAICursorSnap,

    nullptr,                    kStartOptionalSuites,           nullptr,
    kASUserInteractionSuite,    kASUserInteractionVersion,      &sASUserInteraction,
    kAIUIThemeSuite,            kAIUIThemeVersion,              &sAIUITheme,

    nullptr, kEndAllSuites, nullptr
};
