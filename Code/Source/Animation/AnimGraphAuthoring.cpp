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
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/AnimGraphNode.h>
#include <EMotionFX/Source/AnimGraphObject.h>
#include <EMotionFX/Source/AnimGraphObjectFactory.h>
#include <EMotionFX/Source/AnimGraphStateMachine.h>
#include <EMotionFX/Source/BlendTree.h>
#include <EMotionFX/Source/BlendTreeFinalNode.h>
#include <EMotionFX/Source/EMotionFXManager.h>
#include <EMotionFX/Source/Parameter/GroupParameter.h>
#include <EMotionFX/Source/Parameter/Parameter.h>
#include <EMotionFX/Source/Parameter/ParameterFactory.h>
#include <EMotionFX/Source/Parameter/ValueParameter.h>
#include <EMotionFX/Tools/EMotionStudio/EMStudioSDK/Source/EMStudioManager.h>
#include <MCore/Source/CommandGroup.h>

#include <cctype>
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
        if (doc.HasMember("position") && !doc["position"].IsNull())
        {
            const rapidjson::Value& position = doc["position"];
            if (!position.IsArray() || position.Size() != 2 || !position[0].IsInt() || !position[1].IsInt())
            {
                return Fail(RequestError::ValidationFailed, "'position' must be [x, y] with two integers (graph canvas pixels)");
            }
            xPos = position[0].GetInt();
            yPos = position[1].GetInt();
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
} // namespace AiCompanion::AnimGraphAuthoring
