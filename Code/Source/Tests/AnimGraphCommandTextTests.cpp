/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include "Animation/AnimGraphCommandText.h"

#include <AzCore/JSON/document.h>
#include <AzCore/Math/Uuid.h>

// The strings the anim graph authoring request types send to EMotion FX's
// command system, checked without an engine: every builder, the name and
// value rules, the type tables and the path containment check.

namespace UnitTest
{
    namespace Text = AiCompanion::AnimGraphCommandText;

    class AnimGraphCommandTextFixture : public LeakDetectionFixture
    {
    protected:
        rapidjson::Document Parse(const char* json)
        {
            rapidjson::Document doc;
            doc.Parse(json);
            EXPECT_FALSE(doc.HasParseError());
            return doc;
        }

        AZStd::string Format(const char* json, Text::ValueKind kind, bool expectOk = true)
        {
            rapidjson::Document doc = Parse(json);
            AZStd::string text;
            AZStd::string reason;
            const bool ok = Text::FormatParameterValue(doc["v"], kind, text, reason);
            EXPECT_EQ(ok, expectOk) << json << ": " << reason.c_str();
            return ok ? text : reason;
        }

        static AZStd::vector<Text::NodeType> SampleNodeTypes()
        {
            AZStd::vector<Text::NodeType> types;
            Text::NodeType motion;
            motion.m_rttiName = "AnimGraphMotionNode";
            motion.m_paletteName = "Motion";
            motion.m_canActAsState = true;
            types.push_back(motion);
            Text::NodeType blend2;
            blend2.m_rttiName = "BlendTreeBlend2Node";
            blend2.m_paletteName = "Blend Two";
            types.push_back(blend2);
            Text::NodeType tree;
            tree.m_rttiName = "BlendTree";
            tree.m_paletteName = "Blend Tree";
            tree.m_canActAsState = true;
            tree.m_canHaveChildren = true;
            types.push_back(tree);
            return types;
        }

#if defined(AZ_PLATFORM_WINDOWS)
        static constexpr const char* Root = "C:/proj";
#else
        static constexpr const char* Root = "/proj";
#endif
    };

    // -- Names and ids -------------------------------------------------------

    TEST_F(AnimGraphCommandTextFixture, ObjectName_RefusesEmptyAndTheParserCharacters)
    {
        AZStd::string reason;
        EXPECT_FALSE(Text::IsValidObjectName("", reason));
        EXPECT_FALSE(Text::IsValidObjectName("Idle\"Walk", reason));
        EXPECT_FALSE(Text::IsValidObjectName("Speed%", reason));
        EXPECT_FALSE(Text::IsValidObjectName("{Speed}", reason));
        EXPECT_FALSE(Text::IsValidObjectName("Idle\nWalk", reason));
        EXPECT_TRUE(Text::IsValidObjectName("Idle Walk-2_fast.v3", reason));
        EXPECT_TRUE(Text::IsValidObjectName("-leading dash", reason)); // quoted on the command line, so fine
    }

    TEST_F(AnimGraphCommandTextFixture, CommandText_AllowsEmptyButNotTheParserCharacters)
    {
        AZStd::string reason;
        EXPECT_TRUE(Text::IsValidCommandText("", reason));
        EXPECT_TRUE(Text::IsValidCommandText("Walk speed, 0 to 1 - metres per second", reason));
        EXPECT_FALSE(Text::IsValidCommandText("50% faster", reason));
        EXPECT_FALSE(Text::IsValidCommandText("say \"hi\"", reason));
    }

    TEST_F(AnimGraphCommandTextFixture, ObjectId_AcceptsDecimalAndRefusesTheInvalidValues)
    {
        AZ::u64 id = 0;
        EXPECT_TRUE(Text::ParseObjectId("17720413266153757478", id));
        EXPECT_EQ(id, 17720413266153757478ull);
        EXPECT_TRUE(Text::ParseObjectId("42", id));
        EXPECT_EQ(id, 42u);
        EXPECT_FALSE(Text::ParseObjectId("0", id));
        EXPECT_FALSE(Text::ParseObjectId("18446744073709551615", id)); // ObjectId::InvalidId
        EXPECT_FALSE(Text::ParseObjectId("", id));
        EXPECT_FALSE(Text::ParseObjectId("12abc", id));
        EXPECT_FALSE(Text::ParseObjectId("-5", id));
        EXPECT_FALSE(Text::ParseObjectId("111111111111111111111", id)); // 21 digits
    }

    // -- Parameter types -----------------------------------------------------

    TEST_F(AnimGraphCommandTextFixture, ParameterTypeTable_HoldsDistinctValidUuids)
    {
        ASSERT_EQ(Text::ParameterTypeCount(), 13u);
        for (size_t i = 0; i < Text::ParameterTypeCount(); ++i)
        {
            const Text::ParameterType& type = Text::ParameterTypeAt(i);
            const AZ::Uuid uuid = AZ::Uuid::CreateStringPermissive(type.m_uuid);
            EXPECT_FALSE(uuid.IsNull()) << type.m_friendlyName;
            EXPECT_EQ(AZStd::string(uuid.ToFixedString().c_str()), AZStd::string(type.m_uuid)) << type.m_friendlyName; // braced and dashed
            for (size_t j = i + 1; j < Text::ParameterTypeCount(); ++j)
            {
                EXPECT_STRNE(type.m_uuid, Text::ParameterTypeAt(j).m_uuid);
            }
            // Every entry resolves to itself by friendly name and by class name.
            EXPECT_EQ(Text::FindParameterType(type.m_friendlyName), &type);
            EXPECT_EQ(Text::FindParameterType(type.m_className), &type);
        }
    }

