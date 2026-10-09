/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AnimGraphInspector.h"

#include <AzCore/Component/EntityId.h>
#include <AzCore/Module/Environment.h>
#include <AzCore/std/string/conversions.h>

#include <EMotionFX/Source/ActorInstance.h>
#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphInstance.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/AnimGraphNode.h>
#include <EMotionFX/Source/AnimGraphNodeGroup.h>
#include <EMotionFX/Source/AnimGraphObject.h>
#include <EMotionFX/Source/AnimGraphStateMachine.h>
#include <EMotionFX/Source/AnimGraphStateTransition.h>
#include <EMotionFX/Source/AnimGraphTransitionCondition.h>
#include <EMotionFX/Source/BlendTreeConnection.h>
#include <EMotionFX/Source/EMotionFXManager.h>
#include <EMotionFX/Source/MotionSet.h>
#include <EMotionFX/Source/Parameter/FloatParameter.h>
#include <EMotionFX/Source/Parameter/GroupParameter.h>
#include <EMotionFX/Source/Parameter/IntParameter.h>
#include <EMotionFX/Source/Parameter/Parameter.h>
#include <EMotionFX/Source/Parameter/ValueParameter.h>
#include <EMotionFX/Source/Parameter/Vector2Parameter.h>
#include <EMotionFX/Source/Parameter/Vector3Parameter.h>
#include <EMotionFX/Source/Parameter/Vector4Parameter.h>
#include <MCore/Source/Attribute.h>

#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>

#include <cctype>
#include <cstdlib>

namespace AiCompanion::AnimGraphInspector
{
    namespace
    {
        using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

        constexpr const char* NotAvailable = "EMotion FX is not available";

        //! The EMotion FX manager, found through the AZ::Environment variable
        //! the EMotionFX gem publishes, so a missing gem is a null here rather
        //! than a crash inside GetEMotionFX().
        EMotionFX::EMotionFXManager* FindEMotionFX()
        {
            auto variable = AZ::Environment::FindVariable<EMotionFX::EMotionFXManager*>(EMotionFX::kEMotionFXInstanceVarName);
            if (!variable)
            {
                return nullptr;
            }
            return variable.Get();
        }

        AZStd::string IdString(const EMotionFX::ObjectId& id)
        {
            return AZStd::string::format("%llu", static_cast<unsigned long long>(static_cast<AZ::u64>(id)));
        }

        void WriteIdOrNull(Writer& w, const EMotionFX::ObjectId& id)
        {
            if (id.IsValid())
            {
                w.String(IdString(id).c_str());
            }
            else
            {
                w.Null();
            }
        }

        void WriteStringOrNull(Writer& w, const AZStd::string* text)
        {
            if (text)
            {
                w.String(text->c_str());
            }
            else
            {
                w.Null();
            }
        }

        size_t CountNodes(const EMotionFX::AnimGraphNode* node)
        {
            if (!node)
            {
                return 0;
            }
            size_t count = 1;
            for (size_t i = 0; i < node->GetNumChildNodes(); ++i)
            {
                count += CountNodes(node->GetChildNode(i));
            }
            return count;
        }

        size_t CountValueParameters(const EMotionFX::AnimGraph& graph)
        {
            return graph.RecursivelyGetValueParameters().size();
        }

        void WriteInstances(Writer& w, const EMotionFX::AnimGraph& graph)
        {
            w.StartArray();
            for (size_t i = 0; i < graph.GetNumAnimGraphInstances(); ++i)
            {
                const EMotionFX::AnimGraphInstance* instance = graph.GetAnimGraphInstance(i);
                if (!instance)
                {
                    continue;
                }
                w.StartObject();
                w.Key("entity_id");
                const EMotionFX::ActorInstance* actorInstance = instance->GetActorInstance();
                const AZ::EntityId entityId = actorInstance ? actorInstance->GetEntityId() : AZ::EntityId();
                if (entityId.IsValid())
                {
                    w.Uint64(static_cast<AZ::u64>(entityId));
                }
                else
                {
                    w.Null();
                }
                w.Key("actor_instance_id");
                w.Uint(actorInstance ? actorInstance->GetID() : 0);
                w.Key("motion_set");
                if (const EMotionFX::MotionSet* motionSet = instance->GetMotionSet())
                {
                    w.String(motionSet->GetName());
                }
                else
                {
                    w.Null();
                }
                w.EndObject();
            }
            w.EndArray();
        }

