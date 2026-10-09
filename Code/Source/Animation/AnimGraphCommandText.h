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
#include <AzCore/std/utility/pair.h>

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

    //! A float as the engine's text parsers read it: %.9g in the C locale,
    //! whatever the process locale (survey 0.5).
    AZStd::string FloatText(double value);

    //! A JSON scalar as the text ReflectionSerializer::DeserializeIntoMember
    //! hands a field's serializer: an integer as decimal, another number as
    //! FloatText, a bool as the exact words true and false, a string
    //! verbatim under the command-text character rule. False with a reason
    //! for an array, object or null.
    bool FormatScalarText(const rapidjson::Value& value, AZStd::string& outText, AZStd::string& outReason);

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

    // -- Transitions and conditions -----------------------------------------

    //! AnimGraphStateTransition's AZ_RTTI id, the -transitionType that makes
    //! AnimGraphCreateConnection create a state transition
    //! (AnimGraphStateTransition.h:33; survey 3).
    inline constexpr const char* StateTransitionTypeUuid = "{E69C8C6E-7066-43DD-B1BF-0D2FFBDDF457}";

    //! How a condition attribute is typed on the wire and formatted for
    //! MCore::ReflectionSerializer::DeserializeIntoMember (survey 4).
    enum class AttributeKind : AZ::u8
    {
        Float, //!< a JSON number; FloatText
        Count, //!< a JSON integer from 0 to 2^32-1; decimal
        Bool, //!< a JSON bool; true or false
        String, //!< a JSON string under the command-text rule; verbatim
        NodeId, //!< a node id, decimal string or number; the decimal u64 (the engine-facing code checks the node exists)
        Enum, //!< a JSON integer the enum defines, or one of its names; decimal
        StringList //!< a JSON array of strings; the engine-facing code serializes the vector to ObjectStream XML
    };

    struct EnumName
    {
        const char* m_name; //!< e.g. "GREATER"
        int m_value;
    };

    struct EnumTable
    {
        const char* m_prefix; //!< the engine identifier's prefix, e.g. "FUNCTION_", accepted in front of a name too
        const EnumName* m_names;
        size_t m_count;
    };

    struct ConditionAttribute
    {
        const char* m_key; //!< the reflected serialize field name, e.g. "parameterName"
        AttributeKind m_kind;
        const EnumTable* m_enum; //!< for AttributeKind::Enum, else null
    };

    struct ConditionType
    {
        const char* m_shortName; //!< the name a request passes, e.g. "ParameterCondition"
        const char* m_rttiName; //!< the engine class, e.g. "AnimGraphParameterCondition"; accepted as a name too
        const char* m_uuid; //!< the class's AZ_RTTI id, braced and dashed, as -conditionType takes it
        const ConditionAttribute* m_attributes;
        size_t m_attributeCount;
    };

    size_t ConditionTypeCount();
    const ConditionType& ConditionTypeAt(size_t index);

    //! Resolves a request's condition_type, case-insensitively, against each
    //! short name and each class name. Null when unknown.
    const ConditionType* FindConditionType(const AZStd::string& name);

    //! "ParameterCondition, TimeCondition, ..." for the unknown-type message.
    AZStd::string ConditionTypeNames();

    //! The attribute descriptor for `key` on `type` (exact, case-sensitive:
    //! the serialize field names are), or null.
    const ConditionAttribute* FindConditionAttribute(const ConditionType& type, const AZStd::string& key);

    //! "parameterName, function, ..." for the unsupported-attribute message.
    AZStd::string ConditionAttributeNames(const ConditionType& type);

    //! The names of an enum joined with ", " for a message.
    AZStd::string EnumNames(const EnumTable& table);

    //! A formatted condition attribute: the text for every kind but
    //! StringList, whose strings wait for the engine-facing serializer.
    struct AttributeValue
    {
        AZStd::string m_text;
        AZStd::vector<AZStd::string> m_list;
    };

    //! Formats a JSON value for `attribute`'s kind. False with a reason for a
    //! value of the wrong shape, an enum value or name the table lacks, a
    //! malformed node id, or a string carrying the parser's characters.
    bool FormatConditionAttribute(
        const ConditionAttribute& attribute, const rapidjson::Value& value, AttributeValue& out, AZStd::string& outReason);

    // -- Ports --------------------------------------------------------------

    inline constexpr size_t NoPort = static_cast<size_t>(-1);

    //! Resolves a request's port, a JSON unsigned integer (the index) or a
    //! string (the port name, matched exactly and then case-insensitively,
    //! since the engine's own lookup is exact and answers InvalidIndex
    //! otherwise, AnimGraphNode.cpp:515-533), against `portNames`. False with
    //! a reason that lists the ports for an out-of-range index, an unknown
    //! name or another JSON shape.
    bool ResolvePort(
        const AZStd::vector<AZStd::string>& portNames, const rapidjson::Value& spec, size_t& outIndex, AZStd::string& outReason);

    //! "\"Pose 1\" (0), \"Pose 2\" (1), ..." for a message; "none" when empty.
    AZStd::string PortNames(const AZStd::vector<AZStd::string>& portNames);

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

    //! AnimGraphCreateConnection -animGraphID <id> -sourceNode "<source>"
    //! -targetNode "<target>" -sourcePort 0 -targetPort 0 -startOffsetX 0
    //! -startOffsetY 0 -endOffsetX 0 -endOffsetY 0 -id <transition id>
    //! -transitionType {E69C8C6E-...}. An empty source makes a wildcard
    //! transition (the engine finds no node named "" and sets the flag,
    //! AnimGraphConnectionCommands.cpp:102, 246-266); -id fixes the id so
    //! the commands that follow in the same group can name it.
    AZStd::string CreateTransitionCommand(
        AZ::u32 animGraphId, const AZStd::string& sourceName, const AZStd::string& targetName, const AZStd::string& transitionId);

    struct TransitionAdjustments
    {
        float m_blendTime = 0.0f;
        AZ::u32 m_priority = 0;
        bool m_disabled = false;
        int m_syncMode = 0;
        int m_interpolation = 0;
        bool m_hasBlendTime = false;
        bool m_hasPriority = false;
        bool m_hasDisabled = false;
        bool m_hasSyncMode = false;
        bool m_hasInterpolation = false;

        bool Any() const
        {
            return m_hasBlendTime || m_hasPriority || m_hasDisabled || m_hasSyncMode || m_hasInterpolation;
        }
    };

    //! AnimGraphAdjustTransition -animGraphId <id> -transitionId <id>
    //! [-isDisabled true|false] [-attributesString {-transitionTime <f>
    //! -priority <u> -syncMode <d> -interpolationType <d>}], each piece only
    //! when given. The attribute names are AnimGraphStateTransition's
    //! reflected fields (AnimGraphStateTransition.cpp:1002-1030); the enums
    //! serialize as their underlying integer.
    AZStd::string AdjustTransitionCommand(AZ::u32 animGraphId, const AZStd::string& transitionId, const TransitionAdjustments& adjustments);

    //! AnimGraphAddCondition -animGraphId <id> -transitionId <id>
    //! -conditionType {uuid} -contents {<ObjectStream XML>}. -contents is a
    //! required parameter of the command (survey 4) and goes last, since the
    //! XML is free text.
    AZStd::string AddConditionCommand(
        AZ::u32 animGraphId, const AZStd::string& transitionId, const AZStd::string& conditionUuid, const AZStd::string& contentsXml);

    //! AnimGraphRemoveConnection -animGraphID <id> -sourceNode "<source>"
    //! -targetNode "<target>" -sourcePort 0 -targetPort 0 -id <transition id>.
    //! The ports are required by the syntax and unused for a state
    //! transition, which the command finds by -id
    //! (AnimGraphConnectionCommands.cpp:534-548, 651-661); an empty source
    //! is a wildcard transition.
    AZStd::string RemoveTransitionCommand(
        AZ::u32 animGraphId, const AZStd::string& sourceName, const AZStd::string& targetName, const AZStd::string& transitionId);

    //! AnimGraphCreateConnection -animGraphID <id> -sourceNode "<source>"
    //! -targetNode "<target>" -sourcePort <s> -targetPort <t> -startOffsetX 0
    //! -startOffsetY 0 -endOffsetX 0 -endOffsetY 0: a blend tree connection
    //! (no -transitionType). Ports go by index; the gem resolves names first.
    AZStd::string CreatePortConnectionCommand(
        AZ::u32 animGraphId, const AZStd::string& sourceName, const AZStd::string& targetName, size_t sourcePort, size_t targetPort);

    //! AnimGraphRemoveConnection -animGraphID <id> -sourceNode "<source>"
    //! -targetNode "<target>" -sourcePort <s> -targetPort <t>.
    AZStd::string RemovePortConnectionCommand(
        AZ::u32 animGraphId, const AZStd::string& sourceName, const AZStd::string& targetName, size_t sourcePort, size_t targetPort);

    //! "-<field> {<text>} -<field> {<text>}": the -attributesString syntax,
    //! each value braced so multi-token text, negative numbers and XML stay
    //! one value (the engine's own SerializeIntoCommandLine form,
    //! ReflectionSerializer.cpp:302-323).
    AZStd::string AttributesString(const AZStd::vector<AZStd::pair<AZStd::string, AZStd::string>>& fields);

    struct NodeAdjustments
    {
        AZStd::string m_newName; //!< empty: keep the name
        int m_xPos = 0;
        int m_yPos = 0;
        bool m_hasPosition = false;
        bool m_enabled = true;
        bool m_hasEnabled = false;
        AZStd::vector<AZStd::pair<AZStd::string, AZStd::string>> m_attributes; //!< field name, formatted text
    };

    //! AnimGraphAdjustNode -animGraphID <id> -name "<current>"
    //! [-newName "<name>"] [-xPos <x> -yPos <y>] [-enabled true|false]
    //! -updateAttributes true [-attributesString {...}], the attributes last
    //! since they may hold XML. -updateAttributes makes the node Reinit
    //! after the change (AnimGraphNodeCommands.cpp:572-577).
    AZStd::string AdjustNodeCommand(AZ::u32 animGraphId, const AZStd::string& currentName, const NodeAdjustments& adjustments);

    // -- Paths --------------------------------------------------------------

    //! A request path made absolute: a relative one is joined onto `root`,
    //! an absolute one is kept, and the result is lexically normalized (so
    //! "../x" cannot escape the root unnoticed). No file system access.
    AZ::IO::FixedMaxPath ResolveAgainstRoot(const AZStd::string& input, const AZ::IO::PathView& root);

    //! Whether `path` lies inside `root`, by path components (so "/proj2"
    //! is not under "/proj"). False for an empty root.
    bool IsUnderRoot(const AZ::IO::PathView& path, const AZ::IO::PathView& root);
} // namespace AiCompanion::AnimGraphCommandText
