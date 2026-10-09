/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AnimGraphAuthoring.h"

#include "AnimGraphCommandText.h"
#include "AnimGraphInspector.h"
#include "Network/RequestError.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/JSON/document.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/Module/Environment.h>
#include <AzCore/RTTI/RTTI.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Utils/Utils.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/sort.h>

#include <EMotionFX/CommandSystem/Source/CommandManager.h>
#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphExitNode.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/AnimGraphMotionNode.h>
#include <EMotionFX/Source/AnimGraphNode.h>
#include <EMotionFX/Source/AnimGraphObject.h>
#include <EMotionFX/Source/AnimGraphObjectFactory.h>
#include <EMotionFX/Source/AnimGraphStateMachine.h>
#include <EMotionFX/Source/AnimGraphStateTransition.h>
#include <EMotionFX/Source/AnimGraphTransitionCondition.h>
#include <EMotionFX/Source/BlendTree.h>
#include <EMotionFX/Source/BlendTreeConnection.h>
#include <EMotionFX/Source/BlendTreeFinalNode.h>
#include <EMotionFX/Source/EMotionFXManager.h>
#include <EMotionFX/Source/Parameter/GroupParameter.h>
#include <EMotionFX/Source/Parameter/Parameter.h>
#include <EMotionFX/Source/Parameter/ParameterFactory.h>
#include <EMotionFX/Source/Parameter/ValueParameter.h>
#include <EMotionFX/Tools/EMotionStudio/EMStudioSDK/Source/EMStudioManager.h>
#include <MCore/Source/CommandGroup.h>
#include <MCore/Source/ReflectionSerializer.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace AiCompanion::AnimGraphAuthoring
{
    namespace
    {
        namespace Text = AnimGraphCommandText;
        using Outcome = AZ::Outcome<AZStd::string, AZStd::string>;
        using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

        constexpr const char* StudioNotAvailable =
            "EMotion Studio is not available: the EMotionFX gem's editor module (Animation Editor) is not loaded";

        Outcome Fail(const char* code, const AZStd::string& message)
        {
            return AZ::Failure(RequestError::EncodeError(code, message));
        }

        CommandSystem::CommandManager* FindCommandManager()
        {
            EMStudio::EMStudioManager* studio = EMStudio::GetManager();
            return studio ? studio->GetCommandManager() : nullptr;
        }

        EMotionFX::AnimGraphManager* FindAnimGraphManager()
        {
            auto variable = AZ::Environment::FindVariable<EMotionFX::EMotionFXManager*>(EMotionFX::kEMotionFXInstanceVarName);
            EMotionFX::EMotionFXManager* emfx = variable ? variable.Get() : nullptr;
            return emfx ? emfx->GetAnimGraphManager() : nullptr;
        }

        EMotionFX::AnimGraph* FindGraph(AZ::u32 animGraphId)
        {
            EMotionFX::AnimGraphManager* manager = FindAnimGraphManager();
            return manager ? manager->FindAnimGraphByID(animGraphId) : nullptr;
        }

        //! The graph a write may touch: it must exist, and it must not be the
        //! copy an asset or a runtime instance owns (survey 7, Ownership: the
        //! same filter CommandLoadAnimGraph applies, AnimGraphCommands.cpp
        //! :85-86). Null with the encoded error otherwise.
        EMotionFX::AnimGraph* FindEditableGraph(AZ::u32 animGraphId, AZStd::string& outError)
        {
            EMotionFX::AnimGraph* graph = FindGraph(animGraphId);
            if (!graph)
            {
                outError =
                    RequestError::EncodeError(RequestError::NotFound, AZStd::string::format("anim graph not found: %u", animGraphId));
                return nullptr;
            }
            if (graph->GetIsOwnedByRuntime() || graph->GetIsOwnedByAsset())
            {
                outError = RequestError::EncodeError(
                    RequestError::ValidationFailed,
                    AZStd::string::format(
                        "anim graph %u is owned by an asset/runtime instance and cannot be edited; load it with load_anim_graph",
                        animGraphId));
                return nullptr;
            }
            return graph;
        }

        bool ParseGraphId(const AZStd::string& text, AZ::u32& outId)
        {
            char* end = nullptr;
            const unsigned long parsed = strtoul(text.c_str(), &end, 10);
            if (text.empty() || !end || end == text.c_str() || *end != '\0' || parsed > 0xFFFFFFFFul)
            {
                return false;
            }
            outId = static_cast<AZ::u32>(parsed);
            return true;
        }

        AZStd::string IdString(const EMotionFX::ObjectId& id)
        {
            return AZStd::string::format("%llu", static_cast<unsigned long long>(static_cast<AZ::u64>(id)));
        }

        AZStd::string GraphJson(const EMotionFX::AnimGraph& graph)
        {
            rapidjson::StringBuffer sb;
            Writer w(sb);
            w.StartObject();
            w.Key("id");
            w.Uint(graph.GetID());
            w.Key("file_name");
            w.String(graph.GetFileName());
            w.EndObject();
            return AZStd::string(sb.GetString());
        }

        AZStd::string SingleStringJson(const char* key, const AZStd::string& value)
        {
            rapidjson::StringBuffer sb;
            Writer w(sb);
            w.StartObject();
            w.Key(key);
            w.String(value.c_str(), static_cast<rapidjson::SizeType>(value.size()));
            w.EndObject();
            return AZStd::string(sb.GetString());
        }

        //! A string member; absent or null counts as not given.
        bool ReadOptionalString(
            const rapidjson::Value& object, const char* key, AZStd::string& out, bool& outPresent, AZStd::string& outError)
        {
            outPresent = false;
            if (!object.HasMember(key) || object[key].IsNull())
            {
                return true;
            }
            if (!object[key].IsString())
            {
                outError = RequestError::EncodeError(RequestError::ValidationFailed, AZStd::string::format("'%s' must be a string", key));
                return false;
            }
            out.assign(object[key].GetString(), object[key].GetStringLength());
            outPresent = true;
            return true;
        }

        //! A node id member: a decimal string, or a number, as text.
        bool ReadOptionalIdText(
            const rapidjson::Value& object, const char* key, AZStd::string& out, bool& outPresent, AZStd::string& outError)
        {
            outPresent = false;
            if (!object.HasMember(key) || object[key].IsNull())
            {
                return true;
            }
            if (object[key].IsUint64())
            {
                out = AZStd::string::format("%llu", static_cast<unsigned long long>(object[key].GetUint64()));
                outPresent = true;
                return true;
            }
            return ReadOptionalString(object, key, out, outPresent, outError);
        }

        //! A node by its wire id (a decimal u64 string). Null with the
        //! encoded error: validation_failed for a malformed id, not_found for
        //! a well-formed one no node has.
        EMotionFX::AnimGraphNode* FindNode(
            const EMotionFX::AnimGraph& graph, const AZStd::string& nodeId, const char* what, AZStd::string& outError)
        {
            AZ::u64 raw = 0;
            if (!Text::ParseObjectId(nodeId, raw))
            {
                outError = RequestError::EncodeError(
                    RequestError::ValidationFailed,
                    AZStd::string::format(
                        "%s must be a node id, the decimal string get_anim_graph reports (got '%s')", what, nodeId.c_str()));
                return nullptr;
            }
            EMotionFX::AnimGraphNode* node = graph.RecursiveFindNodeById(EMotionFX::AnimGraphNodeId(raw));
            if (!node)
            {
                outError = RequestError::EncodeError(
                    RequestError::NotFound,
                    AZStd::string::format("%s: no node with id %s in anim graph %u", what, nodeId.c_str(), graph.GetID()));
            }
            return node;
        }

        //! A name that is about to travel quoted on a command line. A node or
        //! parameter loaded from a file could hold the parser's characters.
        bool CommandSafeName(const AZStd::string& name, const char* what, AZStd::string& outError)
        {
            AZStd::string reason;
            if (!Text::IsValidObjectName(name, reason))
            {
                outError = RequestError::EncodeError(
                    RequestError::ValidationFailed,
                    AZStd::string::format("%s '%s' cannot be placed on a command line: %s", what, name.c_str(), reason.c_str()));
                return false;
            }
            return true;
        }

        bool EqualsNoCase(const AZStd::string& a, const char* b)
        {
            const size_t length = strlen(b);
            if (a.size() != length)
            {
                return false;
            }
            for (size_t i = 0; i < length; ++i)
            {
                if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i])))
                {
                    return false;
                }
            }
            return true;
        }

        bool IsStateMachine(const EMotionFX::AnimGraphNode* node)
        {
            // The engine's own test is the exact type (AnimGraphConnectionCommands.cpp:151).
            return node && azrtti_typeid(node) == azrtti_typeid<EMotionFX::AnimGraphStateMachine>();
        }

        //! The JSON object a request's arguments must be.
        bool ParseArguments(const AZStd::string& argumentsJson, const char* requestType, rapidjson::Document& doc, AZStd::string& outError)
        {
            doc.Parse(argumentsJson.c_str(), argumentsJson.size());
            if (doc.HasParseError() || !doc.IsObject())
            {
                outError = RequestError::EncodeError(
                    RequestError::ValidationFailed, AZStd::string::format("%s arguments must be a JSON object", requestType));
                return false;
            }
            return true;
        }

        //! "position": [x, y], two integers in graph canvas pixels.
        bool ReadPosition(const rapidjson::Value& doc, int& outX, int& outY, bool& outPresent, AZStd::string& outError)
        {
            outPresent = false;
            if (!doc.HasMember("position") || doc["position"].IsNull())
            {
                return true;
            }
            const rapidjson::Value& position = doc["position"];
            if (!position.IsArray() || position.Size() != 2 || !position[0].IsInt() || !position[1].IsInt())
            {
                outError = RequestError::EncodeError(
                    RequestError::ValidationFailed, "'position' must be [x, y] with two integers (graph canvas pixels)");
                return false;
            }
            outX = position[0].GetInt();
            outY = position[1].GetInt();
            outPresent = true;
            return true;
        }

        //! A transition by its wire id. Null with the encoded error:
        //! validation_failed for a malformed id, not_found otherwise.
        EMotionFX::AnimGraphStateTransition* FindTransition(
            const EMotionFX::AnimGraph& graph, const AZStd::string& transitionId, AZStd::string& outError)
        {
            AZ::u64 raw = 0;
            if (!Text::ParseObjectId(transitionId, raw))
            {
                outError = RequestError::EncodeError(
                    RequestError::ValidationFailed,
                    AZStd::string::format(
                        "transition_id must be a transition id, the decimal string get_anim_graph reports (got '%s')",
                        transitionId.c_str()));
                return nullptr;
            }
            EMotionFX::AnimGraphStateTransition* transition = graph.RecursiveFindTransitionById(EMotionFX::AnimGraphConnectionId(raw));
            if (!transition)
            {
                outError = RequestError::EncodeError(
                    RequestError::NotFound,
                    AZStd::string::format("transition_id: no transition with id %s in anim graph %u", transitionId.c_str(), graph.GetID()));
            }
            return transition;
        }

        Outcome TransitionReply(const EMotionFX::AnimGraphStateTransition& transition)
        {
            const EMotionFX::AnimGraphStateMachine* stateMachine = transition.GetStateMachine();
            if (!stateMachine)
            {
                return Fail(
                    RequestError::EngineError,
                    AZStd::string::format("transition %s is not inside a state machine", IdString(transition.GetId()).c_str()));
            }
            rapidjson::StringBuffer sb;
            Writer w(sb);
            AnimGraphInspector::WriteTransition(w, *stateMachine, transition);
            return AZ::Success(AZStd::string(sb.GetString()));
        }

        //! The optional transition fields shared by add and set: blend_time,
        //! priority, disabled, sync_mode, interpolation.
        bool ReadTransitionAdjustments(const rapidjson::Value& doc, Text::TransitionAdjustments& out, AZStd::string& outError)
        {
            auto given = [&doc](const char* key) -> const rapidjson::Value*
            {
                return doc.HasMember(key) && !doc[key].IsNull() ? &doc[key] : nullptr;
            };
            auto refuse = [&outError](const char* message)
            {
                outError = RequestError::EncodeError(RequestError::ValidationFailed, message);
                return false;
            };
            if (const rapidjson::Value* value = given("blend_time"))
            {
                if (!value->IsNumber() || !std::isfinite(value->GetDouble()) || value->GetDouble() < 0.0)
                {
                    return refuse("'blend_time' must be a number of seconds, 0 or more");
                }
                out.m_blendTime = static_cast<float>(value->GetDouble());
                out.m_hasBlendTime = true;
            }
            if (const rapidjson::Value* value = given("priority"))
            {
                if (!value->IsUint())
                {
                    return refuse("'priority' must be an integer from 0 to 4294967295 (higher wins when several transitions are ready)");
                }
                out.m_priority = value->GetUint();
                out.m_hasPriority = true;
            }
            if (const rapidjson::Value* value = given("disabled"))
            {
                if (!value->IsBool())
                {
                    return refuse("'disabled' must be a JSON bool");
                }
                out.m_disabled = value->GetBool();
                out.m_hasDisabled = true;
            }
            if (const rapidjson::Value* value = given("sync_mode"))
            {
                if (!value->IsInt() || value->GetInt() < 0 || value->GetInt() > 2)
                {
                    return refuse("'sync_mode' must be 0 (disabled), 1 (track based) or 2 (clip based)");
                }
                out.m_syncMode = value->GetInt();
                out.m_hasSyncMode = true;
            }
            if (const rapidjson::Value* value = given("interpolation"))
            {
                if (!value->IsInt() || value->GetInt() < 0 || value->GetInt() > 1)
                {
                    return refuse("'interpolation' must be 0 (linear) or 1 (ease curve)");
                }
                out.m_interpolation = value->GetInt();
                out.m_hasInterpolation = true;
            }
            return true;
        }

        //! The ObjectStream XML AnimGraphAddCondition's -contents takes: a
        //! condition of `type` with `values` applied, serialized the way copy
        //! and paste does (survey 4, Recommended -contents approach). The
        //! prototype is created with no graph, so InitAfterLoading never runs
        //! and the object is not registered with any graph's object list
        //! (AnimGraphTransitionCondition.cpp:33-43), which makes deleting it
        //! here safe; the command creates the real condition on the graph.
        bool BuildConditionContents(
            const Text::ConditionType& type,
            const AZStd::vector<AZStd::pair<const Text::ConditionAttribute*, Text::AttributeValue>>& values,
            AZStd::string& outXml,
            AZStd::string& outError)
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            const AZ::TypeId typeId = AZ::Uuid::CreateStringPermissive(type.m_uuid);
            const AZ::SerializeContext::ClassData* classData = serializeContext ? serializeContext->FindClassData(typeId) : nullptr;
            // AnimGraphObjectFactory::Create dereferences the factory with no
            // null check and reinterpret_casts the result (survey 1).
            if (!classData || !classData->m_factory || !classData->m_azRtti ||
                !classData->m_azRtti->IsTypeOf(azrtti_typeid<EMotionFX::AnimGraphTransitionCondition>()))
            {
                outError = RequestError::EncodeError(
                    RequestError::ValidationFailed,
                    AZStd::string::format("condition type %s (%s) is not creatable in this engine build", type.m_shortName, type.m_uuid));
                return false;
            }
            EMotionFX::AnimGraphObject* object = EMotionFX::AnimGraphObjectFactory::Create(typeId);
            auto* condition = azrtti_cast<EMotionFX::AnimGraphTransitionCondition*>(object);
            if (!condition)
            {
                delete object;
                outError = RequestError::EncodeError(
                    RequestError::EngineError, AZStd::string::format("could not create a %s prototype", type.m_rttiName));
                return false;
            }
            for (const auto& [attribute, value] : values)
            {
                AZStd::string text = value.m_text;
                if (attribute->m_kind == Text::AttributeKind::StringList)
                {
                    // A container field has no text form; DeserializeIntoMember
                    // reads it from ObjectStream XML (ReflectionSerializer.cpp:174-212).
                    const AZ::Outcome<AZStd::string> xml = MCore::ReflectionSerializer::Serialize(&value.m_list);
                    if (!xml.IsSuccess())
                    {
                        delete condition;
                        outError = RequestError::EncodeError(
                            RequestError::EngineError, AZStd::string::format("could not serialize the list for %s", attribute->m_key));
                        return false;
                    }
                    text = xml.GetValue();
                }
                if (!MCore::ReflectionSerializer::DeserializeIntoMember(condition, attribute->m_key, text))
                {
                    delete condition;
                    outError = RequestError::EncodeError(
                        RequestError::EngineError,
                        AZStd::string::format(
                            "the engine refused the value for condition attribute %s of %s", attribute->m_key, type.m_shortName));
                    return false;
                }
            }
            const AZ::Outcome<AZStd::string> xml = MCore::ReflectionSerializer::Serialize(condition);
            delete condition;
            if (!xml.IsSuccess())
            {
                outError = RequestError::EncodeError(
                    RequestError::EngineError, AZStd::string::format("could not serialize the %s", type.m_rttiName));
                return false;
            }
            outXml = xml.GetValue();
            return true;
        }

        //! The fields a request may not set through "attributes": the node's
        //! identity and the graph's structure, which the dedicated commands
        //! own (AnimGraphNode.cpp:2543-2553, AnimGraphStateMachine.cpp
        //! :1565-1566, BlendTree.cpp:416).
        bool IsStructuralField(const char* name)
        {
            static constexpr const char* s_structural[] = { "id",          "name",         "childNodes",  "connections",
                                                            "actionSetup", "entryStateId", "transitions", "finalNodeId" };
            for (const char* structural : s_structural)
            {
                if (strcmp(name, structural) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        //! The reflected fields of a node's class and its bases above
        //! AnimGraphNode (whose own fields are the dedicated parameters),
        //! structural fields left out.
        void CollectSettableFields(
            const AZ::SerializeContext& serializeContext,
            const AZ::SerializeContext::ClassData* classData,
            AZStd::vector<const AZ::SerializeContext::ClassElement*>& out)
        {
            if (!classData)
            {
                return;
            }
            for (const AZ::SerializeContext::ClassElement& element : classData->m_elements)
            {
                if (element.m_flags & AZ::SerializeContext::ClassElement::FLG_BASE_CLASS)
                {
                    if (element.m_typeId == azrtti_typeid<EMotionFX::AnimGraphNode>() ||
                        element.m_typeId == azrtti_typeid<EMotionFX::AnimGraphObject>())
                    {
                        continue;
                    }
                    CollectSettableFields(serializeContext, serializeContext.FindClassData(element.m_typeId), out);
                }
                else if (!IsStructuralField(element.m_name))
                {
                    out.push_back(&element);
                }
            }
        }

        //! Resolves "attributes" into (field, text) pairs for -attributesString.
        //! A field with a serializer takes a number, bool or string; a string
        //! list takes a JSON array of strings, serialized to ObjectStream XML;
        //! a motion node's motionIds is a list of (id, cumulative weight)
        //! pairs in the engine (AnimGraphMotionNode.h:155), built from the
        //! ids the way SetMotionIds does (AnimGraphMotionNode.cpp:897-915).
        bool ResolveNodeAttributes(
            const EMotionFX::AnimGraphNode& node,
            const rapidjson::Value& attributes,
            AZStd::vector<AZStd::pair<AZStd::string, AZStd::string>>& out,
            AZStd::string& outError)
        {
            if (!attributes.IsObject())
            {
                outError = RequestError::EncodeError(
                    RequestError::ValidationFailed, "'attributes' must be a JSON object of {<reflected field>: <value>}");
                return false;
            }
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            if (!serializeContext)
            {
                outError = RequestError::EncodeError(RequestError::Unavailable, "no SerializeContext is available");
                return false;
            }
            AZStd::vector<const AZ::SerializeContext::ClassElement*> fields;
            CollectSettableFields(*serializeContext, serializeContext->FindClassData(azrtti_typeid(&node)), fields);
            AZStd::string fieldNames;
            for (const AZ::SerializeContext::ClassElement* field : fields)
            {
                if (!fieldNames.empty())
                {
                    fieldNames += ", ";
                }
                fieldNames += field->m_name;
            }
            if (fieldNames.empty())
            {
                fieldNames = "none";
            }
            const AZ::TypeId stringListId = azrtti_typeid<AZStd::vector<AZStd::string>>();
            const AZ::TypeId weightedIdListId = azrtti_typeid<AZStd::vector<AZStd::pair<AZStd::string, float>>>();
            for (auto member = attributes.MemberBegin(); member != attributes.MemberEnd(); ++member)
            {
                const AZStd::string key(member->name.GetString(), member->name.GetStringLength());
                const AZ::SerializeContext::ClassElement* element = nullptr;
                for (const AZ::SerializeContext::ClassElement* field : fields)
                {
                    if (key == field->m_name)
                    {
                        element = field;
                        break;
                    }
                }
                if (!element)
                {
                    outError = RequestError::EncodeError(
                        RequestError::ValidationFailed,
                        AZStd::string::format(
                            "unknown attribute '%s' for a %s (settable: %s)", key.c_str(), node.RTTI_GetTypeName(), fieldNames.c_str()));
                    return false;
                }
                const AZ::SerializeContext::ClassData* elementClass = serializeContext->FindClassData(element->m_typeId);
                if (!elementClass && element->m_genericClassInfo)
                {
                    elementClass = element->m_genericClassInfo->GetClassData();
                }
                AZStd::string text;
                AZStd::string reason;
                if (element->m_typeId == stringListId || element->m_typeId == weightedIdListId)
                {
                    const Text::ConditionAttribute listAttribute = { element->m_name, Text::AttributeKind::StringList, nullptr };
                    Text::AttributeValue list;
                    if (!Text::FormatConditionAttribute(listAttribute, member->value, list, reason))
                    {
                        outError = RequestError::EncodeError(
                            RequestError::ValidationFailed, AZStd::string::format("attribute '%s' %s", key.c_str(), reason.c_str()));
                        return false;
                    }
                    AZ::Outcome<AZStd::string> xml = AZ::Failure();
                    if (element->m_typeId == stringListId)
                    {
                        xml = MCore::ReflectionSerializer::Serialize(&list.m_list);
                    }
                    else
                    {
                        AZStd::vector<AZStd::pair<AZStd::string, float>> weighted;
                        EMotionFX::AnimGraphMotionNode::InitializeDefaultMotionIdsRandomWeights(list.m_list, weighted);
                        xml = MCore::ReflectionSerializer::Serialize(&weighted);
                    }
                    if (!xml.IsSuccess())
                    {
                        outError = RequestError::EncodeError(
                            RequestError::EngineError,
                            AZStd::string::format("could not serialize the list for attribute '%s'", key.c_str()));
                        return false;
                    }
                    text = xml.GetValue();
                }
                else if (elementClass && elementClass->m_serializer)
                {
                    if (!Text::FormatScalarText(member->value, text, reason))
                    {
                        outError = RequestError::EncodeError(
                            RequestError::ValidationFailed, AZStd::string::format("attribute '%s' %s", key.c_str(), reason.c_str()));
                        return false;
                    }
                }
                else
                {
                    outError = RequestError::EncodeError(
                        RequestError::ValidationFailed,
                        AZStd::string::format(
                            "attribute '%s' has type %s, which this request cannot set (numbers, bools, strings and string lists only)",
                            key.c_str(),
                            elementClass ? elementClass->m_name : "unknown"));
                    return false;
                }
                out.emplace_back(key, AZStd::move(text));
            }
            return true;
        }

        //! The names of a node's input or output ports, for port resolution.
        AZStd::vector<AZStd::string> PortNameList(const AZStd::vector<EMotionFX::AnimGraphNode::Port>& ports)
        {
            AZStd::vector<AZStd::string> names;
            names.reserve(ports.size());
            for (const EMotionFX::AnimGraphNode::Port& port : ports)
            {
                names.push_back(port.GetNameString());
            }
            return names;
        }

        // -- Paths ----------------------------------------------------------

        //! A request path made absolute and normalized: an @alias@ path
        //! through FileIO, a relative one against the project root, an
        //! absolute one kept. False with the encoded error for a path that
        //! carries the command line's syntax characters or an unknown alias.
        //! Relative paths never go to the engine as such: LoadObjectFromFile
        //! and SaveStreamToFile resolve them under @products@, the cache
        //! (survey 7, Path resolution).
        bool ResolveRequestPath(
            const AZStd::string& input, AZ::IO::FileIOBase& fileIo, AZ::IO::FixedMaxPath& outPath, AZStd::string& outError)
        {
            AZStd::string reason;
            if (!Text::IsValidCommandText(input, reason))
            {
                outError = RequestError::EncodeError(RequestError::ValidationFailed, "file_name " + reason);
                return false;
            }
            if (input.front() == '@')
            {
                AZ::IO::FixedMaxPath aliased;
                if (!fileIo.ResolvePath(aliased, AZ::IO::PathView(input.c_str())))
                {
                    outError = RequestError::EncodeError(
                        RequestError::ValidationFailed, AZStd::string::format("could not resolve the path alias in '%s'", input.c_str()));
                    return false;
                }
                outPath = aliased.LexicallyNormal();
                return true;
            }
            const AZ::IO::FixedMaxPathString projectRoot = AZ::Utils::GetProjectPath();
            outPath = Text::ResolveAgainstRoot(input, AZ::IO::PathView(projectRoot.c_str()));
            return true;
        }

        // -- Node types -----------------------------------------------------

        //! Every creatable AnimGraphNode class, with the names and placement
        //! flags a request is validated against, read off a prototype of each
        //! (the object factory's own technique, AnimGraphObjectFactory.cpp
        //! :100-107). Built per request and freed before it answers.
        //! GetUITypes() is this module's copy of the built-in list; node types
        //! other gems register reach only the EMotionFX module's copy
        //! (survey 1), so they are not creatable through this request.
        //! AnimGraphObjectFactory::Create dereferences the class factory with
        //! no null check and reinterpret_casts the result (survey 1), so only
        //! concrete classes that the SerializeContext says derive from
        //! AnimGraphNode get a prototype.
        AZStd::vector<Text::NodeType> CollectNodeTypes()
        {
            AZStd::vector<Text::NodeType> types;
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            if (!serializeContext)
            {
                return types;
            }
            const AZ::TypeId nodeBase = azrtti_typeid<EMotionFX::AnimGraphNode>();
            for (const AZ::TypeId& typeId : EMotionFX::AnimGraphObjectFactory::GetUITypes())
            {
                const AZ::SerializeContext::ClassData* classData = serializeContext->FindClassData(typeId);
                if (!classData || !classData->m_factory || !classData->m_azRtti || !classData->m_azRtti->IsTypeOf(nodeBase))
                {
                    continue;
                }
                EMotionFX::AnimGraphObject* object = EMotionFX::AnimGraphObjectFactory::Create(typeId);
                if (!object)
                {
                    continue;
                }
                if (const auto* node = azrtti_cast<const EMotionFX::AnimGraphNode*>(object))
                {
                    Text::NodeType type;
                    type.m_rttiName = node->RTTI_GetTypeName();
                    type.m_paletteName = node->GetPaletteName();
                    type.m_typeId = typeId;
                    type.m_canActAsState = node->GetCanActAsState();
                    type.m_canHaveChildren = node->GetCanHaveChildren();
                    type.m_insideStateMachineOnly = node->GetCanBeInsideStateMachineOnly();
                    type.m_insideChildStateMachineOnly = node->GetCanBeInsideChildStateMachineOnly();
                    type.m_onlyOneInsideParent = node->GetCanHaveOnlyOneInsideParent();
                    types.push_back(AZStd::move(type));
                }
                delete object;
            }
            AZStd::sort(
                types.begin(),
                types.end(),
                [](const Text::NodeType& a, const Text::NodeType& b)
                {
                    return a.m_rttiName < b.m_rttiName;
                });
            return types;
        }

        //! The placement rules the create command does not enforce, from the
        //! Animation Editor's palette filter (AnimGraphPlugin::CheckIfCanCreateObject,
        //! AnimGraphPlugin.cpp:1055-1132; survey 2, Placement checks). Empty
        //! when the node may go under the parent, else the reason.
        AZStd::string PlacementProblem(
            const Text::NodeType& type, const EMotionFX::AnimGraphNode& parent, const EMotionFX::AnimGraph& graph)
        {
            const bool parentIsStateMachine = azrtti_typeid(&parent) == azrtti_typeid<EMotionFX::AnimGraphStateMachine>();
            const bool parentIsRoot = &parent == graph.GetRootStateMachine();
            if (!parent.GetCanHaveChildren())
            {
                return AZStd::string::format(
                    "parent node '%s' (%s) cannot contain child nodes", parent.GetName(), parent.RTTI_GetTypeName());
            }
            if (parentIsStateMachine && !type.m_canActAsState)
            {
                return AZStd::string::format(
                    "%s cannot act as a state; a state machine ('%s') holds only states (motion nodes, blend trees, state machines, "
                    "entry/exit/hub nodes)",
                    type.m_rttiName.c_str(),
                    parent.GetName());
            }
            if (!parentIsStateMachine && type.m_insideStateMachineOnly)
            {
                return AZStd::string::format(
                    "%s can only be placed inside a state machine, and '%s' is a %s",
                    type.m_rttiName.c_str(),
                    parent.GetName(),
                    parent.RTTI_GetTypeName());
            }
            if (!(parentIsStateMachine && !parentIsRoot) && type.m_insideChildStateMachineOnly)
            {
                return AZStd::string::format(
                    "%s can only be placed inside a child state machine, not the root state machine", type.m_rttiName.c_str());
            }
            if (type.m_typeId == azrtti_typeid<EMotionFX::BlendTreeFinalNode>() &&
                azrtti_typeid(&parent) != azrtti_typeid<EMotionFX::BlendTree>())
            {
                return AZStd::string::format(
                    "BlendTreeFinalNode can only be placed inside a BlendTree, and '%s' is a %s",
                    parent.GetName(),
                    parent.RTTI_GetTypeName());
            }
            if (type.m_onlyOneInsideParent && parent.CheckIfHasChildOfType(type.m_typeId))
            {
                return AZStd::string::format(
                    "parent node '%s' already holds a %s; only one is allowed per parent", parent.GetName(), type.m_rttiName.c_str());
            }
            return AZStd::string();
        }

        // -- Command groups -------------------------------------------------

        //! Runs several commands as one undo step. The group stops at the
        //! first failure, is still pushed to the history, and is undone from
        //! there, so a failed request leaves nothing behind (survey 8, One
        //! undo step per request). The engine's text for the failing command
        //! goes to the editor log (AZ_Error in ExecuteCommandGroup) and is
        //! not in the group's result when a later command never ran.
        Outcome RunCommandGroup(const char* groupName, const AZStd::vector<AZStd::string>& commands)
        {
            CommandSystem::CommandManager* commandManager = FindCommandManager();
            if (!commandManager)
            {
                return Fail(RequestError::Unavailable, StudioNotAvailable);
            }
            // All three setters: the constructors leave one of the flags
            // uninitialized each (MCore/CommandGroup.cpp:15-27).
            MCore::CommandGroup group(AZStd::string(groupName), commands.size());
            group.SetContinueAfterError(false);
            group.SetAddToHistoryAfterError(true);
            group.SetReturnFalseAfterError(true);
            for (const AZStd::string& command : commands)
            {
                group.AddCommandString(command);
            }
            AZStd::string result;
            if (!commandManager->ExecuteCommandGroup(group, result, /*addToHistory=*/true, /*clearErrors=*/true, /*handleErrors=*/false))
            {
                // Undo() indexes the history without a guard of its own
                // (MCoreCommandManager.cpp:772-779).
                if (commandManager->GetHistoryIndex() >= 0)
                {
                    AZStd::string undoResult;
                    commandManager->Undo(undoResult);
                }
                return Fail(
                    RequestError::EngineError,
                    result.empty() ? AZStd::string::format("%s failed; the failing command's text is in the editor log", groupName)
                                   : result);
            }
            return AZ::Success(result);
        }
    } // namespace

    AZ::Outcome<AZStd::string, AZStd::string> RunCommand(const AZStd::string& commandLine)
    {
        CommandSystem::CommandManager* commandManager = FindCommandManager();
        if (!commandManager)
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string result;
        // handleErrors=false: the error-report callback opens an ErrorWindow
        // otherwise (survey 0.3, MainWindow.cpp:575-585).
        const bool ok = commandManager->ExecuteCommand(
            commandLine,
            result,
            /*addToHistory=*/true,
            /*outExecutedCommand=*/nullptr,
            /*outExecutedParameters=*/nullptr,
            /*callFromCommandGroup=*/false,
            /*clearErrors=*/true,
            /*handleErrors=*/false);
        if (!ok)
        {
            // The command system's own text when it gives one.
            return Fail(
                RequestError::EngineError, result.empty() ? AZStd::string::format("command failed: %s", commandLine.c_str()) : result);
        }
        return AZ::Success(result);
    }

    AZ::Outcome<AZStd::string, AZStd::string> CreateAnimGraph()
    {
        auto run = RunCommand("CreateAnimGraph");
        if (!run.IsSuccess())
        {
            return run;
        }
        // The command answers with the new graph's id as text.
        AZ::u32 id = 0;
        EMotionFX::AnimGraph* graph = ParseGraphId(run.GetValue(), id) ? FindGraph(id) : nullptr;
        if (!graph)
        {
            return Fail(
                RequestError::EngineError,
                AZStd::string::format("CreateAnimGraph answered '%s' but no graph with that id exists", run.GetValue().c_str()));
        }
        return AZ::Success(GraphJson(*graph));
    }

    AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraph(AZ::u32 animGraphId)
    {
        AZStd::string error;
        if (!FindEditableGraph(animGraphId, error))
        {
            return AZ::Failure(error);
        }
        auto run = RunCommand(AZStd::string::format("RemoveAnimGraph -animGraphID %u", animGraphId));
        if (!run.IsSuccess())
        {
            return run;
        }
        rapidjson::StringBuffer sb;
        Writer w(sb);
        w.StartObject();
        w.Key("removed");
        w.Uint(animGraphId);
        w.EndObject();
        return AZ::Success(AZStd::string(sb.GetString()));
    }

    AZ::Outcome<AZStd::string, AZStd::string> LoadAnimGraph(const AZStd::string& fileName)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        if (fileName.empty())
        {
            return Fail(
                RequestError::ValidationFailed,
                "file_name is required: an absolute path, an @alias@ path, or a path relative to the project root");
        }
        AZ::IO::FileIOBase* fileIo = AZ::IO::FileIOBase::GetInstance();
        if (!fileIo)
        {
            return Fail(RequestError::Unavailable, "file IO is not available");
        }
        AZStd::string error;
        AZ::IO::FixedMaxPath path;
        if (!ResolveRequestPath(fileName, *fileIo, path, error))
        {
            return AZ::Failure(error);
        }
        const AZ::IO::FixedMaxPathString projectRoot = AZ::Utils::GetProjectPath();
        const AZ::IO::FixedMaxPathString engineRoot = AZ::Utils::GetEnginePath();
        if (!Text::IsUnderRoot(path, AZ::IO::PathView(projectRoot.c_str())) &&
            !Text::IsUnderRoot(path, AZ::IO::PathView(engineRoot.c_str())))
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "'%s' is outside the project root (%s) and the engine root (%s)",
                    path.c_str(),
                    projectRoot.c_str(),
                    engineRoot.c_str()));
        }
        if (!fileIo->Exists(path.c_str()))
        {
            return Fail(RequestError::NotFound, AZStd::string::format("file not found: %s", path.c_str()));
        }
        // outResult is the graph's id; a command-loaded graph with the same
        // file name is answered instead of loaded twice (survey 7).
        auto run = RunCommand(Text::LoadCommand(path.c_str()));
        if (!run.IsSuccess())
        {
            return run;
        }
        AZ::u32 id = 0;
        EMotionFX::AnimGraph* graph = ParseGraphId(run.GetValue(), id) ? FindGraph(id) : nullptr;
        if (!graph)
        {
            return Fail(
                RequestError::EngineError,
                AZStd::string::format("LoadAnimGraph answered '%s' but no graph with that id exists", run.GetValue().c_str()));
        }
        return AZ::Success(GraphJson(*graph));
    }

    AZ::Outcome<AZStd::string, AZStd::string> SaveAnimGraph(AZ::u32 animGraphId, const AZStd::string& fileName)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        const AZStd::string target = fileName.empty() ? graph->GetFileNameString() : fileName;
        if (target.empty())
        {
            return Fail(RequestError::ValidationFailed, "the graph has no file name; pass file_name");
        }
        AZ::IO::FileIOBase* fileIo = AZ::IO::FileIOBase::GetInstance();
        if (!fileIo)
        {
            return Fail(RequestError::Unavailable, "file IO is not available");
        }
        AZ::IO::FixedMaxPath path;
        if (!ResolveRequestPath(target, *fileIo, path, error))
        {
            return AZ::Failure(error);
        }
        const AZ::IO::FixedMaxPathString projectRoot = AZ::Utils::GetProjectPath();
        if (!Text::IsUnderRoot(path, AZ::IO::PathView(projectRoot.c_str())))
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format("save target '%s' is outside the project root (%s)", path.c_str(), projectRoot.c_str()));
        }
        const AZ::IO::FixedMaxPath parentDir = path.ParentPath();
        if (!parentDir.empty() && !fileIo->Exists(parentDir.c_str()) && !fileIo->CreatePath(parentDir.c_str()))
        {
            return Fail(RequestError::EngineError, AZStd::string::format("could not create the directory %s", parentDir.c_str()));
        }
        EMotionFX::AnimGraphManager* manager = FindAnimGraphManager();
        const size_t index = manager ? manager->FindAnimGraphIndex(graph) : 0;
        if (!manager || index >= manager->GetNumAnimGraphs())
        {
            return Fail(
                RequestError::EngineError, AZStd::string::format("anim graph %u is not registered with the AnimGraphManager", animGraphId));
        }
        // Not undoable (Commands.h:66); -sourceControl false keeps
        // RequestEditForFileBlocking off the main thread (survey 0.3).
        auto run = RunCommand(Text::SaveCommand(path.c_str(), index));
        if (!run.IsSuccess())
        {
            return run;
        }
        return AZ::Success(GraphJson(*graph));
    }

    AZ::Outcome<AZStd::string, AZStd::string> AddNode(AZ::u32 animGraphId, const AZStd::string& argumentsJson)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        rapidjson::Document doc;
        doc.Parse(argumentsJson.c_str(), argumentsJson.size());
        if (doc.HasParseError() || !doc.IsObject())
        {
            return Fail(RequestError::ValidationFailed, "add_anim_graph_node arguments must be a JSON object");
        }

        AZStd::string typeName;
        bool hasType = false;
        if (!ReadOptionalString(doc, "node_type", typeName, hasType, error))
        {
            return AZ::Failure(error);
        }
        if (!hasType || typeName.empty())
        {
            return Fail(
                RequestError::ValidationFailed,
                "add_anim_graph_node requires 'node_type': an AnimGraphNode class name such as AnimGraphMotionNode, or its palette "
                "name such as Motion");
        }

        AZStd::string name;
        bool hasName = false;
        if (!ReadOptionalString(doc, "name", name, hasName, error))
        {
            return AZ::Failure(error);
        }
        if (hasName && !name.empty())
        {
            AZStd::string reason;
            if (!Text::IsValidObjectName(name, reason))
            {
                return Fail(RequestError::ValidationFailed, "name " + reason);
            }
            // The engine reads that name as "generate one" (AnimGraphNodeCommands.cpp:241-246).
            if (EqualsNoCase(name, "GENERATE"))
            {
                return Fail(
                    RequestError::ValidationFailed, "the name GENERATE is reserved by the engine; omit 'name' to get a generated one");
            }
            if (graph->RecursiveFindNodeByName(name.c_str()))
            {
                return Fail(
                    RequestError::ValidationFailed,
                    AZStd::string::format(
                        "a node named '%s' already exists in anim graph %u (names are unique graph-wide)", name.c_str(), animGraphId));
            }
        }
        else
        {
            name.clear();
        }

        EMotionFX::AnimGraphNode* parent = graph->GetRootStateMachine();
        AZStd::string parentId;
        bool hasParent = false;
        if (!ReadOptionalIdText(doc, "parent_id", parentId, hasParent, error))
        {
            return AZ::Failure(error);
        }
        if (hasParent)
        {
            parent = FindNode(*graph, parentId, "parent_id", error);
            if (!parent)
            {
                return AZ::Failure(error);
            }
        }
        if (!parent)
        {
            return Fail(RequestError::EngineError, AZStd::string::format("anim graph %u has no root state machine", animGraphId));
        }

        int xPos = 0;
        int yPos = 0;
        bool hasPosition = false;
        if (!ReadPosition(doc, xPos, yPos, hasPosition, error))
        {
            return AZ::Failure(error);
        }

        const AZStd::vector<Text::NodeType> types = CollectNodeTypes();
        if (types.empty())
        {
            return Fail(RequestError::Unavailable, "no AnimGraphNode classes are reflected; is the EMotionFX gem loaded?");
        }
        const size_t typeIndex = Text::FindNodeType(types, typeName);
        if (typeIndex == Text::NoNodeType)
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format("unknown node type: %s (known: %s)", typeName.c_str(), Text::NodeTypeNames(types).c_str()));
        }
        const Text::NodeType& type = types[typeIndex];
        const AZStd::string problem = PlacementProblem(type, *parent, *graph);
        if (!problem.empty())
        {
            return Fail(RequestError::ValidationFailed, problem);
        }
        const AZStd::string parentName = parent->GetName();
        if (!CommandSafeName(parentName, "parent node name", error))
        {
            return AZ::Failure(error);
        }

        auto run = RunCommand(Text::CreateNodeCommand(
            graph->GetID(),
            type.m_typeId.ToFixedString().c_str(),
            parentName,
            name,
            Text::GeneratedNamePrefix(type.m_rttiName),
            xPos,
            yPos));
        if (!run.IsSuccess())
        {
            return run;
        }
        // outResult is the node's name (AnimGraphNodeCommands.cpp:364).
        const EMotionFX::AnimGraphNode* node = graph->RecursiveFindNodeByName(run.GetValue().c_str());
        if (!node)
        {
            return Fail(
                RequestError::EngineError,
                AZStd::string::format("AnimGraphCreateNode answered '%s' but no node with that name exists", run.GetValue().c_str()));
        }
        rapidjson::StringBuffer sb;
        Writer w(sb);
        AnimGraphInspector::WriteNode(w, *node);
        return AZ::Success(AZStd::string(sb.GetString()));
    }

    AZ::Outcome<AZStd::string, AZStd::string> RemoveNode(AZ::u32 animGraphId, const AZStd::string& nodeId)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        const EMotionFX::AnimGraphNode* node = FindNode(*graph, nodeId, "node_id", error);
        if (!node)
        {
            return AZ::Failure(error);
        }
        // The command asserts on it (AnimGraphNodeCommands.cpp:776-783).
        if (!node->GetParentNode() || node == graph->GetRootStateMachine())
        {
            return Fail(
                RequestError::ValidationFailed, "refusing to remove the root state machine; remove_anim_graph removes the whole graph");
        }
        const AZStd::string nodeName = node->GetName();
        const AZStd::string idText = IdString(node->GetId());
        if (!CommandSafeName(nodeName, "node name", error))
        {
            return AZ::Failure(error);
        }
        auto run = RunCommand(Text::RemoveNodeCommand(graph->GetID(), nodeName));
        if (!run.IsSuccess())
        {
            return run;
        }
        return AZ::Success(SingleStringJson("removed", idText));
    }

    AZ::Outcome<AZStd::string, AZStd::string> SetEntryState(AZ::u32 animGraphId, const AZStd::string& nodeId)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        const EMotionFX::AnimGraphNode* node = FindNode(*graph, nodeId, "node_id", error);
        if (!node)
        {
            return AZ::Failure(error);
        }
        const EMotionFX::AnimGraphNode* parent = node->GetParentNode();
        if (!parent || azrtti_typeid(parent) != azrtti_typeid<EMotionFX::AnimGraphStateMachine>())
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "node '%s' is not inside a state machine (its parent is %s); only a state machine's child can be its entry state",
                    node->GetName(),
                    parent ? parent->RTTI_GetTypeName() : "none"));
        }
        if (!node->GetCanBeEntryNode())
        {
            return Fail(RequestError::ValidationFailed, AZStd::string::format("a %s cannot be an entry state", node->RTTI_GetTypeName()));
        }
        const AZStd::string nodeName = node->GetName();
        const AZStd::string idText = IdString(node->GetId());
        if (!CommandSafeName(nodeName, "node name", error))
        {
            return AZ::Failure(error);
        }
        auto run = RunCommand(Text::SetEntryStateCommand(graph->GetID(), nodeName));
        if (!run.IsSuccess())
        {
            return run;
        }
        return AZ::Success(SingleStringJson("entry_state_id", idText));
    }

    AZ::Outcome<AZStd::string, AZStd::string> AddParameter(AZ::u32 animGraphId, const AZStd::string& argumentsJson)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        rapidjson::Document doc;
        doc.Parse(argumentsJson.c_str(), argumentsJson.size());
        if (doc.HasParseError() || !doc.IsObject())
        {
            return Fail(RequestError::ValidationFailed, "add_anim_graph_parameter arguments must be a JSON object");
        }

        Text::ParameterSpec spec;
        bool present = false;
        if (!ReadOptionalString(doc, "name", spec.m_name, present, error))
        {
            return AZ::Failure(error);
        }
        AZStd::string reason;
        if (!present || !Text::IsValidObjectName(spec.m_name, reason))
        {
            return Fail(
                RequestError::ValidationFailed, present ? "name " + reason : AZStd::string("add_anim_graph_parameter requires 'name'"));
        }
        if (graph->FindParameterByName(spec.m_name))
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format("a parameter named '%s' already exists in anim graph %u", spec.m_name.c_str(), animGraphId));
        }

        AZStd::string typeName;
        if (!ReadOptionalString(doc, "parameter_type", typeName, present, error))
        {
            return AZ::Failure(error);
        }
        if (!present || typeName.empty())
        {
            return Fail(
                RequestError::ValidationFailed,
                "add_anim_graph_parameter requires 'parameter_type' (one of " + Text::ParameterTypeNames() + ", or the engine class name)");
        }
        const Text::ParameterType* type = Text::FindParameterType(typeName);
        if (!type)
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format("unknown parameter type: %s (known: %s)", typeName.c_str(), Text::ParameterTypeNames().c_str()));
        }
        // ParameterFactory::Create dereferences the class data with no null
        // check (survey 6), so the id must be one the factory lists.
        const AZ::TypeId typeId = AZ::Uuid::CreateStringPermissive(type->m_uuid);
        const AZStd::vector<AZ::TypeId> creatable = EMotionFX::ParameterFactory::GetParameterTypes();
        if (AZStd::find(creatable.begin(), creatable.end(), typeId) == creatable.end())
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format("parameter type %s (%s) is not creatable in this engine build", type->m_friendlyName, type->m_uuid));
        }
        spec.m_typeUuid = type->m_uuid;

        struct ValueField
        {
            const char* m_key;
            bool* m_has;
            AZStd::string* m_text;
            bool m_rangeOnly;
        };
        const ValueField fields[] = {
            { "default", &spec.m_hasDefault, &spec.m_defaultText, false },
            { "min", &spec.m_hasMin, &spec.m_minText, true },
            { "max", &spec.m_hasMax, &spec.m_maxText, true },
        };
        for (const ValueField& field : fields)
        {
            if (!doc.HasMember(field.m_key) || doc[field.m_key].IsNull())
            {
                continue;
            }
            if (field.m_rangeOnly && !type->m_ranged)
            {
                return Fail(
                    RequestError::ValidationFailed,
                    AZStd::string::format(
                        "'%s' is not valid for a %s parameter; only ranged types (Float, Int, Vector2/3/4, Color, Rotation) take min and "
                        "max",
                        field.m_key,
                        type->m_friendlyName));
            }
            if (!Text::FormatParameterValue(doc[field.m_key], type->m_kind, *field.m_text, reason))
            {
                return Fail(
                    RequestError::ValidationFailed,
                    AZStd::string::format("'%s' for a %s parameter %s", field.m_key, type->m_friendlyName, reason.c_str()));
            }
            *field.m_has = true;
        }

        if (!ReadOptionalString(doc, "description", spec.m_description, spec.m_hasDescription, error))
        {
            return AZ::Failure(error);
        }
        if (spec.m_hasDescription && !Text::IsValidCommandText(spec.m_description, reason))
        {
            return Fail(RequestError::ValidationFailed, "description " + reason);
        }

        bool createGroup = false;
        if (!ReadOptionalString(doc, "group", spec.m_group, spec.m_hasGroup, error))
        {
            return AZ::Failure(error);
        }
        if (spec.m_hasGroup && spec.m_group.empty())
        {
            spec.m_hasGroup = false;
        }
        if (spec.m_hasGroup)
        {
            if (!Text::IsValidObjectName(spec.m_group, reason))
            {
                return Fail(RequestError::ValidationFailed, "group " + reason);
            }
            const EMotionFX::Parameter* existing = graph->FindParameterByName(spec.m_group);
            if (existing && azrtti_typeid(existing) != azrtti_typeid<EMotionFX::GroupParameter>())
            {
                return Fail(
                    RequestError::ValidationFailed, AZStd::string::format("'%s' is a value parameter, not a group", spec.m_group.c_str()));
            }
            // An unknown -parent only logs a warning and adds the parameter at
            // the root (survey 6), so a missing group is created first, in the
            // same undo step (survey 10).
            createGroup = existing == nullptr;
        }

        const AZStd::string createCommand = Text::CreateParameterCommand(graph->GetID(), spec);
        auto run = createGroup
            ? RunCommandGroup(
                  "AiCompanion add_anim_graph_parameter", { Text::AddGroupParameterCommand(graph->GetID(), spec.m_group), createCommand })
            : RunCommand(createCommand);
        if (!run.IsSuccess())
        {
            return run;
        }
        // outResult is the parameter's name (AnimGraphParameterCommands_Impl.inl:166).
        const EMotionFX::Parameter* created = graph->FindParameterByName(spec.m_name);
        const auto* valueParameter = azrtti_cast<const EMotionFX::ValueParameter*>(created);
        if (!valueParameter)
        {
            return Fail(
                RequestError::EngineError,
                AZStd::string::format("AnimGraphCreateParameter answered but no value parameter named '%s' exists", spec.m_name.c_str()));
        }
        const EMotionFX::GroupParameter* parentGroup = graph->FindParentGroupParameter(created);
        rapidjson::StringBuffer sb;
        Writer w(sb);
        AnimGraphInspector::WriteValueParameter(w, *valueParameter, parentGroup ? &parentGroup->GetName() : nullptr);
        return AZ::Success(AZStd::string(sb.GetString()));
    }

    AZ::Outcome<AZStd::string, AZStd::string> RemoveParameter(AZ::u32 animGraphId, const AZStd::string& name)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        AZStd::string reason;
        if (!Text::IsValidObjectName(name, reason))
        {
            return Fail(RequestError::ValidationFailed, "name " + reason);
        }
        const EMotionFX::Parameter* parameter = graph->FindParameterByName(name);
        if (!parameter)
        {
            return Fail(
                RequestError::NotFound, AZStd::string::format("parameter not found: %s (anim graph %u)", name.c_str(), animGraphId));
        }
        // The command asserts on a group (AnimGraphParameterCommands_Impl.inl:257).
        if (azrtti_typeid(parameter) == azrtti_typeid<EMotionFX::GroupParameter>())
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "'%s' is a group parameter; remove_anim_graph_parameter removes value parameters only", name.c_str()));
        }
        auto run = RunCommand(Text::RemoveParameterCommand(graph->GetID(), name));
        if (!run.IsSuccess())
        {
            return run;
        }
        return AZ::Success(SingleStringJson("removed", name));
    }

    AZ::Outcome<AZStd::string, AZStd::string> AddTransition(AZ::u32 animGraphId, const AZStd::string& argumentsJson)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        rapidjson::Document doc;
        if (!ParseArguments(argumentsJson, "add_anim_graph_transition", doc, error))
        {
            return AZ::Failure(error);
        }

        AZStd::string targetId;
        bool present = false;
        if (!ReadOptionalIdText(doc, "target_node_id", targetId, present, error))
        {
            return AZ::Failure(error);
        }
        if (!present)
        {
            return Fail(
                RequestError::ValidationFailed, "add_anim_graph_transition requires 'target_node_id' (a state inside a state machine)");
        }
        EMotionFX::AnimGraphNode* target = FindNode(*graph, targetId, "target_node_id", error);
        if (!target)
        {
            return AZ::Failure(error);
        }
        EMotionFX::AnimGraphNode* parent = target->GetParentNode();
        if (!IsStateMachine(parent))
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "target node '%s' is not a state: its parent is %s; a transition joins two states of one state machine (inside a "
                    "blend tree, use connect_anim_graph_ports)",
                    target->GetName(),
                    parent ? parent->RTTI_GetTypeName() : "none (it is the root)"));
        }

        AZStd::string sourceId;
        bool hasSource = false;
        if (!ReadOptionalIdText(doc, "source_node_id", sourceId, hasSource, error))
        {
            return AZ::Failure(error);
        }
        EMotionFX::AnimGraphNode* source = nullptr;
        if (hasSource)
        {
            source = FindNode(*graph, sourceId, "source_node_id", error);
            if (!source)
            {
                return AZ::Failure(error);
            }
            if (source == target)
            {
                return Fail(
                    RequestError::ValidationFailed,
                    AZStd::string::format(
                        "source and target are the same node ('%s'); omit source_node_id for a wildcard transition", target->GetName()));
            }
            // The command never compares the parents (survey 3, Missing checks).
            if (source->GetParentNode() != parent)
            {
                return Fail(
                    RequestError::ValidationFailed,
                    AZStd::string::format(
                        "source '%s' (inside '%s') and target '%s' (inside '%s') are not states of the same state machine",
                        source->GetName(),
                        source->GetParentNode() ? source->GetParentNode()->GetName() : "none",
                        target->GetName(),
                        parent->GetName()));
            }
            // The Animation Editor refuses it (BlendGraphWidget.cpp:915-922).
            if (azrtti_typeid(source) == azrtti_typeid<EMotionFX::AnimGraphExitNode>())
            {
                return Fail(
                    RequestError::ValidationFailed,
                    AZStd::string::format("'%s' is an exit node; an exit node cannot be a transition's source", source->GetName()));
            }
        }

        Text::TransitionAdjustments adjustments;
        if (!ReadTransitionAdjustments(doc, adjustments, error))
        {
            return AZ::Failure(error);
        }

        struct PendingCondition
        {
            const Text::ConditionType* m_type;
            AZStd::string m_contentsXml;
        };
        AZStd::vector<PendingCondition> conditions;
        if (doc.HasMember("conditions") && !doc["conditions"].IsNull())
        {
            const rapidjson::Value& list = doc["conditions"];
            if (!list.IsArray())
            {
                return Fail(RequestError::ValidationFailed, "'conditions' must be a JSON array of {condition_type, attributes} objects");
            }
            for (rapidjson::SizeType i = 0; i < list.Size(); ++i)
            {
                const rapidjson::Value& entry = list[i];
                if (!entry.IsObject())
                {
                    return Fail(
                        RequestError::ValidationFailed,
                        AZStd::string::format("conditions[%u] must be an object with 'condition_type' and optional 'attributes'", i));
                }
                AZStd::string typeName;
                if (!ReadOptionalString(entry, "condition_type", typeName, present, error))
                {
                    return AZ::Failure(error);
                }
                if (!present || typeName.empty())
                {
                    return Fail(
                        RequestError::ValidationFailed,
                        AZStd::string::format(
                            "conditions[%u] requires 'condition_type' (one of %s)", i, Text::ConditionTypeNames().c_str()));
                }
                const Text::ConditionType* type = Text::FindConditionType(typeName);
                if (!type)
                {
                    return Fail(
                        RequestError::ValidationFailed,
                        AZStd::string::format(
                            "unknown condition type: %s (known: %s)", typeName.c_str(), Text::ConditionTypeNames().c_str()));
                }
                AZStd::vector<AZStd::pair<const Text::ConditionAttribute*, Text::AttributeValue>> values;
                if (entry.HasMember("attributes") && !entry["attributes"].IsNull())
                {
                    const rapidjson::Value& attributes = entry["attributes"];
                    if (!attributes.IsObject())
                    {
                        return Fail(
                            RequestError::ValidationFailed,
                            AZStd::string::format(
                                "conditions[%u].attributes must be a JSON object (supported: %s)",
                                i,
                                Text::ConditionAttributeNames(*type).c_str()));
                    }
                    for (auto member = attributes.MemberBegin(); member != attributes.MemberEnd(); ++member)
                    {
                        const AZStd::string key(member->name.GetString(), member->name.GetStringLength());
                        const Text::ConditionAttribute* attribute = Text::FindConditionAttribute(*type, key);
                        if (!attribute)
                        {
                            return Fail(
                                RequestError::ValidationFailed,
                                AZStd::string::format(
                                    "unsupported condition attribute %s for %s (supported: %s)",
                                    key.c_str(),
                                    type->m_shortName,
                                    Text::ConditionAttributeNames(*type).c_str()));
                        }
                        Text::AttributeValue value;
                        AZStd::string reason;
                        if (!Text::FormatConditionAttribute(*attribute, member->value, value, reason))
                        {
                            return Fail(
                                RequestError::ValidationFailed,
                                AZStd::string::format("conditions[%u].attributes.%s %s", i, key.c_str(), reason.c_str()));
                        }
                        if (attribute->m_kind == Text::AttributeKind::NodeId)
                        {
                            const EMotionFX::AnimGraphNode* referenced =
                                graph->RecursiveFindNodeById(EMotionFX::AnimGraphNodeId(strtoull(value.m_text.c_str(), nullptr, 10)));
                            if (!referenced)
                            {
                                return Fail(
                                    RequestError::NotFound,
                                    AZStd::string::format(
                                        "conditions[%u].attributes.%s: no node with id %s in anim graph %u",
                                        i,
                                        key.c_str(),
                                        value.m_text.c_str(),
                                        animGraphId));
                            }
                            if (key == "motionNodeId" && !azrtti_cast<const EMotionFX::AnimGraphMotionNode*>(referenced))
                            {
                                return Fail(
                                    RequestError::ValidationFailed,
                                    AZStd::string::format(
                                        "conditions[%u].attributes.motionNodeId: '%s' is a %s, not a motion node",
                                        i,
                                        referenced->GetName(),
                                        referenced->RTTI_GetTypeName()));
                            }
                        }
                        if (attribute->m_kind == Text::AttributeKind::String && key == "parameterName" && !value.m_text.empty() &&
                            !graph->FindValueParameterByName(value.m_text))
                        {
                            return Fail(
                                RequestError::ValidationFailed,
                                AZStd::string::format(
                                    "conditions[%u].attributes.parameterName: no value parameter named '%s' in anim graph %u",
                                    i,
                                    value.m_text.c_str(),
                                    animGraphId));
                        }
                        if (attribute->m_kind == Text::AttributeKind::StringList && key == "tags")
                        {
                            for (const AZStd::string& tag : value.m_list)
                            {
                                if (!graph->FindValueParameterByName(tag))
                                {
                                    return Fail(
                                        RequestError::ValidationFailed,
                                        AZStd::string::format(
                                            "conditions[%u].attributes.tags: no value parameter named '%s' in anim graph %u",
                                            i,
                                            tag.c_str(),
                                            animGraphId));
                                }
                            }
                        }
                        values.emplace_back(attribute, AZStd::move(value));
                    }
                }
                PendingCondition pending;
                pending.m_type = type;
                if (!BuildConditionContents(*type, values, pending.m_contentsXml, error))
                {
                    return AZ::Failure(error);
                }
                conditions.push_back(AZStd::move(pending));
            }
        }

        const AZStd::string targetName = target->GetName();
        if (!CommandSafeName(targetName, "target node name", error))
        {
            return AZ::Failure(error);
        }
        AZStd::string sourceName;
        if (source)
        {
            sourceName = source->GetName();
            if (!CommandSafeName(sourceName, "source node name", error))
            {
                return AZ::Failure(error);
            }
        }

        // The id is chosen here (-id) so the adjust and condition commands
        // in the same group can name the transition the first one creates.
        const EMotionFX::AnimGraphConnectionId transitionId = EMotionFX::AnimGraphConnectionId::Create();
        const AZStd::string idText = IdString(transitionId);
        AZStd::vector<AZStd::string> commands;
        commands.push_back(Text::CreateTransitionCommand(graph->GetID(), sourceName, targetName, idText));
        if (adjustments.Any())
        {
            commands.push_back(Text::AdjustTransitionCommand(graph->GetID(), idText, adjustments));
        }
        for (const PendingCondition& pending : conditions)
        {
            commands.push_back(Text::AddConditionCommand(graph->GetID(), idText, pending.m_type->m_uuid, pending.m_contentsXml));
        }
        auto run = commands.size() == 1 ? RunCommand(commands.front()) : RunCommandGroup("AiCompanion add_anim_graph_transition", commands);
        if (!run.IsSuccess())
        {
            return run;
        }
        const EMotionFX::AnimGraphStateTransition* transition = graph->RecursiveFindTransitionById(transitionId);
        if (!transition)
        {
            return Fail(
                RequestError::EngineError,
                AZStd::string::format("AnimGraphCreateConnection answered but no transition with id %s exists", idText.c_str()));
        }
        return TransitionReply(*transition);
    }

    AZ::Outcome<AZStd::string, AZStd::string> RemoveTransition(AZ::u32 animGraphId, const AZStd::string& transitionId)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        const EMotionFX::AnimGraphStateTransition* transition = FindTransition(*graph, transitionId, error);
        if (!transition)
        {
            return AZ::Failure(error);
        }
        const EMotionFX::AnimGraphNode* target = transition->GetTargetNode();
        if (!target || !IsStateMachine(target->GetParentNode()))
        {
            return Fail(
                RequestError::EngineError,
                AZStd::string::format("transition %s has no target state inside a state machine", transitionId.c_str()));
        }
        const AZStd::string targetName = target->GetName();
        if (!CommandSafeName(targetName, "target node name", error))
        {
            return AZ::Failure(error);
        }
        AZStd::string sourceName;
        if (!transition->GetIsWildcardTransition() && transition->GetSourceNode())
        {
            sourceName = transition->GetSourceNode()->GetName();
            if (!CommandSafeName(sourceName, "source node name", error))
            {
                return AZ::Failure(error);
            }
        }
        const AZStd::string idText = IdString(transition->GetId());
        auto run = RunCommand(Text::RemoveTransitionCommand(graph->GetID(), sourceName, targetName, idText));
        if (!run.IsSuccess())
        {
            return run;
        }
        return AZ::Success(SingleStringJson("removed", idText));
    }

    AZ::Outcome<AZStd::string, AZStd::string> SetTransition(AZ::u32 animGraphId, const AZStd::string& argumentsJson)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        rapidjson::Document doc;
        if (!ParseArguments(argumentsJson, "set_anim_graph_transition", doc, error))
        {
            return AZ::Failure(error);
        }
        AZStd::string transitionId;
        bool present = false;
        if (!ReadOptionalIdText(doc, "transition_id", transitionId, present, error))
        {
            return AZ::Failure(error);
        }
        if (!present)
        {
            return Fail(RequestError::ValidationFailed, "set_anim_graph_transition requires 'transition_id' (the decimal id string)");
        }
        const EMotionFX::AnimGraphStateTransition* transition = FindTransition(*graph, transitionId, error);
        if (!transition)
        {
            return AZ::Failure(error);
        }
        Text::TransitionAdjustments adjustments;
        if (!ReadTransitionAdjustments(doc, adjustments, error))
        {
            return AZ::Failure(error);
        }
        if (!adjustments.Any())
        {
            return Fail(
                RequestError::ValidationFailed,
                "set_anim_graph_transition needs at least one of blend_time, priority, disabled, sync_mode, interpolation");
        }
        const EMotionFX::AnimGraphConnectionId id = transition->GetId();
        auto run = RunCommand(Text::AdjustTransitionCommand(graph->GetID(), IdString(id), adjustments));
        if (!run.IsSuccess())
        {
            return run;
        }
        transition = graph->RecursiveFindTransitionById(id);
        if (!transition)
        {
            return Fail(
                RequestError::EngineError,
                AZStd::string::format("AnimGraphAdjustTransition answered but transition %s is gone", transitionId.c_str()));
        }
        return TransitionReply(*transition);
    }

    AZ::Outcome<AZStd::string, AZStd::string> ConnectPorts(AZ::u32 animGraphId, const AZStd::string& argumentsJson)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        rapidjson::Document doc;
        if (!ParseArguments(argumentsJson, "connect_anim_graph_ports", doc, error))
        {
            return AZ::Failure(error);
        }
        AZStd::string sourceId;
        AZStd::string targetId;
        bool hasSource = false;
        bool hasTarget = false;
        if (!ReadOptionalIdText(doc, "source_node_id", sourceId, hasSource, error) ||
            !ReadOptionalIdText(doc, "target_node_id", targetId, hasTarget, error))
        {
            return AZ::Failure(error);
        }
        if (!hasSource || !hasTarget || !doc.HasMember("source_port") || doc["source_port"].IsNull() || !doc.HasMember("target_port") ||
            doc["target_port"].IsNull())
        {
            return Fail(
                RequestError::ValidationFailed,
                "connect_anim_graph_ports requires 'source_node_id', 'source_port', 'target_node_id' and 'target_port' (a port is an "
                "index or a name)");
        }
        EMotionFX::AnimGraphNode* source = FindNode(*graph, sourceId, "source_node_id", error);
        if (!source)
        {
            return AZ::Failure(error);
        }
        EMotionFX::AnimGraphNode* target = FindNode(*graph, targetId, "target_node_id", error);
        if (!target)
        {
            return AZ::Failure(error);
        }
        if (source == target)
        {
            return Fail(
                RequestError::ValidationFailed, AZStd::string::format("source and target are the same node ('%s')", target->GetName()));
        }
        EMotionFX::AnimGraphNode* parent = target->GetParentNode();
        if (!parent)
        {
            return Fail(RequestError::ValidationFailed, "the root state machine has no ports to connect");
        }
        if (IsStateMachine(parent))
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "target node '%s' is a state inside state machine '%s'; use add_anim_graph_transition for states "
                    "(connect_anim_graph_ports joins the nodes of a blend tree)",
                    target->GetName(),
                    parent->GetName()));
        }
        if (source->GetParentNode() != parent)
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "source '%s' (inside '%s') and target '%s' (inside '%s') are not in the same blend tree",
                    source->GetName(),
                    source->GetParentNode() ? source->GetParentNode()->GetName() : "none",
                    target->GetName(),
                    parent->GetName()));
        }
        const auto& outputs = source->GetOutputPorts();
        const auto& inputs = target->GetInputPorts();
        size_t sourcePort = Text::NoPort;
        size_t targetPort = Text::NoPort;
        AZStd::string reason;
        if (!Text::ResolvePort(PortNameList(outputs), doc["source_port"], sourcePort, reason))
        {
            return Fail(
                RequestError::ValidationFailed, AZStd::string::format("source_port of '%s': %s", source->GetName(), reason.c_str()));
        }
        if (!Text::ResolvePort(PortNameList(inputs), doc["target_port"], targetPort, reason))
        {
            return Fail(
                RequestError::ValidationFailed, AZStd::string::format("target_port of '%s': %s", target->GetName(), reason.c_str()));
        }
        const EMotionFX::AnimGraphNode::Port& output = outputs[sourcePort];
        const EMotionFX::AnimGraphNode::Port& input = inputs[targetPort];
        // The Animation Editor's own drop rules (BlendGraphWidget.cpp:855-913).
        if (!output.CheckIfIsCompatibleWith(input))
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "output port \"%s\" of '%s' and input port \"%s\" of '%s' carry incompatible data types",
                    output.GetName(),
                    source->GetName(),
                    input.GetName(),
                    target->GetName()));
        }
        if (input.m_connection)
        {
            // AddConnection would overwrite the port's connection and orphan
            // the old one in the node's list (AnimGraphNode.cpp:172-184).
            const EMotionFX::AnimGraphNode* current = input.m_connection->GetSourceNode();
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "input port \"%s\" of '%s' already has a connection from '%s' port %u; disconnect_anim_graph_ports it first",
                    input.GetName(),
                    target->GetName(),
                    current ? current->GetName() : "?",
                    static_cast<unsigned>(input.m_connection->GetSourcePort())));
        }
        if (target->GetHasConnection(source, static_cast<uint16>(sourcePort), static_cast<uint16>(targetPort)))
        {
            return Fail(RequestError::ValidationFailed, "the connection already exists");
        }
        if (const auto* blendTree = azrtti_cast<const EMotionFX::BlendTree*>(parent))
        {
            if (blendTree->ConnectionWillProduceCycle(source, target))
            {
                return Fail(
                    RequestError::ValidationFailed,
                    AZStd::string::format(
                        "connecting '%s' to '%s' would close a cycle in blend tree '%s'",
                        source->GetName(),
                        target->GetName(),
                        parent->GetName()));
            }
        }
        const AZStd::string sourceName = source->GetName();
        const AZStd::string targetName = target->GetName();
        if (!CommandSafeName(sourceName, "source node name", error) || !CommandSafeName(targetName, "target node name", error))
        {
            return AZ::Failure(error);
        }
        auto run = RunCommand(Text::CreatePortConnectionCommand(graph->GetID(), sourceName, targetName, sourcePort, targetPort));
        if (!run.IsSuccess())
        {
            return run;
        }
        rapidjson::StringBuffer sb;
        Writer w(sb);
        AnimGraphInspector::WriteInputPort(w, *target, targetPort);
        return AZ::Success(AZStd::string(sb.GetString()));
    }

    AZ::Outcome<AZStd::string, AZStd::string> DisconnectPorts(AZ::u32 animGraphId, const AZStd::string& argumentsJson)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        rapidjson::Document doc;
        if (!ParseArguments(argumentsJson, "disconnect_anim_graph_ports", doc, error))
        {
            return AZ::Failure(error);
        }
        AZStd::string targetId;
        bool hasTarget = false;
        if (!ReadOptionalIdText(doc, "target_node_id", targetId, hasTarget, error))
        {
            return AZ::Failure(error);
        }
        if (!hasTarget || !doc.HasMember("target_port") || doc["target_port"].IsNull())
        {
            return Fail(
                RequestError::ValidationFailed,
                "disconnect_anim_graph_ports requires 'target_node_id' and 'target_port' (an input port index or name)");
        }
        EMotionFX::AnimGraphNode* target = FindNode(*graph, targetId, "target_node_id", error);
        if (!target)
        {
            return AZ::Failure(error);
        }
        const EMotionFX::AnimGraphNode* parent = target->GetParentNode();
        if (!parent || IsStateMachine(parent))
        {
            return Fail(
                RequestError::ValidationFailed,
                AZStd::string::format(
                    "node '%s' is not inside a blend tree; use remove_anim_graph_transition for a state's transitions", target->GetName()));
        }
        const auto& inputs = target->GetInputPorts();
        size_t targetPort = Text::NoPort;
        AZStd::string reason;
        if (!Text::ResolvePort(PortNameList(inputs), doc["target_port"], targetPort, reason))
        {
            return Fail(
                RequestError::ValidationFailed, AZStd::string::format("target_port of '%s': %s", target->GetName(), reason.c_str()));
        }
        const EMotionFX::BlendTreeConnection* connection = inputs[targetPort].m_connection;
        if (!connection)
        {
            return Fail(
                RequestError::NotFound,
                AZStd::string::format(
                    "input port \"%s\" (%zu) of '%s' has no connection", inputs[targetPort].GetName(), targetPort, target->GetName()));
        }
        const EMotionFX::AnimGraphNode* source = connection->GetSourceNode();
        if (!source)
        {
            return Fail(
                RequestError::EngineError,
                AZStd::string::format("the connection into '%s' port %zu has no source node", target->GetName(), targetPort));
        }
        const AZStd::string sourceName = source->GetName();
        const AZStd::string targetName = target->GetName();
        if (!CommandSafeName(sourceName, "source node name", error) || !CommandSafeName(targetName, "target node name", error))
        {
            return AZ::Failure(error);
        }
        const AZStd::string connectionId = IdString(connection->GetId());
        auto run =
            RunCommand(Text::RemovePortConnectionCommand(graph->GetID(), sourceName, targetName, connection->GetSourcePort(), targetPort));
        if (!run.IsSuccess())
        {
            return run;
        }
        return AZ::Success(SingleStringJson("removed", connectionId));
    }

    AZ::Outcome<AZStd::string, AZStd::string> SetNode(AZ::u32 animGraphId, const AZStd::string& argumentsJson)
    {
        if (!FindCommandManager())
        {
            return Fail(RequestError::Unavailable, StudioNotAvailable);
        }
        AZStd::string error;
        EMotionFX::AnimGraph* graph = FindEditableGraph(animGraphId, error);
        if (!graph)
        {
            return AZ::Failure(error);
        }
        rapidjson::Document doc;
        if (!ParseArguments(argumentsJson, "set_anim_graph_node", doc, error))
        {
            return AZ::Failure(error);
        }
        AZStd::string nodeId;
        bool present = false;
        if (!ReadOptionalIdText(doc, "node_id", nodeId, present, error))
        {
            return AZ::Failure(error);
        }
        if (!present)
        {
            return Fail(RequestError::ValidationFailed, "set_anim_graph_node requires 'node_id' (the node's decimal id string)");
        }
        const EMotionFX::AnimGraphNode* node = FindNode(*graph, nodeId, "node_id", error);
        if (!node)
        {
            return AZ::Failure(error);
        }

        Text::NodeAdjustments adjustments;
        bool anyGiven = false;
        AZStd::string name;
        if (!ReadOptionalString(doc, "name", name, present, error))
        {
            return AZ::Failure(error);
        }
        if (present)
        {
            AZStd::string reason;
            if (!Text::IsValidObjectName(name, reason))
            {
                return Fail(RequestError::ValidationFailed, "name " + reason);
            }
            anyGiven = true;
            if (name != node->GetName())
            {
                // The command renames without a uniqueness check (AnimGraphNodeCommands.cpp:527-554).
                if (graph->RecursiveFindNodeByName(name.c_str()))
                {
                    return Fail(
                        RequestError::ValidationFailed,
                        AZStd::string::format(
                            "a node named '%s' already exists in anim graph %u (names are unique graph-wide)", name.c_str(), animGraphId));
                }
                adjustments.m_newName = name;
            }
        }
        if (!ReadPosition(doc, adjustments.m_xPos, adjustments.m_yPos, adjustments.m_hasPosition, error))
        {
            return AZ::Failure(error);
        }
        anyGiven = anyGiven || adjustments.m_hasPosition;
        if (doc.HasMember("enabled") && !doc["enabled"].IsNull())
        {
            if (!doc["enabled"].IsBool())
            {
                return Fail(RequestError::ValidationFailed, "'enabled' must be a JSON bool");
            }
            adjustments.m_enabled = doc["enabled"].GetBool();
            adjustments.m_hasEnabled = true;
            anyGiven = true;
        }
        if (doc.HasMember("attributes") && !doc["attributes"].IsNull())
        {
            if (!ResolveNodeAttributes(*node, doc["attributes"], adjustments.m_attributes, error))
            {
                return AZ::Failure(error);
            }
            anyGiven = anyGiven || !adjustments.m_attributes.empty();
        }
        if (!anyGiven)
        {
            return Fail(RequestError::ValidationFailed, "set_anim_graph_node needs at least one of name, position, enabled, attributes");
        }
        const AZStd::string currentName = node->GetName();
        if (!CommandSafeName(currentName, "node name", error))
        {
            return AZ::Failure(error);
        }
        const EMotionFX::AnimGraphNodeId id = node->GetId();
        auto run = RunCommand(Text::AdjustNodeCommand(graph->GetID(), currentName, adjustments));
        if (!run.IsSuccess())
        {
            return run;
        }
        node = graph->RecursiveFindNodeById(id);
        if (!node)
        {
            return Fail(
                RequestError::EngineError, AZStd::string::format("AnimGraphAdjustNode answered but node %s is gone", nodeId.c_str()));
        }
        rapidjson::StringBuffer sb;
        Writer w(sb);
        AnimGraphInspector::WriteNode(w, *node);
        return AZ::Success(AZStd::string(sb.GetString()));
    }
} // namespace AiCompanion::AnimGraphAuthoring
