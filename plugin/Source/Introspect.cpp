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

//  Introspect.cpp -- see Introspect.h.

#include "IllustratorSDK.h"
#include "Introspect.h"
#include "FDPSuites.h"

#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace
{
    std::string Indent(int depth) { return std::string(static_cast<size_t>(depth) * 2, ' '); }

    std::string Real(double v)
    {
        std::ostringstream o;
        o << std::setprecision(12) << v;
        return o.str();
    }

    const char* EntryTypeName(AIEntryType t)
    {
        switch (t)
        {
            case IntegerType:       return "Integer";
            case BooleanType:       return "Boolean";
            case RealType:          return "Real";
            case StringType:        return "String";
            case DictType:          return "Dict";
            case ArrayType:         return "Array";
            case BinaryType:        return "Binary";
            case PointType:         return "Point";
            case MatrixType:        return "Matrix";
            case UnicodeStringType: return "UnicodeString";
            case UIDType:           return "UID";
            case UIDREFType:        return "UIDREF";
            case ArtworkPointType:  return "ArtworkPoint";
            default:                return "Other";
        }
    }

    const char* ArtTypeName(short type)
    {
        switch (type)
        {
            case kPathArt:          return "Path";
            case kGroupArt:         return "Group";
            case kCompoundPathArt:  return "CompoundPath";
            case kTextFrameArt:     return "TextFrame";
            case kPlacedArt:        return "Placed";
            case kRasterArt:        return "Raster";
            case kPluginArt:        return "PluginArt";
            case kMeshArt:          return "Mesh";
            case kSymbolArt:        return "Symbol";
            case kForeignArt:       return "Foreign";
            case kLegacyTextArt:    return "LegacyText";
            case kChartArt:         return "Chart";
            default:                return "Other";
        }
    }

    void DumpDictionary(std::ostringstream& out, ConstAIDictionaryRef dict, int depth)
    {
        if (dict == nullptr) { out << Indent(depth) << "<null dictionary>\n"; return; }
        if (depth > 8) { out << Indent(depth) << "<recursion limit>\n"; return; }

        out << Indent(depth) << "# " << sAIDictionary->Size(dict) << " entries\n";
        AIDictionaryIterator iter = nullptr;
        if (sAIDictionary->Begin(dict, &iter) || iter == nullptr)
        {
            out << Indent(depth) << "<cannot iterate>\n";
            return;
        }
        for (; !sAIDictionaryIterator->AtEnd(iter); sAIDictionaryIterator->Next(iter))
        {
            const AIDictKey key = sAIDictionaryIterator->GetKey(iter);
            const char* keyName = sAIDictionary->GetKeyString(key);
            AIEntryType type = UnknownType;
            sAIDictionary->GetEntryType(dict, key, &type);
            out << Indent(depth) << (keyName ? keyName : "<null key>") << " (" << EntryTypeName(type) << ") = ";
            switch (type)
            {
                case IntegerType: { ai::int32 v = 0; sAIDictionary->GetIntegerEntry(dict, key, &v); out << v; break; }
                case BooleanType: { AIBoolean v = false; sAIDictionary->GetBooleanEntry(dict, key, &v); out << (v ? "true" : "false"); break; }
                case RealType:    { AIReal v = 0; sAIDictionary->GetRealEntry(dict, key, &v); out << Real(v); break; }
                case StringType:  { const char* v = nullptr; sAIDictionary->GetStringEntry(dict, key, &v); out << "\"" << (v ? v : "") << "\""; break; }
                case UnicodeStringType: { ai::UnicodeString v; sAIDictionary->GetUnicodeStringEntry(dict, key, v); out << "\"" << v.as_UTF8() << "\""; break; }
                case DictType:
                {
                    AIDictionaryRef sub = nullptr;
                    if (!sAIDictionary->GetDictEntry(dict, key, &sub) && sub)
                    {
                        out << "{\n";
                        DumpDictionary(out, sub, depth + 1);
                        out << Indent(depth) << "}";
                        sAIDictionary->Release(sub);
                    }
                    break;
                }
                default: out << "<not expanded>"; break;
            }
            out << "\n";
        }
        sAIDictionaryIterator->Release(iter);
    }

    void DumpEffect(std::ostringstream& out, AIParserLiveEffect effect, ai::int32 index, int depth)
    {
        const char* name = nullptr;
        ai::int32 major = 0, minor = 0;
        sAIArtStyleParser->GetLiveEffectNameAndVersion(effect, &name, &major, &minor);
        AIBoolean visible = true;
        sAIArtStyleParser->GetEffectVisible(effect, &visible);
        out << Indent(depth) << "[" << index << "] \"" << (name ? name : "?") << "\" v"
            << major << "." << minor << (visible ? "" : " (hidden)") << "\n";
        AILiveEffectParameters params = nullptr;
        if (!sAIArtStyleParser->GetLiveEffectParams(effect, &params))
            DumpDictionary(out, params, depth + 1);
    }

    void DumpArtStyle(std::ostringstream& out, AIArtStyleHandle style)
    {
        AIStyleParser parser = nullptr;
        if (sAIArtStyleParser->NewParser(&parser) || parser == nullptr) { out << "  <cannot create style parser>\n"; return; }
        if (sAIArtStyleParser->ParseStyle(parser, style))
        {
            out << "  <cannot parse style>\n";
            sAIArtStyleParser->DisposeParser(parser);
            return;
        }

        ai::int32 n = sAIArtStyleParser->CountPreEffects(parser);
        out << "  pre-effects: " << n << "\n";
        for (ai::int32 i = 0; i < n; ++i)
        {
            AIParserLiveEffect pe = nullptr;
            if (!sAIArtStyleParser->GetNthPreEffect(parser, i, &pe) && pe) DumpEffect(out, pe, i, 2);
        }

        n = sAIArtStyleParser->CountPaintFields(parser);
        out << "  paint fields: " << n << "\n";
        for (ai::int32 i = 0; i < n; ++i)
        {
            AIParserPaintField pf = nullptr;
            if (sAIArtStyleParser->GetNthPaintField(parser, i, &pf) || pf == nullptr) continue;
            const ai::int32 fx = sAIArtStyleParser->CountEffectsOfPaintField(pf);
            out << "    [" << i << "] " << (sAIArtStyleParser->IsFill(pf) ? "fill" : "stroke")
                << ", nested effects: " << fx << "\n";
            for (ai::int32 j = 0; j < fx; ++j)
            {
                AIParserLiveEffect pe = nullptr;
                if (!sAIArtStyleParser->GetNthEffectOfPaintField(pf, j, &pe) && pe) DumpEffect(out, pe, j, 4);
            }
        }

        n = sAIArtStyleParser->CountPostEffects(parser);
        out << "  post-effects: " << n << "\n";
        for (ai::int32 i = 0; i < n; ++i)
        {
            AIParserLiveEffect pe = nullptr;
            if (!sAIArtStyleParser->GetNthPostEffect(parser, i, &pe) && pe) DumpEffect(out, pe, i, 2);
        }
        sAIArtStyleParser->DisposeParser(parser);
    }

    /** Rebuilds one object's style with the nth post-effect removed or moved. */
    std::string RestackOne(AIArtHandle art, ai::int32 from, ai::int32 to, bool remove)
    {
        std::ostringstream out;
        AIArtStyleHandle style = nullptr;
        if (sAIArtStyle->GetArtStyle(art, &style) || style == nullptr) return "  no art style\n";
        AIStyleParser parser = nullptr;
        if (sAIArtStyleParser->NewParser(&parser) || parser == nullptr) return "  cannot create parser\n";
        if (sAIArtStyleParser->ParseStyle(parser, style))
        {
            sAIArtStyleParser->DisposeParser(parser);
            return "  cannot parse style\n";
        }
        const ai::int32 n = sAIArtStyleParser->CountPostEffects(parser);
        if (from < 0 || from >= n || (!remove && (to < 0 || to > n - 1)))
        {
            sAIArtStyleParser->DisposeParser(parser);
            out << "  index out of range (" << n << " post-effects)\n";
            return out.str();
        }
        AIParserLiveEffect effect = nullptr;
        ASErr err = sAIArtStyleParser->GetNthPostEffect(parser, from, &effect);
        if (!err && remove)
        {
            err = sAIArtStyleParser->RemovePostEffect(parser, effect, true);
        }
        else if (!err)
        {
            AIParserLiveEffect clone = nullptr;
            err = sAIArtStyleParser->CloneLiveEffect(effect, &clone);
            if (!err && clone)
            {
                err = sAIArtStyleParser->RemovePostEffect(parser, effect, true);
                if (!err) err = sAIArtStyleParser->InsertNthPostEffect(parser, to, clone);
                if (err) sAIArtStyleParser->DisposeParserLiveEffect(clone);
            }
        }
        if (!err)
        {
            AIArtStyleHandle newStyle = nullptr;
            err = sAIArtStyleParser->CreateNewStyle(parser, &newStyle);
            if (!err && newStyle) err = sAIArtStyle->SetArtStyle(art, newStyle);
        }
        sAIArtStyleParser->DisposeParser(parser);
        out << "  " << (remove ? "removed " : "moved ") << from;
        if (!remove) out << " to " << to;
        out << " (result " << err << ")\n";
        return out.str();
    }
}

