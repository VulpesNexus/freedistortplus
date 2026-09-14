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

//  Introspect.h -- inspection of appearances and effect dictionaries, for the
//  test bridge. Carried over from LiveShear, where it was written, and trimmed
//  to what this plugin's probes use.

#ifndef __INTROSPECT_H__
#define __INTROSPECT_H__

#include "IllustratorSDK.h"
#include <string>
#include <vector>

namespace introspect
{
    /** The objects an appearance applies to: the selection with the layer
        container and the children of selected groups filtered out.
        AIMatchingArtSuite::GetSelectedArt returns a flattened hierarchy, and
        none of the ready-made matching specifications gives this set. */
    std::vector<AIArtHandle> SelectedTopLevelArt();

    /** Every effect registered with the running application. */
    std::string DumpLiveEffectRegistry();

    /** The full appearance of every selected object, with each effect's
        parameter dictionary dumped recursively. */
    std::string DumpSelectionAppearance();

    /** Anchor points and handles of every selected path, plus bounds. */
    std::string DumpSelectionGeometry();

    /** Appends a registered live effect by unique name to every selected
        object, with parameters from `paramSpec` (key=r:1.5;key=b:true;...). */
    std::string ApplyEffectByName(const std::string& effectName, const std::string& paramSpec);

    /** Sets (or deletes) one key in the nth post-effect's dictionary of every
        selected object, writing a fresh copy of the dictionary so objects that
        share a style are not changed together. */
    std::string EditEffectParameter(ai::int32 effectIndex, const std::string& key,
                                    const std::string& type, const std::string& value, bool deleteKey);

    /** Opens the nth post-effect's own editor on the first selected object,
        exactly as double-clicking it in the Appearance panel does. */
    std::string EditEffect(ai::int32 effectIndex);

    /** Deletes the nth post-effect of every selected object. */
    std::string RemoveEffect(ai::int32 index);

    /** Moves the nth post-effect of every selected object to another index. */
    std::string MoveEffect(ai::int32 from, ai::int32 to);

    /** Every menu group Illustrator holds, one name per line. */
    std::string DumpMenuGroups();
}

#endif // __INTROSPECT_H__