    TEST_F(AnimGraphCommandTextFixture, ParameterType_ResolvesAliasesCaseInsensitively)
    {
        const Text::ParameterType* floatSlider = Text::FindParameterType("Float");
        ASSERT_NE(floatSlider, nullptr);
        EXPECT_STREQ(floatSlider->m_className, "FloatSliderParameter");
        EXPECT_STREQ(floatSlider->m_uuid, "{2ED6BBAF-5C82-4EAA-8678-B220667254F2}");
        EXPECT_TRUE(floatSlider->m_ranged);
        EXPECT_EQ(Text::FindParameterType("float"), floatSlider);
        EXPECT_EQ(Text::FindParameterType("FLOATSLIDER"), floatSlider);
        EXPECT_EQ(Text::FindParameterType("floatsliderparameter"), floatSlider);

        const Text::ParameterType* intSlider = Text::FindParameterType("int");
        ASSERT_NE(intSlider, nullptr);
        EXPECT_STREQ(intSlider->m_className, "IntSliderParameter");

        const Text::ParameterType* boolean = Text::FindParameterType("bool");
        ASSERT_NE(boolean, nullptr);
        EXPECT_FALSE(boolean->m_ranged);
        EXPECT_EQ(boolean->m_kind, Text::ValueKind::Bool);
        EXPECT_EQ(Text::FindParameterType("Tag")->m_kind, Text::ValueKind::Bool);
        EXPECT_FALSE(Text::FindParameterType("String")->m_ranged);

        EXPECT_EQ(Text::FindParameterType("Group"), nullptr); // groups are made by AnimGraphAddGroupParameter
        EXPECT_EQ(Text::FindParameterType("Quaternion"), nullptr);
        EXPECT_EQ(Text::FindParameterType(""), nullptr);

        const AZStd::string names = Text::ParameterTypeNames();
        EXPECT_NE(names.find("Float, Int, FloatSlider"), AZStd::string::npos) << names.c_str();
        EXPECT_NE(names.find("Rotation"), AZStd::string::npos);
    }

    TEST_F(AnimGraphCommandTextFixture, ParameterValue_ScalarsAndBools)
    {
        EXPECT_EQ(Format(R"({"v": 0.2})", Text::ValueKind::Float), "0.2");
        EXPECT_EQ(Format(R"({"v": 1})", Text::ValueKind::Float), "1");
        EXPECT_EQ(Format(R"({"v": -2.5e-3})", Text::ValueKind::Float), "-0.0025");
        EXPECT_EQ(Format(R"({"v": "0.2"})", Text::ValueKind::Float, false), "must be a JSON number");
        EXPECT_EQ(Format(R"({"v": 3})", Text::ValueKind::Int), "3");
        EXPECT_EQ(Format(R"({"v": -7})", Text::ValueKind::Int), "-7");
        EXPECT_EQ(Format(R"({"v": 2.5})", Text::ValueKind::Int, false), "must be a JSON integer within the 32-bit range");
        EXPECT_EQ(Format(R"({"v": 4294967296})", Text::ValueKind::Int, false), "must be a JSON integer within the 32-bit range");
        EXPECT_EQ(Format(R"({"v": true})", Text::ValueKind::Bool), "true");
        EXPECT_EQ(Format(R"({"v": false})", Text::ValueKind::Bool), "false");
        EXPECT_EQ(Format(R"({"v": 1})", Text::ValueKind::Bool, false), "must be a JSON bool");
        EXPECT_EQ(Format(R"({"v": "hello there"})", Text::ValueKind::String), "hello there");
        EXPECT_EQ(Format(R"({"v": ""})", Text::ValueKind::String), "");
        EXPECT_NE(Format(R"({"v": "a {b}"})", Text::ValueKind::String, false).find("may not contain"), AZStd::string::npos);
        EXPECT_EQ(Format(R"({"v": 5})", Text::ValueKind::String, false), "must be a JSON string");
    }

    TEST_F(AnimGraphCommandTextFixture, ParameterValue_VectorsColorsAndRotations)
    {
        EXPECT_EQ(Format(R"({"v": [1, -1]})", Text::ValueKind::Vector2), "1.0000000 -1.0000000");
        EXPECT_EQ(Format(R"({"v": [1, -1, 0.5]})", Text::ValueKind::Vector3), "1.0000000 -1.0000000 0.5000000");
        EXPECT_EQ(Format(R"({"v": [1, 2, 3, 4]})", Text::ValueKind::Vector4), "1.0000000 2.0000000 3.0000000 4.0000000");
        EXPECT_EQ(Format(R"({"v": [1, 2]})", Text::ValueKind::Vector3, false), "must be a JSON array of 3 numbers");
        EXPECT_EQ(Format(R"({"v": [1, "2", 3]})", Text::ValueKind::Vector3, false), "element 1 is not a number");
        EXPECT_EQ(Format(R"({"v": 1})", Text::ValueKind::Vector2, false), "must be a JSON array of 2 numbers");
        // A colour without alpha gets alpha 1; the engine reads r g b a.
        EXPECT_EQ(Format(R"({"v": [1, 0.5, 0]})", Text::ValueKind::Color), "1.0000000 0.5000000 0.0000000 1.0000000");
        EXPECT_EQ(Format(R"({"v": [1, 0.5, 0, 0.25]})", Text::ValueKind::Color), "1.0000000 0.5000000 0.0000000 0.2500000");
        EXPECT_EQ(Format(R"({"v": [1, 0.5]})", Text::ValueKind::Color, false), "must be a JSON array of 3 or 4 numbers");
        // A rotation is the quaternion x y z w the engine's text format uses.
        EXPECT_EQ(Format(R"({"v": [0, 0, 0, 1]})", Text::ValueKind::Rotation), "0.0000000 0.0000000 0.0000000 1.0000000");
        EXPECT_EQ(Format(R"({"v": [0, 0, 0]})", Text::ValueKind::Rotation, false), "must be a JSON array of 4 numbers");
    }

