/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AnimGraphAuthoring.h"

#include "Network/RequestError.h"

#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>

#include <EMotionFX/CommandSystem/Source/CommandManager.h>
#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/EMotionFXManager.h>
#include <EMotionFX/Tools/EMotionStudio/EMStudioSDK/Source/EMStudioManager.h>

#include <cstdlib>

namespace AiCompanion::AnimGraphAuthoring
{
    namespace
    {
        constexpr const char* StudioNotAvailable =
            "EMotion Studio is not available: the EMotionFX gem's editor module (Animation Editor) is not loaded";

        CommandSystem::CommandManager* FindCommandManager()
        {
            EMStudio::EMStudioManager* studio = EMStudio::GetManager();
            return studio ? studio->GetCommandManager() : nullptr;
        }

        EMotionFX::AnimGraph* FindGraph(AZ::u32 animGraphId)
        {
            auto variable = AZ::Environment::FindVariable<EMotionFX::EMotionFXManager*>(EMotionFX::kEMotionFXInstanceVarName);
            EMotionFX::EMotionFXManager* emfx = variable ? variable.Get() : nullptr;
            EMotionFX::AnimGraphManager* manager = emfx ? emfx->GetAnimGraphManager() : nullptr;
            return manager ? manager->FindAnimGraphByID(animGraphId) : nullptr;
        }
    } // namespace

    AZ::Outcome<AZStd::string, AZStd::string> RunCommand(const AZStd::string& commandLine)
    {
        CommandSystem::CommandManager* commandManager = FindCommandManager();
        if (!commandManager)
        {
            return AZ::Failure(RequestError::EncodeError(RequestError::Unavailable, StudioNotAvailable));
        }
        AZStd::string result;
        if (!commandManager->ExecuteCommand(commandLine, result))
        {
            // The command system's own text when it gives one.
            return AZ::Failure(RequestError::EncodeError(
                RequestError::EngineError, result.empty() ? AZStd::string::format("command failed: %s", commandLine.c_str()) : result));
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
        char* end = nullptr;
        const unsigned long parsed = strtoul(run.GetValue().c_str(), &end, 10);
        const bool parsedOk = end && end != run.GetValue().c_str() && parsed <= 0xFFFFFFFFul;
        EMotionFX::AnimGraph* graph = parsedOk ? FindGraph(static_cast<AZ::u32>(parsed)) : nullptr;
        if (!graph)
        {
            return AZ::Failure(RequestError::EncodeError(
                RequestError::EngineError,
                AZStd::string::format("CreateAnimGraph answered '%s' but no graph with that id exists", run.GetValue().c_str())));
        }

        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("id");
        w.Uint(graph->GetID());
        w.Key("file_name");
        w.String(graph->GetFileName());
        w.EndObject();
        return AZ::Success(AZStd::string(sb.GetString()));
    }

    AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraph(AZ::u32 animGraphId)
    {
        if (!FindGraph(animGraphId))
        {
            return AZ::Failure(
                RequestError::EncodeError(RequestError::NotFound, AZStd::string::format("anim graph not found: %u", animGraphId)));
        }
        auto run = RunCommand(AZStd::string::format("RemoveAnimGraph -animGraphID %u", animGraphId));
        if (!run.IsSuccess())
        {
            return run;
        }
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("removed");
        w.Uint(animGraphId);
        w.EndObject();
        return AZ::Success(AZStd::string(sb.GetString()));
    }
} // namespace AiCompanion::AnimGraphAuthoring