        // -- get_anim_graph pieces --------------------------------------------

        void WritePorts(Writer& w, const EMotionFX::AnimGraphNode& node)
        {
            w.Key("input_ports");
            w.StartArray();
            const auto& inputs = node.GetInputPorts();
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                const EMotionFX::AnimGraphNode::Port& port = inputs[i];
                w.StartObject();
                w.Key("index");
                w.Uint64(i);
                w.Key("name");
                w.String(port.GetName());
                w.Key("connection");
                if (const EMotionFX::BlendTreeConnection* connection = port.m_connection)
                {
                    w.StartObject();
                    w.Key("source_node_id");
                    WriteIdOrNull(w, connection->GetSourceNodeId());
                    w.Key("source_port");
                    w.Uint(connection->GetSourcePort());
                    w.EndObject();
                }
                else
                {
                    w.Null();
                }
                w.EndObject();
            }
            w.EndArray();

            w.Key("output_ports");
            w.StartArray();
            const auto& outputs = node.GetOutputPorts();
            for (size_t i = 0; i < outputs.size(); ++i)
            {
                w.StartObject();
                w.Key("index");
                w.Uint64(i);
                w.Key("name");
                w.String(outputs[i].GetName());
                w.EndObject();
            }
            w.EndArray();
        }

        void WriteNode(Writer& w, const EMotionFX::AnimGraphNode& node)
        {
            w.StartObject();
            w.Key("id");
            w.String(IdString(node.GetId()).c_str());
            w.Key("name");
            w.String(node.GetName());
            w.Key("type");
            w.String(node.RTTI_GetTypeName());
            w.Key("palette_name");
            w.String(node.GetPaletteName());
            w.Key("category");
            w.String(EMotionFX::AnimGraphObject::GetCategoryName(node.GetPaletteCategory()));
            w.Key("parent_id");
            if (const EMotionFX::AnimGraphNode* parent = node.GetParentNode())
            {
                w.String(IdString(parent->GetId()).c_str());
            }
            else
            {
                w.Null();
            }
            w.Key("can_act_as_state");
            w.Bool(node.GetCanActAsState());
            w.Key("has_output_pose");
            w.Bool(node.GetHasOutputPose());
            w.Key("enabled");
            w.Bool(node.GetIsEnabled());
            w.Key("position");
            w.StartArray();
            w.Int(node.GetVisualPosX());
            w.Int(node.GetVisualPosY());
            w.EndArray();
            WritePorts(w, node);
            w.EndObject();
        }

        void WriteNodesRecursive(Writer& w, const EMotionFX::AnimGraphNode* node)
        {
            if (!node)
            {
                return;
            }
            WriteNode(w, *node);
            for (size_t i = 0; i < node->GetNumChildNodes(); ++i)
            {
                WriteNodesRecursive(w, node->GetChildNode(i));
            }
        }