    // -- Node types ----------------------------------------------------------

    TEST_F(AnimGraphCommandTextFixture, NodeType_ResolvesRttiAndPaletteNamesCaseInsensitively)
    {
        const AZStd::vector<Text::NodeType> types = SampleNodeTypes();
        EXPECT_EQ(Text::FindNodeType(types, "AnimGraphMotionNode"), 0u);
        EXPECT_EQ(Text::FindNodeType(types, "motion"), 0u);
        EXPECT_EQ(Text::FindNodeType(types, "MOTION"), 0u);
        EXPECT_EQ(Text::FindNodeType(types, "blend two"), 1u);
        EXPECT_EQ(Text::FindNodeType(types, "blendtreeblend2node"), 1u);
        EXPECT_EQ(Text::FindNodeType(types, "Blend Tree"), 2u);
        EXPECT_EQ(Text::FindNodeType(types, "BlendTree"), 2u);
        EXPECT_EQ(Text::FindNodeType(types, "Blend"), Text::NoNodeType);
        EXPECT_EQ(Text::FindNodeType(types, ""), Text::NoNodeType);
        EXPECT_EQ(Text::NodeTypeNames(types), "AnimGraphMotionNode, BlendTreeBlend2Node, BlendTree");
    }

    TEST_F(AnimGraphCommandTextFixture, GeneratedNamePrefix_StripsTheFamilyPrefixes)
    {
        EXPECT_EQ(Text::GeneratedNamePrefix("AnimGraphMotionNode"), "MotionNode");
        EXPECT_EQ(Text::GeneratedNamePrefix("AnimGraphStateMachine"), "StateMachine");
        EXPECT_EQ(Text::GeneratedNamePrefix("BlendTreeFinalNode"), "FinalNode");
        EXPECT_EQ(Text::GeneratedNamePrefix("BlendTreeBlend2Node"), "Blend2Node");
        EXPECT_EQ(Text::GeneratedNamePrefix("BlendTree"), "BlendTree");
        EXPECT_EQ(Text::GeneratedNamePrefix("BlendSpace1DNode"), "BlendSpace1DNode");
        EXPECT_EQ(Text::GeneratedNamePrefix("AnimGraph"), "AnimGraph"); // never empty
    }

    // -- Command lines -------------------------------------------------------

    TEST_F(AnimGraphCommandTextFixture, CreateNodeCommand_NamedAndGenerated)
    {
        EXPECT_EQ(
            Text::CreateNodeCommand(7, "{B8B8AAE6-E532-4BF8-898F-3D40AA41BC82}", "Root", "Idle", "MotionNode", 10, -20),
            "AnimGraphCreateNode -animGraphID 7 -type {B8B8AAE6-E532-4BF8-898F-3D40AA41BC82} -parentName \"Root\" -name \"Idle\" "
            "-xPos 10 -yPos -20");
        EXPECT_EQ(
            Text::CreateNodeCommand(7, "{A8B5BB1E-5BA9-4B0A-88E9-21BB7A199ED2}", "Root", "", "BlendTree", 0, 0),
            "AnimGraphCreateNode -animGraphID 7 -type {A8B5BB1E-5BA9-4B0A-88E9-21BB7A199ED2} -parentName \"Root\" -name GENERATE "
            "-namePrefix \"BlendTree\" -xPos 0 -yPos 0");
        // A name with spaces stays one value because it is quoted.
        EXPECT_EQ(
            Text::CreateNodeCommand(0, "{A8B5BB1E-5BA9-4B0A-88E9-21BB7A199ED2}", "Upper Body", "Walk Fast", "", 1, 2),
            "AnimGraphCreateNode -animGraphID 0 -type {A8B5BB1E-5BA9-4B0A-88E9-21BB7A199ED2} -parentName \"Upper Body\" "
            "-name \"Walk Fast\" -xPos 1 -yPos 2");
    }

    TEST_F(AnimGraphCommandTextFixture, NodeCommands_QuoteTheName)
    {
        EXPECT_EQ(Text::RemoveNodeCommand(7, "Walk Fast"), "AnimGraphRemoveNode -animGraphID 7 -name \"Walk Fast\"");
        EXPECT_EQ(Text::SetEntryStateCommand(7, "Walk"), "AnimGraphSetEntryState -animGraphID 7 -entryNodeName \"Walk\"");
    }