namespace introspect
{

std::vector<AIArtHandle> SelectedTopLevelArt()
{
    std::vector<AIArtHandle> keep;
    AIArtHandle** selected = nullptr;
    ai::int32 count = 0;
    if (sAIMatchingArt->GetSelectedArt(&selected, &count) || selected == nullptr) return keep;

    auto inSelection = [&](AIArtHandle art) {
        for (ai::int32 i = 0; i < count; ++i)
            if ((*selected)[i] == art) return true;
        return false;
    };
    for (ai::int32 i = 0; i < count; ++i)
    {
        const AIArtHandle art = (*selected)[i];
        AIArtHandle parent = nullptr;
        // Only the layer's own container has no parent; it is not a target.
        if (sAIArt->GetArtParent(art, &parent) || parent == nullptr) continue;
        bool covered = false;
        for (AIArtHandle a = parent; a != nullptr; )
        {
            AIArtHandle next = nullptr;
            if (sAIArt->GetArtParent(a, &next)) break;
            // The layer container is in the set too and is everybody's
            // ancestor, so it must not count as covering.
            if (next != nullptr && inSelection(a)) { covered = true; break; }
            a = next;
        }
        if (!covered) keep.push_back(art);
    }
    sAIMdMemory->MdMemoryDisposeHandle(reinterpret_cast<AIMdMemoryHandle>(selected));
    return keep;
}

std::string DumpLiveEffectRegistry()
{
    std::ostringstream out;
    ai::int32 count = 0;
    if (sAILiveEffect->CountLiveEffects(&count)) return "CountLiveEffects failed\n";
    out << "idx\tname\ttitle\tversion\tinputPrefs\tstyleFlags\n";
    for (ai::int32 i = 0; i < count; ++i)
    {
        AILiveEffectHandle effect = nullptr;
        if (sAILiveEffect->GetNthLiveEffect(i, &effect) || effect == nullptr) continue;
        const char* name = nullptr;
        const char* title = nullptr;
        ai::int32 major = 0, minor = 0, input = 0, flags = 0;
        sAILiveEffect->GetLiveEffectName(effect, &name);
        sAILiveEffect->GetLiveEffectTitle(effect, &title);
        sAILiveEffect->GetLiveEffectVersion(effect, &major, &minor);
        sAILiveEffect->GetInputPreference(effect, &input);
        sAILiveEffect->GetStyleFilterFlags(effect, &flags);
        out << i << "\t" << (name ? name : "?") << "\t" << (title ? title : "?") << "\t"
            << major << "." << minor << "\t0x" << std::hex << input << "\t0x" << flags << std::dec << "\n";
    }
    return out.str();
}

std::string DumpSelectionAppearance()
{
    std::ostringstream out;
    const std::vector<AIArtHandle> arts = SelectedTopLevelArt();
    if (arts.empty()) return "No selection.\n";
    out << "Selected objects: " << arts.size() << "\n";
    for (size_t i = 0; i < arts.size(); ++i)
    {
        short type = kUnknownArt;
        sAIArt->GetArtType(arts[i], &type);
        ai::UnicodeString name;
        ASBoolean isDefault = false;
        sAIArt->GetArtName(arts[i], name, &isDefault);
        out << "\n[" << i << "] " << ArtTypeName(type) << " \"" << name.as_UTF8() << "\"\n";
        AIArtStyleHandle style = nullptr;
        if (sAIArtStyle->GetArtStyle(arts[i], &style) || style == nullptr)
        {
            out << "  <no art style>\n";
            continue;
        }
        DumpArtStyle(out, style);
    }
    return out.str();
}

std::string DumpSelectionGeometry()
{
    std::ostringstream out;
    const std::vector<AIArtHandle> arts = SelectedTopLevelArt();
    if (arts.empty()) return "No selection.\n";
    for (size_t i = 0; i < arts.size(); ++i)
    {
        short type = kUnknownArt;
        sAIArt->GetArtType(arts[i], &type);
        out << "[" << i << "] " << ArtTypeName(type) << "\n";
        AIRealRect g = { 0, 0, 0, 0 };
        if (!sAIArt->GetArtTransformBounds(arts[i], nullptr, kVisibleBounds | kNoStrokeBounds | kNoExtendedBounds, &g))
            out << "  geometric bounds\t" << Real(g.left) << "\t" << Real(g.top) << "\t" << Real(g.right) << "\t" << Real(g.bottom) << "\n";
        AIRealRect v = { 0, 0, 0, 0 };
        if (!sAIArt->GetArtBounds(arts[i], &v))
            out << "  visible bounds\t" << Real(v.left) << "\t" << Real(v.top) << "\t" << Real(v.right) << "\t" << Real(v.bottom) << "\n";
        if (type != kPathArt) continue;
        ai::int16 segs = 0;
        sAIPath->GetPathSegmentCount(arts[i], &segs);
        for (ai::int16 s = 0; s < segs; ++s)
        {
            AIPathSegment seg;
            if (sAIPath->GetPathSegments(arts[i], s, 1, &seg)) continue;
            out << "  seg\t" << s << "\t" << Real(seg.p.h) << "\t" << Real(seg.p.v) << "\t"
                << Real(seg.in.h) << "\t" << Real(seg.in.v) << "\t" << Real(seg.out.h) << "\t" << Real(seg.out.v) << "\n";
        }
    }
    return out.str();
}

std::string ApplyEffectByName(const std::string& effectName, const std::string& paramSpec)
{
    std::ostringstream out;
    AILiveEffectHandle effect = nullptr;
    if (sAILiveEffect->GetLiveEffectHandleByName(effectName.c_str(), &effect) || effect == nullptr)
        return "No effect named \"" + effectName + "\".\n";

    AILiveEffectParameters params = nullptr;
    if (sAILiveEffect->CreateLiveEffectParameters(&params) || params == nullptr)
        return "CreateLiveEffectParameters failed.\n";

    size_t pos = 0;
    while (pos < paramSpec.size())
    {
        size_t semi = paramSpec.find(';', pos);
        if (semi == std::string::npos) semi = paramSpec.size();
        const std::string pair = paramSpec.substr(pos, semi - pos);
        pos = semi + 1;
        const size_t eq = pair.find('=');
        if (eq == std::string::npos || eq + 2 >= pair.size()) continue;
        const AIDictKey key = sAIDictionary->Key(pair.substr(0, eq).c_str());
        const char tag = pair[eq + 1];
        const std::string val = pair.substr(eq + 3);
        switch (tag)
        {
            case 'r': sAIDictionary->SetRealEntry(params, key, static_cast<AIReal>(std::atof(val.c_str()))); break;
            case 'i': sAIDictionary->SetIntegerEntry(params, key, std::atoi(val.c_str())); break;
            case 'b': sAIDictionary->SetBooleanEntry(params, key, val == "true" || val == "1"); break;
            case 's': sAIDictionary->SetStringEntry(params, key, val.c_str()); break;
            default: break;
        }
    }

    const std::vector<AIArtHandle> arts = SelectedTopLevelArt();
    int applied = 0;
    for (AIArtHandle art : arts)
    {
        AIArtStyleHandle style = nullptr;
        if (sAIArtStyle->GetArtStyle(art, &style)) continue;
        if (style == nullptr)
        {
            AIStyleParser parser = nullptr;
            if (!sAIArtStyleParser->NewParser(&parser) && parser)
            {
                sAIArtStyleParser->CreateNewStyle(parser, &style);
                sAIArtStyleParser->DisposeParser(parser);
            }
            if (style == nullptr) continue;
        }
        AIArtStyleHandle merged = nullptr;
        if (sAILiveEffect->NewArtStyleByMergingLiveEffect(style, effect, params, kAppendLiveEffectToStyle, &merged) || merged == nullptr)
            continue;
        if (!sAIArtStyle->SetArtStyle(art, merged)) ++applied;
    }
    sAIDictionary->Release(params);
    out << "Applied \"" << effectName << "\" to " << applied << " of " << arts.size() << " objects.\n";
    return out.str();
}

std::string EditEffectParameter(ai::int32 effectIndex, const std::string& keyName,
                                const std::string& type, const std::string& value, bool deleteKey)
{
    std::ostringstream out;
    const std::vector<AIArtHandle> arts = SelectedTopLevelArt();
    if (arts.empty()) return "No selection.\n";
    for (AIArtHandle art : arts)
    {
        AIArtStyleHandle style = nullptr;
        if (sAIArtStyle->GetArtStyle(art, &style) || style == nullptr) continue;
        AIStyleParser parser = nullptr;
        if (sAIArtStyleParser->NewParser(&parser) || parser == nullptr) continue;
        AIParserLiveEffect pe = nullptr;
        AILiveEffectParameters current = nullptr;
        ASErr err = sAIArtStyleParser->ParseStyle(parser, style);
        if (!err && (effectIndex < 0 || effectIndex >= sAIArtStyleParser->CountPostEffects(parser))) err = kBadParameterErr;
        if (!err) err = sAIArtStyleParser->GetNthPostEffect(parser, effectIndex, &pe);
        if (!err) err = sAIArtStyleParser->GetLiveEffectParams(pe, &current);
        AILiveEffectParameters fresh = nullptr;
        if (!err) err = sAILiveEffect->CreateLiveEffectParameters(&fresh);
        if (!err && current) err = sAIDictionary->Copy(fresh, current);
        if (!err)
        {
            const AIDictKey key = sAIDictionary->Key(keyName.c_str());
            if (deleteKey) err = sAIDictionary->DeleteEntry(fresh, key);
            else if (type == "real") err = sAIDictionary->SetRealEntry(fresh, key, static_cast<AIReal>(std::atof(value.c_str())));
            else if (type == "int") err = sAIDictionary->SetIntegerEntry(fresh, key, std::atoi(value.c_str()));
            else if (type == "bool") err = sAIDictionary->SetBooleanEntry(fresh, key, value == "true" || value == "1");
            else if (type == "string") err = sAIDictionary->SetStringEntry(fresh, key, value.c_str());
            else err = kBadParameterErr;
        }
        bool parserHoldsReference = false;
        if (!err)
        {
            err = sAIArtStyleParser->SetLiveEffectParams(pe, fresh);
            // As fd::Write does: ask the dictionary whether the parser took a
            // reference, and release ours only if it did.
            const ai::int32 countWithProbe = sAIDictionary->AddRef(fresh);
            sAIDictionary->Release(fresh);
            parserHoldsReference = countWithProbe >= 3;
        }
        else if (fresh != nullptr)
        {
            sAIDictionary->Release(fresh);
            fresh = nullptr;
        }
        AIArtStyleHandle newStyle = nullptr;
        if (!err) err = sAIArtStyleParser->CreateNewStyle(parser, &newStyle);
        if (!err && newStyle) err = sAIArtStyle->SetArtStyle(art, newStyle);
        if (parserHoldsReference) sAIDictionary->Release(fresh);
        sAIArtStyleParser->DisposeParser(parser);
        out << "  " << (deleteKey ? "deleted " : "set ") << keyName << " on post-effect " << effectIndex
            << " (result " << err << ")\n";
    }
    return out.str();
}

std::string EditEffect(ai::int32 effectIndex)
{
    std::ostringstream out;
    const std::vector<AIArtHandle> arts = SelectedTopLevelArt();
    if (arts.empty()) return "No selection.\n";
    AIArtStyleHandle style = nullptr;
    if (sAIArtStyle->GetArtStyle(arts[0], &style) || style == nullptr) return "No art style.\n";
    AIStyleParser parser = nullptr;
    if (sAIArtStyleParser->NewParser(&parser) || parser == nullptr) return "Cannot create parser.\n";
    if (sAIArtStyleParser->ParseStyle(parser, style) || effectIndex < 0 ||
        effectIndex >= sAIArtStyleParser->CountPostEffects(parser))
    {
        sAIArtStyleParser->DisposeParser(parser);
        return "No such post-effect.\n";
    }
    AIParserLiveEffect pe = nullptr;
    sAIArtStyleParser->GetNthPostEffect(parser, effectIndex, &pe);
    const ASErr err = sAIArtStyleParser->EditEffectParameters(style, pe);
    sAIArtStyleParser->DisposeParser(parser);
    out << "EditEffectParameters on post-effect " << effectIndex << " returned " << err << "\n";
    return out.str();
}

std::string RemoveEffect(ai::int32 index)
{
    std::string out;
    for (AIArtHandle art : SelectedTopLevelArt()) out += RestackOne(art, index, 0, true);
    return out.empty() ? "No selection.\n" : out;
}

std::string MoveEffect(ai::int32 from, ai::int32 to)
{
    std::string out;
    for (AIArtHandle art : SelectedTopLevelArt()) out += RestackOne(art, from, to, false);
    return out.empty() ? "No selection.\n" : out;
}

std::string DumpMenuGroups()
{
    std::ostringstream o;
    ai::int32 count = 0;
    if (sAIMenu->CountMenuGroups(&count)) return "CountMenuGroups failed\n";
    for (ai::int32 i = 0; i < count; ++i)
    {
        AIMenuGroup group = nullptr;
        const char* name = nullptr;
        if (!sAIMenu->GetNthMenuGroup(i, &group) && group && !sAIMenu->GetMenuGroupName(group, &name) && name)
            o << name << "\n";
    }
    return o.str();
}

} // namespace introspect