        void WriteTransition(
            Writer& w, const EMotionFX::AnimGraphStateMachine& stateMachine, const EMotionFX::AnimGraphStateTransition& transition)
        {
            w.StartObject();
            w.Key("id");
            w.String(IdString(transition.GetId()).c_str());
            w.Key("state_machine_id");
            w.String(IdString(stateMachine.GetId()).c_str());
            w.Key("source_node_id");
            // A wildcard transition has no fixed source.
            WriteIdOrNull(w, transition.GetIsWildcardTransition() ? EMotionFX::ObjectId() : transition.GetSourceNodeId());
            w.Key("target_node_id");
            WriteIdOrNull(w, transition.GetTargetNodeId());
            w.Key("wildcard");
            w.Bool(transition.GetIsWildcardTransition());
            w.Key("blend_time");
            w.Double(transition.GetBlendTime(nullptr));
            w.Key("priority");
            w.Uint(transition.GetPriority());
            w.Key("disabled");
            w.Bool(transition.GetIsDisabled());
            w.Key("conditions");
            w.StartArray();
            for (size_t i = 0; i < transition.GetNumConditions(); ++i)
            {
                const EMotionFX::AnimGraphTransitionCondition* condition = transition.GetCondition(i);
                if (!condition)
                {
                    continue;
                }
                AZStd::string summary;
                condition->GetSummary(&summary);
                w.StartObject();
                w.Key("type");
                w.String(condition->RTTI_GetTypeName());
                w.Key("summary");
                w.String(summary.c_str());
                w.EndObject();
            }
            w.EndArray();
            w.EndObject();
        }

        void WriteTransitionsRecursive(Writer& w, EMotionFX::AnimGraphNode* node)
        {
            if (!node)
            {
                return;
            }
            if (auto* stateMachine = azrtti_cast<EMotionFX::AnimGraphStateMachine*>(node))
            {
                for (size_t i = 0; i < stateMachine->GetNumTransitions(); ++i)
                {
                    if (const EMotionFX::AnimGraphStateTransition* transition = stateMachine->GetTransition(i))
                    {
                        WriteTransition(w, *stateMachine, *transition);
                    }
                }
            }
            for (size_t i = 0; i < node->GetNumChildNodes(); ++i)
            {
                WriteTransitionsRecursive(w, node->GetChildNode(i));
            }
        }

        AZStd::string FormatFloat(float value)
        {
            return AZStd::string::format("%g", static_cast<double>(value));
        }

        //! The range of a parameter type that has one, formatted as text;
        //! other parameter types (bool, string, color, rotation, tag) have no
        //! range and get nulls.
        bool ParameterRange(const EMotionFX::ValueParameter& parameter, AZStd::string& outMin, AZStd::string& outMax)
        {
            if (const auto* f = azrtti_cast<const EMotionFX::FloatParameter*>(&parameter))
            {
                outMin = FormatFloat(f->GetMinValue());
                outMax = FormatFloat(f->GetMaxValue());
                return true;
            }
            if (const auto* i = azrtti_cast<const EMotionFX::IntParameter*>(&parameter))
            {
                outMin = AZStd::string::format("%d", i->GetMinValue());
                outMax = AZStd::string::format("%d", i->GetMaxValue());
                return true;
            }
            if (const auto* v = azrtti_cast<const EMotionFX::Vector2Parameter*>(&parameter))
            {
                const AZ::Vector2 lo = v->GetMinValue();
                const AZ::Vector2 hi = v->GetMaxValue();
                outMin = FormatFloat(lo.GetX()) + "," + FormatFloat(lo.GetY());
                outMax = FormatFloat(hi.GetX()) + "," + FormatFloat(hi.GetY());
                return true;
            }
            if (const auto* v = azrtti_cast<const EMotionFX::Vector3Parameter*>(&parameter))
            {
                const AZ::Vector3 lo = v->GetMinValue();
                const AZ::Vector3 hi = v->GetMaxValue();
                outMin = FormatFloat(lo.GetX()) + "," + FormatFloat(lo.GetY()) + "," + FormatFloat(lo.GetZ());
                outMax = FormatFloat(hi.GetX()) + "," + FormatFloat(hi.GetY()) + "," + FormatFloat(hi.GetZ());
                return true;
            }
            if (const auto* v = azrtti_cast<const EMotionFX::Vector4Parameter*>(&parameter))
            {
                const AZ::Vector4 lo = v->GetMinValue();
                const AZ::Vector4 hi = v->GetMaxValue();
                outMin =
                    FormatFloat(lo.GetX()) + "," + FormatFloat(lo.GetY()) + "," + FormatFloat(lo.GetZ()) + "," + FormatFloat(lo.GetW());
                outMax =
                    FormatFloat(hi.GetX()) + "," + FormatFloat(hi.GetY()) + "," + FormatFloat(hi.GetZ()) + "," + FormatFloat(hi.GetW());
                return true;
            }
            return false;
        }