    TEST_F(AnimGraphCommandTextFixture, CreateParameterCommand_MinimalAndFull)
    {
        Text::ParameterSpec spec;
        spec.m_typeUuid = "{2ED6BBAF-5C82-4EAA-8678-B220667254F2}";
        spec.m_name = "Speed";
        EXPECT_EQ(
            Text::CreateParameterCommand(7, spec),
            "AnimGraphCreateParameter -animGraphID 7 -type {2ED6BBAF-5C82-4EAA-8678-B220667254F2} -name \"Speed\"");

        spec.m_hasDefault = true;
        spec.m_defaultText = "0.2";
        spec.m_hasMin = true;
        spec.m_minText = "0";
        spec.m_hasMax = true;
        spec.m_maxText = "1";
        spec.m_hasDescription = true;
        spec.m_description = "Walk speed - metres per second";
        spec.m_hasGroup = true;
        spec.m_group = "Locomotion";
        EXPECT_EQ(
            Text::CreateParameterCommand(7, spec),
            "AnimGraphCreateParameter -animGraphID 7 -type {2ED6BBAF-5C82-4EAA-8678-B220667254F2} -name \"Speed\" "
            "-defaultValue {0.2} -minValue {0} -maxValue {1} -description {Walk speed - metres per second} -parent \"Locomotion\"");

        // Vector values are braced so their space-separated components, and
        // a negative one, stay one value.
        Text::ParameterSpec vec;
        vec.m_typeUuid = "{E647B621-27DA-454E-A14F-45C65E2C7874}";
        vec.m_name = "Dir";
        vec.m_hasDefault = true;
        vec.m_defaultText = "0.0000000 -1.0000000 0.0000000";
        EXPECT_EQ(
            Text::CreateParameterCommand(3, vec),
            "AnimGraphCreateParameter -animGraphID 3 -type {E647B621-27DA-454E-A14F-45C65E2C7874} -name \"Dir\" "
            "-defaultValue {0.0000000 -1.0000000 0.0000000}");
    }

    TEST_F(AnimGraphCommandTextFixture, ParameterAndGraphCommands)
    {
        EXPECT_EQ(Text::AddGroupParameterCommand(7, "Locomotion"), "AnimGraphAddGroupParameter -animGraphID 7 -name \"Locomotion\"");
        EXPECT_EQ(Text::RemoveParameterCommand(7, "Speed"), "AnimGraphRemoveParameter -animGraphID 7 -name \"Speed\"");
        EXPECT_EQ(
            Text::LoadCommand("/proj/Assets/Graphs/My Graph.animgraph"),
            "LoadAnimGraph -filename \"/proj/Assets/Graphs/My Graph.animgraph\"");
        EXPECT_EQ(
            Text::SaveCommand("/proj/Assets/Graphs/My Graph.animgraph", 2),
            "SaveAnimGraph -filename \"/proj/Assets/Graphs/My Graph.animgraph\" -index 2 -sourceControl false");
    }

    // -- Scalars -------------------------------------------------------------

