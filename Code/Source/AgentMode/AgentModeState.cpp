/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AgentModeState.h"

#include <AzCore/Utils/Utils.h>

#include <rapidjson/document.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#if defined(AZ_PLATFORM_WINDOWS)
#include <direct.h>
#define AI_COMPANION_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define AI_COMPANION_MKDIR(path) ::mkdir(path, 0700)
#endif

namespace AiCompanion::AgentMode
{
    namespace
    {
        std::string ReadEnv(const char* name)
        {
            char buffer[4096];
            auto outcome = AZ::Utils::GetEnv(buffer, name);
            return outcome.IsSuccess() ? std::string(outcome.GetValue().data(), outcome.GetValue().size()) : std::string();
        }

        std::string GetStateDir()
        {
#if defined(AZ_PLATFORM_WINDOWS)
            if (auto val = ReadEnv("LOCALAPPDATA"); !val.empty())
            {
                return val + "/o3de-ai-companion";
            }
            if (auto val = ReadEnv("USERPROFILE"); !val.empty())
            {
                return val + "/.o3de-ai-companion";
            }
#else
            if (auto val = ReadEnv("XDG_STATE_HOME"); !val.empty())
            {
                return val + "/o3de-ai-companion";
            }
            if (auto val = ReadEnv("HOME"); !val.empty())
            {
                return val + "/.local/state/o3de-ai-companion";
            }
#endif
            return "/tmp/o3de-ai-companion";
        }
    } // namespace

    AZ::IO::FixedMaxPath GetStatePath()
    {
        const std::string dir = GetStateDir();
        return AZ::IO::FixedMaxPath((dir + "/agent_mode.json").c_str());
    }

    AZ::IO::FixedMaxPath GetObservedStatePath()
    {
        const std::string dir = GetStateDir();
        return AZ::IO::FixedMaxPath((dir + "/agent_mode_observed.json").c_str());
    }

    bool LoadState(State& outState)
    {
        const auto path = GetStatePath();

        std::ifstream file(path.c_str());
        if (!file.is_open())
        {
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string content = buffer.str();

        rapidjson::Document doc;
        doc.Parse(content.c_str());
        if (doc.HasParseError() || !doc.IsObject())
        {
            return false;
        }

        if (doc.HasMember("enabled") && doc["enabled"].IsBool())
        {
            outState.enabled = doc["enabled"].GetBool();
        }
        if (doc.HasMember("suppress_dialogs") && doc["suppress_dialogs"].IsBool())
        {
            outState.suppressDialogs = doc["suppress_dialogs"].GetBool();
        }
        if (doc.HasMember("updated_at"))
        {
            if (doc["updated_at"].IsInt64())
            {
                outState.updatedAt = doc["updated_at"].GetInt64();
            }
            else if (doc["updated_at"].IsInt())
            {
                outState.updatedAt = static_cast<AZ::s64>(doc["updated_at"].GetInt());
            }
        }
        return true;
    }

    void WriteObservedState(const State& state, bool filterInstalled)
    {
        const auto path = GetObservedStatePath();
        const auto pathStr = std::string(path.c_str());

        const auto slash = pathStr.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            const std::string dir = pathStr.substr(0, slash);
            AI_COMPANION_MKDIR(dir.c_str());
        }

        const auto now = std::chrono::system_clock::now();
        const auto observedAt = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        const std::string tmp = pathStr + ".tmp";
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open())
        {
            return;
        }
        out << "{\n"
            << "  \"enabled\": " << (state.enabled ? "true" : "false") << ",\n"
            << "  \"suppress_dialogs\": " << (state.suppressDialogs ? "true" : "false") << ",\n"
            << "  \"filter_installed\": " << (filterInstalled ? "true" : "false") << ",\n"
            << "  \"source_updated_at\": " << static_cast<long long>(state.updatedAt) << ",\n"
            << "  \"observed_at\": " << static_cast<long long>(observedAt) << ",\n"
            << "  \"version\": 1\n"
            << "}\n";
        out.close();
        std::rename(tmp.c_str(), pathStr.c_str());
    }
} // namespace AiCompanion::AgentMode
