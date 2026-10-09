/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AnimGraphCommandText.h"

#include <AzCore/Serialization/Locale.h>
#include <AzCore/std/string/string_view.h>

#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>

namespace AiCompanion::AnimGraphCommandText
{
    namespace
    {
        // The UUIDs are the AZ_RTTI ids in the installed engine headers
        // (EMotionFX/Source/Parameter/*.h, emfx-write-survey section 6); the
        // engine-facing code refuses any of them that is not also in
        // ParameterFactory::GetParameterTypes(), so a stale entry answers
        // validation_failed rather than reaching the factory. "Float" and
        // "Int" are not entries: they are aliases resolved in
        // FindParameterType (the abstract FloatParameter and IntParameter are
        // not creatable).
        constexpr ParameterType s_parameterTypes[] = {
            { "FloatSlider", "FloatSliderParameter", "{2ED6BBAF-5C82-4EAA-8678-B220667254F2}", ValueKind::Float, true },
            { "FloatSpinner", "FloatSpinnerParameter", "{AD3D4357-F965-42E7-BAC8-7F4FF7F25FD0}", ValueKind::Float, true },
            { "IntSlider", "IntSliderParameter", "{0AF8A855-06F1-4C3C-8860-88DF52095DD8}", ValueKind::Int, true },
            { "IntSpinner", "IntSpinnerParameter", "{3FFC78F6-B2CC-47D0-B453-4EFD358B1020}", ValueKind::Int, true },
            { "Bool", "BoolParameter", "{1057BEFA-09A8-4B13-93CD-614BACF18106}", ValueKind::Bool, false },
            { "Tag", "TagParameter", "{E952924C-8C3D-452E-9E5F-45776BB83F33}", ValueKind::Bool, false },
            { "String", "StringParameter", "{2ADFD165-B5F9-4C6F-977C-2879610B2445}", ValueKind::String, false },
            { "Vector2", "Vector2Parameter", "{4133BFD5-81F3-4CBC-AA9B-28CF0C1439E0}", ValueKind::Vector2, true },
            { "Vector3", "Vector3Parameter", "{E647B621-27DA-454E-A14F-45C65E2C7874}", ValueKind::Vector3, true },
            { "Vector3Gizmo", "Vector3GizmoParameter", "{67A19A92-14A1-4A45-AE3B-DF5A8AB62E68}", ValueKind::Vector3, true },
            { "Vector4", "Vector4Parameter", "{63D0D19F-DC97-4F56-9FE8-A5A4225E0850}", ValueKind::Vector4, true },
            { "Color", "ColorParameter", "{F6F59F14-0A81-4BA0-BEB5-E5DFEE6787A0}", ValueKind::Color, true },
            { "Rotation", "RotationParameter", "{D84302D2-2977-43DD-B953-F038222E65BF}", ValueKind::Rotation, true },
        };
        constexpr size_t s_parameterTypeCount = sizeof(s_parameterTypes) / sizeof(s_parameterTypes[0]);