    TEST_F(AnimGraphCommandTextFixture, ScalarText_NumbersBoolsAndStrings)
    {
        rapidjson::Document doc = Parse(R"({"i": 3, "n": -7, "f": 0.25, "b": false, "s": "jack_idle_zup", "bad": "a}b", "arr": [1]})");
        AZStd::string text;
        AZStd::string reason;
        EXPECT_TRUE(Text::FormatScalarText(doc["i"], text, reason));
        EXPECT_EQ(text, "3");
        EXPECT_TRUE(Text::FormatScalarText(doc["n"], text, reason));
        EXPECT_EQ(text, "-7");
        EXPECT_TRUE(Text::FormatScalarText(doc["f"], text, reason));
        EXPECT_EQ(text, "0.25");
        EXPECT_TRUE(Text::FormatScalarText(doc["b"], text, reason));
        EXPECT_EQ(text, "false");
        EXPECT_TRUE(Text::FormatScalarText(doc["s"], text, reason));
        EXPECT_EQ(text, "jack_idle_zup");
        EXPECT_FALSE(Text::FormatScalarText(doc["bad"], text, reason));
        EXPECT_NE(reason.find("may not contain"), AZStd::string::npos) << reason.c_str();
        EXPECT_FALSE(Text::FormatScalarText(doc["arr"], text, reason));
        EXPECT_EQ(reason, "must be a JSON number, bool or string");
        EXPECT_EQ(Text::FloatText(0.3), "0.3");
        EXPECT_EQ(Text::FloatText(1.0), "1");
    }

    // -- Conditions ----------------------------------------------------------

    TEST_F(AnimGraphCommandTextFixture, ConditionTypeTable_HoldsDistinctValidUuidsAndResolvesNames)
    {
        ASSERT_EQ(Text::ConditionTypeCount(), 7u);
        for (size_t i = 0; i < Text::ConditionTypeCount(); ++i)
        {
            const Text::ConditionType& type = Text::ConditionTypeAt(i);
            const AZ::Uuid uuid = AZ::Uuid::CreateStringPermissive(type.m_uuid);
            EXPECT_FALSE(uuid.IsNull()) << type.m_shortName;
            EXPECT_EQ(AZStd::string(uuid.ToFixedString().c_str()), AZStd::string(type.m_uuid)) << type.m_shortName;
            for (size_t j = i + 1; j < Text::ConditionTypeCount(); ++j)
            {
                EXPECT_STRNE(type.m_uuid, Text::ConditionTypeAt(j).m_uuid);
            }
            EXPECT_EQ(Text::FindConditionType(type.m_shortName), &type);
            EXPECT_EQ(Text::FindConditionType(type.m_rttiName), &type);
            EXPECT_GT(type.m_attributeCount, 0u);
            // Every enum attribute carries its table.
            for (size_t a = 0; a < type.m_attributeCount; ++a)
            {
                const Text::ConditionAttribute& attribute = type.m_attributes[a];
                EXPECT_EQ(attribute.m_kind == Text::AttributeKind::Enum, attribute.m_enum != nullptr) << attribute.m_key;
            }
        }
        EXPECT_STREQ(Text::FindConditionType("parametercondition")->m_uuid, "{458D0D08-3F1E-4116-89FC-50F447EDC84E}");
        EXPECT_STREQ(Text::FindConditionType("ANIMGRAPHTIMECONDITION")->m_shortName, "TimeCondition");
        EXPECT_EQ(Text::FindConditionType("Condition"), nullptr);
        EXPECT_EQ(Text::FindConditionType(""), nullptr);
        EXPECT_EQ(
            Text::ConditionTypeNames(),
            "ParameterCondition, TimeCondition, PlayTimeCondition, MotionCondition, StateCondition, TagCondition, Vector2Condition");
    }

    TEST_F(AnimGraphCommandTextFixture, ConditionAttributes_PerTypeKeysAreTheReflectedFieldNames)
    {
        const Text::ConditionType* parameter = Text::FindConditionType("ParameterCondition");
        ASSERT_NE(parameter, nullptr);
        EXPECT_EQ(
            Text::ConditionAttributeNames(*parameter),
            "parameterName, function, testValue, rangeValue, timeRequirement, stringFunction, testString");
        EXPECT_EQ(
            Text::ConditionAttributeNames(*Text::FindConditionType("TimeCondition")),
            "countDownTime, useRandomization, minRandomTime, maxRandomTime");
        EXPECT_EQ(Text::ConditionAttributeNames(*Text::FindConditionType("PlayTimeCondition")), "nodeId, mode, playTime");
        EXPECT_EQ(
            Text::ConditionAttributeNames(*Text::FindConditionType("MotionCondition")), "motionNodeId, testFunction, numLoops, playTime");
        EXPECT_EQ(Text::ConditionAttributeNames(*Text::FindConditionType("StateCondition")), "stateId, testFunction, playTime");
        EXPECT_EQ(Text::ConditionAttributeNames(*Text::FindConditionType("TagCondition")), "function, tags");
        EXPECT_EQ(
            Text::ConditionAttributeNames(*Text::FindConditionType("Vector2Condition")),
            "parameterName, operation, testFunction, testValue, rangeValue");

        const Text::ConditionAttribute* function = Text::FindConditionAttribute(*parameter, "function");
        ASSERT_NE(function, nullptr);
        EXPECT_EQ(function->m_kind, Text::AttributeKind::Enum);
        EXPECT_EQ(Text::EnumNames(*function->m_enum), "GREATER, GREATEREQUAL, LESS, LESSEQUAL, NOTEQUAL, EQUAL, INRANGE, NOTINRANGE");
        EXPECT_EQ(Text::FindConditionAttribute(*parameter, "Function"), nullptr); // field names are case-sensitive
        EXPECT_EQ(Text::FindConditionAttribute(*parameter, "tags"), nullptr);
        EXPECT_EQ(Text::FindConditionAttribute(*Text::FindConditionType("TagCondition"), "tags")->m_kind, Text::AttributeKind::StringList);
        EXPECT_EQ(
            Text::FindConditionAttribute(*Text::FindConditionType("MotionCondition"), "motionNodeId")->m_kind, Text::AttributeKind::NodeId);
        EXPECT_EQ(
            Text::FindConditionAttribute(*Text::FindConditionType("MotionCondition"), "numLoops")->m_kind, Text::AttributeKind::Count);
    }

    TEST_F(AnimGraphCommandTextFixture, ConditionAttribute_FormatsEveryKind)
    {
        const Text::ConditionType& parameter = *Text::FindConditionType("ParameterCondition");
        const Text::ConditionType& time = *Text::FindConditionType("TimeCondition");
        const Text::ConditionType& motion = *Text::FindConditionType("MotionCondition");
        const Text::ConditionType& tag = *Text::FindConditionType("TagCondition");
        rapidjson::Document doc = Parse(
            R"({"f": 0.5, "fs": "0.5", "c": 3, "cn": -1, "cf": 2.5, "b": true, "bi": 1, "s": "Speed", "sbad": "50%",
                "id": "17720413266153757478", "idn": 42, "idbad": "0", "e": 6, "ebad": 8, "en": "greater", "enp": "FUNCTION_LESS",
                "enbad": "BIGGER", "eb": true, "l": ["Attack", "Jump"], "lbad": ["Attack", 2], "lchar": ["a{b"], "ls": "Attack"})");
        Text::AttributeValue value;
        AZStd::string reason;
        auto format = [&](const Text::ConditionType& type, const char* key, const char* json)
        {
            const Text::ConditionAttribute* attribute = Text::FindConditionAttribute(type, key);
            EXPECT_NE(attribute, nullptr) << key;
            reason.clear();
            return attribute && Text::FormatConditionAttribute(*attribute, doc[json], value, reason);
        };

        EXPECT_TRUE(format(parameter, "testValue", "f"));
        EXPECT_EQ(value.m_text, "0.5");
        EXPECT_FALSE(format(parameter, "testValue", "fs"));
        EXPECT_EQ(reason, "must be a JSON number");

        EXPECT_TRUE(format(motion, "numLoops", "c"));
        EXPECT_EQ(value.m_text, "3");
        EXPECT_FALSE(format(motion, "numLoops", "cn"));
        EXPECT_FALSE(format(motion, "numLoops", "cf"));
        EXPECT_EQ(reason, "must be a JSON integer from 0 to 4294967295");

        EXPECT_TRUE(format(time, "useRandomization", "b"));
        EXPECT_EQ(value.m_text, "true");
        EXPECT_FALSE(format(time, "useRandomization", "bi"));

        EXPECT_TRUE(format(parameter, "parameterName", "s"));
        EXPECT_EQ(value.m_text, "Speed");
        EXPECT_FALSE(format(parameter, "parameterName", "sbad"));
        EXPECT_FALSE(format(parameter, "parameterName", "f"));

        EXPECT_TRUE(format(motion, "motionNodeId", "id"));
        EXPECT_EQ(value.m_text, "17720413266153757478");
        EXPECT_TRUE(format(motion, "motionNodeId", "idn"));
        EXPECT_EQ(value.m_text, "42");
        EXPECT_FALSE(format(motion, "motionNodeId", "idbad"));
        EXPECT_NE(reason.find("node id"), AZStd::string::npos) << reason.c_str();

        EXPECT_TRUE(format(parameter, "function", "e"));
        EXPECT_EQ(value.m_text, "6");
        EXPECT_FALSE(format(parameter, "function", "ebad"));
        EXPECT_NE(reason.find("GREATER, GREATEREQUAL"), AZStd::string::npos) << reason.c_str();
        EXPECT_TRUE(format(parameter, "function", "en")); // case-insensitive name
        EXPECT_EQ(value.m_text, "0");
        EXPECT_TRUE(format(parameter, "function", "enp")); // the engine identifier's prefix is accepted
        EXPECT_EQ(value.m_text, "2");
        EXPECT_FALSE(format(parameter, "function", "enbad"));
        EXPECT_NE(reason.find("'BIGGER' is not a name"), AZStd::string::npos) << reason.c_str();
        EXPECT_FALSE(format(parameter, "function", "eb"));
        // The other enums map their own identifiers.
        EXPECT_FALSE(format(motion, "testFunction", "enp")); // the FUNCTION_ prefix is fine, but LESS is not a motion test function
        EXPECT_FALSE(format(tag, "function", "e")); // 6 is out of the tag range 0..3
        {
            rapidjson::Document names =
                Parse(R"({"m": "HASENDED", "t": "oneormore", "p": "MODE_REACHEDEND", "o": "GetY", "sf": "NOTEQUAL_CASESENSITIVE"})");
            const Text::ConditionAttribute* a = Text::FindConditionAttribute(motion, "testFunction");
            EXPECT_TRUE(Text::FormatConditionAttribute(*a, names["m"], value, reason));
            EXPECT_EQ(value.m_text, "1");
            a = Text::FindConditionAttribute(tag, "function");
            EXPECT_TRUE(Text::FormatConditionAttribute(*a, names["t"], value, reason));
            EXPECT_EQ(value.m_text, "2");
            a = Text::FindConditionAttribute(*Text::FindConditionType("PlayTimeCondition"), "mode");
            EXPECT_TRUE(Text::FormatConditionAttribute(*a, names["p"], value, reason));
            EXPECT_EQ(value.m_text, "1");
            a = Text::FindConditionAttribute(*Text::FindConditionType("Vector2Condition"), "operation");
            EXPECT_TRUE(Text::FormatConditionAttribute(*a, names["o"], value, reason));
            EXPECT_EQ(value.m_text, "2");
            a = Text::FindConditionAttribute(parameter, "stringFunction");
            EXPECT_TRUE(Text::FormatConditionAttribute(*a, names["sf"], value, reason));
            EXPECT_EQ(value.m_text, "1");
        }

        EXPECT_TRUE(format(tag, "tags", "l"));
        ASSERT_EQ(value.m_list.size(), 2u);
        EXPECT_EQ(value.m_list[0], "Attack");
        EXPECT_EQ(value.m_list[1], "Jump");
        EXPECT_TRUE(value.m_text.empty());
        EXPECT_FALSE(format(tag, "tags", "lbad"));
        EXPECT_EQ(reason, "element 1 is not a string");
        EXPECT_FALSE(format(tag, "tags", "lchar"));
        EXPECT_FALSE(format(tag, "tags", "ls"));
        EXPECT_EQ(reason, "must be a JSON array of strings");
    }

    // -- Ports ---------------------------------------------------------------

    TEST_F(AnimGraphCommandTextFixture, ResolvePort_IndexOrNameExactThenCaseInsensitive)
    {
        const AZStd::vector<AZStd::string> ports = { "Pose 1", "Pose 2", "Weight" };
        rapidjson::Document doc = Parse(R"({"i": 1, "big": 3, "neg": -1, "n": "Weight", "ci": "pose 2", "no": "Mask", "o": {}})");
        size_t index = Text::NoPort;
        AZStd::string reason;
        EXPECT_TRUE(Text::ResolvePort(ports, doc["i"], index, reason));
        EXPECT_EQ(index, 1u);
        EXPECT_TRUE(Text::ResolvePort(ports, doc["n"], index, reason));
        EXPECT_EQ(index, 2u);
        EXPECT_TRUE(Text::ResolvePort(ports, doc["ci"], index, reason));
        EXPECT_EQ(index, 1u);
        EXPECT_FALSE(Text::ResolvePort(ports, doc["big"], index, reason));
        EXPECT_EQ(index, Text::NoPort);
        EXPECT_NE(reason.find("out of range"), AZStd::string::npos) << reason.c_str();
        EXPECT_NE(reason.find("\"Weight\" (2)"), AZStd::string::npos) << reason.c_str();
        EXPECT_FALSE(Text::ResolvePort(ports, doc["neg"], index, reason)); // a negative number is not an index
        EXPECT_FALSE(Text::ResolvePort(ports, doc["no"], index, reason));
        EXPECT_NE(reason.find("no port named 'Mask'"), AZStd::string::npos) << reason.c_str();
        EXPECT_NE(reason.find("\"Pose 1\" (0), \"Pose 2\" (1), \"Weight\" (2)"), AZStd::string::npos) << reason.c_str();
        EXPECT_FALSE(Text::ResolvePort(ports, doc["o"], index, reason));
        EXPECT_EQ(Text::PortNames({}), "none");
        EXPECT_FALSE(Text::ResolvePort({}, doc["i"], index, reason));
        EXPECT_NE(reason.find("the ports are none"), AZStd::string::npos) << reason.c_str();
    }

    // -- Transition, connection and node commands ----------------------------

    TEST_F(AnimGraphCommandTextFixture, CreateTransitionCommand_NamedAndWildcard)
    {
        EXPECT_EQ(
            Text::CreateTransitionCommand(7, "Idle", "Walk", "123"),
            "AnimGraphCreateConnection -animGraphID 7 -sourceNode \"Idle\" -targetNode \"Walk\" -sourcePort 0 -targetPort 0 "
            "-startOffsetX 0 -startOffsetY 0 -endOffsetX 0 -endOffsetY 0 -id 123 -transitionType {E69C8C6E-7066-43DD-B1BF-0D2FFBDDF457}");
        // A wildcard transition names no source: the engine finds no node
        // named "" and sets the wildcard flag.
        EXPECT_EQ(
            Text::CreateTransitionCommand(7, "", "Idle", "456"),
            "AnimGraphCreateConnection -animGraphID 7 -sourceNode \"\" -targetNode \"Idle\" -sourcePort 0 -targetPort 0 "
            "-startOffsetX 0 -startOffsetY 0 -endOffsetX 0 -endOffsetY 0 -id 456 -transitionType {E69C8C6E-7066-43DD-B1BF-0D2FFBDDF457}");
    }

    TEST_F(AnimGraphCommandTextFixture, AdjustTransitionCommand_OnlyTheGivenFields)
    {
        Text::TransitionAdjustments none;
        EXPECT_FALSE(none.Any());
        EXPECT_EQ(Text::AdjustTransitionCommand(7, "123", none), "AnimGraphAdjustTransition -animGraphId 7 -transitionId 123");

        Text::TransitionAdjustments blend;
        blend.m_hasBlendTime = true;
        blend.m_blendTime = 0.25f;
        EXPECT_TRUE(blend.Any());
        EXPECT_EQ(
            Text::AdjustTransitionCommand(7, "123", blend),
            "AnimGraphAdjustTransition -animGraphId 7 -transitionId 123 -attributesString {-transitionTime {0.25}}");

        Text::TransitionAdjustments all;
        all.m_hasBlendTime = true;
        all.m_blendTime = 0.5f;
        all.m_hasPriority = true;
        all.m_priority = 3;
        all.m_hasDisabled = true;
        all.m_disabled = true;
        all.m_hasSyncMode = true;
        all.m_syncMode = 2;
        all.m_hasInterpolation = true;
        all.m_interpolation = 1;
        EXPECT_EQ(
            Text::AdjustTransitionCommand(7, "123", all),
            "AnimGraphAdjustTransition -animGraphId 7 -transitionId 123 -isDisabled true "
            "-attributesString {-transitionTime {0.5} -priority {3} -syncMode {2} -interpolationType {1}}");

        Text::TransitionAdjustments enable;
        enable.m_hasDisabled = true;
        enable.m_disabled = false;
        EXPECT_EQ(
            Text::AdjustTransitionCommand(7, "123", enable),
            "AnimGraphAdjustTransition -animGraphId 7 -transitionId 123 -isDisabled false");
    }

    TEST_F(AnimGraphCommandTextFixture, ConditionAndRemoveTransitionCommands)
    {
        EXPECT_EQ(
            Text::AddConditionCommand(
                7, "123", "{458D0D08-3F1E-4116-89FC-50F447EDC84E}", "<ObjectStream version=\"3\">\n</ObjectStream>\n"),
            "AnimGraphAddCondition -animGraphId 7 -transitionId 123 -conditionType {458D0D08-3F1E-4116-89FC-50F447EDC84E} "
            "-contents {<ObjectStream version=\"3\">\n</ObjectStream>\n}");
        EXPECT_EQ(
            Text::RemoveTransitionCommand(7, "Idle", "Walk", "123"),
            "AnimGraphRemoveConnection -animGraphID 7 -sourceNode \"Idle\" -targetNode \"Walk\" -sourcePort 0 -targetPort 0 -id 123");
        EXPECT_EQ(
            Text::RemoveTransitionCommand(7, "", "Idle", "456"),
            "AnimGraphRemoveConnection -animGraphID 7 -sourceNode \"\" -targetNode \"Idle\" -sourcePort 0 -targetPort 0 -id 456");
    }

    TEST_F(AnimGraphCommandTextFixture, PortConnectionCommands_ByIndexWithoutTransitionType)
    {
        EXPECT_EQ(
            Text::CreatePortConnectionCommand(7, "Blend", "Final Node", 0, 2),
            "AnimGraphCreateConnection -animGraphID 7 -sourceNode \"Blend\" -targetNode \"Final Node\" -sourcePort 0 -targetPort 2 "
            "-startOffsetX 0 -startOffsetY 0 -endOffsetX 0 -endOffsetY 0");
        EXPECT_EQ(
            Text::RemovePortConnectionCommand(7, "Blend", "Final Node", 0, 2),
            "AnimGraphRemoveConnection -animGraphID 7 -sourceNode \"Blend\" -targetNode \"Final Node\" -sourcePort 0 -targetPort 2");
    }

    TEST_F(AnimGraphCommandTextFixture, AttributesString_BracesEachValue)
    {
        EXPECT_EQ(Text::AttributesString({}), "");
        EXPECT_EQ(Text::AttributesString({ { "playSpeed", "-0.5" } }), "-playSpeed {-0.5}");
        EXPECT_EQ(
            Text::AttributesString({ { "loop", "false" }, { "motionIds", "<ObjectStream version=\"3\">\n</ObjectStream>" } }),
            "-loop {false} -motionIds {<ObjectStream version=\"3\">\n</ObjectStream>}");
    }

    TEST_F(AnimGraphCommandTextFixture, AdjustNodeCommand_OnlyTheGivenPieces)
    {
        Text::NodeAdjustments none;
        EXPECT_EQ(Text::AdjustNodeCommand(7, "Idle", none), "AnimGraphAdjustNode -animGraphID 7 -name \"Idle\" -updateAttributes true");

        Text::NodeAdjustments rename;
        rename.m_newName = "Stand Still";
        rename.m_hasPosition = true;
        rename.m_xPos = -10;
        rename.m_yPos = 20;
        EXPECT_EQ(
            Text::AdjustNodeCommand(7, "Idle", rename),
            "AnimGraphAdjustNode -animGraphID 7 -name \"Idle\" -newName \"Stand Still\" -xPos -10 -yPos 20 -updateAttributes true");

        Text::NodeAdjustments disable;
        disable.m_hasEnabled = true;
        disable.m_enabled = false;
        disable.m_attributes = { { "loop", "false" }, { "playSpeed", "1.5" } };
        EXPECT_EQ(
            Text::AdjustNodeCommand(7, "Idle", disable),
            "AnimGraphAdjustNode -animGraphID 7 -name \"Idle\" -enabled false -updateAttributes true "
            "-attributesString {-loop {false} -playSpeed {1.5}}");
    }

    // -- Paths ---------------------------------------------------------------

    TEST_F(AnimGraphCommandTextFixture, ResolveAgainstRoot_JoinsRelativeKeepsAbsoluteAndNormalizes)
    {
        const AZ::IO::FixedMaxPath root(Root);
        AZ::IO::FixedMaxPath expected = root / "Assets" / "x.animgraph";
        EXPECT_EQ(Text::ResolveAgainstRoot("Assets/x.animgraph", root), expected);
        EXPECT_EQ(Text::ResolveAgainstRoot("./Assets/sub/../x.animgraph", root), expected);
        EXPECT_EQ(Text::ResolveAgainstRoot(expected.c_str(), root), expected);
        // A climbing path resolves to where it really points, so the root
        // check below can refuse it.
        const AZ::IO::FixedMaxPath climbed = Text::ResolveAgainstRoot("../outside.animgraph", root);
        EXPECT_FALSE(Text::IsUnderRoot(climbed, root)) << climbed.c_str();
    }

    TEST_F(AnimGraphCommandTextFixture, IsUnderRoot_ComparesByComponent)
    {
        const AZ::IO::FixedMaxPath root(Root);
        EXPECT_TRUE(Text::IsUnderRoot(root / "Assets" / "x.animgraph", root));
        EXPECT_TRUE(Text::IsUnderRoot(root / "x.animgraph", root));
        EXPECT_FALSE(Text::IsUnderRoot(AZ::IO::FixedMaxPath(AZStd::string(Root) + "2") / "x.animgraph", root)); // "/proj2" is not "/proj"
        EXPECT_FALSE(Text::IsUnderRoot(AZ::IO::FixedMaxPath(Root).ParentPath(), root));
        EXPECT_FALSE(Text::IsUnderRoot(root / "x.animgraph", AZ::IO::PathView()));
        EXPECT_FALSE(Text::IsUnderRoot(AZ::IO::PathView(), root));
    }
} // namespace UnitTest