        void WriteValueParameter(Writer& w, const EMotionFX::ValueParameter& parameter, const AZStd::string* groupName)
        {
            w.StartObject();
            w.Key("name");
            w.String(parameter.GetName().c_str());
            w.Key("type");
            w.String(parameter.GetTypeDisplayName());
            w.Key("description");
            w.String(parameter.GetDescription().c_str());
            w.Key("default");
            AZStd::string defaultText;
            if (MCore::Attribute* attribute = parameter.ConstructDefaultValueAsAttribute())
            {
                attribute->ConvertToString(defaultText);
                delete attribute;
            }
            w.String(defaultText.c_str());
            AZStd::string minText;
            AZStd::string maxText;
            const bool ranged = ParameterRange(parameter, minText, maxText);
            w.Key("min");
            WriteStringOrNull(w, ranged ? &minText : nullptr);
            w.Key("max");
            WriteStringOrNull(w, ranged ? &maxText : nullptr);
            w.Key("group");
            WriteStringOrNull(w, groupName);
            w.EndObject();
        }

        //! Depth-first over the parameter tree; `groupName` is null at the
        //! root group (whose name is empty) and the enclosing group's name
        //! below it.
        void WriteParametersRecursive(Writer& w, const EMotionFX::ParameterVector& parameters, const AZStd::string* groupName)
        {
            for (const EMotionFX::Parameter* parameter : parameters)
            {
                if (!parameter)
                {
                    continue;
                }
                if (const auto* group = azrtti_cast<const EMotionFX::GroupParameter*>(parameter))
                {
                    WriteParametersRecursive(w, group->GetChildParameters(), &group->GetName());
                }
                else if (const auto* value = azrtti_cast<const EMotionFX::ValueParameter*>(parameter))
                {
                    WriteValueParameter(w, *value, groupName);
                }
            }
        }

        void WriteNodeGroups(Writer& w, const EMotionFX::AnimGraph& graph)
        {
            w.StartArray();
            for (size_t i = 0; i < graph.GetNumNodeGroups(); ++i)
            {
                const EMotionFX::AnimGraphNodeGroup* group = graph.GetNodeGroup(i);
                if (!group)
                {
                    continue;
                }
                w.StartObject();
                w.Key("name");
                w.String(group->GetName());
                w.Key("node_ids");
                w.StartArray();
                for (size_t n = 0; n < group->GetNumNodes(); ++n)
                {
                    w.String(IdString(group->GetNode(n)).c_str());
                }
                w.EndArray();
                w.EndObject();
            }
            w.EndArray();
        }

        bool EndsWithNoCase(const AZStd::string& text, const AZStd::string& tail)
        {
            if (tail.empty() || tail.size() > text.size())
            {
                return false;
            }
            const size_t offset = text.size() - tail.size();
            for (size_t i = 0; i < tail.size(); ++i)
            {
                if (tolower(static_cast<unsigned char>(text[offset + i])) != tolower(static_cast<unsigned char>(tail[i])))
                {
                    return false;
                }
            }
            return true;
        }

