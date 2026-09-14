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

//  FDPPlugin.h -- plugin entry object.

#ifndef __FDPPLUGIN_H__
#define __FDPPLUGIN_H__

#include "IllustratorSDK.h"
#include "Plugin.hpp"
#include "AIScriptMessage.h"
#include "FDPID.h"
#include "DistortEditor.h"

class FDPPlugin : public Plugin
{
public:
    explicit FDPPlugin(SPPluginRef pluginRef);
    virtual ~FDPPlugin() {}

    FIXUP_VTABLE_EX(FDPPlugin, Plugin);

    ASErr Message(char* caller, char* selector, void* message) override;
    ASErr StartupPlugin(SPInterfaceMessage* message) override;
    ASErr PostStartupPlugin() override;
    ASErr ShutdownPlugin(SPInterfaceMessage* message) override;

    ASErr GoMenuItem(AIMenuMessage* message) override;
    ASErr Notify(AINotifierMessage* message) override;

    ASErr SelectTool(AIToolMessage* message) override;
    ASErr DeselectTool(AIToolMessage* message) override;
    ASErr ToolMouseDown(AIToolMessage* message) override;
    ASErr ToolMouseDrag(AIToolMessage* message) override;
    ASErr ToolMouseUp(AIToolMessage* message) override;

private:
    ASErr HandleScriptMessage(const char* selector, AIScriptMessage* message);
    std::string OpenEditor();

    AIMenuItemHandle fAboutMenu = nullptr;
    AIMenuItemHandle fEditorMenu = nullptr;
    std::string fEditorMenuPlacement;
    DistortEditor fEditor;
};

#endif // __FDPPLUGIN_H__
