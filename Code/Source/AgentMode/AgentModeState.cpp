/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AgentModeState.h"

#include <rapidjson/document.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

namespace AiCompanion::AgentMode
{
    namespace
    {
        std::string GetStateDir()
        {
            if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg)
            {
                return std::string(xdg) + "/o3de-ai-companion";
            }
            if (const char* home = std::getenv("HOME"); home && *home)
            {
                return std::string(home) + "/.local/state/o3de-ai-companion";
            }
            // No HOME, no XDG_STATE_HOME. Fall back to /tmp so we never write
            // into the cwd, but also do not break.
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

        // Make sure the parent directory exists.
        const auto slash = pathStr.find_last_of('/');
        if (slash != std::string::npos)
        {
            const std::string dir = pathStr.substr(0, slash);
            ::mkdir(dir.c_str(), 0700);
        }

        const auto now = std::chrono::system_clock::now();
        const auto observedAt = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        const std::string tmp = pathStr + ".tmp";
        FILE* f = std::fopen(tmp.c_str(), "w");
        if (!f)
        {
            return;
        }
        std::fprintf(
            f,
            "{\n"
            "  \"enabled\": %s,\n"
            "  \"suppress_dialogs\": %s,\n"
            "  \"filter_installed\": %s,\n"
            "  \"source_updated_at\": %lld,\n"
            "  \"observed_at\": %lld,\n"
            "  \"version\": 1\n"
            "}\n",
            state.enabled ? "true" : "false",
            state.suppressDialogs ? "true" : "false",
            filterInstalled ? "true" : "false",
            static_cast<long long>(state.updatedAt),
            static_cast<long long>(observedAt));
        std::fclose(f);
        std::rename(tmp.c_str(), pathStr.c_str());
    }
} // namespace AiCompanion::AgentMode
