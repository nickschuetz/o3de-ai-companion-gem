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

        // The condition classes' AZ_RTTI ids and reflected fields, from the
        // installed headers and the engine's Reflect functions (survey 4):
        // AnimGraphParameterCondition.cpp:575-583, AnimGraphTimeCondition.cpp
        // :231-236, AnimGraphPlayTimeCondition.cpp:298-302,
        // AnimGraphMotionCondition.cpp:511-517 (eventDatas left out),
        // AnimGraphStateCondition.cpp:473-477, AnimGraphTagCondition.cpp
        // :356-359, AnimGraphVector2Condition.cpp:393-399. The enums are
        // `enum X : AZ::u8` members reflected without an Enum<> entry, so
        // they serialize as "unsigned char" and take integer text; the names
        // are the engine's identifiers with their prefix stripped.
        constexpr EnumName s_parameterFunctionNames[] = {
            { "GREATER", 0 },  { "GREATEREQUAL", 1 }, { "LESS", 2 },    { "LESSEQUAL", 3 },
            { "NOTEQUAL", 4 }, { "EQUAL", 5 },        { "INRANGE", 6 }, { "NOTINRANGE", 7 },
        };
        constexpr EnumTable s_parameterFunction = { "FUNCTION_", s_parameterFunctionNames, 8 };

        constexpr EnumName s_stringFunctionNames[] = {
            { "EQUAL_CASESENSITIVE", 0 },
            { "NOTEQUAL_CASESENSITIVE", 1 },
        };
        constexpr EnumTable s_stringFunction = { "STRINGFUNCTION_", s_stringFunctionNames, 2 };

        constexpr EnumName s_playTimeModeNames[] = {
            { "REACHEDTIME", 0 },
            { "REACHEDEND", 1 },
            { "HASLESSTHAN", 2 },
        };
        constexpr EnumTable s_playTimeMode = { "MODE_", s_playTimeModeNames, 3 };

        constexpr EnumName s_motionTestFunctionNames[] = {
            { "EVENT", 0 },        { "HASENDED", 1 },         { "HASREACHEDMAXNUMLOOPS", 2 }, { "PLAYTIME", 3 },
            { "PLAYTIMELEFT", 4 }, { "ISMOTIONASSIGNED", 5 }, { "ISMOTIONNOTASSIGNED", 6 },   { "NONE", 7 },
        };
        constexpr EnumTable s_motionTestFunction = { "FUNCTION_", s_motionTestFunctionNames, 8 };

        constexpr EnumName s_stateTestFunctionNames[] = {
            { "EXITSTATES", 0 }, { "ENTERING", 1 }, { "ENTER", 2 }, { "EXIT", 3 }, { "END", 4 }, { "PLAYTIME", 5 }, { "NONE", 6 },
        };
        constexpr EnumTable s_stateTestFunction = { "FUNCTION_", s_stateTestFunctionNames, 7 };

        constexpr EnumName s_tagFunctionNames[] = {
            { "ALL", 0 },
            { "NOTALL", 1 },
            { "ONEORMORE", 2 },
            { "NONE", 3 },
        };
        constexpr EnumTable s_tagFunction = { "FUNCTION_", s_tagFunctionNames, 4 };

        constexpr EnumName s_vector2OperationNames[] = {
            { "LENGTH", 0 },
            { "GETX", 1 },
            { "GETY", 2 },
        };
        constexpr EnumTable s_vector2Operation = { "OPERATION_", s_vector2OperationNames, 3 };

        constexpr ConditionAttribute s_parameterConditionAttributes[] = {
            { "parameterName", AttributeKind::String, nullptr },  { "function", AttributeKind::Enum, &s_parameterFunction },
            { "testValue", AttributeKind::Float, nullptr },       { "rangeValue", AttributeKind::Float, nullptr },
            { "timeRequirement", AttributeKind::Float, nullptr }, { "stringFunction", AttributeKind::Enum, &s_stringFunction },
            { "testString", AttributeKind::String, nullptr },
        };
        constexpr ConditionAttribute s_timeConditionAttributes[] = {
            { "countDownTime", AttributeKind::Float, nullptr },
            { "useRandomization", AttributeKind::Bool, nullptr },
            { "minRandomTime", AttributeKind::Float, nullptr },
            { "maxRandomTime", AttributeKind::Float, nullptr },
        };
        constexpr ConditionAttribute s_playTimeConditionAttributes[] = {
            { "nodeId", AttributeKind::NodeId, nullptr },
            { "mode", AttributeKind::Enum, &s_playTimeMode },
            { "playTime", AttributeKind::Float, nullptr },
        };
        constexpr ConditionAttribute s_motionConditionAttributes[] = {
            { "motionNodeId", AttributeKind::NodeId, nullptr },
            { "testFunction", AttributeKind::Enum, &s_motionTestFunction },
            { "numLoops", AttributeKind::Count, nullptr },
            { "playTime", AttributeKind::Float, nullptr },
        };
        constexpr ConditionAttribute s_stateConditionAttributes[] = {
            { "stateId", AttributeKind::NodeId, nullptr },
            { "testFunction", AttributeKind::Enum, &s_stateTestFunction },
            { "playTime", AttributeKind::Float, nullptr },
        };
        constexpr ConditionAttribute s_tagConditionAttributes[] = {
            { "function", AttributeKind::Enum, &s_tagFunction },
            { "tags", AttributeKind::StringList, nullptr },
        };
        constexpr ConditionAttribute s_vector2ConditionAttributes[] = {
            { "parameterName", AttributeKind::String, nullptr },
            { "operation", AttributeKind::Enum, &s_vector2Operation },
            { "testFunction", AttributeKind::Enum, &s_parameterFunction },
            { "testValue", AttributeKind::Float, nullptr },
            { "rangeValue", AttributeKind::Float, nullptr },
        };

        constexpr ConditionType s_conditionTypes[] = {
            { "ParameterCondition",
              "AnimGraphParameterCondition",
              "{458D0D08-3F1E-4116-89FC-50F447EDC84E}",
              s_parameterConditionAttributes,
              7 },
            { "TimeCondition", "AnimGraphTimeCondition", "{9CFC3B92-0D9B-4EC8-9999-625EF21A9993}", s_timeConditionAttributes, 4 },
            { "PlayTimeCondition",
              "AnimGraphPlayTimeCondition",
              "{5368D058-9552-4282-A273-AA9344E65D2E}",
              s_playTimeConditionAttributes,
              3 },
            { "MotionCondition", "AnimGraphMotionCondition", "{0E2EDE4E-BDEE-4383-AB18-208CE7F7A784}", s_motionConditionAttributes, 4 },
            { "StateCondition", "AnimGraphStateCondition", "{8C955719-5D14-4BB5-BA64-F2A3385CAF7E}", s_stateConditionAttributes, 3 },
            { "TagCondition", "AnimGraphTagCondition", "{2A786756-80F5-4A55-B00F-5AA876CC4D3A}", s_tagConditionAttributes, 2 },
            { "Vector2Condition", "AnimGraphVector2Condition", "{605DF8B0-C39A-4BB4-B1A9-ABAF528E0739}", s_vector2ConditionAttributes, 5 },
        };
        constexpr size_t s_conditionTypeCount = sizeof(s_conditionTypes) / sizeof(s_conditionTypes[0]);

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
            outText = FloatText(value.GetDouble());
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

    AZStd::string FloatText(double value)
    {
        AZ::Locale::ScopedSerializationLocale cLocale;
        return AZStd::string::format("%.9g", value);
    }

    bool FormatScalarText(const rapidjson::Value& value, AZStd::string& outText, AZStd::string& outReason)
    {
        if (value.IsBool())
        {
            outText = value.GetBool() ? "true" : "false";
            return true;
        }
        if (value.IsInt64())
        {
            outText = AZStd::string::format("%lld", static_cast<long long>(value.GetInt64()));
            return true;
        }
        if (value.IsUint64())
        {
            outText = AZStd::string::format("%llu", static_cast<unsigned long long>(value.GetUint64()));
            return true;
        }
        if (value.IsNumber())
        {
            if (!std::isfinite(value.GetDouble()))
            {
                outReason = "must be a finite number";
                return false;
            }
            outText = FloatText(value.GetDouble());
            return true;
        }
        if (value.IsString())
        {
            outText.assign(value.GetString(), value.GetStringLength());
            return IsValidCommandText(outText, outReason);
        }
        outReason = "must be a JSON number, bool or string";
        return false;
    }

    // -- Conditions -------------------------------------------------------------

    size_t ConditionTypeCount()
    {
        return s_conditionTypeCount;
    }

    const ConditionType& ConditionTypeAt(size_t index)
    {
        return s_conditionTypes[index < s_conditionTypeCount ? index : 0];
    }

    const ConditionType* FindConditionType(const AZStd::string& name)
    {
        for (const ConditionType& type : s_conditionTypes)
        {
            if (EqualsNoCase(name, type.m_shortName) || EqualsNoCase(name, type.m_rttiName))
            {
                return &type;
            }
        }
        return nullptr;
    }

    AZStd::string ConditionTypeNames()
    {
        AZStd::string names;
        for (const ConditionType& type : s_conditionTypes)
        {
            if (!names.empty())
            {
                names += ", ";
            }
            names += type.m_shortName;
        }
        return names;
    }

    const ConditionAttribute* FindConditionAttribute(const ConditionType& type, const AZStd::string& key)
    {
        for (size_t i = 0; i < type.m_attributeCount; ++i)
        {
            if (key == type.m_attributes[i].m_key)
            {
                return &type.m_attributes[i];
            }
        }
        return nullptr;
    }

    AZStd::string ConditionAttributeNames(const ConditionType& type)
    {
        AZStd::string names;
        for (size_t i = 0; i < type.m_attributeCount; ++i)
        {
            if (!names.empty())
            {
                names += ", ";
            }
            names += type.m_attributes[i].m_key;
        }
        return names;
    }

    AZStd::string EnumNames(const EnumTable& table)
    {
        AZStd::string names;
        for (size_t i = 0; i < table.m_count; ++i)
        {
            if (!names.empty())
            {
                names += ", ";
            }
            names += table.m_names[i].m_name;
        }
        return names;
    }

    bool FormatConditionAttribute(
        const ConditionAttribute& attribute, const rapidjson::Value& value, AttributeValue& out, AZStd::string& outReason)
    {
        out.m_text.clear();
        out.m_list.clear();
        switch (attribute.m_kind)
        {
        case AttributeKind::Float:
            if (!value.IsNumber() || !std::isfinite(value.GetDouble()))
            {
                outReason = "must be a JSON number";
                return false;
            }
            out.m_text = FloatText(value.GetDouble());
            return true;
        case AttributeKind::Count:
            if (!value.IsUint())
            {
                outReason = "must be a JSON integer from 0 to 4294967295";
                return false;
            }
            out.m_text = AZStd::string::format("%u", value.GetUint());
            return true;
        case AttributeKind::Bool:
            if (!value.IsBool())
            {
                outReason = "must be a JSON bool";
                return false;
            }
            out.m_text = value.GetBool() ? "true" : "false";
            return true;
        case AttributeKind::String:
            if (!value.IsString())
            {
                outReason = "must be a JSON string";
                return false;
            }
            out.m_text.assign(value.GetString(), value.GetStringLength());
            return IsValidCommandText(out.m_text, outReason);
        case AttributeKind::NodeId:
            {
                AZStd::string text;
                if (value.IsUint64())
                {
                    text = AZStd::string::format("%llu", static_cast<unsigned long long>(value.GetUint64()));
                }
                else if (value.IsString())
                {
                    text.assign(value.GetString(), value.GetStringLength());
                }
                AZ::u64 id = 0;
                if (!ParseObjectId(text, id))
                {
                    outReason = "must be a node id, the decimal string get_anim_graph reports";
                    return false;
                }
                out.m_text = AZStd::string::format("%llu", static_cast<unsigned long long>(id));
                return true;
            }
        case AttributeKind::Enum:
            {
                const EnumTable* table = attribute.m_enum;
                if (!table)
                {
                    outReason = "has no enum table";
                    return false;
                }
                if (value.IsInt())
                {
                    for (size_t i = 0; i < table->m_count; ++i)
                    {
                        if (table->m_names[i].m_value == value.GetInt())
                        {
                            out.m_text = AZStd::string::format("%d", value.GetInt());
                            return true;
                        }
                    }
                    outReason = AZStd::string::format("%d is not a value of this enum (%s)", value.GetInt(), EnumNames(*table).c_str());
                    return false;
                }
                if (value.IsString())
                {
                    AZStd::string_view name(value.GetString(), value.GetStringLength());
                    const AZStd::string_view prefix = table->m_prefix;
                    if (name.size() > prefix.size() && EqualsNoCase(name.substr(0, prefix.size()), prefix))
                    {
                        name.remove_prefix(prefix.size());
                    }
                    for (size_t i = 0; i < table->m_count; ++i)
                    {
                        if (EqualsNoCase(name, table->m_names[i].m_name))
                        {
                            out.m_text = AZStd::string::format("%d", table->m_names[i].m_value);
                            return true;
                        }
                    }
                    outReason = AZStd::string::format(
                        "'%.*s' is not a name of this enum (%s)",
                        static_cast<int>(value.GetStringLength()),
                        value.GetString(),
                        EnumNames(*table).c_str());
                    return false;
                }
                outReason = AZStd::string::format("must be an integer or one of %s", EnumNames(*table).c_str());
                return false;
            }
        case AttributeKind::StringList:
            if (!value.IsArray())
            {
                outReason = "must be a JSON array of strings";
                return false;
            }
            for (rapidjson::SizeType i = 0; i < value.Size(); ++i)
            {
                if (!value[i].IsString())
                {
                    outReason = AZStd::string::format("element %u is not a string", i);
                    return false;
                }
                AZStd::string element(value[i].GetString(), value[i].GetStringLength());
                AZStd::string reason;
                if (!IsValidCommandText(element, reason))
                {
                    outReason = AZStd::string::format("element %u %s", i, reason.c_str());
                    return false;
                }
                out.m_list.push_back(AZStd::move(element));
            }
            return true;
        }
        outReason = "unsupported attribute kind";
        return false;
    }

    // -- Ports --------------------------------------------------------------

    bool ResolvePort(
        const AZStd::vector<AZStd::string>& portNames, const rapidjson::Value& spec, size_t& outIndex, AZStd::string& outReason)
    {
        outIndex = NoPort;
        if (spec.IsUint64())
        {
            const AZ::u64 index = spec.GetUint64();
            if (index >= portNames.size())
            {
                outReason = AZStd::string::format(
                    "port index %llu is out of range; the ports are %s",
                    static_cast<unsigned long long>(index),
                    PortNames(portNames).c_str());
                return false;
            }
            outIndex = static_cast<size_t>(index);
            return true;
        }
        if (spec.IsString())
        {
            const AZStd::string_view name(spec.GetString(), spec.GetStringLength());
            for (size_t i = 0; i < portNames.size(); ++i)
            {
                if (AZStd::string_view(portNames[i]) == name)
                {
                    outIndex = i;
                    return true;
                }
            }
            for (size_t i = 0; i < portNames.size(); ++i)
            {
                if (EqualsNoCase(portNames[i], name))
                {
                    outIndex = i;
                    return true;
                }
            }
            outReason = AZStd::string::format(
                "no port named '%.*s'; the ports are %s", static_cast<int>(name.size()), name.data(), PortNames(portNames).c_str());
            return false;
        }
        outReason = AZStd::string::format("must be a port index or a port name; the ports are %s", PortNames(portNames).c_str());
        return false;
    }

    AZStd::string PortNames(const AZStd::vector<AZStd::string>& portNames)
    {
        if (portNames.empty())
        {
            return "none";
        }
        AZStd::string names;
        for (size_t i = 0; i < portNames.size(); ++i)
        {
            if (i > 0)
            {
                names += ", ";
            }
            names += AZStd::string::format("\"%s\" (%zu)", portNames[i].c_str(), i);
        }
        return names;
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

    AZStd::string CreateTransitionCommand(
        AZ::u32 animGraphId, const AZStd::string& sourceName, const AZStd::string& targetName, const AZStd::string& transitionId)
    {
        return AZStd::string::format(
            "AnimGraphCreateConnection -animGraphID %u -sourceNode %s -targetNode %s -sourcePort 0 -targetPort 0 -startOffsetX 0 "
            "-startOffsetY 0 -endOffsetX 0 -endOffsetY 0 -id %s -transitionType %s",
            animGraphId,
            Quoted(sourceName).c_str(),
            Quoted(targetName).c_str(),
            transitionId.c_str(),
            StateTransitionTypeUuid);
    }

    AZStd::string AdjustTransitionCommand(AZ::u32 animGraphId, const AZStd::string& transitionId, const TransitionAdjustments& adjustments)
    {
        AZStd::string command =
            AZStd::string::format("AnimGraphAdjustTransition -animGraphId %u -transitionId %s", animGraphId, transitionId.c_str());
        if (adjustments.m_hasDisabled)
        {
            command += adjustments.m_disabled ? " -isDisabled true" : " -isDisabled false";
        }
        AZStd::vector<AZStd::pair<AZStd::string, AZStd::string>> fields;
        if (adjustments.m_hasBlendTime)
        {
            fields.emplace_back("transitionTime", FloatText(adjustments.m_blendTime));
        }
        if (adjustments.m_hasPriority)
        {
            fields.emplace_back("priority", AZStd::string::format("%u", adjustments.m_priority));
        }
        if (adjustments.m_hasSyncMode)
        {
            fields.emplace_back("syncMode", AZStd::string::format("%d", adjustments.m_syncMode));
        }
        if (adjustments.m_hasInterpolation)
        {
            fields.emplace_back("interpolationType", AZStd::string::format("%d", adjustments.m_interpolation));
        }
        if (!fields.empty())
        {
            command += " -attributesString " + Braced(AttributesString(fields));
        }
        return command;
    }

    AZStd::string AddConditionCommand(
        AZ::u32 animGraphId, const AZStd::string& transitionId, const AZStd::string& conditionUuid, const AZStd::string& contentsXml)
    {
        return AZStd::string::format(
            "AnimGraphAddCondition -animGraphId %u -transitionId %s -conditionType %s -contents %s",
            animGraphId,
            transitionId.c_str(),
            conditionUuid.c_str(),
            Braced(contentsXml).c_str());
    }

    AZStd::string RemoveTransitionCommand(
        AZ::u32 animGraphId, const AZStd::string& sourceName, const AZStd::string& targetName, const AZStd::string& transitionId)
    {
        return AZStd::string::format(
            "AnimGraphRemoveConnection -animGraphID %u -sourceNode %s -targetNode %s -sourcePort 0 -targetPort 0 -id %s",
            animGraphId,
            Quoted(sourceName).c_str(),
            Quoted(targetName).c_str(),
            transitionId.c_str());
    }

    AZStd::string CreatePortConnectionCommand(
        AZ::u32 animGraphId, const AZStd::string& sourceName, const AZStd::string& targetName, size_t sourcePort, size_t targetPort)
    {
        return AZStd::string::format(
            "AnimGraphCreateConnection -animGraphID %u -sourceNode %s -targetNode %s -sourcePort %zu -targetPort %zu -startOffsetX 0 "
            "-startOffsetY 0 -endOffsetX 0 -endOffsetY 0",
            animGraphId,
            Quoted(sourceName).c_str(),
            Quoted(targetName).c_str(),
            sourcePort,
            targetPort);
    }

    AZStd::string RemovePortConnectionCommand(
        AZ::u32 animGraphId, const AZStd::string& sourceName, const AZStd::string& targetName, size_t sourcePort, size_t targetPort)
    {
        return AZStd::string::format(
            "AnimGraphRemoveConnection -animGraphID %u -sourceNode %s -targetNode %s -sourcePort %zu -targetPort %zu",
            animGraphId,
            Quoted(sourceName).c_str(),
            Quoted(targetName).c_str(),
            sourcePort,
            targetPort);
    }

    AZStd::string AttributesString(const AZStd::vector<AZStd::pair<AZStd::string, AZStd::string>>& fields)
    {
        AZStd::string text;
        for (const auto& field : fields)
        {
            if (!text.empty())
            {
                text += ' ';
            }
            text += "-" + field.first + " " + Braced(field.second);
        }
        return text;
    }

    AZStd::string AdjustNodeCommand(AZ::u32 animGraphId, const AZStd::string& currentName, const NodeAdjustments& adjustments)
    {
        AZStd::string command =
            AZStd::string::format("AnimGraphAdjustNode -animGraphID %u -name %s", animGraphId, Quoted(currentName).c_str());
        if (!adjustments.m_newName.empty())
        {
            command += " -newName " + Quoted(adjustments.m_newName);
        }
        if (adjustments.m_hasPosition)
        {
            command += AZStd::string::format(" -xPos %d -yPos %d", adjustments.m_xPos, adjustments.m_yPos);
        }
        if (adjustments.m_hasEnabled)
        {
            command += adjustments.m_enabled ? " -enabled true" : " -enabled false";
        }
        command += " -updateAttributes true";
        if (!adjustments.m_attributes.empty())
        {
            command += " -attributesString " + Braced(AttributesString(adjustments.m_attributes));
        }
        return command;
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