        //! A selector that is entirely decimal digits and fits a u32 is tried
        //! as a graph id first; any selector is then matched against file names.
        EMotionFX::AnimGraph* ResolveGraph(EMotionFX::AnimGraphManager& manager, const AZStd::string& selector)
        {
            if (!selector.empty() && selector.find_first_not_of("0123456789") == AZStd::string::npos)
            {
                char* end = nullptr;
                const unsigned long long parsed = strtoull(selector.c_str(), &end, 10);
                if (end && *end == '\0' && parsed <= 0xFFFFFFFFull)
                {
                    if (EMotionFX::AnimGraph* byId = manager.FindAnimGraphByID(static_cast<AZ::u32>(parsed)))
                    {
                        return byId;
                    }
                }
            }
            const size_t count = manager.GetNumAnimGraphs();
            for (size_t i = 0; i < count; ++i)
            {
                EMotionFX::AnimGraph* graph = manager.GetAnimGraph(i);
                if (graph && graph->GetFileNameString() == selector)
                {
                    return graph;
                }
            }
            for (size_t i = 0; i < count; ++i)
            {
                EMotionFX::AnimGraph* graph = manager.GetAnimGraph(i);
                if (graph && EndsWithNoCase(graph->GetFileNameString(), selector))
                {
                    return graph;
                }
            }
            return nullptr;
        }
    } // namespace

    AZ::Outcome<AZStd::string, AZStd::string> ListAnimGraphs()
    {
        EMotionFX::EMotionFXManager* emfx = FindEMotionFX();
        EMotionFX::AnimGraphManager* manager = emfx ? emfx->GetAnimGraphManager() : nullptr;
        if (!manager)
        {
            return AZ::Failure(AZStd::string(NotAvailable));
        }

        rapidjson::StringBuffer sb;
        Writer w(sb);
        w.StartObject();
        w.Key("editor_mode");
        w.Bool(emfx->GetIsInEditorMode());
        w.Key("anim_graphs");
        w.StartArray();
        const size_t count = manager->GetNumAnimGraphs();
        for (size_t i = 0; i < count; ++i)
        {
            const EMotionFX::AnimGraph* graph = manager->GetAnimGraph(i);
            if (!graph)
            {
                continue;
            }
            w.StartObject();
            w.Key("id");
            w.Uint(graph->GetID());
            w.Key("file_name");
            w.String(graph->GetFileName());
            w.Key("owned_by_runtime");
            w.Bool(graph->GetIsOwnedByRuntime());
            w.Key("owned_by_asset");
            w.Bool(graph->GetIsOwnedByAsset());
            w.Key("dirty");
            w.Bool(graph->GetDirtyFlag());
            w.Key("num_nodes");
            w.Uint64(CountNodes(graph->GetRootStateMachine()));
            w.Key("num_parameters");
            w.Uint64(CountValueParameters(*graph));
            w.Key("instances");
            WriteInstances(w, *graph);
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
        return AZ::Success(AZStd::string(sb.GetString()));
    }

    AZ::Outcome<AZStd::string, AZStd::string> DescribeAnimGraph(const AZStd::string& selector)
    {
        EMotionFX::EMotionFXManager* emfx = FindEMotionFX();
        EMotionFX::AnimGraphManager* manager = emfx ? emfx->GetAnimGraphManager() : nullptr;
        if (!manager)
        {
            return AZ::Failure(AZStd::string(NotAvailable));
        }
        EMotionFX::AnimGraph* graph = ResolveGraph(*manager, selector);
        if (!graph)
        {
            return AZ::Failure(AZStd::string::format("anim graph not found: %s", selector.c_str()));
        }

        EMotionFX::AnimGraphStateMachine* root = graph->GetRootStateMachine();

        rapidjson::StringBuffer sb;
        Writer w(sb);
        w.StartObject();
        w.Key("id");
        w.Uint(graph->GetID());
        w.Key("file_name");
        w.String(graph->GetFileName());
        w.Key("root_state_machine_id");
        WriteIdOrNull(w, root ? root->GetId() : EMotionFX::ObjectId());

        w.Key("nodes");
        w.StartArray();
        WriteNodesRecursive(w, root);
        w.EndArray();

        w.Key("transitions");
        w.StartArray();
        WriteTransitionsRecursive(w, root);
        w.EndArray();

        w.Key("parameters");
        w.StartArray();
        WriteParametersRecursive(w, graph->GetChildParameters(), nullptr);
        w.EndArray();

        w.Key("node_groups");
        WriteNodeGroups(w, *graph);

        w.EndObject();
        return AZ::Success(AZStd::string(sb.GetString()));
    }
} // namespace AiCompanion::AnimGraphInspector
