/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/IO/Path/Path.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/RTTI/TypeInfo.h>
#include <AzCore/base.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion::AnimGraphCommandText
{
    //! The text rules and command-line builders behind the anim graph
    //! authoring request types. Nothing here touches EMotion FX objects, so
    //! the unit tests cover every string the gem sends to the command system
    //! (see Animation/AnimGraphAuthoring for the engine-facing half).
    //!
    //! Command-line parser rules (MCore/Source/CommandLine.cpp:400-514): a
    //! parameter starts at a '-' outside quotes and braces; a value ends at a
    //! '-' preceded by a space outside quotes and braces; each '"' toggles
    //! quoting; '{' and '}' nest; one leading '{', one trailing '}' and the
    //! surrounding '"' are stripped from a value; there is no escape
    //! character. Names are therefore always quoted and free text and
    //! multi-token values are wrapped in braces.

    //! Whether `name` can be a node, parameter or group name. Refuses an
    //! empty name, control characters, and the four characters the engine's
    //! own parameter rule forbids (EMotionFX/Source/Parameter/Parameter.cpp:51,
    //! s_invalidCharacters = '"', '%', '{', '}'), which are also the parser's
    //! syntax characters and the command group's %LASTRESULT% marker.
    bool IsValidObjectName(const AZStd::string& name, AZStd::string& outReason);

    //! The same character rule for free text that travels inside braces (a
    //! description, a string default) and for file paths. Empty is allowed.
    bool IsValidCommandText(const AZStd::string& text, AZStd::string& outReason);

    //! "text" and {text}.
    AZStd::string Quoted(const AZStd::string& text);
    AZStd::string Braced(const AZStd::string& text);

    //! Reads a node or transition id in its wire form: a decimal u64 string.
    //! Refuses anything but digits, 0 and 2^64-1 (both are
    //! EMotionFX::ObjectId::InvalidId; ObjectId.cpp:42-52).
    bool ParseObjectId(const AZStd::string& text, AZ::u64& outId);

    // -- Parameters ---------------------------------------------------------

    //! How a parameter's default, min and max are typed on the wire and
    //! formatted for the engine's TextToData.
    enum class ValueKind : AZ::u8
    {
        Float, //!< a JSON number
        Int, //!< a JSON integer
        Bool, //!< a JSON bool
        String, //!< a JSON string
        Vector2, //!< [x, y]
        Vector3, //!< [x, y, z]
        Vector4, //!< [x, y, z, w]
        Color, //!< [r, g, b] or [r, g, b, a]
        Rotation //!< a quaternion [x, y, z, w]
    };

    struct ParameterType
    {
        const char* m_friendlyName; //!< The name a request passes, e.g. "FloatSlider".
        const char* m_className; //!< The engine class, e.g. "FloatSliderParameter"; accepted as a name too.
        const char* m_uuid; //!< The class's AZ_RTTI id, braced and dashed, as -type takes it.
        ValueKind m_kind;
        bool m_ranged; //!< A RangedValueParameter: takes min and max.
    };

    size_t ParameterTypeCount();
    const ParameterType& ParameterTypeAt(size_t index);

    //! Resolves a request's parameter type name, case-insensitively, against
    //! each friendly name, each class name, and the aliases "Float"
    //! (FloatSlider) and "Int" (IntSlider). Null when unknown.
    const ParameterType* FindParameterType(const AZStd::string& name);

    //! "Float, FloatSlider, FloatSpinner, ..." for the unknown-type message.
    AZStd::string ParameterTypeNames();

    //! Formats a JSON value as the text the engine's serializers parse for
    //! `kind` (AzCore/Serialization/SerializeContext.cpp:355-360 for bool,
    //! AzCore/Math/MathScriptHelpers.cpp:157-171 for vectors, colors and
    //! quaternions): floats in the C locale, bools as the exact words true
    //! and false, strings verbatim, vector-likes as space-separated %.7f.
    //! Returns false with a reason for a value of the wrong JSON shape.
    bool FormatParameterValue(const rapidjson::Value& value, ValueKind kind, AZStd::string& outText, AZStd::string& outReason);

    // -- Nodes --------------------------------------------------------------

    //! One creatable AnimGraphNode class, as the engine-facing code collects
    //! it from the object factory's prototypes.
    struct NodeType
    {
        AZStd::string m_rttiName; //!< e.g. "AnimGraphMotionNode"
        AZStd::string m_paletteName; //!< e.g. "Motion"
        AZ::TypeId m_typeId;
        bool m_canActAsState = false;
        bool m_canHaveChildren = false;
        bool m_insideStateMachineOnly = false;
        bool m_insideChildStateMachineOnly = false;
        bool m_onlyOneInsideParent = false;
    };

    inline constexpr size_t NoNodeType = static_cast<size_t>(-1);

    //! The index of the type whose RTTI name or palette name equals `name`
    //! case-insensitively, or NoNodeType.
    size_t FindNodeType(const AZStd::vector<NodeType>& types, const AZStd::string& name);

    //! The RTTI names joined with ", " for the unknown-type message.
    AZStd::string NodeTypeNames(const AZStd::vector<NodeType>& types);

    //! The -namePrefix for a generated node name: the RTTI name without its
    //! "AnimGraph" prefix, and without its "BlendTree" prefix unless the type
    //! is BlendTree itself. The engine applies the same strip to the name it
    //! generates (CommandSystem::GenerateUniqueNodeName,
    //! AnimGraphNodeCommands.cpp:34-57), so "MotionNode0" is what a user of
    //! the Animation Editor also gets.
    AZStd::string GeneratedNamePrefix(const AZStd::string& rttiName);

    // -- Command lines ------------------------------------------------------

    //! AnimGraphCreateNode -animGraphID <id> -type <uuid> -parentName "<parent>"
    //! -name "<name>" -xPos <x> -yPos <y>; with an empty name, -name GENERATE
    //! -namePrefix "<namePrefix>" instead, so the engine picks "<prefix><n>".
    //! -parentName is never omitted: without it a state machine type leaks a
    //! parentless node after an assert (AnimGraphNodeCommands.cpp:342-347).
    AZStd::string CreateNodeCommand(
        AZ::u32 animGraphId,
        const AZStd::string& typeUuid,
        const AZStd::string& parentName,
        const AZStd::string& name,
        const AZStd::string& namePrefix,
        int xPos,
        int yPos);

    //! AnimGraphRemoveNode -animGraphID <id> -name "<node>"
    AZStd::string RemoveNodeCommand(AZ::u32 animGraphId, const AZStd::string& nodeName);

    //! AnimGraphSetEntryState -animGraphID <id> -entryNodeName "<node>"
    AZStd::string SetEntryStateCommand(AZ::u32 animGraphId, const AZStd::string& nodeName);

    struct ParameterSpec
    {
        AZStd::string m_typeUuid;
        AZStd::string m_name;
        AZStd::string m_defaultText;
        AZStd::string m_minText;
        AZStd::string m_maxText;
        AZStd::string m_description;
        AZStd::string m_group;
        bool m_hasDefault = false;
        bool m_hasMin = false;
        bool m_hasMax = false;
        bool m_hasDescription = false;
        bool m_hasGroup = false;
    };

    //! AnimGraphCreateParameter -animGraphID <id> -type <uuid> -name "<name>"
    //! [-defaultValue {..}] [-minValue {..}] [-maxValue {..}]
    //! [-description {..}] [-parent "<group>"]. The engine applies min, then
    //! max, then default, whatever the order here
    //! (AnimGraphParameterCommands_Impl.inl:70-96).
    AZStd::string CreateParameterCommand(AZ::u32 animGraphId, const ParameterSpec& spec);

    //! AnimGraphAddGroupParameter -animGraphID <id> -name "<group>"
    AZStd::string AddGroupParameterCommand(AZ::u32 animGraphId, const AZStd::string& groupName);

    //! AnimGraphRemoveParameter -animGraphID <id> -name "<name>"
    AZStd::string RemoveParameterCommand(AZ::u32 animGraphId, const AZStd::string& name);

    //! LoadAnimGraph -filename "<absolute path>"
    AZStd::string LoadCommand(const AZStd::string& absolutePath);

    //! SaveAnimGraph -filename "<absolute path>" -index <manager index>
    //! -sourceControl false. The index is the AnimGraphManager index, not the
    //! graph id (EMStudioSDK/Source/Commands.cpp:554-562); -sourceControl
    //! false keeps RequestEditForFileBlocking from blocking the main thread.
    AZStd::string SaveCommand(const AZStd::string& absolutePath, size_t managerIndex);

    // -- Paths --------------------------------------------------------------

    //! A request path made absolute: a relative one is joined onto `root`,
    //! an absolute one is kept, and the result is lexically normalized (so
    //! "../x" cannot escape the root unnoticed). No file system access.
    AZ::IO::FixedMaxPath ResolveAgainstRoot(const AZStd::string& input, const AZ::IO::PathView& root);

    //! Whether `path` lies inside `root`, by path components (so "/proj2"
    //! is not under "/proj"). False for an empty root.
    bool IsUnderRoot(const AZ::IO::PathView& path, const AZ::IO::PathView& root);
} // namespace AiCompanion::AnimGraphCommandText
