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