        bool EqualsNoCase(AZStd::string_view a, AZStd::string_view b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i])))
                {
                    return false;
                }
            }
            return true;
        }

        bool StartsWith(AZStd::string_view text, AZStd::string_view prefix)
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        //! The four forbidden characters plus control characters; `what`
        //! names the thing for the reason text.
        bool HasForbiddenCharacter(const AZStd::string& text, const char* what, AZStd::string& outReason)
        {
            for (char c : text)
            {
                if (c == '"' || c == '%' || c == '{' || c == '}')
                {
                    outReason = AZStd::string::format("%s may not contain '\"', '%%', '{' or '}' (found '%c')", what, c);
                    return true;
                }
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                {
                    outReason = AZStd::string::format("%s may not contain control characters", what);
                    return true;
                }
            }
            return false;
        }

        bool ReadNumbers(
            const rapidjson::Value& value, size_t minCount, size_t maxCount, AZStd::vector<double>& out, AZStd::string& outReason)
        {
            if (!value.IsArray() || value.Size() < minCount || value.Size() > maxCount)
            {
                outReason = minCount == maxCount ? AZStd::string::format("must be a JSON array of %zu numbers", minCount)
                                                 : AZStd::string::format("must be a JSON array of %zu or %zu numbers", minCount, maxCount);
                return false;
            }
            out.clear();
            for (rapidjson::SizeType i = 0; i < value.Size(); ++i)
            {
                if (!value[i].IsNumber())
                {
                    outReason = AZStd::string::format("element %u is not a number", i);
                    return false;
                }
                out.push_back(value[i].GetDouble());
            }
            return true;
        }

        //! Space-separated %.7f, the engine's own output format for vectors
        //! (MathScriptHelpers.cpp:126-153), parsed back with strtod.
        AZStd::string JoinFloats(const AZStd::vector<double>& values)
        {
            AZStd::string text;
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                {
                    text += ' ';
                }
                text += AZStd::string::format("%.7f", values[i]);
            }
            return text;
        }
    } // namespace

    bool IsValidObjectName(const AZStd::string& name, AZStd::string& outReason)
    {
        if (name.empty())
        {
            outReason = "name must not be empty";
            return false;
        }
        return !HasForbiddenCharacter(name, "name", outReason);
    }

    bool IsValidCommandText(const AZStd::string& text, AZStd::string& outReason)
    {
        return !HasForbiddenCharacter(text, "text", outReason);
    }

    AZStd::string Quoted(const AZStd::string& text)
    {
        return "\"" + text + "\"";
    }

    AZStd::string Braced(const AZStd::string& text)
    {
        return "{" + text + "}";
    }

    bool ParseObjectId(const AZStd::string& text, AZ::u64& outId)
    {
        if (text.empty() || text.size() > 20 || text.find_first_not_of("0123456789") != AZStd::string::npos)
        {
            return false;
        }
        char* end = nullptr;
        const unsigned long long parsed = strtoull(text.c_str(), &end, 10);
        if (end == text.c_str() || (end && *end != '\0') || parsed == 0 || parsed == ULLONG_MAX)
        {
            return false;
        }
        outId = static_cast<AZ::u64>(parsed);
        return true;
    }

    size_t ParameterTypeCount()
    {
        return s_parameterTypeCount;
    }

    const ParameterType& ParameterTypeAt(size_t index)
    {
        return s_parameterTypes[index < s_parameterTypeCount ? index : 0];
    }

    const ParameterType* FindParameterType(const AZStd::string& name)
    {
        // Float and Int are what the Animation Editor's parameter dialog calls
        // the slider variants; the spinner ones are named in full.
        AZStd::string_view wanted = name;
        if (EqualsNoCase(wanted, "Float") || EqualsNoCase(wanted, "FloatParameter"))
        {
            wanted = "FloatSlider";
        }
        else if (EqualsNoCase(wanted, "Int") || EqualsNoCase(wanted, "IntParameter"))
        {
            wanted = "IntSlider";
        }
        for (const ParameterType& type : s_parameterTypes)
        {
            if (EqualsNoCase(wanted, type.m_friendlyName) || EqualsNoCase(wanted, type.m_className))
            {
                return &type;
            }
        }
        return nullptr;
    }

    AZStd::string ParameterTypeNames()
    {
        AZStd::string names = "Float, Int";
        for (const ParameterType& type : s_parameterTypes)
        {
            names += ", ";
            names += type.m_friendlyName;
        }
        return names;
    }

    bool FormatParameterValue(const rapidjson::Value& value, ValueKind kind, AZStd::string& outText, AZStd::string& outReason)
    {
        // Every float goes out with '.' whatever the process locale (survey 0.5).
        AZ::Locale::ScopedSerializationLocale cLocale;
        AZStd::vector<double> numbers;
        switch (kind)
        {
        case ValueKind::Float:
            if (!value.IsNumber() || !std::isfinite(value.GetDouble()))
            {
                outReason = "must be a JSON number";
                return false;
            }
            outText = AZStd::string::format("%.9g", value.GetDouble());
            return true;
        case ValueKind::Int:
            if (!value.IsInt())
            {
                outReason = "must be a JSON integer within the 32-bit range";
                return false;
            }
            outText = AZStd::string::format("%d", value.GetInt());
            return true;
        case ValueKind::Bool:
            if (!value.IsBool())
            {
                outReason = "must be a JSON bool";
                return false;
            }
            // TextToData for bool is true only for the exact word "true".
            outText = value.GetBool() ? "true" : "false";
            return true;
        case ValueKind::String:
            if (!value.IsString())
            {
                outReason = "must be a JSON string";
                return false;
            }
            outText.assign(value.GetString(), value.GetStringLength());
            return IsValidCommandText(outText, outReason);
        case ValueKind::Vector2:
        case ValueKind::Vector3:
        case ValueKind::Vector4:
        case ValueKind::Rotation:
            {
                const size_t count = kind == ValueKind::Vector2 ? 2 : (kind == ValueKind::Vector3 ? 3 : 4);
                if (!ReadNumbers(value, count, count, numbers, outReason))
                {
                    return false;
                }
                outText = JoinFloats(numbers);
                return true;
            }
        case ValueKind::Color:
            if (!ReadNumbers(value, 3, 4, numbers, outReason))
            {
                return false;
            }
            if (numbers.size() == 3)
            {
                numbers.push_back(1.0);
            }
            outText = JoinFloats(numbers);
            return true;
        }
        outReason = "unsupported value kind";
        return false;
    }

    size_t FindNodeType(const AZStd::vector<NodeType>& types, const AZStd::string& name)
    {
        for (size_t i = 0; i < types.size(); ++i)
        {
            if (EqualsNoCase(name, types[i].m_rttiName) || EqualsNoCase(name, types[i].m_paletteName))
            {
                return i;
            }
        }
        return NoNodeType;
    }

    AZStd::string NodeTypeNames(const AZStd::vector<NodeType>& types)
    {
        AZStd::string names;
        for (const NodeType& type : types)
        {
            if (!names.empty())
            {
                names += ", ";
            }
            names += type.m_rttiName;
        }
        return names;
    }

    AZStd::string GeneratedNamePrefix(const AZStd::string& rttiName)
    {
        AZStd::string_view prefix = rttiName;
        if (StartsWith(prefix, "AnimGraph"))
        {
            prefix.remove_prefix(9);
        }
        else if (rttiName != "BlendTree" && StartsWith(prefix, "BlendTree"))
        {
            prefix.remove_prefix(9);
        }
        return prefix.empty() ? rttiName : AZStd::string(prefix);
    }

    AZStd::string CreateNodeCommand(
        AZ::u32 animGraphId,
        const AZStd::string& typeUuid,
        const AZStd::string& parentName,
        const AZStd::string& name,
        const AZStd::string& namePrefix,
        int xPos,
        int yPos)
    {
        AZStd::string command = AZStd::string::format(
            "AnimGraphCreateNode -animGraphID %u -type %s -parentName %s", animGraphId, typeUuid.c_str(), Quoted(parentName).c_str());
        if (name.empty())
        {
            command += " -name GENERATE -namePrefix " + Quoted(namePrefix);
        }
        else
        {
            command += " -name " + Quoted(name);
        }
        command += AZStd::string::format(" -xPos %d -yPos %d", xPos, yPos);
        return command;
    }

    AZStd::string RemoveNodeCommand(AZ::u32 animGraphId, const AZStd::string& nodeName)
    {
        return AZStd::string::format("AnimGraphRemoveNode -animGraphID %u -name %s", animGraphId, Quoted(nodeName).c_str());
    }

    AZStd::string SetEntryStateCommand(AZ::u32 animGraphId, const AZStd::string& nodeName)
    {
        return AZStd::string::format("AnimGraphSetEntryState -animGraphID %u -entryNodeName %s", animGraphId, Quoted(nodeName).c_str());
    }

    AZStd::string CreateParameterCommand(AZ::u32 animGraphId, const ParameterSpec& spec)
    {
        AZStd::string command = AZStd::string::format(
            "AnimGraphCreateParameter -animGraphID %u -type %s -name %s",
            animGraphId,
            spec.m_typeUuid.c_str(),
            Quoted(spec.m_name).c_str());
        if (spec.m_hasDefault)
        {
            command += " -defaultValue " + Braced(spec.m_defaultText);
        }
        if (spec.m_hasMin)
        {
            command += " -minValue " + Braced(spec.m_minText);
        }
        if (spec.m_hasMax)
        {
            command += " -maxValue " + Braced(spec.m_maxText);
        }
        if (spec.m_hasDescription)
        {
            command += " -description " + Braced(spec.m_description);
        }
        if (spec.m_hasGroup)
        {
            command += " -parent " + Quoted(spec.m_group);
        }
        return command;
    }

    AZStd::string AddGroupParameterCommand(AZ::u32 animGraphId, const AZStd::string& groupName)
    {
        return AZStd::string::format("AnimGraphAddGroupParameter -animGraphID %u -name %s", animGraphId, Quoted(groupName).c_str());
    }

    AZStd::string RemoveParameterCommand(AZ::u32 animGraphId, const AZStd::string& name)
    {
        return AZStd::string::format("AnimGraphRemoveParameter -animGraphID %u -name %s", animGraphId, Quoted(name).c_str());
    }

    AZStd::string LoadCommand(const AZStd::string& absolutePath)
    {
        return "LoadAnimGraph -filename " + Quoted(absolutePath);
    }

    AZStd::string SaveCommand(const AZStd::string& absolutePath, size_t managerIndex)
    {
        return AZStd::string::format(
            "SaveAnimGraph -filename %s -index %zu -sourceControl false", Quoted(absolutePath).c_str(), managerIndex);
    }

    AZ::IO::FixedMaxPath ResolveAgainstRoot(const AZStd::string& input, const AZ::IO::PathView& root)
    {
        AZ::IO::FixedMaxPath path(input.c_str());
        if (path.IsRelative())
        {
            AZ::IO::FixedMaxPath joined(root.Native());
            joined /= path;
            path = AZStd::move(joined);
        }
        return path.LexicallyNormal();
    }

    bool IsUnderRoot(const AZ::IO::PathView& path, const AZ::IO::PathView& root)
    {
        if (root.empty() || path.empty())
        {
            return false;
        }
        return path.IsRelativeTo(root);
    }
} // namespace AiCompanion::AnimGraphCommandText
